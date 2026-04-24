#pragma once
#include <stdint.h>

void initDHT22();
bool readDht(float &temp, float &hum);

// Calibrare de boot: media valorilor valide devine baseline local.
bool dhtCalibrate(int samples = 2, uint32_t intervalMs = 2200);

bool  dhtBaselineReady();
float dhtBaselineTemp();
float dhtBaselineHum();
