#include "actuators.h"
#include "config.h"
#include <Arduino.h>

void initActuators() {
    // ── RGB LED — LEDC (hardware PWM, arduino-esp32 v3.x API) ─────────────────
    ledcAttach(PIN_LED_R, LEDC_FREQ_HZ, LEDC_RESOLUTION);
    ledcAttach(PIN_LED_G, LEDC_FREQ_HZ, LEDC_RESOLUTION);
    ledcAttach(PIN_LED_B, LEDC_FREQ_HZ, LEDC_RESOLUTION);

    // ── Buzzer — Digital output (full ON/OFF, not PWM) ───────────────────────
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);

    // ── Fan — active HIGH relay / MOSFET ─────────────────────────────────────
    pinMode(PIN_FAN, OUTPUT);
    digitalWrite(PIN_FAN, LOW);


    Serial.println("[ACTUATOR] All actuators initialised");
}

// ── LED self-test at boot (R → G(logic) → B(logic) → white → off) ───────────
void ledSelfTest() {
    setLed(255, 0,   0);   delay(250);
    setLed(0,   255, 0);   delay(250);
    setLed(0,   0,   255); delay(250);
    setLed(255, 255, 255); delay(250);
    setLed(0,   0,   0);
}

void setLed(uint8_t r, uint8_t g, uint8_t b) {
    // Hardware wiring is R/Y/G (not pure RGB):
    //   PIN_LED_R = red, PIN_LED_G = yellow, PIN_LED_B = green.
    // Keep API as logical RGB and remap channels here.
    ledcWrite(PIN_LED_R, r);  // logical R -> physical RED
    ledcWrite(PIN_LED_B, g);  // logical G -> physical GREEN
    ledcWrite(PIN_LED_G, b);  // logical B -> physical YELLOW
}

void setFan(bool on) {
    digitalWrite(PIN_FAN, on ? HIGH : LOW);
    Serial.printf("[ACTUATOR] Fan  → %s\n", on ? "ON" : "OFF");
}

void setBuzzer(bool on) {
    digitalWrite(PIN_BUZZER, on ? HIGH : LOW);  // Full digital ON/OFF for passive buzzer
}
