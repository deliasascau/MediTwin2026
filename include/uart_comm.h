#pragma once
#include "types.h"

// ─── Protocol overview ────────────────────────────────────────────────────────
//
//  ESP32 → Pi  (USB-CDC / Serial):
//    One JSON line per telemetry packet, terminated with '\n'
//    {
//      "timestamp_ms":12345,
//      "temperature_c":25.30,
//      "humidity_pct":60.5,
//      "air_quality_adc":2450,
//      "light_level_adc":1800,
//      "distance_cm":45.2,
//      "accel_g":1.02,
//      "gyro_x_rad_s":0.01,
//      "gyro_y_rad_s":0.00,
//      "gyro_z_rad_s":0.00,
//      "current_a":0.000,
//      "dht_ok":true,
//      "mpu_ok":true,
//      "fsm_state":"SAFE",
//      "risk_score":5.0
//    }
//
//  Pi → ESP32  (UART1, RX=GPIO17):
//    CMD:<COMMAND>\n
//      CMD:FAN_ON      CMD:FAN_OFF          (manual, de la utilizator)
//      CMD:HC_ON       CMD:HC_OFF           (manual, toggle alarma HC-SR04)
//      CMD:ALARM_ON    CMD:ALARM_OFF        (automat, din decizia FSM Pi)
//      CMD:FSM_RESET                        (automat, recalibrare)

// ─── Commands from Raspberry Pi ──────────────────────────────────────────────
typedef enum {
    PI_CMD_NONE = 0,
    PI_CMD_FAN_ON,
    PI_CMD_FAN_OFF,
    PI_CMD_HC_ON,
    PI_CMD_HC_OFF,
    PI_CMD_ALARM_ON,
    PI_CMD_ALARM_OFF,
    PI_CMD_FSM_RESET
} PiCommand;

// Initialise USB-CDC transport at UART_BAUD. Call once in setup().
void      initUart();

// Serialise SensorData to JSON and send over USB-CDC Serial.
void      sendTelemetry(const SensorData &d, const char *state, float riskScore);

// Read pending bytes from USB-CDC Serial; returns the first complete command found,
// or PiCommand::NONE. Non-blocking — call every loop iteration.
PiCommand pollCommand();
