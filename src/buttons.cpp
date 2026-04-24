#include "buttons.h"
#include "config.h"
#include <Arduino.h>

#define DEBOUNCE_MS      50
#define SHORT_MAX_MS    1000   // < 1s  → silence
#define MEDIUM_MAX_MS   3000   // 1-3s  → rearm
                               // > 3s  → standby

static bool     _lastRaw    = true;   // HIGH = neapasat
static bool     _pressed    = false;
static uint32_t _pressTime  = 0;      // millis() la momentul apasarii
static uint32_t _changeTime = 0;      // millis() la ultima tranzitie raw
static bool     _eventFired = false;

void initButtons() {
    pinMode(PIN_BTN_SILENCE, INPUT_PULLUP);
    Serial.printf("[BTN] Buton unic GPIO%d (INPUT_PULLUP, active LOW)\n", PIN_BTN_SILENCE);
}

BtnEvent pollButtons() {
    bool raw = (digitalRead(PIN_BTN_SILENCE) == LOW);
    uint32_t now = millis();

    // Detectare tranzitie
    if (raw != _lastRaw) {
        _lastRaw    = raw;
        _changeTime = now;
    }

    // Debounce
    if (now - _changeTime < DEBOUNCE_MS) return BTN_EVT_NONE;

    if (raw && !_pressed) {
        // Apasat
        _pressed    = true;
        _pressTime  = now;
        _eventFired = false;
    } else if (!raw && _pressed) {
        // Eliberat
        _pressed = false;
        if (!_eventFired) {
            _eventFired = true;
            uint32_t duration = now - _pressTime;
            if (duration < SHORT_MAX_MS)  return BTN_EVT_SILENCE_SHORT;
            if (duration < MEDIUM_MAX_MS) return BTN_EVT_REARM;
            return BTN_EVT_STANDBY;
        }
    }
    return BTN_EVT_NONE;
}
