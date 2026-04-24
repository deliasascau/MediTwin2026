/*
 * sensor_test.cpp — MediTwin AI  |  Sensor Bring-Up & Calibration Tool
 *
 * Upload with:
 *   pio run -e meditwin_test -t upload
 *   pio device monitor -e meditwin_test
 *
 * After boot, send a letter via Serial Monitor to run a test:
 *
 *   1  — LDR          (light sensor)
 *   2  — MQ-3         (air quality)
 *   3  — HC-SR04      (distance / presence)
 *   4  — DHT22        (temperature + humidity)
 *   6  — MPU6050      (accelerometer + gyroscope)
 *   7  — ACS712       (current — calibration included)
 *   8  — I2C scanner  (finds all I2C devices on the bus)
 *   9  — ALL sensors  (single snapshot)
 *   0  — Stability    (200 samples, shows min/avg/max for all analog sensors)
 *   L  — LED test     (cycles all RGB colours + sets brightness)
 *   F  — Fan test     (ON 3 s then OFF)
 *   S  — Servo test   (0 → 45 → 90 → 0)
 *   B  — Buzzer test  (beep 3×)
 *   ?  — Print this help
 */

#ifdef SENSOR_TEST_MODE

#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include "config.h"
#include "sensors.h"
#include "ldr.h"
#include "actuators.h"
#include "alarm_fsm.h"
#include "buttons.h"
#include "scheduler.h"
#include "uart_comm.h"

typedef enum {
    MON_LEVEL_OK = 0,
    MON_LEVEL_WARNING,
    MON_LEVEL_CRITICAL
} MonLevel;

typedef enum {
    MON_REASON_NONE = 0,
    MON_REASON_MQ_CRITICAL,
    MON_REASON_LDR_BRIGHT,
    MON_REASON_HC_FLATLINE,
    MON_REASON_DHT_WARNING,
    MON_REASON_MPU_CRITICAL,
    MON_REASON_FAN_NO_CURRENT
} MonReason;

static MonLevel  _monLevel       = MON_LEVEL_OK;
static MonReason _monReason      = MON_REASON_NONE;
static bool      _standby        = false;
static bool      _fanForced      = false;
static bool      _fanApplied     = false;
static uint32_t  _fanStartMs     = 0;
static uint32_t  _recoverSinceMs = 0;
static uint32_t  _bootMs         = 0;
static uint32_t  _manualFanUntilMs = 0;  // fan fortat manual din '+'
static bool      _hcEnabled        = false; // HC alarm ON/OFF controlat de Pi (CMD:HC_ON/HC_OFF)
static uint32_t  _ldrTrigSinceMs   = 0;     // momentul cand LDR a depasit pragul prima data
// timestamps ultimei rulari per task (pentru afisare paralelism)
static uint32_t  _tsButtons = 0, _tsSensors = 0, _tsDht = 0, _tsAlarm = 0, _tsUart = 0, _tsPrint = 0;
static uint32_t  _hcWindowStart  = 0;
static float     _hcMin          = 9999.0f;
static float     _hcMax          = 0.0f;
static int       _hcSamples      = 0;

static const char *monLevelName(MonLevel level) {
    switch (level) {
        case MON_LEVEL_OK:       return "SAFE";
        case MON_LEVEL_WARNING:  return "WARNING";
        case MON_LEVEL_CRITICAL: return "CRITICAL";
        default:                 return "UNKNOWN";
    }
}

static const char *monReasonName(MonReason reason) {
    switch (reason) {
        case MON_REASON_MQ_CRITICAL:  return "MQ critical";
        case MON_REASON_LDR_BRIGHT:   return "LDR deviere mare";
        case MON_REASON_HC_FLATLINE:  return "HC fara fluctuatii";
        case MON_REASON_DHT_WARNING:  return "DHT temp warning";
        case MON_REASON_MPU_CRITICAL: return "MPU miscare";
        case MON_REASON_FAN_NO_CURRENT:return "Fan fara curent";
        default:                      return "none";
    }
}

static void resetHcWindow(uint32_t now) {
    _hcWindowStart = now;
    _hcMin = 9999.0f;
    _hcMax = 0.0f;
    _hcSamples = 0;
}

static void setFanManaged(bool on) {
    if (on == _fanApplied) return;
    _fanApplied = on;
    fsmSetFan(on);
    setFan(on);
}

static void setMonitorAlarm(MonLevel level, MonReason reason, bool forceFan, uint32_t now) {
    _monLevel = level;
    _monReason = reason;
    _fanForced = forceFan;
    _recoverSinceMs = 0;

    if (forceFan) {
        _fanStartMs = now;
        setFanManaged(true);
    } else {
        setFanManaged(false);
    }

    if (level == MON_LEVEL_CRITICAL) {
        setLed(255, 0, 0);
        setBuzzer(true);
    } else if (level == MON_LEVEL_WARNING) {
        setLed(255, 220, 0);
        setBuzzer(true);
    } else {
        setLed(0, 20, 0);
        setBuzzer(false);
    }

    Serial.printf("[ALARM] %s (%s)\n", monLevelName(level), monReasonName(reason));
}

static void acknowledgeMonitorAlarm(uint32_t now) {
    MonReason prev = _monReason;

    _monLevel = MON_LEVEL_OK;
    _monReason = MON_REASON_NONE;
    _fanForced = false;
    _recoverSinceMs = 0;
    setFanManaged(false);
    setBuzzer(false);
    setLed(0, 20, 0);

    // Cerinta: la reset dupa MQ critical, baseline MQ devine valoarea noua.
    if (prev == MON_REASON_MQ_CRITICAL || prev == MON_REASON_FAN_NO_CURRENT) {
        Serial.println("[BTN] Recalibrare MQ-3 baseline...");
        mq135Calibrate(20);
    }

    resetHcWindow(now);
    Serial.println("[BTN] Alarma confirmata, sistem revenit in SAFE");
}

