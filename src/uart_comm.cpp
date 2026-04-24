#include "uart_comm.h"
#include "config.h"
#include <Arduino.h>
#include <ArduinoJson.h>

// USB-CDC mode: Serial este atat canalul de telemetrie cat si de comenzi.
// Serial1 (GPIO16/17) nu mai este folosit.
#define PI_UART Serial

// Buffer linie pentru comezi primite de la Pi
static char    _rxBuf[128];
static uint8_t _rxLen = 0;

// ─── initUart ────────────────────────────────────────────────────────────────
void initUart() {
    // Serial (USB-CDC) este deja initializat in main.cpp cu Serial.begin(115200).
    // In modul USB nu este nevoie de reinitializare cu parametri GPIO.
    Serial.printf("[UART] Pi comm: USB-CDC  %lu baud\n", (unsigned long)UART_BAUD);
}

// ─── sendTelemetry ───────────────────────────────────────────────────────────
// Trimite un singur rand JSON terminat cu '\n' pe UART1.
void sendTelemetry(const SensorData &d, const char *state, float riskScore) {
    JsonDocument doc;

    // Payload explicit: chei clare + unitati in nume pentru parsare usoara pe Pi.
    doc["timestamp_ms"]   = millis();
    doc["temperature_c"]  = serialized(String(d.temperature, 2));
    doc["humidity_pct"]   = serialized(String(d.humidity, 1));
    doc["air_quality_adc"] = d.airQuality;
    doc["light_level_adc"] = d.lightLevel;
    doc["distance_cm"]    = serialized(String(d.distanceCm, 1));
    doc["accel_g"]        = serialized(String(d.accelG, 3));
    doc["gyro_x_rad_s"]   = serialized(String(d.gyroX, 3));
    doc["gyro_y_rad_s"]   = serialized(String(d.gyroY, 3));
    doc["gyro_z_rad_s"]   = serialized(String(d.gyroZ, 3));
    doc["current_a"]      = serialized(String(d.currentA, 3));
    doc["dht_ok"]         = d.dhtOk;
    doc["mpu_ok"]         = d.mpuOk;
    doc["fsm_state"]      = state;
    doc["risk_score"]     = serialized(String(riskScore, 1));

    // Trimite JSON pe Serial (USB-CDC) - Pi citeste pe /dev/ttyACM0
    serializeJson(doc, PI_UART);
    PI_UART.print('\n');
}

// ─── pollCommand ─────────────────────────────────────────────────────────────
// Acumuleaza bytes in buffer. La '\n' parseaza "CMD:<TOKEN>" si returneaza comanda.
// Non-blocking — apeleaza la fiecare iteratie de loop.
PiCommand pollCommand() {
    while (PI_UART.available()) {
        char c = (char)PI_UART.read();

        if (c == '\n' || c == '\r') {
            if (_rxLen > 0) {
                _rxBuf[_rxLen] = '\0';
                _rxLen = 0;

                            if (strncmp(_rxBuf, "CMD:", 4) == 0) {
                    const char *tok = _rxBuf + 4;
                    if      (strcmp(tok, "FAN_ON")      == 0) return PI_CMD_FAN_ON;
                    else if (strcmp(tok, "FAN_OFF")     == 0) return PI_CMD_FAN_OFF;
                    else if (strcmp(tok, "HC_ON")       == 0) return PI_CMD_HC_ON;
                    else if (strcmp(tok, "HC_OFF")      == 0) return PI_CMD_HC_OFF;
                    else if (strcmp(tok, "ALARM_ON")    == 0) return PI_CMD_ALARM_ON;
                    else if (strcmp(tok, "ALARM_OFF")   == 0) return PI_CMD_ALARM_OFF;
                    else if (strcmp(tok, "FSM_RESET")   == 0) return PI_CMD_FSM_RESET;
                    // Comanda necunoscuta — log pe debug dar nu bloca
                    Serial.printf("[UART] Comanda necunoscuta: %s\n", _rxBuf);
                }
            }
        } else if (_rxLen < sizeof(_rxBuf) - 1) {
            _rxBuf[_rxLen++] = c;
        } else {
            // Overflow — ignora rand corupt
            _rxLen = 0;
        }
    }
    return PI_CMD_NONE;
}
