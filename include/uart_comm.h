#pragma once
#include "types.h"

// ─── Protocol overview ────────────────────────────────────────────────────────
//
//  ESP32 → Pi  (UART1, TX=GPIO22):
//    One compact JSON line per telemetry packet, terminated with '\n'
//    {"ts":12345,"temp":25.30,"hum":60.5,"air":2450,"light":1800,
//     "dist":45.2,"accel":1.02,"gx":0.01,"gy":0.00,"gz":0.00,
//     "cur":0.000,"dht":1,"mpu":1,"state":"SAFE","risk":5.0}
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

// Initialise UART1 (Serial1) at UART_BAUD. Call once in setup().
void      initUart();

// Serialise SensorData to JSON and send over UART1.
void      sendTelemetry(const SensorData &d, const char *state, float riskScore);

// Read pending bytes from UART1; returns the first complete command found,
// or PiCommand::NONE. Non-blocking — call every loop iteration.
PiCommand pollCommand();