// ─── helpers ─────────────────────────────────────────────────────────────────
static void printHeader(const char *title) {
    Serial.println();
    Serial.println("+-----------------------------------------+");
    Serial.printf( "|  %-40s|\n", title);
    Serial.println("+-----------------------------------------+");
}

static void printHelp() {
    Serial.println(F(
        "\n  1=LDR  2=MQ3  3=HC-SR04  4=DHT22\n"
        "  6=MPU6050  7=ACS712  8=I2C scan  9=ALL  0=Stability\n"
        "  L=LED  F=Fan(auto)  +=Fan ON  -=Fan OFF  B=Buzzer  R=Breathing\n"
        "  M=Motion alarm  A=FSM Alarm (monitor automat)  ?=Help\n"
        "  BTN GPIO15: short/medium = ACK alarm, long = standby toggle\n"
        "  Comenzile SAFE/WARNING/CRITICAL + FAN/HC se trimit de la Raspberry Pi\n"
    ));
}

// ─── individual tests ────────────────────────────────────────────────────────

static void testLdr() {
    printHeader("LDR — Light Sensor");
    Serial.println("Cover sensor (dark) then expose to light.");
    Serial.println("Press any key to stop.\n");
    while (!Serial.available()) {
        int v = readLdr();
        float pct = (v / 4095.0f) * 100.0f;
        Serial.printf("  ADC raw: %4d  |  %.1f %%  %s  (baseline: %d)\n",
                      v, pct, ldrLabel(v), ldrBaseline());
        delay(400);
    }
    Serial.read();
}

static void testMq135() {
    printHeader("MQ-3 — Air Quality");
    Serial.println("Calibrating baseline... keep air clean for 3 seconds.");
    // Average 6 samples over 3 seconds as baseline
    long sum = 0;
    for (int i = 0; i < 6; i++) { sum += readMq135(); delay(500); }
    int baseline = (int)(sum / 6);
    Serial.printf("  Baseline: %d  |  WARNING at +100, CRITICAL at +200\n\n", baseline);
    Serial.println("Press any key to stop.");
    while (!Serial.available()) {
        int v = readMq135();
        int delta = v - baseline;
        Serial.printf("  ADC raw: %4d  (delta: %+d)  |  %s\n",
                      v, delta,
                      delta < 100 ? "[CLEAN]"   :
                      delta < 200 ? "[WARNING]" : "[CRITICAL]");
        delay(500);
    }
    Serial.read();
}

static void testHcsr04() {
    printHeader("HC-SR04 — Distance / Presence");
    Serial.println("Move hand in front. Max reliable range ~300 cm.");
    Serial.println("Press any key to stop.\n");
    while (!Serial.available()) {
        float d = readDistance();
        if (d < 0)
            Serial.println("  No echo — out of range or no object");
        else
            Serial.printf("  Distance: %6.1f cm  %s\n",
                          d,
                          d < THRESH_PRESENCE_CM ? "[PRESENCE]" : "[empty]");
        delay(300);
    }
    Serial.read();
}

static void testBreathing() {
    printHeader("HC-SR04 — Breathing Rate (balloon sim)");
    Serial.println("Keep balloon still for 3 seconds (baseline calibration)...");

    // Calibrate baseline
    float sum = 0; int n = 0;
    unsigned long t0 = millis();
    while (millis() - t0 < 3000) {
        float d = readDistance();
        if (d > 0) { sum += d; n++; }
        delay(100);
    }
    if (n == 0) { Serial.println("  ERROR: no echo — check wiring."); return; }
    float baseline = sum / n;
    const float THRESH_CM = 0.5f;  // min movement to count as a breath

    Serial.printf("  Baseline: %.1f cm | threshold: %.1f cm\n", baseline, THRESH_CM);
    Serial.println("  Measuring 30 s (press any key to stop early)...\n");

    int breathCount = 0;
    bool inBreath = false;
    unsigned long startMs = millis();

    while (!Serial.available() && (millis() - startMs) < 30000UL) {
        float d = readDistance();
        if (d < 0) { delay(100); continue; }
        float delta = baseline - d;  // positive = balloon expanding toward sensor
        if (!inBreath && delta > THRESH_CM) {
            inBreath = true;          // balloon started expanding
        } else if (inBreath && delta < (THRESH_CM * 0.3f)) {
            inBreath = false;         // balloon returned to rest
            breathCount++;
            float elapsed = (millis() - startMs) / 1000.0f;
            float bpm = (breathCount / elapsed) * 60.0f;
            Serial.printf("  Breath #%d  |  %.1f s elapsed  |  BPM: %.1f\n",
                          breathCount, elapsed, bpm);
        }
        delay(100);
    }
    if (Serial.available()) Serial.read();

    float elapsed = (millis() - startMs) / 1000.0f;
    if (elapsed < 0.1f || breathCount == 0) {
        Serial.println("  No breaths detected — check balloon movement or threshold.");
        return;
    }
    float bpm = (breathCount / elapsed) * 60.0f;
    Serial.printf("\n  === RESULT: %d breaths in %.1f s = %.1f BPM ===\n",
                  breathCount, elapsed, bpm);
    Serial.printf("  Status: %s\n",
                  bpm < 20 ? "[BRADYPNEA — prea rar]"    :
                  bpm < 30 ? "[LENT]"                    :
                  bpm < 60 ? "[NORMAL — nou-nascut]"     :
                  bpm < 70 ? "[RAPID]"                   : "[TAHIPNEE — prea rapid]");
}

static void testDht22() {
    printHeader("DHT22 — Temperature + Humidity");
    Serial.println("Min read interval: 2 s (DHT protocol limit).");
    Serial.println("Press any key to stop.\n");
    int errors = 0;
    while (!Serial.available()) {
        float t, h;
        if (readDht(t, h)) {
            Serial.printf("  Temp: %5.1f °C  |  Hum: %5.1f %%\n", t, h);
            errors = 0;
        } else {
            errors++;
            Serial.printf("  Read error #%d — check wiring / pull-up\n", errors);
        }
        delay(2200);
    }
    Serial.read();
}

