#include "alarm_fsm.h"
#include "actuators.h"
#include "config.h"
#include "dht22.h"
#include "mq135.h"
#include <Arduino.h>
#include <math.h>

// Miscare detectata daca magnitudinea gyro depaseste baseline-ul cu mai mult de:
#define FSM_GYRO_MOTION_THRESH  0.50f   // rad/s

static float    _gxB = 0, _gyB = 0, _gzB = 0;
static bool     _baselineReady = false;

// Sensor fault tracking — setat la prima citire reusita, niciodata resetat
static bool     _seenDht = false;
static bool     _seenMpu = false;

// Stare interna pentru buzzer non-blocking
static FsmState _prevApplyState = FSM_OK;
static uint32_t _buzzTimer      = 0;
static bool     _buzzOn         = false;

// Stare FSM anterioara — folosita pentru histerezis la iesire din alarma
static FsmState _prevFsmState   = FSM_OK;

// Fan fault tracking via ACS712
static bool     _fanOn      = false;
static uint32_t _fanOnTime  = 0;

// ─── fsmInit ─────────────────────────────────────────────────────────────────
void fsmInit(float gxBase, float gyBase, float gzBase) {
    _gxB = gxBase;
    _gyB = gyBase;
    _gzB = gzBase;
    _baselineReady   = true;
    _prevApplyState  = FSM_OK;
    _buzzTimer       = 0;
    _buzzOn          = false;
    _seenDht         = false;
    _seenMpu         = false;
    _fanOn           = false;
    _fanOnTime       = 0;
    setBuzzer(false);
    setLed(0, 40, 0);   // verde dim = idle
}

// ─── fsmUpdate ───────────────────────────────────────────────────────────────
FsmState fsmUpdate(const SensorData &d) {
    // Actualizeaza masca senzorilor vazuti vreodata
    if (d.dhtOk) _seenDht = true;
    if (d.mpuOk) _seenMpu = true;

    // Prioritate 0 — SENSOR FAULT: senzor care a functionat a picat (flag fals)
    bool dhtFault = _seenDht && !d.dhtOk;
    bool mpuFault = _seenMpu && !d.mpuOk;

    if (dhtFault || mpuFault) {
        _prevFsmState = FSM_CRITICAL;
        return FSM_CRITICAL;
    }

    // Prioritate 0b — SENSOR FAULT: valori anormale = senzor blocat / defect electric
    // DHT22: temperatura sau umiditate in afara limitelor fizice
    bool dhtAbnormal = d.dhtOk &&
                       (d.temperature < SANITY_TEMP_MIN  ||
                        d.temperature > SANITY_TEMP_MAX  ||
                        d.humidity    < SANITY_HUM_MIN   ||
                        d.humidity    > SANITY_HUM_MAX);
    // MPU6050: acceleratie imposibila (senzor blocat = 0 sau supraincarcat)
    bool mpuAbnormal = d.mpuOk && (d.accelG > SANITY_ACCEL_MAX);
    // MQ-3: stuck la 0 (fir intrerupt) sau la 4095 (scurtcircuit)
    bool mqAbnormal  = (d.airQuality <= SANITY_MQ135_MIN ||
                        d.airQuality >= SANITY_MQ135_MAX);

    if (dhtAbnormal || mpuAbnormal || mqAbnormal) {
        _prevFsmState = FSM_CRITICAL;
        return FSM_CRITICAL;
    }

    // Prioritate 0c — FAN FAULT: ventilatorul e ON dar nu consuma curent
    if (_fanOn) {
        uint32_t elapsed = millis() - _fanOnTime;
        if (elapsed > 1000 && fabsf(d.currentA) < FAN_CURRENT_MIN_A) {
            _prevFsmState = FSM_CRITICAL;
            return FSM_CRITICAL;
        }
    }

    // Prioritate 1 — CRITICAL (valori periculoase)
    // Histerezis: daca eram deja in stare de alarma, pragul de IESIRE e mai mic cu 100 ADC
    //   -> previne oscilatie la limita care reseteaza buzzerul
    int critThresh = (mq135Baseline() > 0) ? mq135CritThresh() : THRESH_AIR_CRITICAL;
    int warnThresh = (mq135Baseline() > 0) ? mq135WarnThresh() : THRESH_AIR_WARNING;
    float tempWarn = THRESH_TEMP_WARNING;
    float tempCrit = THRESH_TEMP_CRITICAL;
    if (dhtBaselineReady()) {
        float relWarn = dhtBaselineTemp() + DHT_TEMP_RISE_WARNING_C;
        float relCrit = dhtBaselineTemp() + DHT_TEMP_RISE_CRITICAL_C;
        if (relWarn < tempWarn) tempWarn = relWarn;
        if (relCrit < tempCrit) tempCrit = relCrit;
    }
    if (_prevFsmState >= FSM_WARNING) { critThresh -= 100; warnThresh -= 100; }

    if (d.airQuality >= critThresh) { _prevFsmState = FSM_CRITICAL; return FSM_CRITICAL; }
    if (d.dhtOk && d.temperature >= tempCrit) { _prevFsmState = FSM_CRITICAL; return FSM_CRITICAL; }

    // Prioritate 2 — MOTION
    if (d.mpuOk && _baselineReady) {
        float dx = d.gyroX - _gxB;
        float dy = d.gyroY - _gyB;
        float dz = d.gyroZ - _gzB;
        if (sqrtf(dx*dx + dy*dy + dz*dz) > FSM_GYRO_MOTION_THRESH) { _prevFsmState = FSM_MOTION; return FSM_MOTION; }
    }

    // Prioritate 3 — WARNING
    if (d.airQuality >= warnThresh) { _prevFsmState = FSM_WARNING; return FSM_WARNING; }
    if (d.dhtOk && d.temperature >= tempWarn) { _prevFsmState = FSM_WARNING; return FSM_WARNING; }

    _prevFsmState = FSM_OK;
    return FSM_OK;
}

