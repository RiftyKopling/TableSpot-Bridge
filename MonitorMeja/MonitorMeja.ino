#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ============ GANTI INI ============
const char* ssid        = "Sopyan";
const char* password    = "riftisopyan15";
const char* mqtt_server = "10.60.123.7";  // IP PC dari TAHAP 3
// ===================================

WiFiClient espClient;
PubSubClient mqtt(espClient);

const int LATCH_PIN = 15, CLOCK_PIN = 5, DATA_PIN = 4;
const int TRIG_PIN  = 13;
const int TOTAL_SENSOR = 7;
const int ECHO_PINS[TOTAL_SENSOR] = {6, 7, 10, 14, 16, 17, 18};
const int THRESHOLD_DIST = 20;

LiquidCrystal_I2C lcd(0x27, 16, 2);

long readDistance(int echoPin) {
  digitalWrite(TRIG_PIN, LOW);  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(echoPin, HIGH, 25000);
  if (duration == 0) return 999;
  return duration * 0.034 / 2;
}

void updateLEDs(uint16_t state) {
  digitalWrite(LATCH_PIN, LOW);
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, (state >> 8) & 0xFF);
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, state & 0xFF);
  digitalWrite(LATCH_PIN, HIGH);
}

void onMqttMessage(char* topic, byte* payload, unsigned int len) {
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, payload, len)) return;
  if (String(topic) == "esp32/command" && doc.containsKey("lcd_line1")) {
    lcd.setCursor(0, 0);
    String s = doc["lcd_line1"].as<String>();
    s = s.substring(0, 16);
    while (s.length() < 16) s += " ";
    lcd.print(s);
  }
}

void mqttReconnect() {
  while (!mqtt.connected()) {
    Serial.print("MQTT connecting...");
    if (mqtt.connect("ESP32_MEJA")) {
      Serial.println(" OK");
      mqtt.subscribe("esp32/command");
    } else {
      Serial.print(" fail rc="); Serial.println(mqtt.state());
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(LATCH_PIN, OUTPUT);
  pinMode(CLOCK_PIN, OUTPUT);
  pinMode(DATA_PIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  for (int i = 0; i < TOTAL_SENSOR; i++) pinMode(ECHO_PINS[i], INPUT);

  Wire.begin(8, 9);
  lcd.init(); lcd.backlight();
  lcd.setCursor(0,0); lcd.print(" SYSTEM MONITOR ");
  lcd.setCursor(0,1); lcd.print(" STATUS MEJA... ");
  delay(1500); lcd.clear();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi OK");

  mqtt.setServer(mqtt_server, 1883);
  mqtt.setCallback(onMqttMessage);
}

void loop() {
  if (!mqtt.connected()) mqttReconnect();
  mqtt.loop();

  bool isKosong[TOTAL_SENSOR];
  for (int i = 0; i < TOTAL_SENSOR; i++) {
    long d = readDistance(ECHO_PINS[i]);
    isKosong[i] = (d > THRESHOLD_DIST);
    delay(10);
  }

  bool statusMeja[5];
  statusMeja[0] = isKosong[0] && isKosong[1];
  statusMeja[1] = isKosong[2] && isKosong[3];
  statusMeja[2] = isKosong[4];
  statusMeja[3] = isKosong[5];
  statusMeja[4] = isKosong[6];

  uint16_t ledBits = 0;
  if (statusMeja[0]) ledBits |= (1 << 2); else ledBits |= (1 << 1);
  if (statusMeja[1]) ledBits |= (1 << 4); else ledBits |= (1 << 3);
  if (statusMeja[2]) ledBits |= (1 << 9); else ledBits |= (1 << 5);
  if (statusMeja[3]) ledBits |= (1 << 11); else ledBits |= (1 << 10);
  if (statusMeja[4]) ledBits |= (1 << 13); else ledBits |= (1 << 12);
  updateLEDs(ledBits);

  int mejaKosong = 0;
  for (int m = 0; m < 5; m++) if (statusMeja[m]) mejaKosong++;
  int mejaTerisi = 5 - mejaKosong;

  lcd.setCursor(0,0);
  lcd.print("M1:"); lcd.print(statusMeja[0]?"K":"T");
  lcd.print(" M2:"); lcd.print(statusMeja[1]?"K":"T");
  lcd.print(" M3:"); lcd.print(statusMeja[2]?"K":"T");
  lcd.setCursor(0,1);
  lcd.print("M4:"); lcd.print(statusMeja[3]?"K":"T");
  lcd.print(" M5:"); lcd.print(statusMeja[4]?"K":"T");
  lcd.print(" | K:"); lcd.print(mejaKosong);
  lcd.print(" T:"); lcd.print(mejaTerisi);

  StaticJsonDocument<256> doc;
  JsonArray kursi = doc.createNestedArray("kursi");
  for (int i = 0; i < TOTAL_SENSOR; i++) kursi.add(isKosong[i] ? 1 : 0);
  JsonArray meja = doc.createNestedArray("meja");
  for (int m = 0; m < 5; m++) meja.add(statusMeja[m] ? 1 : 0);
  doc["kosong"] = mejaKosong;
  doc["terisi"] = mejaTerisi;
  doc["ts"]     = millis();

  char buf[256];
  size_t n = serializeJson(doc, buf);
  mqtt.publish("esp32/sensor", buf, n);

  delay(1000);
}