static void testMpu6050() {
    printHeader("MPU6050 — Accelerometer + Gyroscope");
    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setTimeOut(100);
    if (!initMPU6050()) {
        Serial.println("  FAILED — sensor not found. Check I2C wiring.");
        Serial.println("  Run '8' (I2C scan) to verify address.");
        return;
    }
    Serial.println("Flat on table → accel ≈ 1.00 g, gyro ≈ 0.");
    Serial.println("Tilt it and watch values change.");
    Serial.println("Press any key to stop.\n");
    while (!Serial.available()) {
        float ag, gx, gy, gz;
        if (readMpu6050(ag, gx, gy, gz))
            Serial.printf("  Accel: %5.2f g  |  Gyro X:%6.2f  Y:%6.2f  Z:%6.2f rad/s\n",
                          ag, gx, gy, gz);
        else
            Serial.println("  Read failed");
        delay(250);
    }
    Serial.read();
}

static void testAcs712() {
    printHeader("ACS712 — Current Sensor + Calibration");
    Serial.println("Step 1: No load connected — reading zero-current offset.\n");

    // Measure zero point: average 200 samples
    long sum = 0;
    for (int i = 0; i < 200; i++) {
        sum += analogRead(PIN_ACS712);
        delay(2);
    }
    int zeroRaw = sum / 200;
    float zeroV = (zeroRaw / ADC_RESOLUTION) * ADC_VREF;

    Serial.printf("  Zero-current raw ADC : %d\n", zeroRaw);
    Serial.printf("  Zero-current voltage : %.4f V\n", zeroV);
    Serial.printf("  Configured MIDPOINT  : %d  (config.h)\n\n", ACS712_MIDPOINT);

    if (abs(zeroRaw - ACS712_MIDPOINT) > 100)
        Serial.printf("  ⚠  Offset large — update ACS712_MIDPOINT to %d in config.h\n\n",
                      zeroRaw);
    else
        Serial.println("  OK — midpoint within tolerance.\n");

    Serial.println("Now reading live current. Press any key to stop.");
    while (!Serial.available()) {
        float c = readAcs712();
        Serial.printf("  Current: %+7.3f A  ", c);
        if      (fabsf(c) < 0.05f) Serial.println("[idle / no load]");
        else if (fabsf(c) < 2.0f ) Serial.println("[low load]");
        else                        Serial.println("[load detected]");
        delay(300);
    }
    Serial.read();
}

static void i2cScan() {
    printHeader("I2C Bus Scanner");
    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setTimeOut(100);
    Serial.printf("SDA=GPIO%d  SCL=GPIO%d\n\n", PIN_SDA, PIN_SCL);

    int found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("  Found device at 0x%02X", addr);
            if      (addr == 0x76 || addr == 0x77) Serial.print("  ← BMP280");
            else if (addr == 0x68 || addr == 0x69) Serial.print("  ← MPU6050");
            else if (addr == 0x3C || addr == 0x3D) Serial.print("  ← OLED?");
            Serial.println();
            found++;
        }
        delay(5);
    }
    if (found == 0)
        Serial.println("  No I2C devices found — check SDA/SCL/VCC wiring.");
    else
        Serial.printf("\n  Total: %d device(s)\n", found);
}

static void testAll() {
    printHeader("All Sensors — Single Snapshot");
    SensorData d = collectAll();
    printSensorData(d);
}

// ─── Stability test: 200 samples of all analog sensors ───────────────────────
static void stabilityTest() {
    printHeader("Stability Test — 200 samples");
    Serial.println("Collecting 200 samples over ~10 s. Do not touch hardware.\n");

    int ldrMin=4095, ldrMax=0, mqMin=4095, mqMax=0, acsMin=4095, acsMax=0;
    long ldrSum=0, mqSum=0, acsSum=0;
    const int N = 200;

    for (int i = 0; i < N; i++) {
        int lv = readLdr(), mv = readMq135(), av = analogRead(PIN_ACS712);
        ldrMin=min(ldrMin,lv); ldrMax=max(ldrMax,lv); ldrSum+=lv;
        mqMin =min(mqMin, mv); mqMax =max(mqMax, mv); mqSum +=mv;
        acsMin=min(acsMin,av); acsMax=max(acsMax,av); acsSum+=av;
        delay(50);
        if (i % 40 == 39) Serial.printf("  ...%d%%\n", (i+1)*100/N);
    }

    Serial.println();
    Serial.printf("  LDR     min=%4d  avg=%4d  max=%4d  spread=%3d%s\n",
                  ldrMin, (int)(ldrSum/N), ldrMax, ldrMax-ldrMin,
                  (ldrMax-ldrMin)>100?" ⚠ noisy":"");
    Serial.printf("  MQ-3    min=%4d  avg=%4d  max=%4d  spread=%3d%s\n",
                  mqMin,  (int)(mqSum/N),  mqMax,  mqMax-mqMin,
                  (mqMax-mqMin)>50?" ⚠ noisy (warm-up?)":"");
    Serial.printf("  ACS712  min=%4d  avg=%4d  max=%4d  spread=%3d%s\n",
                  acsMin, (int)(acsSum/N), acsMax, acsMax-acsMin,
                  (acsMax-acsMin)>30?" ⚠ noisy (decoupling?)":"");

    Serial.println("\n  Tip: MQ-3 spread >50 in first 5 min is normal (heater warm-up).");
    Serial.println("  Tip: ACS712 spread >30 → add 100 nF cap between OUT and GND.");
}

