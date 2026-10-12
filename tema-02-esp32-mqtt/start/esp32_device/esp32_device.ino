// Device-ul IoT din tema 01, de data aceasta pe un ESP32-C3 Super Mini.
//
// Aceeași logică și același protocol ca device-simulator din tema 01, cu trei
// taskuri FreeRTOS în loc de trei thread-uri Java:
//
//   periodic_task --(g_alarms)--> sporadic_task
//        |                              |
//        +---- telemetry ---> MQTT <--- alarm
//                              |
//                          commands --(g_commands)--> aperiodic_task ---> status
//
// Scheletul rulează deja: setup() creează cele două cozi și pornește cele trei
// taskuri. Taskul periodic doar afișează un mesaj la fiecare job, iar taskurile
// sporadic și aperiodic stau blocate pe cozile lor, pentru că deocamdată
// nimeni nu pune nimic în ele. Locurile unde lucrați: TODO 0 ... TODO 3.
//
// Setările (Wi-Fi, broker, STUDENT, DEVICE_ID) sunt în config.h.
// Exemplele de pornire sunt în laboratorul 0 din rt-labs-esp32c3; vezi
// exemple/README.md din folderul temei.
//
// Placa: ESP32C3 Dev Module, USB CDC On Boot: Enabled. Serial Monitor: 115200 baud.

#include <WiFi.h>
#include <sys/time.h>

#include "mqtt_client.h"

#include "config.h"

// Prioritățile taskurilor: un număr mai mare înseamnă prioritate mai mare.
// Toate sunt peste loopTask (1) și sub taskurile sistemului pentru
// evenimente și Wi-Fi (20, 23). Le puteți schimba.
const UBaseType_t PRIO_SPORADIC = 5;   // alarme: termen hard, rulează rar (intervalul minim între sosiri)
const UBaseType_t PRIO_PERIODIC = 4;   // telemetrie
const UBaseType_t PRIO_APERIODIC = 3;  // comenzi: termen soft, „cât mai repede”

// Stiva fiecărui task, în octeți. snprintf cu numere zecimale are nevoie de
// stivă: sub 4096, placa poate reporni cu "Stack canary watchpoint triggered".
const uint32_t TASK_STACK_BYTES = 4096;

// Elementul cozii de alarme, pus de taskul periodic și preluat de cel sporadic.
// Se copiază în coadă la xQueueSend. Pragul se trimite odată cu temperatura,
// ca alarma să conțină pragul din momentul detectării.
struct AlarmEvent {
    float temp;
    float threshold;
};

// Elementul cozii de comenzi, pus de handlerul MQTT și preluat de taskul
// aperiodic. Textul comenzii se copiază în tablou (cu '\0' la final), nu se
// trimite un pointer: bufferul primit de la MQTT nu mai este valid după ce
// handlerul se termină.
struct Command {
    char text[64];
};

const UBaseType_t ALARM_QUEUE_LENGTH = 8;
const UBaseType_t COMMAND_QUEUE_LENGTH = 8;

// Cozile prin care comunică taskurile (echivalentul BlockingQueue din tema 01).
// Sunt create în setup(), înainte de pornirea taskurilor și a clientului MQTT.
static QueueHandle_t g_alarms = NULL;
static QueueHandle_t g_commands = NULL;

// Perioada taskului periodic și pragul de alarmă. Sunt citite de taskul
// periodic și modificate de taskul aperiodic (alt task FreeRTOS), deci volatile.
static volatile uint32_t g_period_ms = PERIOD_MS_INITIAL;
static volatile float g_threshold = THRESHOLD_INITIAL;

// Temperatura cipului, în °C, de la senzorul intern al ESP32-C3.
// Este temperatura siliciului, nu a camerei: imediat după pornirea Wi-Fi-ului
// poate fi în jur de 50 °C, apoi scade în câteva minute spre 35-40 °C.
// Rezoluția este de aproximativ 1 °C. Ca să provocați alarme, puneți pragul
// cu SET_THRESHOLD cu un grad sub valoarea citită.
float read_temperature(void) {
    return temperatureRead();
}

