#include "acs712.h"
#include "config.h"
#include <Arduino.h>

static float _midRaw = (float)ACS712_MIDPOINT;

void acs712Calibrate(int samples) {
    long sum = 0;
    for (int i = 0; i < samples; i++) {
        sum += analogRead(PIN_ACS712);
        delay(2);
    }
    _midRaw = sum / (float)samples;
    Serial.printf("[ACS712] Midpoint calibrat: %.1f raw ADC\n", _midRaw);
}

int acs712MidpointRaw() {
    return (int)(_midRaw + 0.5f);
}

float readAcs712() {
    // Average 20 quick samples to reduce ADC noise
    long sum = 0;
    for (int i = 0; i < 20; i++) {
        sum += analogRead(PIN_ACS712);
        delayMicroseconds(500);
    }
    float avg  = sum / 20.0f;
    // Tensiunea la pinul ADC (dupa divizor 10k/10k = factor 0.5)
    float adcV = (avg / ADC_RESOLUTION) * ADC_VREF;
    // Tensiunea reala la iesirea ACS712 (reconstituita prin inmultire cu inversul divizorului)
    float voltage = adcV * ACS712_DIVIDER_INV;
    float midV    = (_midRaw / ADC_RESOLUTION) * ADC_VREF * ACS712_DIVIDER_INV;
    // current = ΔV / sensibilitate (mV/A → V/A)
    return (voltage - midV) / (ACS712_MV_PER_A / 1000.0f);
}