// ─── Actuator tests ──────────────────────────────────────────────────────────
static void testLed() {
    printHeader("RGB LED Test");
    const struct { uint8_t r,g,b; const char *name; } cols[] = {
        {255,0,0,"RED"},{0,255,0,"GREEN"},{0,0,255,"BLUE"},
        {255,165,0,"ORANGE"},{255,0,255,"MAGENTA"},{0,255,255,"CYAN"},
        {255,255,255,"WHITE"},{0,0,0,"OFF"}
    };
    for (auto &c : cols) {
        Serial.printf("  → %s\n", c.name);
        setLed(c.r, c.g, c.b);
        delay(600);
    }

    // Brightness sweep on green
    Serial.println("  Brightness sweep (green)...");
    for (int b = 0; b <= 255; b += 5) { setLed(0, b, 0); delay(10); }
    for (int b = 255; b >= 0; b -= 5) { setLed(0, b, 0); delay(10); }
    setLed(0, 0, 0);
    Serial.println("  Done.");
}

static void testFan() {
    printHeader("Fan Test (relay/MOSFET)");
    Serial.println("  Fan ON  → watch ACS712 current rise if wired in series.");
    setFan(true);
    delay(3000);
    float c = readAcs712();
    Serial.printf("  Current while ON: %.3f A\n", c);
    if (fabsf(c) < 0.05f)
        Serial.println("  ⚠  No current detected — check fan wiring or ACS712 circuit.");
    setFan(false);
    Serial.println("  Fan OFF.");
}

static void testBuzzer() {
    printHeader("Buzzer Test");
    for (int i = 0; i < 3; i++) {
        setBuzzer(true);  delay(150);
        setBuzzer(false); delay(200);
    }
    Serial.println("  3 beeps done.");
}

// ─── Motion alarm: MPU6050 + LED rosu + Buzzer ───────────────────────────────
static void testMpuAlarm() {
    printHeader("MPU6050 — Motion Alarm");
    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setTimeOut(100);
    if (!initMPU6050()) {
        Serial.println("  FAILED — MPU6050 not found. Run '8' to scan I2C.");
        return;
    }

    // Calibrare auto baseline gyro (cu retry daca detecteaza miscare)
    Serial.println("  Autocalibrare gyro in curs...");
    float gxBase = 0, gyBase = 0, gzBase = 0;
    if (!calibrateMpuGyroBaseline(gxBase, gyBase, gzBase, MPU_GYRO_CAL_MS)) {
        Serial.println("  Read error.");
        return;
    }
    Serial.printf("  Baseline gyro: X=%.3f Y=%.3f Z=%.3f rad/s\n", gxBase, gyBase, gzBase);
    Serial.println("  Praguri: SLOW>0.30 | MEDIUM>0.80 | FAST>2.00 rad/s");
    Serial.println("  Apasa orice tasta pentru a opri.\n");

    bool alertActive = false;

    while (!Serial.available()) {
        float ag, gx, gy, gz;
        if (!readMpu6050(ag, gx, gy, gz)) { delay(100); continue; }

        // Gyro magnitude relative to baseline
        float dx = gx - gxBase;
        float dy = gy - gyBase;
        float dz = gz - gzBase;
        float gyroMag = sqrtf(dx*dx + dy*dy + dz*dz);

        if (gyroMag > 2.00f) {
            if (!alertActive) { Serial.println("  *** MISCARE RAPIDA ***"); alertActive = true; }
            setLed(255, 0, 0);     // LED rosu
            setBuzzer(true);
            delay(80);
            setBuzzer(false);
            delay(70);
        } else if (gyroMag > 0.80f) {
            if (!alertActive) { Serial.println("  ** Miscare medie"); alertActive = true; }
            setLed(255, 80, 0);    // LED portocaliu
            setBuzzer(true);
            delay(50);
            setBuzzer(false);
            delay(450);
        } else if (gyroMag > 0.30f) {
            if (!alertActive) { Serial.println("  * Miscare lenta"); alertActive = true; }
            setLed(255, 0, 0);     // LED rosu
            delay(500);
        } else {
            // Still — turn off
            if (alertActive) {
                Serial.printf("  Nemiscat  |  gyro=%.3f rad/s  |  accel=%.2f g\n", gyroMag, ag);
                alertActive = false;
            }
            setLed(0, 0, 0);
            setBuzzer(false);
            delay(100);
        }
    }
    // Cleanup
    setLed(0, 0, 0);
    setBuzzer(false);
    Serial.read();
    Serial.println("  Test oprit.");
}

// ─── FSM Alarm — monitor automat cu LED + Buzzer ─────────────────────────────
static void testFsmAlarm() {
    printHeader("FSM Alarm — Monitor Automat");
    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setTimeOut(100);

    bool mpuOk = initMPU6050();
    if (!mpuOk)
        Serial.println("  [WARN] MPU6050 absent — detectia de miscare dezactivata.");

    // Calibrare auto baseline gyro (cu retry daca detecteaza miscare)
    Serial.println("  Autocalibrare gyro in curs...");
    float gxB = 0, gyB = 0, gzB = 0;
    calibrateMpuGyroBaseline(gxB, gyB, gzB, MPU_GYRO_CAL_MS);
    fsmInit(gxB, gyB, gzB);

    Serial.println("  === FSM pornit ===");
    Serial.println("  Stari: OK(verde) | WARNING(portocaliu,beep 3s) | CRITICAL(rosu,beep rapid) | MOTION(rosu,alert)");
    Serial.println("  Triggers: miscare(MPU) | aer degradat(MQ-3) | temperatura(DHT22)");
    Serial.println("  Apasa orice tasta pentru a opri.\n");

    FsmState state     = FSM_OK;
    FsmState prevState = FSM_OK;
    SensorData d       = {};
    d.mpuOk            = mpuOk;

    uint32_t lastSensorRead = 0;
    uint32_t lastDhtRead    = 0;

    while (!Serial.available()) {
        uint32_t now = millis();

        // Citire senzori la 500 ms
        if (now - lastSensorRead >= 500) {
            lastSensorRead = now;
            d.airQuality = readMq135();
            if (mpuOk) {
                float ag, gx, gy, gz;
                if (readMpu6050(ag, gx, gy, gz)) {
                    d.accelG = ag; d.gyroX = gx; d.gyroY = gy; d.gyroZ = gz;
                }
            }
        }

        // DHT22 — minim 2.2 s intre citiri
        if (now - lastDhtRead >= 2200) {
            lastDhtRead = now;
            d.dhtOk = readDht(d.temperature, d.humidity);
        }

        state = fsmUpdate(d);
        fsmApplyActuators(state);   // non-blocking, apelat la ~50 ms

        if (state != prevState) {
            const char *reason =
                (state == FSM_CRITICAL && (!d.dhtOk || !d.mpuOk) && prevState != FSM_OK) ? "SENSOR FAULT" :
                (state == FSM_CRITICAL && d.airQuality >= THRESH_AIR_CRITICAL) ? "aer CRITIC" :
                (state == FSM_CRITICAL) ? "temperatura CRITICA / sensor fault" :
                (state == FSM_MOTION)   ? "MISCARE detectata" :
                (state == FSM_WARNING && d.airQuality >= THRESH_AIR_WARNING) ? "aer WARNING" :
                (state == FSM_WARNING)  ? "temperatura WARNING" : "conditii normale";
            Serial.printf("  [%6lu ms] %-8s -> %-8s  (%s)\n",
                          now, fsmStateName(prevState), fsmStateName(state), reason);
            prevState = state;
        }
        delay(50);
    }

    setLed(0, 0, 0);
    setBuzzer(false);
    Serial.read();
    Serial.println("  FSM oprit.");
}

