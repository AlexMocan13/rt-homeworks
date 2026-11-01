#pragma once

#include <Arduino.h>

const char* const WIFI_SSID = "iot-demo";
const char* const WIFI_PASSWORD = "password";

const char* const BROKER_URI = "mqtt://control.aut.utcluj.ro:11188";
// const char* const BROKER_URI = "mqtt://broker.hivemq.com:1883"; am testat cu broker-ul acesta

const char* const NTP_SERVER = "pool.ntp.org";
const char* const STUDENT = "mocan-alex";

extern const char* const DEVICE_ID;

const uint32_t PERIOD_MS_INITIAL = 2000;
const float THRESHOLD_INITIAL = 45.0f;
const uint32_t MIN_INTERARRIVAL_MS = 5000;

inline void make_topic(char* buf, size_t size, const char* kind) {
    snprintf(buf, size, "ssatr/%s/devices/%s/%s", STUDENT, DEVICE_ID, kind);
}