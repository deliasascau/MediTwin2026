#pragma once

// ─── SensorData — single snapshot of all hardware readings ───────────────────
struct SensorData {
    // DHT22
    float temperature;   // °C
    float humidity;      // %RH
    bool  dhtOk;

    // MQ-135
    int   airQuality;    // raw ADC 0–4095

    // LDR
    int   lightLevel;    // raw ADC 0–4095

    // HC-SR04
    float distanceCm;    // cm  (< 0 → timeout / out of range)

    // MPU6050
    float accelG;        // total acceleration magnitude in g
    float gyroX;         // rad/s
    float gyroY;
    float gyroZ;
    bool  mpuOk;

    // ACS712
    float currentA;      // Amperes (negative = reverse flow)
};
