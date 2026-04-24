#include "mq135.h"
#include "config.h"
#include <Arduino.h>

static int _baseline = 0;

int readMq135() {
    return analogRead(PIN_MQ135);
}

void mq135Calibrate(int samples) {
    long sum = 0;
    for (int i = 0; i < samples; i++) {
        sum += analogRead(PIN_MQ135);
        delay(10);
    }
    _baseline = (int)(sum / samples);
    if (_baseline < SANITY_MQ135_MIN)  _baseline = SANITY_MQ135_MIN;
    if (_baseline > 3500)              _baseline = 3500;  // cap: nu calibra in aer poluat
    Serial.printf("[MQ3] Baseline calibrat: %d  (warn=%d  crit=%d)\n",
                  _baseline, mq135WarnThresh(), mq135CritThresh());
}

int mq135Baseline()    { return _baseline; }
int mq135WarnThresh()  { return _baseline + MQ135_OFFSET_WARNING; }
int mq135CritThresh()  { return _baseline + MQ135_OFFSET_CRITICAL; }