// ─── Scheduler + shared sensor state (accesibil din toate task-urile) ────────
static SensorData mon        = {};
static bool       monReady   = false;
static bool       hcFlatlineTrigger = false;
static bool       hcRecoveredWindow = false;
static Scheduler  sched;

// ─── Task: citire senzori rapizi (500 ms) ─────────────────────────────────────
static void taskSensors() {
    _tsSensors = millis();
    uint32_t now = millis();
    hcFlatlineTrigger = false;
    hcRecoveredWindow = false;

    mon.airQuality = readMq135();
    mon.distanceCm = readDistance();
    mon.currentA   = readAcs712();
    mon.lightLevel = readLdr();
    float ag, gx, gy, gz;
    mon.mpuOk = readMpu6050(ag, gx, gy, gz);
    if (mon.mpuOk) { mon.accelG = ag; mon.gyroX = gx; mon.gyroY = gy; mon.gyroZ = gz; }
    monReady = true;

    if ((now - _bootMs) >= HC_STARTUP_GRACE_MS &&
        mon.distanceCm > 0 && mon.distanceCm <= HC_VALID_MAX_CM) {
        if (_hcWindowStart == 0) resetHcWindow(now);
        if (mon.distanceCm < _hcMin) _hcMin = mon.distanceCm;
        if (mon.distanceCm > _hcMax) _hcMax = mon.distanceCm;
        _hcSamples++;
    }
    if ((now - _bootMs) >= HC_STARTUP_GRACE_MS &&
        (now - _hcWindowStart) >= HC_FLATLINE_WINDOW_MS) {
        float spread = (_hcSamples > 0) ? (_hcMax - _hcMin) : 9999.0f;
        if (_hcSamples >= HC_MIN_VALID_SAMPLES) {
            if (spread < HC_MIN_SPREAD_CM) hcFlatlineTrigger = true;
            else                           hcRecoveredWindow = true;
        }
        resetHcWindow(now);
    }
}

// ─── Task: citire DHT22 (2200 ms — limita protocolului) ───────────────────────
static void taskDht() {
    _tsDht = millis();
    mon.dhtOk = readDht(mon.temperature, mon.humidity);
}

// ─── Task: poll buton (20 ms) ─────────────────────────────────────────────────
static void taskButtons() {
    _tsButtons = millis();
    uint32_t now = millis();
    BtnEvent evt = pollButtons();
    if (evt == BTN_EVT_STANDBY) {
        _standby = !_standby;
        if (_standby) {
            setBuzzer(false);
            setFanManaged(false);
            setLed(0, 0, 5);
            Serial.println("[BTN] STANDBY");
        } else {
            Serial.println("[BTN] ACTIV");
            if (_monLevel == MON_LEVEL_OK) {
                setLed(0, 20, 0);
                setBuzzer(false);
            }
        }
    } else if (evt == BTN_EVT_SILENCE_SHORT || evt == BTN_EVT_REARM) {
        if (_monLevel != MON_LEVEL_OK)
            acknowledgeMonitorAlarm(now);
        else
            Serial.println("[BTN] SAFE deja");
    }
}

