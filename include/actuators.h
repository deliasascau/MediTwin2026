#pragma once
#include <Arduino.h>

void initActuators();
void ledSelfTest();

void setLed(uint8_t r, uint8_t g, uint8_t b);
void setFan(bool on);
void setBuzzer(bool on);
