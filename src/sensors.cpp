#include "sensors.h"
#include "config.h"
#include "ldr.h"
#include "mq135.h"
#include <Arduino.h>
#include <Wire.h>

// ─── initSensors — starts I2C bus then delegates to each driver ───────────────
void initSensors() {
    Wire.begin(PIN_SDA, PIN_SCL);
    Serial.printf("[SENSOR] I2C bus: SDA=GPIO%d  SCL=GPIO%d\n", PIN_SDA, PIN_SCL);

    initDHT22();
    initHCSR04();
    initMPU6050();

    // ADC sensors: explicit INPUT for clarity (analogRead works without it too)
    pinMode(PIN_LDR,    INPUT);
    pinMode(PIN_MQ135,  INPUT);
    pinMode(PIN_ACS712, INPUT);

    // Calibrare LDR: presupune lumina normala la pornire
    ldrCalibrate(LDR_CAL_SAMPLES);

    // Calibrare MQ-3: apelata DUPA warmup din setup()
    // Nu apelam mq135Calibrate() aici — warmup-ul e in setup(), calibrarea vine imediat dupa

    Serial.println("[SENSOR] All sensors initialised");
}

// ─── collectAll — reads every sensor into one snapshot ───────────────────────
SensorData collectAll() {
    SensorData d = {};

    d.lightLevel = readLdr();
    d.airQuality = readMq135();
    d.distanceCm = readDistance();
    d.currentA   = readAcs712();

    d.dhtOk = readDht(d.temperature, d.humidity);

    d.mpuOk = readMpu6050(d.accelG, d.gyroX, d.gyroY, d.gyroZ);

    return d;
}

// ─── printSensorData — formatted Serial output ───────────────────────────────
void printSensorData(const SensorData &d) {
    Serial.println("========== MediTwin Sensors ==========");

    if (d.dhtOk)
        Serial.printf("TEMP     : %.1f C\n", d.temperature);
    else
        Serial.println("TEMP     : ERROR (DHT22)");

    if (d.dhtOk)
        Serial.printf("HUMIDITY : %.1f %%\n", d.humidity);
    else
        Serial.println("HUMIDITY : ERROR (DHT22)");

    Serial.printf("AIR QUAL : %d\n",   d.airQuality);
    Serial.printf("LIGHT    : %d\n",   d.lightLevel);

    if (d.distanceCm > 0)
        Serial.printf("DISTANCE : %.1f cm\n", d.distanceCm);
    else
        Serial.println("DISTANCE : no echo / out of range");

    if (d.mpuOk) {
        Serial.printf("ACCEL    : %.2f g\n", d.accelG);
        Serial.printf("GYRO     : X=%.2f  Y=%.2f  Z=%.2f rad/s\n",
                      d.gyroX, d.gyroY, d.gyroZ);
    } else {
        Serial.println("ACCEL    : N/A (MPU6050 not found)");
    }

    Serial.printf("CURRENT  : %.3f A\n", d.currentA);
    Serial.println("======================================");
}


