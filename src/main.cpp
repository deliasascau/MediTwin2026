#include <Arduino.h>
#include "config.h"
#include "sensors.h"
#include "mq135.h"
#include "actuators.h"
#include "uart_comm.h"
#include "alarm_fsm.h"
#include "buttons.h"

// ─── Forward declarations ─────────────────────────────────────────────────────
static const char *classifyState(const SensorData &d);
static float       computeRisk(const SensorData &d);
static void        applyLocalSafety(const SensorData &d);
static void        handleCommand(PiCommand cmd);
static void        handleButtonEvent(BtnEvent evt);
static int         currentAirWarnThreshold();
static int         currentAirCritThreshold();
static float       currentTempWarnThreshold();

// ─── setup ───────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(1500);   // wait for USB CDC to enumerate on ESP32-C6

    Serial.println("\n================================================");
    Serial.println("  MediTwin AI — ESP32-C6  |  Booting...        ");
    Serial.println("================================================\n");

    initActuators();
    ledSelfTest();

    Serial.println("[BOOT] Initialising sensors...");
    initSensors();

    Serial.printf("[BOOT] MQ-3 warm-up (%d ms)...\n", MQ135_WARMUP_MS);
    delay(MQ135_WARMUP_MS);
    mq135Calibrate(MQ135_CAL_SAMPLES);

    Serial.println("[BOOT] DHT22 baseline calibration...");
    dhtCalibrate(DHT_CAL_SAMPLES, DHT_CAL_INTERVAL_MS);

    Serial.println("[BOOT] HC-SR04 baseline calibration...");
    hcsr04Calibrate(HCSR04_CAL_SAMPLES);

    Serial.println("[BOOT] ACS712 zero-current calibration...");
    setFan(false);
    acs712Calibrate(ACS712_CAL_SAMPLES);

    // Calibrare baseline gyro pentru FSM
    Serial.println("[BOOT] Calibrare FSM gyro baseline...");
    float gxB = 0, gyB = 0, gzB = 0;
    calibrateMpuGyroBaseline(gxB, gyB, gzB, MPU_GYRO_CAL_MS);
    fsmInit(gxB, gyB, gzB);
    Serial.println("[BOOT] FSM ready.");

    Serial.println("[BOOT] Initialising UART (Raspberry Pi)...");
    initUart();

    Serial.println("[BOOT] Initialising buttons...");
    initButtons();

    Serial.println("[BOOT] System ready.\n");
    setLed(0, 20, 0);  // dim green = idle + safe
}

// ─── loop ────────────────────────────────────────────────────────────────────
void loop() {
    static uint32_t lastSensor = 0;
    static uint32_t lastSend   = 0;

    if (millis() - lastSensor >= SENSOR_INTERVAL_MS) {
        lastSensor = millis();

        SensorData  data  = collectAll();
        float       score = computeRisk(data);
        const char *state = classifyState(data);

        printSensorData(data);
        Serial.printf("STATE: %s  |  RISK SCORE: %.0f\n\n", state, score);

        applyLocalSafety(data);

        // Trimite telemetrie la Pi prin UART
        if (millis() - lastSend >= SEND_INTERVAL_MS) {
            lastSend = millis();
            sendTelemetry(data, state, score);
        }

        // Citeste comanda de la Pi
        PiCommand cmd = pollCommand();
        handleCommand(cmd);
    }

    // Butoanele se verifica la FIECARE iteratie de loop (nu doar la intervalul senzorilor)
    BtnEvent evt = pollButtons();
    if (evt != BTN_EVT_NONE) handleButtonEvent(evt);
}

