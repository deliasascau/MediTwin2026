#pragma once

// ─── types ───────────────────────────────────────────────────────────────────
#include "types.h"

// ─── per-sensor modules ──────────────────────────────────────────────────────
#include "dht22.h"
#include "mq135.h"
#include "ldr.h"
#include "hcsr04.h"
#include "mpu6050_sens.h"
#include "acs712.h"

// ─── coordinator (sensors.cpp) ───────────────────────────────────────────────
void       initSensors();         // init I2C bus + all sensor drivers
SensorData collectAll();          // read every sensor → one snapshot
void       printSensorData(const SensorData &d);


