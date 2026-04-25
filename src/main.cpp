#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <Wire.h>
#include <MPU6050.h>
#include <ESP32Servo.h>
#include <time.h>
#include <math.h>

// ===== WiFi / Backend =====
static const char* WIFI_SSID = "test";
static const char* WIFI_PASSWORD = "DN18062022";
static const char* HEALTH_URL = "https://residency-resistant-perfected.ngrok-free.dev/health";
static const char* INGEST_URL = "https://residency-resistant-perfected.ngrok-free.dev/ingest";

// ===== Pins =====
static const int PIN_DHT = 4;
static const int PIN_MQ135 = 34;
static const int PIN_LDR = 35;
static const int PIN_ACS712 = 32;
static const int PIN_HCSR04_TRIG = 25;
static const int PIN_HCSR04_ECHO = 26;
static const int PIN_SERVO = 27;
static const int PIN_I2C_SDA = 21;
static const int PIN_I2C_SCL = 22;

// ===== Sensors / Actuators =====
#define DHT_TYPE DHT22
DHT dht(PIN_DHT, DHT_TYPE);
MPU6050 mpu;
Servo ventServo;

// ===== Timing =====
static const uint32_t LOOP_MS = 1000;
uint32_t lastLoopMs = 0;
uint32_t lastWifiReconnectMs = 0;

// ===== ACS712 calibration =====
float acsOffsetRaw = 0.0f;
static const float ADC_VREF = 3.3f;
static const float ADC_MAX = 4095.0f;
static const float ACS_SENSITIVITY_V_PER_A = 0.185f; // ACS712 5A

// ===== State =====
int currentServoAngle = 90;
bool mpuOk = false;

// ---------- Helpers ----------
float readDistanceCm() {
  digitalWrite(PIN_HCSR04_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_HCSR04_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_HCSR04_TRIG, LOW);

  unsigned long duration = pulseIn(PIN_HCSR04_ECHO, HIGH, 30000UL); // 30ms timeout
  if (duration == 0) return NAN;
  return (duration * 0.0343f) / 2.0f;
}

void connectWiFiBlocking() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("[WIFI] Connected, IP=");
  Serial.println(WiFi.localIP());
}

void syncTimeBlocking() {
  configTime(0, 0, "pool.ntp.org");

  time_t now = time(nullptr);
  while (now < 1700000000) {
    delay(500);
    Serial.print("*");
    now = time(nullptr);
  }
  Serial.println();
  Serial.print("[NTP] UNIX=");
  Serial.println((long)now);
}

void checkBackendHealth() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  if (!https.begin(client, HEALTH_URL)) {
    Serial.println("[HEALTH] begin failed");
    return;
  }

  int code = https.GET();
  Serial.printf("[HEALTH] code=%d\n", code);
  https.end();
}

void calibrateACS712() {
  const int samples = 200;
  uint64_t sum = 0;

  for (int i = 0; i < samples; i++) {
    sum += analogRead(PIN_ACS712);
    delay(5);
  }

  acsOffsetRaw = (float)sum / (float)samples;
  Serial.print("[ACS712] Offset raw=");
  Serial.println(acsOffsetRaw, 2);
}

float rawToCurrentA(float raw) {
  float deltaRaw = raw - acsOffsetRaw;
  float deltaV = (deltaRaw / ADC_MAX) * ADC_VREF;
  return deltaV / ACS_SENSITIVITY_V_PER_A;
}

bool readMPU(float &ax, float &ay, float &az, float &tempC) {
  if (!mpuOk) return false;

  int16_t axRaw, ayRaw, azRaw, gxRaw, gyRaw, gzRaw, tRaw;
  mpu.getMotion6(&axRaw, &ayRaw, &azRaw, &gxRaw, &gyRaw, &gzRaw);
  tRaw = mpu.getTemperature();

  ax = (float)axRaw / 16384.0f; // default +/-2g
  ay = (float)ayRaw / 16384.0f;
  az = (float)azRaw / 16384.0f;
  tempC = (float)tRaw / 340.0f + 36.53f;
  return true;
}

