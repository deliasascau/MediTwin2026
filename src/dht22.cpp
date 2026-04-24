#include "dht22.h"
#include "config.h"
#include <Arduino.h>
#include <DHT.h>

static DHT dht(PIN_DHT22, DHT_TYPE);
static bool  _baselineReady = false;
static float _baseTemp = 0.0f;
static float _baseHum  = 0.0f;

void initDHT22() {
    dht.begin();
    Serial.println("[SENSOR] DHT22 initialised");
}

bool readDht(float &temp, float &hum) {
    hum  = dht.readHumidity();
    temp = dht.readTemperature();
    if (isnan(hum) || isnan(temp)) return false;
    return true;
}

bool dhtCalibrate(int samples, uint32_t intervalMs) {
    float sumT = 0.0f, sumH = 0.0f;
    int ok = 0;

    for (int i = 0; i < samples; i++) {
        float t, h;
        if (readDht(t, h)) {
            sumT += t;
            sumH += h;
            ok++;
        }
        if (i != samples - 1) delay(intervalMs);
    }

    if (ok == 0) {
        _baselineReady = false;
        Serial.println("[DHT22] Baseline calibrare esuata (fara citiri valide)");
        return false;
    }

    _baseTemp = sumT / ok;
    _baseHum  = sumH / ok;
    _baselineReady = true;
    Serial.printf("[DHT22] Baseline calibrat: T=%.1fC  H=%.1f%%\n", _baseTemp, _baseHum);
    return true;
}

bool dhtBaselineReady() { return _baselineReady; }
float dhtBaselineTemp() { return _baseTemp; }
float dhtBaselineHum()  { return _baseHum; }
