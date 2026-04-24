#pragma once

// ─── WiFi / Network ──────────────────────────────────────────────────────────
#define WIFI_SSID      "YOUR_SSID"
#define WIFI_PASSWORD  "YOUR_PASSWORD"
#define RASPBERRY_IP   "192.168.1.100"
#define RASPBERRY_PORT 5000

// ─── Sensor Pins (ADC1 capable: GPIO0–GPIO6 on ESP32-C6) ─────────────────────
#define PIN_LDR        0    // ADC1_CH0 — light sensor
#define PIN_MQ135      1    // ADC1_CH1 — air quality (MQ-3)
#define PIN_ACS712     2    // ADC1_CH2 — current sensor

// ─── Digital Sensors ─────────────────────────────────────────────────────────
#define PIN_DHT22      10   // DHT22 data
#define DHT_TYPE       DHT22

#define PIN_TRIG       11   // HC-SR04 trigger
#define PIN_ECHO       12   // HC-SR04 echo

// ─── I2C Bus (BMP280 + MPU6050 share the same bus) ───────────────────────────
#define PIN_SDA        8
#define PIN_SCL        9
#define BMP280_ADDR    0x76  // SDO → GND = 0x76,  SDO → 3V3 = 0x77
#define MPU6050_ADDR   0x68  // AD0 → GND = 0x68,  AD0 → 3V3 = 0x69

// ─── Actuator Pins ───────────────────────────────────────────────────────────
#define PIN_LED_R      6    // Red LED    — GPIO6
#define PIN_LED_G      4    // Yellow LED — GPIO4
#define PIN_LED_B      5    // Green LED  — GPIO5  (moved from GPIO8=PIN_SDA)
#define PIN_BUZZER     18   // Buzzer
#define PIN_FAN        19   // Fan via relay / MOSFET

// Buzzer PWM: lower duty = quieter. Frequency changes pitch for passive buzzers.
#define BUZZER_PWM_FREQ_HZ  1200
#define BUZZER_DUTY         70    // 0-255, ~27% duty

// ─── Butoane ─────────────────────────────────────────────────────────────────
// Active LOW — buton conectat intre GPIO si GND (INPUT_PULLUP)
#define PIN_BTN_SILENCE  15  // Oprire alarma (buzzer snooze 30s / permanent)
#define PIN_BTN_START    16  // Rearmare FSM / toggle standby (apasare lunga)

// ─── LEDC PWM ────────────────────────────────────────────────────────────────
#define LEDC_FREQ_HZ   5000
#define LEDC_RESOLUTION 8   // 8-bit: 0–255

// ─── ADC ─────────────────────────────────────────────────────────────────────
#define ADC_RESOLUTION  4095.0f
#define ADC_VREF        3.3f

// ─── LDR Thresholds ──────────────────────────────────────────────────────────
#define THRESH_LDR_DARK      1600
#define THRESH_LDR_DIM       2400
#define THRESH_LDR_NORMAL    3100

// ─── Safety Thresholds ───────────────────────────────────────────────────────
// MQ-3: praguri RELATIVE fata de baseline calibrat la pornire
//   Warning  = baseline + MQ135_OFFSET_WARNING
//   Critical = baseline + MQ135_OFFSET_CRITICAL
// Valori tipice aer interior: baseline ~1800-2600
// Offset 200 = ~6-10% degradare → Warning
// Offset 400 = ~12-17% degradare → Critical
#define MQ135_OFFSET_WARNING   200   // +200 ADC fata de baseline = WARNING
#define MQ135_OFFSET_CRITICAL  400   // +400 ADC fata de baseline = CRITICAL
// Praguri absolute (fallback daca nu s-a calibrat)
#define THRESH_AIR_WARNING   2700
#define THRESH_AIR_CRITICAL  3000
#define THRESH_TEMP_WARNING  28.0f  // °C
#define THRESH_TEMP_CRITICAL 32.0f
#define THRESH_PRESENCE_CM   80.0f  // HC-SR04 presence threshold
#define DHT_TEMP_RISE_WARNING_C  1.5f // WARNING daca urca peste baseline cu 1.5°C
#define DHT_TEMP_RISE_CRITICAL_C 3.0f // CRITICAL daca urca peste baseline cu 3.0°C

// ─── Custom monitor thresholds (latched alarms, reset only from button) ─────
// LDR critical daca se deviaza mult fata de baseline (atat prea intuneric,
// cat si prea luminos), pentru a evita dependenta de orientarea divizorului.
#define LDR_CRITICAL_OFFSET      600   // ~15% din scala ADC (0-4095) — mai putin sensibil
#define LDR_DEBOUNCE_MS          1500UL // LDR trebuie sa stea peste prag 1.5s inainte de alarma

