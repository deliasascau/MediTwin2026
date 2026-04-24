#pragma once
#include <stdint.h>

// ─── Logica buton unic ────────────────────────────────────────────────────────
//
//  BTN_SILENCE (GPIO15) — Buton unic de control alarma
//    Apasare scurta  (< 1s)  → Silence buzzer 30 s (medicul a ajuns)
//    Apasare medie   (1-3s)  → Rearmare FSM (reset dupa rezolvarea situatiei)
//    Apasare lunga   (> 3s)  → Toggle standby (sistem oprit/pornit)
//
//  Buton: active LOW (INPUT_PULLUP), debounce 50 ms.
//  Conectare: GPIO15 → buton → GND  (fara rezistor extern)

typedef enum {
    BTN_EVT_NONE = 0,
    BTN_EVT_SILENCE_SHORT,   // < 1s   → silence 30s
    BTN_EVT_REARM,           // 1-3s   → rearmare FSM
    BTN_EVT_STANDBY          // > 3s   → toggle standby
} BtnEvent;

void     initButtons();
BtnEvent pollButtons();