// ─── fsmApplyActuators ───────────────────────────────────────────────────────
// Non-blocking — apeleaza la ~50 ms pentru patterns responsive
void fsmApplyActuators(FsmState state) {
    uint32_t now = millis();

    // La tranzitie de stare: reset buzzer
    // _buzzTimer setat in trecut -> primul beep suna IMEDIAT la intrarea in alarma
    if (state != _prevApplyState) {
        setBuzzer(false);
        _buzzOn         = false;
        _buzzTimer      = (state == FSM_OK) ? now : (now - 3001UL);
        _prevApplyState = state;
    }

    switch (state) {

        case FSM_OK:
            setLed(0, 40, 0);
            setBuzzer(false);
            break;

        case FSM_WARNING:
            setLed(255, 100, 0);                        // portocaliu
            // beep 400 ms ON / 2600 ms OFF (clar audibil)
            if (_buzzOn  && now - _buzzTimer >= 400)  { setBuzzer(false); _buzzOn = false; _buzzTimer = now; }
            if (!_buzzOn && now - _buzzTimer >= 2600) { setBuzzer(true);  _buzzOn = true;  _buzzTimer = now; }
            break;

        case FSM_CRITICAL:
            setLed(255, 0, 0);                          // rosu
            // beep rapid: 150 ms on / 100 ms off
            if (_buzzOn  && now - _buzzTimer >= 150) { setBuzzer(false); _buzzOn = false; _buzzTimer = now; }
            if (!_buzzOn && now - _buzzTimer >= 100) { setBuzzer(true);  _buzzOn = true;  _buzzTimer = now; }
            break;

        case FSM_MOTION:
            setLed(255, 0, 0);                          // rosu
            // alert rapid: 80 ms on / 70 ms off
            if (_buzzOn  && now - _buzzTimer >= 80) { setBuzzer(false); _buzzOn = false; _buzzTimer = now; }
            if (!_buzzOn && now - _buzzTimer >= 70) { setBuzzer(true);  _buzzOn = true;  _buzzTimer = now; }
            break;
    }
}

// ─── fsmSetFan ───────────────────────────────────────────────────────────────
// Apeleaza din main.cpp / sensor_test.cpp inainte de a porni/opri efectiv FAN.
// FSM-ul monitorizeaza curentul ACS712 dupa 1 s de la pornire.
void fsmSetFan(bool on) {
    if (on && !_fanOn) {
        _fanOnTime = millis();  // marcheaza momentul pornirii
    }
    _fanOn = on;
}

// ─── fsmStateName ────────────────────────────────────────────────────────────
const char* fsmStateName(FsmState state) {
    switch (state) {
        case FSM_OK:       return "OK";
        case FSM_WARNING:  return "WARNING";
        case FSM_CRITICAL: return "CRITICAL";
        case FSM_MOTION:   return "MOTION";
        default:                 return "UNKNOWN";
    }
}