// ─── Task: logica alarme (100 ms) ─────────────────────────────────────────────
static void taskAlarmLogic() {
    _tsAlarm = millis();
    if (!monReady || _standby) return;
    uint32_t now = millis();

    int   mqWarn  = (mq135Baseline() > 0) ? mq135WarnThresh() : THRESH_AIR_WARNING;
    int   mqCrit  = (mq135Baseline() > 0) ? mq135CritThresh() : THRESH_AIR_CRITICAL;
    int   ldrBase = (ldrBaseline() > 0) ? ldrBaseline() : THRESH_LDR_NORMAL;
    int   ldrCrit = LDR_CRITICAL_OFFSET;
    float dhtWarn = THRESH_TEMP_WARNING;
    if (dhtBaselineReady()) {
        float relWarn = dhtBaselineTemp() + DHT_TEMP_RISE_WARNING_C;
        if (relWarn > dhtWarn) dhtWarn = relWarn;
    }
    float gyroMag = 0.0f;
    if (mon.mpuOk)
        gyroMag = sqrtf(mon.gyroX * mon.gyroX + mon.gyroY * mon.gyroY + mon.gyroZ * mon.gyroZ);

    bool trigMqCrit       = mon.airQuality >= mqCrit;
    int  ldrDelta         = mon.lightLevel - ldrBase;
    bool ldrOverThresh    = ldrDelta >= ldrCrit;
    // Debounce LDR: trebuie sa stea peste prag cel putin LDR_DEBOUNCE_MS continuu
    if (ldrOverThresh) {
        if (_ldrTrigSinceMs == 0) _ldrTrigSinceMs = now;
    } else {
        _ldrTrigSinceMs = 0;
    }
    bool trigLdrCrit      = ldrOverThresh && (now - _ldrTrigSinceMs) >= LDR_DEBOUNCE_MS;
    bool trigDhtWarn      = mon.dhtOk && mon.temperature >= dhtWarn;
    bool trigMpuCrit      = mon.mpuOk && gyroMag >= MPU_GYRO_CRITICAL_THRESH;
    bool trigFanNoCurrent = _fanForced && (now - _fanStartMs) > 1000 && fabsf(mon.currentA) < FAN_CURRENT_MIN_A;

    if (_monLevel == MON_LEVEL_OK) {
        _recoverSinceMs = 0;
        setLed(0, 20, 0);
        setBuzzer(false);
        // Pastreaza fan ON daca a fost fortat manual si nu a expirat
        if (now >= _manualFanUntilMs) setFanManaged(false);

        if (trigMqCrit) {
            setMonitorAlarm(MON_LEVEL_CRITICAL, MON_REASON_MQ_CRITICAL, true, now);
        } else if (trigLdrCrit) {
            setMonitorAlarm(MON_LEVEL_CRITICAL, MON_REASON_LDR_BRIGHT, false, now);
        }
        else if (_hcEnabled && hcFlatlineTrigger) {
            setMonitorAlarm(MON_LEVEL_CRITICAL, MON_REASON_HC_FLATLINE, false, now);
        }
        if (trigFanNoCurrent) {
            setMonitorAlarm(MON_LEVEL_CRITICAL, MON_REASON_FAN_NO_CURRENT, true, now);
        } else if (trigMpuCrit) {
            setMonitorAlarm(MON_LEVEL_CRITICAL, MON_REASON_MPU_CRITICAL, false, now);
        } else if (trigDhtWarn) {
            setMonitorAlarm(MON_LEVEL_WARNING, MON_REASON_DHT_WARNING, false, now);
        }
    } else {
        if (_monLevel == MON_LEVEL_WARNING) {
            setLed(255, 220, 0);
            setBuzzer(true);
        } else {
            setLed(255, 0, 0);
            setBuzzer(true);
        }

        if (trigFanNoCurrent && _monReason != MON_REASON_FAN_NO_CURRENT) {
            setMonitorAlarm(MON_LEVEL_CRITICAL, MON_REASON_FAN_NO_CURRENT, true, now);
        }

        bool recoveryCond      = false;
        bool autoRecoverAllowed = false;
        uint32_t recoverHoldMs  = AUTO_CLEAR_HOLD_MS;

        if (_hcEnabled && _monReason == MON_REASON_HC_FLATLINE) {
            if (hcRecoveredWindow) {
                Serial.println("[AUTO] HC fluctuatii revenite -> SAFE");
                acknowledgeMonitorAlarm(now);
            }
        }
        if (_monReason == MON_REASON_LDR_BRIGHT) {
            autoRecoverAllowed = true;
            recoverHoldMs = LDR_AUTO_CLEAR_HOLD_MS;
            int ldrRecoverDelta = ldrCrit - LDR_RECOVER_HYST;
            if (ldrRecoverDelta < 0) ldrRecoverDelta = 0;
            recoveryCond = ldrDelta <= ldrRecoverDelta;
        } else if (_monReason == MON_REASON_MPU_CRITICAL) {
            autoRecoverAllowed = true;
            recoveryCond = mon.mpuOk && (gyroMag < MPU_RECOVER_THRESH);
        } else if (_monReason == MON_REASON_DHT_WARNING) {
            autoRecoverAllowed = true;
            recoveryCond = mon.dhtOk && (mon.temperature < (dhtWarn - DHT_RECOVER_HYST_C));
        } else if (_monReason == MON_REASON_FAN_NO_CURRENT) {
            // Auto-recover: daca curentul revine (GND reconectat), iese din CRITICAL singur
            autoRecoverAllowed = true;
            recoverHoldMs = 500;
            recoveryCond = fabsf(mon.currentA) >= FAN_CURRENT_MIN_A;
        }
        if (autoRecoverAllowed && recoveryCond) {
            if (_recoverSinceMs == 0) _recoverSinceMs = now;
            if (now - _recoverSinceMs >= recoverHoldMs) {
                Serial.println("[AUTO] Conditii revenite in interval normal -> SAFE");
                bool wasManualFan = (_monReason == MON_REASON_FAN_NO_CURRENT) && (now < _manualFanUntilMs);
                acknowledgeMonitorAlarm(now);
                // Daca fereastra manuala inca e activa, reporneste fan + forced
                if (wasManualFan) {
                    _fanForced  = true;
                    _fanStartMs = now;
                    setFanManaged(true);
                }
            }
        } else {
            _recoverSinceMs = 0;
        }
    }
}