// Timpul curent în milisecunde de la 1 ianuarie 1970 (Unix epoch), aceeași
// valoare ca System.currentTimeMillis() în Java. Se tipărește cu %lld:
//
//   Serial.printf("%lld\n", (long long)epoch_ms());
//
// Placa nu are ceas de timp real: valoarea este corectă abia după ce ora a
// fost sincronizată prin SNTP (vezi TODO 0). Până atunci numără de la pornire.
int64_t epoch_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

// TASK PERIODIC, cu perioada T = g_period_ms: fiecare job citește senzorul și
// publică valoarea pe topic-ul de telemetrie.
//
// Dacă temperatura depășește pragul, taskul NU tratează el alarma, ci doar
// pune un eveniment în coada g_alarms. Alarma este tratată de taskul sporadic.
void periodic_task(void* arg) {
    (void)arg;
    uint32_t job = 0;

    // momentul primei eliberări; xTaskDelayUntil îl mută la fiecare job cu T
    TickType_t last = xTaskGetTickCount();

    for (;;) {
        job = job + 1;

        // TODO 1: înlocuiți mesajul de mai jos cu jobul taskului periodic:
        //   - citiți temperatura: read_temperature()
        //   - publicați pe topic-ul "telemetry" mesajul "deviceId;timestamp;temperatura"
        //     (timestamp = epoch_ms(); temperatura cu "%.2f")
        //   - dacă temperatura > g_threshold, puneți un AlarmEvent în coada
        //     g_alarms: xQueueSend(g_alarms, &ev, 0). Timeout 0: taskul
        //     periodic nu are voie să se blocheze din cauza alarmelor.
        Serial.printf("[periodic] job %lu, la %lu ms de la pornire\n", (unsigned long)job,
                      (unsigned long)millis());

        // Așteptare absolută până la eliberarea următoare de pe grilă,
        // last + T. Cu vTaskDelay(T) (așteptare relativă) ar apărea derivă:
        // perioada reală ar deveni T + C. g_period_ms se poate schimba în
        // timpul rulării (SET_INTERVAL), de aceea este citit la fiecare job.
        xTaskDelayUntil(&last, pdMS_TO_TICKS(g_period_ms));
    }
}

// TASK SPORADIC: tratează alarmele de temperatură.
//
// Evenimentele sosesc în coada g_alarms (puse de taskul periodic). Între
// două eliberări ale taskului (două alarme publicate) trebuie să treacă cel
// puțin MIN_INTERARRIVAL_MS. Intervalul este impus de cod: evenimentele care
// sosesc mai des sunt respinse, ca să nu fie inundat brokerul.
void sporadic_task(void* arg) {
    (void)arg;

    for (;;) {
        AlarmEvent ev;
        // Taskul stă blocat aici, fără să consume procesor, până sosește un
        // eveniment. Cât timp taskul periodic nu pune nimic în coadă, nu
        // trece niciodată mai departe.
        xQueueReceive(g_alarms, &ev, portMAX_DELAY);

        // TODO 2: tratați evenimentul ev:
        //   - dacă de la ultima alarmă publicată au trecut mai puțin de
        //     MIN_INTERARRIVAL_MS, respingeți evenimentul (afișați un mesaj)
        //   - altfel publicați pe topic-ul "alarm" mesajul
        //     "deviceId;timestamp;temperatura;prag"
        //
        // Intervalul se măsoară de la ultima alarmă PUBLICATĂ, nu de la ultimul
        // eveniment sosit. Pentru intervale folosiți un timp monoton, millis(),
        // nu epoch_ms(): ora sincronizată prin SNTP poate sări.
        Serial.printf("[sporadic] eveniment: %.2f > %.2f\n", ev.temp, ev.threshold);
    }
}