bool postPayloadAndHandleServo(const String &payload, int &httpCodeOut) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  if (!https.begin(client, INGEST_URL)) {
    httpCodeOut = -1;
    return false;
  }

  https.addHeader("Content-Type", "application/json");
  int httpCode = https.POST((uint8_t*)payload.c_str(), payload.length());
  httpCodeOut = httpCode;

  bool ok = false;
  if (httpCode > 0) {
    String resp = https.getString();

    JsonDocument rdoc;
    DeserializationError err = deserializeJson(rdoc, resp);
    if (!err) {
      if (rdoc["servoAngleDeg"].is<int>()) {
        int newAngle = rdoc["servoAngleDeg"].as<int>();
        if (newAngle < 0) newAngle = 0;
        if (newAngle > 180) newAngle = 180;

        if (newAngle != currentServoAngle) {
          currentServoAngle = newAngle;
          ventServo.write(currentServoAngle);
        }
      }
      ok = true;
    }
  }

  https.end();
  return ok;
}

// ---------- Setup / Loop ----------
void setup() {
  Serial.begin(115200);
  delay(200);

  analogReadResolution(12);

  pinMode(PIN_HCSR04_TRIG, OUTPUT);
  pinMode(PIN_HCSR04_ECHO, INPUT);

  dht.begin();

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  mpu.initialize();
  mpuOk = mpu.testConnection();

  ventServo.setPeriodHertz(50);
  ventServo.attach(PIN_SERVO, 500, 2400);
  ventServo.write(currentServoAngle);

  calibrateACS712();
  connectWiFiBlocking();
  syncTimeBlocking();
  checkBackendHealth();

  Serial.println("[BOOT] Ready");
}

void loop() {
  uint32_t nowMs = millis();
  if (nowMs - lastLoopMs < LOOP_MS) return;
  lastLoopMs = nowMs;

  // Keep WiFi alive
  if (WiFi.status() != WL_CONNECTED) {
    if (nowMs - lastWifiReconnectMs > 2000) {
      lastWifiReconnectMs = nowMs;
      WiFi.reconnect();
    }

    Serial.printf("ts=%ld wifi=0 skip_post=1\n", (long)time(nullptr));
    return; // Skip POST while disconnected
  }

  time_t ts = time(nullptr);

  // Read sensors
  float airTempC = dht.readTemperature();
  float humidityPct = dht.readHumidity();

  int airQualityRaw = analogRead(PIN_MQ135);
  int lightRaw = analogRead(PIN_LDR);

  float lidDistanceCm = readDistanceCm();
  bool lidOpen = !isnan(lidDistanceCm) && lidDistanceCm > 5.0f;

  float accelX = NAN, accelY = NAN, accelZ = NAN, mpuTempC = NAN;
  readMPU(accelX, accelY, accelZ, mpuTempC);

  float acsRaw = (float)analogRead(PIN_ACS712);
  float heaterCurrentA = rawToCurrentA(acsRaw);
  bool heaterActive = fabsf(heaterCurrentA) > 0.1f;

  // Build payload
  JsonDocument doc;
  doc["ts"] = (int)ts;

  if (!isnan(airTempC)) doc["airTempC"] = airTempC;
  if (!isnan(humidityPct)) doc["humidityPct"] = humidityPct;
  doc["airQualityRaw"] = airQualityRaw;
  doc["lightRaw"] = lightRaw;

  if (!isnan(lidDistanceCm)) doc["lidDistanceCm"] = lidDistanceCm;
  doc["lidOpen"] = lidOpen;

  if (!isnan(accelX)) doc["accelX"] = accelX;
  if (!isnan(accelY)) doc["accelY"] = accelY;
  if (!isnan(accelZ)) doc["accelZ"] = accelZ;
  if (!isnan(mpuTempC)) doc["mpuTempC"] = mpuTempC;

  doc["heaterCurrentA"] = heaterCurrentA;
  doc["heaterActive"] = heaterActive;
  doc["servoAngleDeg"] = currentServoAngle;

  String payload;
  serializeJson(doc, payload);

  int httpCode = -999;
  bool parsedOk = postPayloadAndHandleServo(payload, httpCode);

  // Compact debug line
  Serial.printf(
    "ts=%ld wifi=1 http=%d ok=%d t=%.2f h=%.2f aq=%d ldr=%d dist=%.2f lid=%d i=%.3f heat=%d servo=%d\n",
    (long)ts,
    httpCode,
    parsedOk ? 1 : 0,
    isnan(airTempC) ? -999.0f : airTempC,
    isnan(humidityPct) ? -999.0f : humidityPct,
    airQualityRaw,
    lightRaw,
    isnan(lidDistanceCm) ? -1.0f : lidDistanceCm,
    lidOpen ? 1 : 0,
    heaterCurrentA,
    heaterActive ? 1 : 0,
    currentServoAngle
  );
}
