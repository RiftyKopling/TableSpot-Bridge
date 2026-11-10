#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ============ GANTI INI ============
const char* ssid        = "Sopyan";
const char* password    = "riftisopyan15";
const char* mqtt_server = "10.60.123.7";  // IP PC kamu
// ===================================

WiFiClient espClient;
PubSubClient mqtt(espClient);

const int LATCH_PIN = 15, CLOCK_PIN = 5, DATA_PIN = 4;
const int TRIG_PIN  = 13;
const int TOTAL_SENSOR = 7;
const int ECHO_PINS[TOTAL_SENSOR] = {6, 7, 10, 14, 16, 17, 18};
const int THRESHOLD_DIST = 20;

LiquidCrystal_I2C lcd(0x27, 16, 2);

// Fungsi baca jarak (Median 5x)
long readDistance(int echoPin) {
  long samples[5];
  int errorCount = 0;
  
  for (int i = 0; i < 5; i++) {
    digitalWrite(TRIG_PIN, LOW);  delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    
    long duration = pulseIn(echoPin, HIGH, 25000);
    
    if (duration == 0) {
      samples[i] = 999;
      errorCount++;
    } else {
      samples[i] = duration * 0.034 / 2;
    }
    delay(10);
  }
  
  if (errorCount == 5) return -1; // Sensor rusak
  
  // Urutkan median
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4 - i; j++) {
      if (samples[j] > samples[j+1]) {
        long temp = samples[j];
        samples[j] = samples[j+1];
        samples[j+1] = temp;
      }
    }
  }
  return samples[2];
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

  int statusKirim[TOTAL_SENSOR]; 
  bool isKosong[TOTAL_SENSOR]; 
  bool isError[TOTAL_SENSOR];

  // 1. Baca jarak
  for (int i = 0; i < TOTAL_SENSOR; i++) {
    long dist = readDistance(ECHO_PINS[i]);
    
    if (dist == -1) {
      statusKirim[i] = -1;
      isKosong[i] = false; 
      isError[i] = true;
    } else {
      isKosong[i] = (dist > THRESHOLD_DIST);
      statusKirim[i] = isKosong[i] ? 1 : 0;
      isError[i] = false;
    }
    
    Serial.print("K"); Serial.print(i + 1); Serial.print(":");
    Serial.print(dist); Serial.print("cm ");
    delay(10); 
  }
  Serial.println();

  // 2. Logika Status Meja (1=Kosong, 0=Terisi, -1=Error)
  int mejaStatus[5]; 
  mejaStatus[0] = (isError[0] || isError[1]) ? -1 : (isKosong[0] && isKosong[1] ? 1 : 0);
  mejaStatus[1] = (isError[2] || isError[3]) ? -1 : (isKosong[2] && isKosong[3] ? 1 : 0);
  mejaStatus[2] = isError[4] ? -1 : (isKosong[4] ? 1 : 0);
  mejaStatus[3] = isError[5] ? -1 : (isKosong[5] ? 1 : 0);
  mejaStatus[4] = isError[6] ? -1 : (isKosong[6] ? 1 : 0);

  // 3. Nyalakan LED via Shift Register (INI YANG TADI HILANG)
  uint16_t ledBits = 0;
  if (mejaStatus[0] == 1) ledBits |= (1 << 2); else ledBits |= (1 << 1);
  if (mejaStatus[1] == 1) ledBits |= (1 << 4); else ledBits |= (1 << 3);
  if (mejaStatus[2] == 1) ledBits |= (1 << 9); else ledBits |= (1 << 5);
  if (mejaStatus[3] == 1) ledBits |= (1 << 11); else ledBits |= (1 << 10);
  if (mejaStatus[4] == 1) ledBits |= (1 << 13); else ledBits |= (1 << 12);
  
  updateLEDs(ledBits); // <-- Eksekusi nyalakan LED

  // 4. Hitung Meja Kosong & Terisi
  int mejaKosong = 0;
  int mejaTerisi = 0;
  for (int m = 0; m < 5; m++) {
    if (mejaStatus[m] == 1) mejaKosong++;
    else if (mejaStatus[m] == 0) mejaTerisi++;
  }

  // 5. Update Tampilan LCD1602
  lcd.setCursor(0, 0);
  lcd.print("M1:"); lcd.print(mejaStatus[0] == 1 ? "K" : (mejaStatus[0] == 0 ? "T" : "E"));
  lcd.print(" M2:"); lcd.print(mejaStatus[1] == 1 ? "K" : (mejaStatus[1] == 0 ? "T" : "E"));
  lcd.print(" M3:"); lcd.print(mejaStatus[2] == 1 ? "K" : (mejaStatus[2] == 0 ? "T" : "E"));
  
  lcd.setCursor(0, 1);
  lcd.print("M4:"); lcd.print(mejaStatus[3] == 1 ? "K" : (mejaStatus[3] == 0 ? "T" : "E"));
  lcd.print(" M5:"); lcd.print(mejaStatus[4] == 1 ? "K" : (mejaStatus[4] == 0 ? "T" : "E"));
  lcd.print(" | K:"); lcd.print(mejaKosong);
  lcd.print(" T:"); lcd.print(mejaTerisi);

  // 6. Kirim JSON ke MQTT
  StaticJsonDocument<256> doc;
  
  JsonArray kursi = doc.createNestedArray("kursi");
  for (int i = 0; i < TOTAL_SENSOR; i++) {
    kursi.add(statusKirim[i]); 
  }
  
  JsonArray meja = doc.createNestedArray("meja");
  for (int m = 0; m < 5; m++) {
    meja.add(mejaStatus[m]);
  }
  
  doc["ts"] = millis();

  char buf[256];
  size_t n = serializeJson(doc, buf);
  mqtt.publish("esp32/sensor", buf, n);

  delay(1000);
}