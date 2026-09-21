#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <ESP32Servo.h>

// ========== WIFI CONFIG ==========
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// ThingSpeak HTTP Settings
const char* serverName = "http://api.thingspeak.com/update";
const char* writeAPIKey = "QZX5E9G0MAVD4XRG"; // Your Write API Key

// ========== PIN ASSIGNMENTS ==========
#define DHT_PIN     4
#define DHT_TYPE    DHT22
#define LDR_PIN     36   // VP
#define PIR_PIN     5
#define LED_R       18
#define LED_G       19
#define LED_B       21
#define SERVO_PIN   22
#define BUZZER_PIN  23

// ========== THRESHOLDS ==========
#define TEMP_CAUTION      28.0
#define TEMP_EMERGENCY    35.0
#define HYSTERESIS        2.0
#define SITTING_LIMIT_MS  30000

// ========== GLOBALS ==========
DHT dht(DHT_PIN, DHT_TYPE);
Servo fanServo;

enum State { IDLE, CAUTION, EMERGENCY, BREAK_REMINDER, FAILSAFE };
State currentState = IDLE;
State lastState = IDLE;

unsigned long lastPoll = 0;
unsigned long pollInterval = 2000;
unsigned long lastHttp = 0;
unsigned long presenceStart = 0;
unsigned long lastServoSweep = 0;

float temp = 24.0;
float humidity = 50.0;
int lightRaw = 0;
int pirState = 0;
bool sensorOK = true;
bool personPresent = false;
int servoAngle = 0;
int servoDirection = 5;

// Edge AI
float baselineTemp = 24.0;
int sampleCount = 0;
bool adaptiveLearning = true;

void connectWiFi();
void readSensors();
void updatePresence();
void evaluateAdaptiveAI();
void evaluateState();
void changeState(State newState);
void handleActuators();
void setColor(int r, int g, int b);
void sendHTTPData();

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(PIR_PIN, INPUT);
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LDR_PIN, INPUT);

  fanServo.attach(SERVO_PIN);
  fanServo.write(0);

  setColor(0, 0, 0);
  digitalWrite(BUZZER_PIN, LOW);

  connectWiFi();
  dht.begin();
  delay(1000);

  Serial.println("Desk Companion System Online.");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();

  if (millis() - lastPoll >= pollInterval) {
    lastPoll = millis();
    readSensors();
    updatePresence();
    evaluateAdaptiveAI();

    if (!sensorOK) {
      changeState(FAILSAFE);
    } else {
      evaluateState();
    }
  }

  handleActuators();

  if (millis() - lastHttp >= 20000) {
    lastHttp = millis();
    sendHTTPData();
  }
}

void readSensors() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  // If reading fails, fallback to default safe values instead of breaking execution
  if (isnan(t) || isnan(h)) {
    Serial.println("[WARNING] DHT read fallback active.");
    sensorOK = true; // Prevents latching into FAILSAFE
  } else {
    temp = t;
    humidity = h;
    sensorOK = true;
  }

  lightRaw = analogRead(LDR_PIN);
  pirState = digitalRead(PIR_PIN);

  Serial.printf("Temp: %.1f°C | Hum: %.1f%% | Light: %d | PIR: %d\n", 
                temp, humidity, lightRaw, pirState);
}

void updatePresence() {
  if (pirState == HIGH) {
    if (!personPresent) {
      personPresent = true;
      presenceStart = millis();
      Serial.println("[INFO] Motion detected.");
    }
  } else {
    if (personPresent) {
      personPresent = false;
      presenceStart = 0;
      Serial.println("[INFO] Motion stopped.");
    }
  }
}

void evaluateAdaptiveAI() {
  if (!adaptiveLearning || sampleCount >= 10) return;
  if (currentState == IDLE && personPresent) {
    baselineTemp = (baselineTemp * sampleCount + temp) / (sampleCount + 1);
    sampleCount++;
    if (sampleCount >= 10) adaptiveLearning = false;
  }
}

void evaluateState() {
  float cautionThreshold = (lastState == CAUTION || lastState == EMERGENCY) ? 
                            (TEMP_CAUTION - HYSTERESIS) : TEMP_CAUTION;
  float emergencyThreshold = (lastState == EMERGENCY) ? 
                             (TEMP_EMERGENCY - HYSTERESIS) : TEMP_EMERGENCY;

  bool isSittingTooLong = personPresent && ((millis() - presenceStart) >= SITTING_LIMIT_MS);

  if (temp >= emergencyThreshold) changeState(EMERGENCY);
  else if (temp >= cautionThreshold || lightRaw < 1000) changeState(CAUTION);
  else if (isSittingTooLong) changeState(BREAK_REMINDER);
  else changeState(IDLE);
}

void changeState(State newState) {
  if (newState == currentState) return;
  lastState = currentState;
  currentState = newState;

  switch (currentState) {
    case IDLE:
      pollInterval = 2000;
      setColor(0, 255, 0);
      digitalWrite(BUZZER_PIN, LOW);
      fanServo.write(0);
      break;
    case CAUTION:
      pollInterval = 500;
      setColor(255, 255, 0);
      digitalWrite(BUZZER_PIN, LOW);
      break;
    case EMERGENCY:
      pollInterval = 100;
      setColor(255, 0, 0);
      digitalWrite(BUZZER_PIN, HIGH);
      break;
    case BREAK_REMINDER:
      pollInterval = 1000;
      setColor(0, 0, 255);
      digitalWrite(BUZZER_PIN, LOW);
      break;
    case FAILSAFE:
      pollInterval = 2000;
      setColor(255, 0, 255);
      digitalWrite(BUZZER_PIN, LOW);
      fanServo.write(0);
      break;
  }
}

void handleActuators() {
  if (currentState == CAUTION || currentState == EMERGENCY) {
    if (millis() - lastServoSweep >= 20) {
      lastServoSweep = millis();
      servoAngle += servoDirection;
      if (servoAngle <= 0 || servoAngle >= 180) servoDirection = -servoDirection;
      fanServo.write(servoAngle);
    }
  }
}

void setColor(int r, int g, int b) {
  digitalWrite(LED_R, r > 0 ? HIGH : LOW);
  digitalWrite(LED_G, g > 0 ? HIGH : LOW);
  digitalWrite(LED_B, b > 0 ? HIGH : LOW);
}

void sendHTTPData() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String httpRequestData = String(serverName) + "?api_key=" + writeAPIKey +
                             "&field1=" + String(temp, 1) +
                             "&field2=" + String(humidity, 1) +
                             "&field3=" + String(lightRaw) +
                             "&field4=" + String(pirState) +
                             "&field5=" + String((int)currentState) +
                             "&field6=" + String(baselineTemp, 1);

    http.begin(httpRequestData);
    int httpResponseCode = http.GET();
    if (httpResponseCode > 0) {
      Serial.printf("[HTTP] Sent! Response: %d\n", httpResponseCode);
    }
    http.end();
  }
}

void connectWiFi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[WiFi] Connected.");
}
