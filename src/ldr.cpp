#include "ldr.h"
#include "config.h"
#include <Arduino.h>

static int _baseline = 0;

int readLdr() {
    return analogRead(PIN_LDR);
}

void ldrCalibrate(int samples) {
    long sum = 0;
    for (int i = 0; i < samples; i++) {
        sum += analogRead(PIN_LDR);
        delay(10);
    }
    _baseline = (int)(sum / samples);
    // Clamp: evita baseline 0 sau valori imposibile
    if (_baseline < 50)   _baseline = 50;
    if (_baseline > 4050) _baseline = 4050;
    Serial.printf("[LDR] Baseline calibrat: %d  (lumina normala la pornire)\n", _baseline);
}

int ldrBaseline() {
    return _baseline;
}

// Praguri relative fata de baseline:
//   BRIGHT  > 120% din baseline  (mai multa lumina decat normal)
//   NORMAL  80-120%
//   DIM     50-80%
//   DARK    < 50%
const char* ldrLabel(int raw) {
    if (_baseline == 0) {
        // Fara calibrare - fallback la praguri absolute din config.h
        return raw < THRESH_LDR_DARK   ? "[DARK]"   :
               raw < THRESH_LDR_DIM    ? "[DIM]"    :
               raw < THRESH_LDR_NORMAL ? "[NORMAL]" : "[BRIGHT]";
    }
    float pct = (raw * 100.0f) / _baseline;
    if (pct >= 120.0f) return "[BRIGHT]";
    if (pct >=  80.0f) return "[NORMAL]";
    if (pct >=  50.0f) return "[DIM]";
    return "[DARK]";
}
