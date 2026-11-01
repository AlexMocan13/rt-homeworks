#include <WiFi.h>
#include "config.h"
#include "connection.h"

// --- Citire senzor de temperatură intern ESP32-C3 ---
float read_temperature() {
    float t = temperatureRead();
    if (isnan(t) || t == 0.0f) {
        return 37.0f;
    }
    return t;
}

// --- Structuri de date pentru cozile FreeRTOS ---
struct CommandMsg {
    char text[COMMAND_MAX_LEN];
};

struct AlarmMsg {
    float temperature;
    float threshold;
};

// --- Handle-uri pentru cozi ---
QueueHandle_t command_queue = NULL;
QueueHandle_t alarm_queue = NULL;

// --- Variabile globale partajate (volatile) ---
volatile uint32_t period_ms = PERIOD_MS_INITIAL;
volatile float threshold = THRESHOLD_INITIAL;

// ============================================================================
// TODO 1 – Task Periodic (Telemetrie)
// ============================================================================
void telemetry_task(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for (;;) {
        float temp = read_temperature();

        char payload[128];
        snprintf(payload, sizeof(payload), "%s;%lld;%.2f", DEVICE_ID, (long long)epoch_ms(), temp);
        
        mqtt_publish("telemetry", payload);
        Serial.printf("[PERIODIC] Telemetrie: %s\n", payload);

        if (temp > threshold && alarm_queue != NULL) {
            AlarmMsg alarm = { .temperature = temp, .threshold = threshold };
            xQueueSend(alarm_queue, &alarm, 0); // Trimitere non-blocantă
        }

        uint32_t current_period = period_ms;
        if (current_period < 100) current_period = 100;

        xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(current_period));
    }
}

// ============================================================================
// TODO 2 – Task Sporadic (Alarme)
// ============================================================================
void alarm_task(void *pvParameters) {
    AlarmMsg alarm;
    uint32_t last_alarm_time_ms = 0;

    for (;;) {
        if (alarm_queue != NULL && xQueueReceive(alarm_queue, &alarm, portMAX_DELAY) == pdTRUE) {
            uint32_t now = millis();

            if (now - last_alarm_time_ms < MIN_INTERARRIVAL_MS) {
                Serial.printf("[SPORADIC] Alarma respinsa (frecventa prea mare): %.2f °C\n", alarm.temperature);
            } else {
                last_alarm_time_ms = now;
                char payload[128];
                snprintf(payload, sizeof(payload), "%s;%lld;%.2f;%.2f", 
                         DEVICE_ID, (long long)epoch_ms(), alarm.temperature, alarm.threshold);
                
                mqtt_publish("alarm", payload);
                Serial.printf("[SPORADIC] Alarma publicata: %s\n", payload);
            }
        }
    }
}

// ============================================================================
// TODO 3 – Task Aperiodic (Comenzi) & Callback MQTT
// ============================================================================
void command_received(const char* text) {
    if (command_queue == NULL || text == NULL) return;

    CommandMsg cmd;
    strncpy(cmd.text, text, COMMAND_MAX_LEN - 1);
    cmd.text[COMMAND_MAX_LEN - 1] = '\0';

    xQueueSend(command_queue, &cmd, 0);
}

void command_task(void *pvParameters) {
    CommandMsg cmd;

    for (;;) {
        if (command_queue != NULL && xQueueReceive(command_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            Serial.printf("[APERIODIC] Comanda primita: %s\n", cmd.text);

            char status_payload[128];
            long long ts = (long long)epoch_ms();

            if (strncmp(cmd.text, "SET_INTERVAL", 12) == 0) {
                uint32_t new_interval = 0;
                if (sscanf(cmd.text, "SET_INTERVAL %lu", &new_interval) == 1 && new_interval > 0) {
                    period_ms = new_interval;
                    snprintf(status_payload, sizeof(status_payload), "%s;%lld;OK SET_INTERVAL %lu", DEVICE_ID, ts, new_interval);
                } else {
                    snprintf(status_payload, sizeof(status_payload), "%s;%lld;ERROR SET_INTERVAL invalid", DEVICE_ID, ts);
                }
            } else if (strncmp(cmd.text, "SET_THRESHOLD", 13) == 0) {
                float new_thresh = 0.0f;
                if (sscanf(cmd.text, "SET_THRESHOLD %f", &new_thresh) == 1) {
                    threshold = new_thresh;
                    snprintf(status_payload, sizeof(status_payload), "%s;%lld;OK SET_THRESHOLD %.2f", DEVICE_ID, ts, new_thresh);
                } else {
                    snprintf(status_payload, sizeof(status_payload), "%s;%lld;ERROR SET_THRESHOLD invalid", DEVICE_ID, ts);
                }
            } else if (strcmp(cmd.text, "RESET") == 0) {
                period_ms = PERIOD_MS_INITIAL;
                threshold = THRESHOLD_INITIAL;
                snprintf(status_payload, sizeof(status_payload), "%s;%lld;OK RESET", DEVICE_ID, ts);
            } else if (strcmp(cmd.text, "STATUS") == 0) {
                snprintf(status_payload, sizeof(status_payload), "%s;%lld;OK STATUS T=%lu TH=%.2f", DEVICE_ID, ts, period_ms, threshold);
            } else {
                snprintf(status_payload, sizeof(status_payload), "%s;%lld;ERROR Unknown command: %s", DEVICE_ID, ts, cmd.text);
            }

            mqtt_publish("status", status_payload);
            Serial.printf("[APERIODIC] Raspuns status: %s\n", status_payload);
        }
    }
}

// ============================================================================
// TODO 0 – Configurare Cozi si Taskuri in setup()
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(2000); 

    Serial.println("\n====================================");
    Serial.println("=== SPARGEM BLOCAJUL WI-FI ===");
    Serial.println("====================================");

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    Serial.print("Asteptam IP-ul de la router ");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n[SUCCES] Conectat! IP: ");
    Serial.println(WiFi.localIP());

    command_queue = xQueueCreate(10, sizeof(CommandMsg));
    alarm_queue = xQueueCreate(10, sizeof(AlarmMsg));

    mqtt_start();

    // 4. Creare taskuri
    xTaskCreate(telemetry_task, "telemetry_task", 4096, NULL, 5, NULL);
    xTaskCreate(alarm_task,     "alarm_task",     4096, NULL, 10, NULL);
    xTaskCreate(command_task,   "command_task",   4096, NULL, 7, NULL);
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}