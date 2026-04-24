#include "mpu6050_sens.h"
#include "config.h"
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

static Adafruit_MPU6050 mpu;
static bool ready = false;

bool initMPU6050() {
    ready = mpu.begin(MPU6050_ADDR, &Wire);
    if (ready) {
        mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
        mpu.setGyroRange(MPU6050_RANGE_250_DEG);
        mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
        Serial.println("[SENSOR] MPU6050 OK");
    } else {
        Serial.printf("[WARN]   MPU6050 NOT found at 0x%02X — check AD0 pin\n", MPU6050_ADDR);
    }
    return ready;
}

bool readMpu6050(float &accelG, float &gx, float &gy, float &gz) {
    if (!ready) return false;
    sensors_event_t a, g, t;
    mpu.getEvent(&a, &g, &t);

    // Total acceleration magnitude converted to g
    accelG = sqrtf(
        a.acceleration.x * a.acceleration.x +
        a.acceleration.y * a.acceleration.y +
        a.acceleration.z * a.acceleration.z
    ) / 9.81f;

    gx = g.gyro.x;
    gy = g.gyro.y;
    gz = g.gyro.z;
    return true;
}

bool calibrateMpuGyroBaseline(float &gxBase, float &gyBase, float &gzBase,
                              uint32_t durationMs) {
    gxBase = gyBase = gzBase = 0.0f;
    if (!ready) {
        Serial.println("[MPU6050] Baseline calibrare esuata (MPU not ready)");
        return false;
    }

    bool haveBest = false;
    float bestGx = 0.0f, bestGy = 0.0f, bestGz = 0.0f;
    float bestAvgMag = 9999.0f, bestPeakMag = 9999.0f;

    for (int attempt = 1; attempt <= MPU_GYRO_CAL_MAX_RETRIES; ++attempt) {
        float sx = 0.0f, sy = 0.0f, sz = 0.0f;
        float sumMag = 0.0f, peakMag = 0.0f;
        int n = 0;
        uint32_t t0 = millis();

        while (millis() - t0 < durationMs) {
            float ag, gx, gy, gz;
            if (readMpu6050(ag, gx, gy, gz)) {
                sx += gx;
                sy += gy;
                sz += gz;
                float mag = sqrtf(gx * gx + gy * gy + gz * gz);
                sumMag += mag;
                if (mag > peakMag) peakMag = mag;
                n++;
            }
            delay(20);
        }

        if (n == 0) {
            delay(MPU_GYRO_CAL_RETRY_DELAY_MS);
            continue;
        }

        float gxTry = sx / n;
        float gyTry = sy / n;
        float gzTry = sz / n;
        float avgMag = sumMag / n;

        if (!haveBest || avgMag < bestAvgMag) {
            haveBest = true;
            bestGx = gxTry;
            bestGy = gyTry;
            bestGz = gzTry;
            bestAvgMag = avgMag;
            bestPeakMag = peakMag;
        }

        bool stable = (avgMag <= MPU_GYRO_STILL_AVG_MAX) &&
                      (peakMag <= MPU_GYRO_STILL_PEAK_MAX);
        if (stable) {
            gxBase = gxTry;
            gyBase = gyTry;
            gzBase = gzTry;
            Serial.printf("[MPU6050] Gyro baseline: X=%.3f Y=%.3f Z=%.3f rad/s (auto %d/%d, avg=%.3f peak=%.3f)\n",
                          gxBase, gyBase, gzBase,
                          attempt, MPU_GYRO_CAL_MAX_RETRIES,
                          avgMag, peakMag);
            return true;
        }

        Serial.printf("[MPU6050] Calibrare instabila (%d/%d): avg=%.3f peak=%.3f -> retry\n",
                      attempt, MPU_GYRO_CAL_MAX_RETRIES, avgMag, peakMag);
        delay(MPU_GYRO_CAL_RETRY_DELAY_MS);
    }

    if (haveBest) {
        gxBase = bestGx;
        gyBase = bestGy;
        gzBase = bestGz;
        Serial.printf("[MPU6050] Baseline fallback (miscare detectata): X=%.3f Y=%.3f Z=%.3f  avg=%.3f peak=%.3f\n",
                      gxBase, gyBase, gzBase, bestAvgMag, bestPeakMag);
        return true;
    }

    Serial.println("[MPU6050] Baseline calibrare esuata (fara citiri valide)");
    return false;
}