// HC-SR04 "flatline" detection:
// dupa perioada de gratie, daca variatia distantei ramane prea mica intr-o
// fereastra de timp, se considera lipsa fluctuatii (simulare "batai").
#define HC_STARTUP_GRACE_MS      8000UL
#define HC_FLATLINE_WINDOW_MS    3500UL
#define HC_MIN_SPREAD_CM         1.2f
#define HC_MIN_VALID_SAMPLES     5
#define HC_VALID_MAX_CM          80.0f
// Temporary test switch: 0 = ignore HC alarm logic in monitor, 1 = enable it
#define HC_ALARM_ENABLED         0

// Auto-recovery pentru alarmele non-latched-hard (HC/LDR/MPU/DHT)
// MQ critical si FAN_NO_CURRENT raman latched pana la buton.
#define AUTO_CLEAR_HOLD_MS       2500UL
#define LDR_RECOVER_HYST         40
#define LDR_AUTO_CLEAR_HOLD_MS   700UL
#define MPU_RECOVER_THRESH       0.35f
#define DHT_RECOVER_HYST_C       0.4f

// MPU6050 critical pe miscare peste pragul de gyro magnitude
#define MPU_GYRO_CRITICAL_THRESH 0.80f

// ─── Sensor Sanity Ranges (valori in afara acestora = fault) ─────────────────
#define SANITY_MQ135_MIN     100    // sub 100 = circuit deschis / senzor deconectat
#define SANITY_MQ135_MAX     4090   // peste 4090 = scurtcircuit / stuck
#define SANITY_TEMP_MIN     -30.0f  // °C minim plauzibil
#define SANITY_TEMP_MAX      80.0f  // °C maxim plauzibil
#define SANITY_HUM_MIN       0.0f   // %RH
#define SANITY_HUM_MAX       100.0f
#define SANITY_PRESSURE_MIN  300.0f // hPa
#define SANITY_PRESSURE_MAX  1200.0f
#define SANITY_ACCEL_MAX     8.0f   // g — peste 8g = shock sau senzor blocat

// ─── ACS712 Calibration ──────────────────────────────────────────────────────
// VCC = 5V, OUT trece prin divizor 10k/10k inainte de GPIO2
// Divizorul halveaza tensiunea: ADC vede Vout/2 → inmultim inapoi cu 2
#define ACS712_MV_PER_A      185.0f  // 5A variant=185, 20A=100, 30A=66
#define ACS712_DIVIDER_INV   2.0f    // 1 / (10k/(10k+10k)) = 2
// Midpoint: ACS712 la 0A scoate 2.5V → dupa divizor: 1.25V → ADC: 1.25/3.3*4095 ≈ 1551
// Calibrare exacta: trimite '7' cu ventilatorul OPRIT si noteaza valoarea ADC
#define ACS712_MIDPOINT      1270    // ADC count la curent zero (calibrat fizic 24 Apr 2026)

// ─── Fan verification via ACS712 ─────────────────────────────────────────────
// Daca ventilatorul e ON dar curentul e sub prag → Fan Fault
#define FAN_CURRENT_MIN_A    0.05f   // minim 50 mA asteptat cand fan e ON

// ─── UART — comunicatie cu Raspberry Pi ──────────────────────────────────────
// Serial1: TX=GPIO16 → Pi5 pin10 (GPIO15/RX),  RX=GPIO17 ← Pi5 pin8 (GPIO14/TX)
// Serial (USB-CDC) ramane liber pentru debug
#define PIN_UART_TX  16
#define PIN_UART_RX  17
#define UART_BAUD    115200

// ─── Timing ──────────────────────────────────────────────────────────────────
#define SENSOR_INTERVAL_MS   2000   // citire senzori la fiecare 2 s
#define SEND_INTERVAL_MS     5000   // trimitere telemetrie la fiecare 5 s
#define MQ135_WARMUP_MS      10000  // pre-incalzire MQ-3 la boot (minim 10 s)

// ─── Boot calibration settings (all sensors) ────────────────────────────────
#define LDR_CAL_SAMPLES         20
#define MQ135_CAL_SAMPLES       20
#define ACS712_CAL_SAMPLES      200
#define DHT_CAL_SAMPLES         2
#define DHT_CAL_INTERVAL_MS     2200UL
#define HCSR04_CAL_SAMPLES      20
#define MPU_GYRO_CAL_MS         700UL
// Autocalibrare MPU: detecteaza miscare in timpul calibrarii si reincearca.
#define MPU_GYRO_CAL_MAX_RETRIES    6
#define MPU_GYRO_STILL_AVG_MAX      0.12f
#define MPU_GYRO_STILL_PEAK_MAX     0.45f
#define MPU_GYRO_CAL_RETRY_DELAY_MS 120UL
