#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_AHTX0.h>
#include <ArduinoJson.h>

// --- CONFIGURAZIONE RETE ---
const char* ssid = "giga";
const char* password = "12345678";
const char* broker = "broker.hivemq.com";
const char* rowID = "B"; // Identificativo di questa fila

// --- SOGLIE ---
const long parkingHeight = 6; 

// --- HARDWARE ---
Adafruit_BMP280 bmp;
Adafruit_AHTX0 aht;
const int sensorCount = 2;

struct LocalSensor {
  int trigPin;
  int echoPin;
  const char* label;
  volatile unsigned long pulseStart;
  volatile long lastDistance;
};

LocalSensor sensors[sensorCount] = {
  {5, 18, "1", 0, -1}, // Sensore 1 della Fila B
  {17, 16, "2", 0, -1} // Sensore 2 della Fila B
};

WiFiClient espClient;
PubSubClient mqttClient(espClient);

float curT = 0, curP = 0, curH = 0;

// --- INTERRUPT PER ULTRASUONI ---
void IRAM_ATTR handleEcho(int i) {
  if (digitalRead(sensors[i].echoPin) == HIGH) {
    sensors[i].pulseStart = micros();
  } else {
    unsigned long duration = micros() - sensors[i].pulseStart;
    sensors[i].lastDistance = duration * 0.034 / 2;
  }
}
void IRAM_ATTR echoISR0() { handleEcho(0); }
void IRAM_ATTR echoISR1() { handleEcho(1); }

void triggerSensors() {
  for(int i=0; i<sensorCount; i++) {
    digitalWrite(sensors[i].trigPin, LOW); delayMicroseconds(2);
    digitalWrite(sensors[i].trigPin, HIGH); delayMicroseconds(10);
    digitalWrite(sensors[i].trigPin, LOW);
    delay(30); 
  }
}

// --- FUNZIONE DI INVIO DATI VIA MQTT ---
void publishData(int sensorIndex) {
  StaticJsonDocument<256> doc;
  long d = sensors[sensorIndex].lastDistance;
  
  doc["sensor"] = sensors[sensorIndex].label; // "1" o "2"
  
  // Logica di validazione distanza
  if (d < 3 || d > 100) {
    doc["distance"] = -1;
    doc["parked"] = false;
  } else {
    doc["distance"] = d;
    doc["parked"] = (d < parkingHeight);
  }
  
  doc["temperature"] = roundf(curT * 100) / 100.0;
  doc["humidity"]    = roundf(curH * 100) / 100.0;
  doc["pressure"]    = roundf(curP * 100) / 100.0;

  char buffer[256];
  serializeJson(doc, buffer);
  
  // Topic: FermiModena/Rejeb/parking/FilaB/status
  String topic = "FermiModena/Rejeb/parking/" + String(rowID) + "/status";
  mqttClient.publish(topic.c_str(), buffer);
  
  Serial.print("Inviato a MQTT: ");
  Serial.println(buffer);
}

void setup() {
  Serial.begin(115200);
  
  for (int i = 0; i < sensorCount; i++) {
    pinMode(sensors[i].trigPin, OUTPUT);
    pinMode(sensors[i].echoPin, INPUT);
  }
  attachInterrupt(digitalPinToInterrupt(sensors[0].echoPin), echoISR0, CHANGE);
  attachInterrupt(digitalPinToInterrupt(sensors[1].echoPin), echoISR1, CHANGE);

  if (!bmp.begin(0x76)) bmp.begin(0x77);
  aht.begin();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connesso!");

  mqttClient.setServer(broker, 1883);
}

void loop() {
  // Riconnessione MQTT
  if (!mqttClient.connected()) {
    Serial.print("Tentativo MQTT...");
    if (mqttClient.connect("ESP32_Remote_RowB")) {
      Serial.println("Connesso!");
    } else {
      delay(5000);
    }
  }
  mqttClient.loop();

  static unsigned long lastMeasure = 0;
  if (millis() - lastMeasure > 3000) { // Invia dati ogni 3 secondi
    triggerSensors();
    
    // Lettura Meteo
    sensors_event_t humidity, temp;
    aht.getEvent(&humidity, &temp);
    curT = temp.temperature;
    curH = humidity.relative_humidity;
    curP = bmp.readPressure() / 100.0F;
    
    // Pubblica i dati di entrambi i sensori
    publishData(0);
    publishData(1);
    
    lastMeasure = millis();
  }
} 