// ─── Task: print status serial (2000 ms) ──────────────────────────────────────
static void taskPrint() {
    _tsPrint = millis();
    uint32_t now = millis();

    int   mqWarn  = (mq135Baseline() > 0) ? mq135WarnThresh() : THRESH_AIR_WARNING;
    int   mqCrit  = (mq135Baseline() > 0) ? mq135CritThresh() : THRESH_AIR_CRITICAL;
    int   ldrBase = (ldrBaseline() > 0) ? ldrBaseline() : THRESH_LDR_NORMAL;
    int   ldrCrit = LDR_CRITICAL_OFFSET;
    float dhtWarn = THRESH_TEMP_WARNING;
    if (dhtBaselineReady()) {
        float relWarn = dhtBaselineTemp() + DHT_TEMP_RISE_WARNING_C;
        if (relWarn > dhtWarn) dhtWarn = relWarn;
    }
    float gyroMag = 0.0f;
    if (mon.mpuOk)
        gyroMag = sqrtf(mon.gyroX * mon.gyroX + mon.gyroY * mon.gyroY + mon.gyroZ * mon.gyroZ);

    bool trigMqCrit       = mon.airQuality >= mqCrit;
    int  ldrDelta         = mon.lightLevel - ldrBase;
    bool trigLdrCrit      = ldrDelta >= ldrCrit;
    bool trigDhtWarn      = mon.dhtOk && mon.temperature >= dhtWarn;
    bool trigMpuCrit      = mon.mpuOk && gyroMag >= MPU_GYRO_CRITICAL_THRESH;
    bool trigFanNoCurrent = _fanForced && (now - _fanStartMs) > 1000 && fabsf(mon.currentA) < FAN_CURRENT_MIN_A;
    bool hcGraceDone      = (now - _bootMs) >= HC_STARTUP_GRACE_MS;
    float hcSpread        = (_hcSamples > 0) ? (_hcMax - _hcMin) : 0.0f;
    uint32_t hcAge        = (_hcWindowStart > 0) ? (now - _hcWindowStart) : 0;

    Serial.printf("[MON] %-8s (%s)  standby=%d\n",
                  monLevelName(_monLevel), monReasonName(_monReason), _standby ? 1 : 0);
    Serial.printf("      DHT: ok=%d  T=%.1fC  H=%.1f%%  baseT=%.1f  warn>=%.1f  trig=%d\n",
                  mon.dhtOk ? 1 : 0, mon.temperature, mon.humidity,
                  dhtBaselineReady() ? dhtBaselineTemp() : -1.0f,
                  dhtWarn, trigDhtWarn ? 1 : 0);
    Serial.printf("      MQ : raw=%d  base=%d  warn=%d  crit=%d  trigCrit=%d\n",
                  mon.airQuality, mq135Baseline(), mqWarn, mqCrit, trigMqCrit ? 1 : 0);
    Serial.printf("      LDR: raw=%d  base=%d  delta=%d  critDelta=%d  %s  trig=%d\n",
                  mon.lightLevel, ldrBase, ldrDelta, ldrCrit, ldrLabel(mon.lightLevel), trigLdrCrit ? 1 : 0);
    Serial.printf("      HC : dist=%.1fcm  base=%.1fcm  spread=%.2f  samp=%d  grace=%d  age=%lums\n",
                  mon.distanceCm, hcsr04Baseline(), hcSpread, _hcSamples,
                  hcGraceDone ? 1 : 0, (unsigned long)hcAge);
    Serial.printf("      MPU: ok=%d  accel=%.2fg  gx=%.3f gy=%.3f gz=%.3f  mag=%.3f  trig=%d\n",
                  mon.mpuOk ? 1 : 0, mon.accelG,
                  mon.gyroX, mon.gyroY, mon.gyroZ, gyroMag, trigMpuCrit ? 1 : 0);
    Serial.printf("      ACS: I=%.3fA  fanForced=%d  fanOnFor=%lums  min=%.3f  trig=%d\n",
                  mon.currentA, _fanForced ? 1 : 0,
                  (unsigned long)(_fanForced ? (now - _fanStartMs) : 0),
                  FAN_CURRENT_MIN_A, trigFanNoCurrent ? 1 : 0);
    Serial.printf("      HC alarm=%s\n",
                  _hcEnabled ? "ON" : "OFF");
    // ── Timestamps task-uri (dovada paralelism scheduler) ────────────────────
    Serial.printf("      [TASKS @%lums] btn=%lu sens=%lu dht=%lu alarm=%lu uart=%lu print=%lu\n",
                  (unsigned long)now,
                  (unsigned long)_tsButtons, (unsigned long)_tsSensors,
                  (unsigned long)_tsDht,     (unsigned long)_tsAlarm,
                  (unsigned long)_tsUart,    (unsigned long)_tsPrint);
}

// ─── Task: UART telemetrie + comenzi Pi (2000 ms) ────────────────────────────
static void taskUart() {
    _tsUart = millis();
    // ── Risk score (0–100) calculat local ────────────────────────────────────
    int   mqWarnT = (mq135Baseline() > 0) ? mq135WarnThresh()  : THRESH_AIR_WARNING;
    int   mqCritT = (mq135Baseline() > 0) ? mq135CritThresh()  : THRESH_AIR_CRITICAL;
    float dhtW    = THRESH_TEMP_WARNING;
    if (dhtBaselineReady()) {
        float rw = dhtBaselineTemp() + DHT_TEMP_RISE_WARNING_C;
        if (rw > dhtW) dhtW = rw;
    }
    float gyroMag = 0.0f;
    if (mon.mpuOk)
        gyroMag = sqrtf(mon.gyroX*mon.gyroX + mon.gyroY*mon.gyroY + mon.gyroZ*mon.gyroZ);

    float risk = 0.0f;
    if (mon.airQuality >= mqCritT)                            risk += 40.0f;
    else if (mon.airQuality >= mqWarnT)                       risk += 20.0f;
    if (mon.dhtOk && mon.temperature >= dhtW)                 risk += 20.0f;
    if (mon.mpuOk && gyroMag >= MPU_GYRO_CRITICAL_THRESH)     risk += 30.0f;
    int ldrDeltaR = mon.lightLevel - (ldrBaseline() > 0 ? ldrBaseline() : THRESH_LDR_NORMAL);
    if (ldrDeltaR >= LDR_CRITICAL_OFFSET)                     risk += 15.0f;
    if (_fanForced && (millis()-_fanStartMs)>1000 && fabsf(mon.currentA) < FAN_CURRENT_MIN_A) risk += 35.0f;
    if (risk > 100.0f) risk = 100.0f;

    sendTelemetry(mon, monLevelName(_monLevel), risk);

    // ── Comenzi primite de la Pi ──────────────────────────────────────────────
    PiCommand cmd = pollCommand();
    uint32_t now = millis();
    switch (cmd) {
        case PI_CMD_FAN_ON:
            _manualFanUntilMs = now + 30000;
            _fanForced  = true;
            _fanStartMs = now;
            setFanManaged(true);
            Serial.println("[PI] CMD:FAN_ON — fan pornit 30s");
            break;
        case PI_CMD_FAN_OFF:
            _manualFanUntilMs = 0;
            _fanForced = false;
            setFanManaged(false);
            Serial.println("[PI] CMD:FAN_OFF");
            break;
        case PI_CMD_HC_ON:
            _hcEnabled = true;
            Serial.println("[PI] CMD:HC_ON — alarma HC-SR04 activa");
            break;
        case PI_CMD_HC_OFF:
            _hcEnabled = false;
            Serial.println("[PI] CMD:HC_OFF — alarma HC-SR04 dezactivata");
            break;
        case PI_CMD_ALARM_ON:
            if (_monLevel == MON_LEVEL_OK)
                setMonitorAlarm(MON_LEVEL_CRITICAL, MON_REASON_MQ_CRITICAL, true, now);
            Serial.println("[PI] CMD:ALARM_ON");
            break;
        case PI_CMD_ALARM_OFF:
            if (_monLevel != MON_LEVEL_OK)
                acknowledgeMonitorAlarm(now);
            Serial.println("[PI] CMD:ALARM_OFF");
            break;
        case PI_CMD_FSM_RESET:
            acknowledgeMonitorAlarm(now);
            Serial.println("[PI] CMD:FSM_RESET");
            break;
        default:
            break;
    }
}

