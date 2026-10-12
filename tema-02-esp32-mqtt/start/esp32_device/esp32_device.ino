// Device-ul IoT din tema 01, de data aceasta pe un ESP32-C3 Super Mini.
//
// Aceeași logică și același protocol ca device-simulator din tema 01, cu trei
// taskuri FreeRTOS în loc de trei thread-uri Java:
//
//   periodic_task --(coada de alarme)--> sporadic_task
//        |                                     |
//        +---- telemetry ---> MQTT <--- alarm -+
//                              |
//                          commands --(coada de comenzi)--> aperiodic_task ---> status
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

// Perioada taskului periodic și pragul de alarmă. Sunt citite de taskul
// periodic și modificate de taskul aperiodic (alt task FreeRTOS), deci volatile.
static volatile uint32_t g_period_ms = PERIOD_MS_INITIAL;
static volatile float g_threshold = THRESHOLD_INITIAL;

// Temperatura cipului, în °C, de la senzorul intern al ESP32-C3.
// Este temperatura siliciului, nu a camerei: e mai mare decât temperatura
// camerei și variază puțin (crește ușor cât timp merge Wi-Fi-ul). Ca să
// provocați alarme, puneți pragul cu SET_THRESHOLD puțin sub valoarea citită.
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
// pune un eveniment în coada de alarme. Alarma este tratată de taskul sporadic.
void periodic_task(void* arg) {
    (void)arg;
    // TODO 1: implementați bucla taskului periodic. Fiecare job:
    //   - citește temperatura: read_temperature()
    //   - publică pe topic-ul "telemetry" mesajul "deviceId;timestamp;temperatura"
    //     (timestamp = epoch_ms(); temperatura cu "%.2f")
    //   - dacă temperatura > g_threshold, pune un eveniment în coada de alarme
    //     (xQueueSend)
    //
    // Folosiți așteptare absolută, nu relativă: vTaskDelay(T) după fiecare job
    // duce la derivă. xTaskDelayUntil(&last, pdMS_TO_TICKS(T)) așteaptă până la
    // eliberarea următoare de pe grilă, last + T, și actualizează last.
    //
    // g_period_ms se poate schimba în timpul rulării (comanda SET_INTERVAL),
    // deci citiți-l la fiecare job.

    vTaskDelete(NULL);  // funcția unui task nu are voie să se termine fără asta
}

// TASK SPORADIC: tratează alarmele de temperatură.
//
// Evenimentele sosesc în coada de alarme (puse de taskul periodic). Între
// două eliberări ale taskului (două alarme publicate) trebuie să treacă cel
// puțin MIN_INTERARRIVAL_MS. Intervalul este impus de cod: evenimentele care
// sosesc mai des sunt respinse, ca să nu fie inundat brokerul.
void sporadic_task(void* arg) {
    (void)arg;
    // TODO 2: implementați taskul sporadic:
    //   - așteptați un eveniment: xQueueReceive(coada, &eveniment, portMAX_DELAY)
    //   - dacă de la ultima eliberare au trecut mai puțin de MIN_INTERARRIVAL_MS,
    //     respingeți evenimentul (afișați un mesaj pe Serial)
    //   - altfel publicați pe topic-ul "alarm" mesajul
    //     "deviceId;timestamp;temperatura;prag"
    //
    // Pentru intervale scurte folosiți un timp monoton, millis(), nu epoch_ms():
    // ora sincronizată prin SNTP poate sări.

    vTaskDelete(NULL);
}

// TASK APERIODIC: execută comenzile primite de la business service.
//
// Comenzile sosesc oricând, pe MQTT, fără un interval minim între ele, și sunt
// puse în coada de comenzi de handlerul evenimentelor MQTT. Fiecare comandă
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
    // TODO 3: implementați taskul aperiodic:
    //   - preluați comenzile din coadă: xQueueReceive(coada, &comanda, portMAX_DELAY)
    //   - separați numele comenzii de parametru (sscanf, strncmp, atol, atof ...)
    //   - executați comanda și publicați confirmarea pe topic-ul "status"
    //   - o comandă necunoscută sau un parametru invalid nu trebuie să
    //     oprească taskul: publicați "ERROR <motiv>"

    vTaskDelete(NULL);
}

void setup() {
    Serial.begin(115200);
    uint32_t t = millis();
    while (!Serial && millis() - t < 3000) delay(10);
    delay(200);
    Serial.println();
    Serial.printf("=== tema 02: %s ===\n", DEVICE_ID);

    // TODO 0: pregătiți aplicația (în tema 01, aceasta era partea dată gata:
    // DeviceSimulator și MqttConnection). Pornind de la mqtt_hello din
    // laboratorul 0:
    //   - conectați placa la Wi-Fi (WIFI_SSID, WIFI_PASSWORD), ca în connect_wifi()
    //   - sincronizați ora: configTime(0, 0, NTP_SERVER), apoi așteptați
    //     sincronizarea cu getLocalTime(&tm, 10000) (struct tm tm;)
    //   - creați cele două cozi: xQueueCreate(lungime, sizeof(element))
    //   - porniți clientul MQTT, ca în start_mqtt(), cu un client id unic pe
    //     broker (de exemplu STUDENT-DEVICE_ID); în handlerul evenimentelor:
    //       MQTT_EVENT_CONNECTED: abonare la topic-ul "commands" (QoS 1);
    //       MQTT_EVENT_DATA: copiați textul comenzii într-un buffer (ev->data
    //         NU se termină cu '\0', lungimea este ev->data_len) și puneți-l în
    //         coada de comenzi. Handlerul rulează pe taskul clientului MQTT:
    //         nu executați comanda acolo, doar o puneți în coadă.
    //   - porniți cele trei taskuri: xTaskCreate(functie, "nume", 4096, NULL,
    //     prioritate, NULL). Alegeți prioritățile între 2 și 19: peste loopTask
    //     (1), sub taskurile sistemului pentru evenimente și Wi-Fi (20, 23).
    //
    // Publicarea, din oricare task: esp_mqtt_client_publish(client, topic,
    // mesaj, 0, 1, 0) (lungime 0 = strlen(mesaj), QoS 1, fără retain).
}

void loop() {
    // Totul se face în taskuri: loop() nu mai are nimic de făcut.
    delay(1000);
}
