#pragma once
#include "types.h"

// ─── FSM States ───────────────────────────────────────────────────────────────
//
//  OK       → LED verde dim,     silentios
//  WARNING  → LED portocaliu,    beep scurt la fiecare 3 s
//  CRITICAL → LED rosu,          beep rapid (150 ms on / 100 ms off)
//  MOTION   → LED rosu,          alert rapid (80 ms on / 70 ms off)
//
//  Transitions (prioritate descrescatoare):
//    sensor fault (DHT/MPU invalid dupa ce au fost valide)         → CRITICAL
//    fan ON + currentA < FAN_CURRENT_MIN_A (ACS712)                → CRITICAL
//    airQuality >= airCritical(dynamic MQ baseline)                → CRITICAL
//    temperature >= THRESH_TEMP_CRITICAL                           → CRITICAL
//    gyroMag (relativ la baseline) > 0.5 rad/s                    → MOTION
//    airQuality >= airWarning(dynamic MQ baseline)                 → WARNING
//    temperature >= tempWarning(dynamic DHT baseline)              → WARNING
//    altfel                                                        → OK
//
//  Nota: "nu respira" (flatline HC-SR04) este tratat in monitorul custom
//  din sensor_test.cpp, nu in FSM-ul generic.

typedef enum {
    FSM_OK = 0,
    FSM_WARNING,
    FSM_CRITICAL,
    FSM_MOTION
} FsmState;

// Apeleaza o data dupa initMPU6050(), cu baseline-ul gyro calculat pe 2 secunde
void        fsmInit(float gxBase, float gyBase, float gzBase);

// Evalueaza senzori, returneaza starea noua — apeleaza la fiecare 500 ms
FsmState    fsmUpdate(const SensorData &d);

// Conduce LED-urile si buzzer-ul — apeleaza la fiecare ~50 ms (non-blocking)
void        fsmApplyActuators(FsmState state);

// Notifica FSM cand ventilatorul este pornit/oprit (pentru verificarea ACS712)
void        fsmSetFan(bool on);

const char* fsmStateName(FsmState state);
