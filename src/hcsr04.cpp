#include "hcsr04.h"
#include "config.h"
#include <Arduino.h>

static float _baseline = -1.0f;

void initHCSR04() {
    pinMode(PIN_TRIG, OUTPUT);
    pinMode(PIN_ECHO, INPUT);
    digitalWrite(PIN_TRIG, LOW);
    Serial.println("[SENSOR] HC-SR04 ready");
}

float readDistance() {
    // Clean 10 µs trigger pulse
    digitalWrite(PIN_TRIG, LOW);
    delayMicroseconds(2);
    digitalWrite(PIN_TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(PIN_TRIG, LOW);

    // 30 000 µs timeout ≈ 5 m max range
    long duration = pulseIn(PIN_ECHO, HIGH, 30000UL);
    if (duration == 0) return -1.0f;
    return (duration * 0.0343f) / 2.0f;
}

bool hcsr04Calibrate(int samples) {
    float sum = 0.0f;
    int ok = 0;

    for (int i = 0; i < samples; i++) {
        float d = readDistance();
        if (d > 0) {
            sum += d;
            ok++;
        }
        delay(60);
    }

    if (ok == 0) {
        _baseline = -1.0f;
        Serial.println("[HC-SR04] Baseline calibrare esuata (fara ecou)");
        return false;
    }

    _baseline = sum / ok;
    Serial.printf("[HC-SR04] Baseline calibrat: %.1f cm\n", _baseline);
    return true;
}

float hcsr04Baseline() {
    return _baseline;
}