// TASK APERIODIC: execută comenzile primite de la business service.
//
// Comenzile sosesc oricând, pe MQTT, fără un interval minim între ele, și sunt
// puse în coada g_commands de handlerul evenimentelor MQTT. Fiecare comandă
// eliberează un job al taskului. Comenzi care trebuie suportate:
//
//   SET_INTERVAL <ms>       schimbă perioada T a taskului periodic
//   SET_THRESHOLD <valoare> schimbă pragul de alarmă
//   RESET                   readuce perioada și pragul la valorile inițiale
//   STATUS                  raportează perioada și pragul curente
//
// După executarea fiecărei comenzi, device-ul publică pe topic-ul "status" o
// confirmare: "deviceId;timestamp;text", de exemplu
// "esp32-1;1727600000000;OK SET_INTERVAL 1000".
void aperiodic_task(void* arg) {
    (void)arg;

    for (;;) {
        Command cmd;
        // Blocat până sosește o comandă. Deocamdată coada rămâne goală:
        // handlerul MQTT care pune comenzile în ea face parte din TODO 0.
        xQueueReceive(g_commands, &cmd, portMAX_DELAY);

        // TODO 3: executați comanda cmd.text:
        //   - separați numele comenzii de parametru (sscanf, strcmp, strtol, strtof ...)
        //   - executați comanda și publicați confirmarea pe topic-ul "status"
        //   - o comandă necunoscută sau un parametru invalid nu trebuie să
        //     oprească taskul: publicați "ERROR <motiv>"
        Serial.printf("[aperiodic] comandă primită: %s\n", cmd.text);
    }
}

void setup() {
    Serial.begin(115200);
    uint32_t t = millis();
    while (!Serial && millis() - t < 3000) delay(10);
    delay(200);
    Serial.println();
    Serial.printf("=== tema 02: %s ===\n", DEVICE_ID);

    // Cozile se creează primele: taskurile și handlerul MQTT le folosesc.
    g_alarms = xQueueCreate(ALARM_QUEUE_LENGTH, sizeof(AlarmEvent));
    g_commands = xQueueCreate(COMMAND_QUEUE_LENGTH, sizeof(Command));
    if (g_alarms == NULL || g_commands == NULL) {
        Serial.println("nu s-au putut crea cozile (memorie insuficientă)");
        return;
    }

    // TODO 0: conectarea (în tema 01, aceasta era partea dată gata, în
    // MqttConnection). Pornind de la mqtt_hello din laboratorul 0:
    //   - conectați placa la Wi-Fi (WIFI_SSID, WIFI_PASSWORD), ca în connect_wifi()
    //   - sincronizați ora: configTime(0, 0, NTP_SERVER), apoi așteptați
    //     sincronizarea cu getLocalTime(&tm, 10000) (struct tm tm;)
    //   - porniți clientul MQTT, ca în start_mqtt(), cu un client id unic pe
    //     broker (de exemplu STUDENT-DEVICE_ID); în handlerul evenimentelor:
    //       MQTT_EVENT_CONNECTED: abonare la topic-ul "commands" (QoS 1);
    //       MQTT_EVENT_DATA: copiați textul comenzii într-un Command (ev->data
    //         NU se termină cu '\0', lungimea este ev->data_len) și puneți-l în
    //         coada: xQueueSend(g_commands, &cmd, 0). Handlerul rulează pe
    //         taskul clientului MQTT: nu executați comanda acolo.
    //
    // Publicarea, din oricare task: esp_mqtt_client_publish(client, topic,
    // mesaj, 0, 1, 0) (lungime 0 = strlen(mesaj), QoS 1, fără retain).

    // Pornirea taskurilor. De aici încolo, planificatorul FreeRTOS rulează
    // mereu taskul gata de execuție cu prioritatea cea mai mare.
    xTaskCreate(periodic_task, "periodic", TASK_STACK_BYTES, NULL, PRIO_PERIODIC, NULL);
    xTaskCreate(sporadic_task, "sporadic", TASK_STACK_BYTES, NULL, PRIO_SPORADIC, NULL);
    xTaskCreate(aperiodic_task, "aperiodic", TASK_STACK_BYTES, NULL, PRIO_APERIODIC, NULL);
    Serial.println("taskuri pornite: periodic, sporadic, aperiodic");
}

void loop() {
    // Totul se face în taskuri: loop() nu mai are nimic de făcut.
    delay(1000);
}