// ─── setup ───────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println("\n+============================================+");
    Serial.println("|  MediTwin AI - Sensor Test & Live Monitor  |");
    Serial.println("+============================================+");

    initActuators();
    ledSelfTest();

    Serial.println("[BOOT] Initialising sensors...");
    initSensors();  // Wire + DHT22 + HCSR04 + MPU6050 + ADC + LDR calibrate

    Serial.printf("[BOOT] MQ-3 warm-up (%d ms)...\n", MQ135_WARMUP_MS);
    delay(MQ135_WARMUP_MS);
    mq135Calibrate(MQ135_CAL_SAMPLES);

    Serial.println("[BOOT] DHT22 baseline calibration...");
    dhtCalibrate(DHT_CAL_SAMPLES, DHT_CAL_INTERVAL_MS);

    Serial.println("[BOOT] HC-SR04 baseline calibration...");
    hcsr04Calibrate(HCSR04_CAL_SAMPLES);

    Serial.println("[BOOT] ACS712 zero-current calibration...");
    setFanManaged(false);
    acs712Calibrate(ACS712_CAL_SAMPLES);

    // Calibrare baseline gyro
    Serial.println("[BOOT] Gyro baseline (senzor nemiscat)...");
    float gxB = 0, gyB = 0, gzB = 0;
    calibrateMpuGyroBaseline(gxB, gyB, gzB, MPU_GYRO_CAL_MS);
    fsmInit(gxB, gyB, gzB);
    Serial.println("[BOOT] FSM activ.");

    initButtons();
    initUart();

    _bootMs = millis();
    _standby = false;
    _monLevel = MON_LEVEL_OK;
    _monReason = MON_REASON_NONE;
    _fanForced = false;
    _fanApplied = false;
    resetHcWindow(_bootMs);

    // ── Inregistrare task-uri scheduler ──────────────────────────────────────
    sched.addTask("buttons",    20,   taskButtons);
    sched.addTask("sensors",   500,   taskSensors);
    sched.addTask("dht",      2200,   taskDht);
    sched.addTask("alarm",     100,   taskAlarmLogic);
    sched.addTask("uart",     2000,   taskUart);
    sched.addTask("print",    2000,   taskPrint);

    Serial.println("[BOOT] Monitorizare automata pornita — LED + Buzzer active.");
    Serial.println("[BOOT] HC alarm OFF (trimite H pentru a activa).");
    printHelp();
    setFanManaged(false);
    setBuzzer(false);
    setLed(0, 20, 0);  // verde dim = idle
}

// ─── loop ────────────────────────────────────────────────────────────────────
void loop() {
    sched.tick();   // ruleaza fiecare task la intervalul sau

    // Comenzi seriale — blocking, nu intra in scheduler
    if (!Serial.available()) return;

    char cmd = (char)Serial.read();
    while (Serial.available()) Serial.read();
    if (cmd == '\r' || cmd == '\n') return;

    switch (cmd) {
        case '1': testLdr();       break;
        case '2': testMq135();     break;
        case '3': testHcsr04();    break;
        case '4': testDht22();     break;
        case '5': testMpu6050();   break;
        case '7': testAcs712();    break;
        case '8': i2cScan();       break;
        case '9': testAll();       break;
        case '0': stabilityTest(); break;
        case 'L': case 'l': testLed();    break;
        case 'F': case 'f': testFan();    break;
        case '+':
            _manualFanUntilMs = millis() + 30000;
            _fanForced  = true;
            _fanStartMs = millis();
            setFanManaged(true);
            Serial.println("  Fan ON (30s)");
            break;
        case '-':
            _manualFanUntilMs = 0;
            _fanForced = false;
            setFanManaged(false);
            Serial.println("  Fan OFF");
            break;
        case 'H': case 'h':
            _hcEnabled = !_hcEnabled;
            Serial.printf("  HC alarm %s (debug local — Pi trimite CMD:HC_ON/HC_OFF)\n", _hcEnabled ? "ON" : "OFF");
            break;
        case 'S': case 's': /* servo removed */  break;
        case 'B': case 'b': testBuzzer();   break;
        case 'R': case 'r': testBreathing();  break;
        case 'M': case 'm': testMpuAlarm();    break;
        case 'A': case 'a': testFsmAlarm();    break;
        case '?': printHelp();                 break;
        default:
            Serial.printf("Unknown command '%c'. Send '?' for help.\n", cmd);
            break;
    }

    printHelp();
}

#endif // SENSOR_TEST_MODE