// ─── handleButtonEvent ────────────────────────────────────────────────────
static void handleButtonEvent(BtnEvent evt) {
    static uint32_t silenceUntil   = 0;  // 0 = buzzer activ
    static bool     standby        = false;
    static bool     permSilence    = false;
    uint32_t        now            = millis();

    switch (evt) {

        case BTN_EVT_SILENCE_SHORT:
            // Silentiere 30 s — LED ramane, FSM continua
            permSilence = false;
            silenceUntil = now + 30000UL;
            setBuzzer(false);
            Serial.println("[BTN] Alarma silentiata 30 s");
            break;

        case BTN_EVT_REARM:
            // Apasare medie (1-3s) — rearmare FSM
            standby     = false;
            permSilence = false;
            silenceUntil = 0;
            fsmInit(0, 0, 0);
            setLed(0, 20, 0);
            Serial.println("[BTN] FSM rearmat");
            break;

        case BTN_EVT_STANDBY:
            // Apasare lunga (>3s) — toggle standby
            standby = !standby;
            if (standby) {
                setBuzzer(false);
                setFan(false);
                setLed(0, 0, 5);
                Serial.println("[BTN] Sistem in STANDBY");
            } else {
                fsmInit(0, 0, 0);
                setLed(0, 20, 0);
                permSilence = false;
                silenceUntil = 0;
                Serial.println("[BTN] Sistem REACTIVAT");
            }
            break;

        default: break;
    }

    // Expirare snooze: re-armeaza buzzerul daca FSM e inca in alarma
    if (!permSilence && silenceUntil != 0 && now >= silenceUntil) {
        silenceUntil = 0;
        Serial.println("[BTN] Snooze expirat — buzzerul se rearmeaza");
    }

    // In standby: FSM-ul NU aplica actuatoare (suprascrie orice)
    // Variabila standby e locala acestei functii — o expunem prin getter simplu
    // (nu e nevoie de extern, main.cpp o foloseste local)
    (void)standby;  // folosita deasupra, suprima warning
}

// ─── classifyState ───────────────────────────────────────────────────────────
// Local classification — runs even when WiFi is down (safety fallback)
static const char *classifyState(const SensorData &d) {
    int airCrit = currentAirCritThreshold();
    int airWarn = currentAirWarnThreshold();
    float tempWarn = currentTempWarnThreshold();

    if (d.airQuality >= airCrit ||
        d.temperature >= THRESH_TEMP_CRITICAL) return "CRITICAL";

    if (d.airQuality >= airWarn ||
        d.temperature >= tempWarn)  return "WARNING";

    return "SAFE";
}

// ─── computeRisk — weighted additive score (0–100) ───────────────────────────
static float computeRisk(const SensorData &d) {
    float score = 0;
    int airCrit = currentAirCritThreshold();
    int airWarn = currentAirWarnThreshold();
    float tempWarn = currentTempWarnThreshold();

    if      (d.airQuality  >= airCrit)  score += 40;
    else if (d.airQuality  >= airWarn)  score += 20;

    if      (d.temperature >= THRESH_TEMP_CRITICAL) score += 30;
    else if (d.temperature >= tempWarn)             score += 15;

    if (d.distanceCm > 0 && d.distanceCm < THRESH_PRESENCE_CM) score += 15;

    if (d.mpuOk && d.accelG > 1.3f) score += 10;  // vibration / tamper

    return score;
}

// ─── applyLocalSafety ────────────────────────────────────────────────────────
// LED + Buzzer → controlate de FSM.  Fan → logica locala.
// Raspberry commands (handleCommand) pot suprascrie aceasta functie.
static void applyLocalSafety(const SensorData &d) {
    FsmState state = fsmUpdate(d);
    fsmApplyActuators(state);

    int airCrit = currentAirCritThreshold();
    int airWarn = currentAirWarnThreshold();
    float tempWarn = currentTempWarnThreshold();

    // Fan controlat direct dupa stare
    if (d.airQuality >= airCrit ||
        d.temperature >= THRESH_TEMP_CRITICAL) {
        fsmSetFan(true);  setFan(true);
    } else if (d.airQuality >= airWarn ||
               d.temperature >= tempWarn) {
        fsmSetFan(true);  setFan(true);
    } else {
        fsmSetFan(false); setFan(false);
    }
}

static int currentAirWarnThreshold() {
    return (mq135Baseline() > 0) ? mq135WarnThresh() : THRESH_AIR_WARNING;
}

static int currentAirCritThreshold() {
    return (mq135Baseline() > 0) ? mq135CritThresh() : THRESH_AIR_CRITICAL;
}

static float currentTempWarnThreshold() {
    float t = THRESH_TEMP_WARNING;
    if (dhtBaselineReady()) {
        float relWarn = dhtBaselineTemp() + DHT_TEMP_RISE_WARNING_C;
        if (relWarn > t) t = relWarn;
    }
    return t;
}

// ─── handleCommand ───────────────────────────────────────────────────────────
static void handleCommand(PiCommand cmd) {
    switch (cmd) {
        case PI_CMD_FAN_ON:      fsmSetFan(true);  setFan(true);   break;
        case PI_CMD_FAN_OFF:     fsmSetFan(false); setFan(false);  break;
        case PI_CMD_ALARM_ON:    setBuzzer(true);                  break;
        case PI_CMD_ALARM_OFF:   setBuzzer(false);                 break;
        case PI_CMD_FSM_RESET:   fsmInit(0, 0, 0);                 break;
        default: break;
    }
}


