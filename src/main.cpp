#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_AHTX0.h>
#include <ArduinoJson.h>
#include <map>
#include "index.h"

// Configurazione di rete
const char* ssid = "giga";
const char* password = "12345678";
const char* broker = "broker.hivemq.com";

// Soglie di sicurezza
const long parkingHeight = 6; 
const float minTemp = 2.0, maxTemp = 40.0;
const float minHum = 30.0, maxHum = 70.0;
const float minPres = 970.0, maxPres = 1050.0;

// Hardware locale
Adafruit_BMP280 bmp;
Adafruit_AHTX0 aht;
const int sensorCount = 2;

struct LocalSensor {                    //Il sensore ha diversi parametri
  int trigPin;                          //Pin Trig
  int echoPin;                          //Pin Echo
  const char* label;                    //Etichetta del sensore all'interno della fila 
  volatile unsigned long pulseStart;    //Variabile per salvataggio partenza onda sonora
  volatile long lastDistance;           //ultima distanza presa
};

LocalSensor localSensors[sensorCount] = { //Inizializzazione di due sensori su...
  {5, 18, "1", 0, -1},                    //...(trigPin=5),(echoPin=18),(sensore "1"),(tempo=0),(distanza iniziale=-1, invalida)
  {17, 16, "2", 0, -1}                    //...(trigPin=17),(echoPin=16),(sensore "2"),(tempo=0),(distanza iniziale=-1, invalida)
};

// Definizione struttura dati dei parcheggi remoti (le file non collegate all'Esp32 centrale)
struct RemoteSensorData {
  long distance;            //Distanza rilevata
  bool isParked;            //Stato
  float temp;               //Valore temperatura
  float hum;                //Valore umidità
  float pres;               //Valore pressione
};
struct RemoteRow { RemoteSensorData sensors[2]; };    //Ogni fila ha 2 sensori
std::map<String, RemoteRow> dynamicParkingLot;        //Un dizionario dinamico che associa il nome della fila ai dati dei suoi sensori

WiFiClient espClient;                                 
PubSubClient mqttClient(espClient);                   //Istanza server Mqtt
WebServer server(80);

float curT = 0, curP = 0, curH = 0;                   //Inizializzazione valori di temperatura, pressione e umidità

void IRAM_ATTR handleEcho(int i) {                    //Interrupt per la lettura dell'echoPin
  if (digitalRead(localSensors[i].echoPin) == HIGH) { //Se si comincia a leggere il sensore, aggiornare pulseStart per il sensore stesso
    localSensors[i].pulseStart = micros();
  } else {
    unsigned long duration = micros() - localSensors[i].pulseStart; //Altrimenti stabilire la durata e calcolare la distanza
    localSensors[i].lastDistance = duration * 0.034 / 2;            //Per HC-SR04 d=(Tempo di volo*Velocità suono)/2
  }
}
void IRAM_ATTR echoISR0() { handleEcho(0); }
void IRAM_ATTR echoISR1() { handleEcho(1); }

// Logica del Server
void handleData() {
  StaticJsonDocument<2048> doc;   //Creazione documento JSON "vuoto" in memoria di dimensiine 2048 byte
  JsonObject local = doc.createNestedObject("local"); //Crea un oggetto chiamato "local" per i sensori collegati fisicamente a questo ESP32
  local["temp"] = curT;         //Aggiungere dati di temperatura
  local["pres"] = (int)curP;    //Aggiungere dati di pressione
  local["hum"] = (int)curH;     //Aggiungere dati di umidità
  JsonArray lSensors = local.createNestedArray("sensors");  //Creazione Array chiamata "sensors" dentro l'oggetto locale
  for(int i=0; i<sensorCount; i++) {
    JsonObject s = lSensors.createNestedObject(); //Creazione di un oggetto per ogni singolo sensore (HC-SR04)
    long d = localSensors[i].lastDistance;        //Recupera l'ultima distanza misurata

    s["label"] = localSensors[i].label;           //Etichetta del posto auto ("1" o "2")
    
    if (d < 3 || d > 100) {                   //Se la distanza è minore di 3cm (disturbo elettrico) o maggiore di 1 metr0 (fuori portata)
      s["dist"] = -1;                         //Assume valore -1, "invalido che viene visualizzato come N/A"
      s["parked"] = false;                    //Stato parcheggio non occupato
      s["dist_err"] = false;                  //Non è un errore di altezza, è proprio assenza di segnale
    } else {
      s["dist"] = d;                          //Distanza valida in cm
      s["parked"] = (d < parkingHeight);      //Se la distanza letta è minore dell'altezza del parcheggio, il posto è OCCUPATO
      s["dist_err"] = (d > parkingHeight);    //Se la distanza è maggiore dell'altezza impostata (es. sensore spostato), segna errore
    }
    s["alert"] = (s["dist_err"] || curT < minTemp || curT > maxTemp || curH < minHum || curH > maxHum); //ALERT: Diventa 'true' se c'è un errore di distanza o se i valori meteo sono fuori soglia
  }

  //Sezione dati remoti (MQTT)
  JsonObject remote = doc.createNestedObject("remote");   //Crea un oggetto "remote" per i dati ricevuti dagli altri ESP32 tramite MQTT
  for (auto const& [name, data] : dynamicParkingLot) {    //Itera sulla mappa che contiene i dati delle altre file (es. Fila B, Fila C)
    JsonObject rRow = remote.createNestedObject(name);    //Crea un oggetto per la fila
    JsonArray rSensors = rRow.createNestedArray("sensors");
    
    for(int j=0; j<2; j++) {      //Per ogni fila remota, gestire i 2 sensori
      JsonObject s = rSensors.createNestedObject();
      s["label"] = j+1;
      s["dist"] = data.sensors[j].distance;
      s["parked"] = data.sensors[j].isParked;
      s["temp"] = data.sensors[j].temp;
      s["hum"] = data.sensors[j].hum;
      s["pres"] = data.sensors[j].pres;
      s["dist_err"] = (data.sensors[j].distance > parkingHeight);
      // Alert remoto: si attiva se c'è errore distanza O temperatura fuori range O umidità fuori range
      s["alert"] = (s["dist_err"] || s["temp"].as<float>() < minTemp || s["temp"].as<float>() > maxTemp || s["hum"].as<float>() < minHum  || s["hum"].as<float>() > maxHum);
    }
  }
  String output;
  serializeJson(doc, output);                   // Trasforma l'oggetto JSON in una stringa di testo
  server.send(200, "application/json", output); // Invia la stringa al browser con codice 200 (OK)
}

void triggerSensors() {                                                 //Funzione per leggere il sensore
  for(int i=0; i<sensorCount; i++) {
    digitalWrite(localSensors[i].trigPin, LOW); delayMicroseconds(2);   //Porre trigPin a Low per confermare lo stato basso
    digitalWrite(localSensors[i].trigPin, HIGH); delayMicroseconds(10); //Porre trigPin a High per iniziare la lettura
    digitalWrite(localSensors[i].trigPin, LOW);                         //Porre trigPin a Low per interrompere la lettura
    delay(30); 
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {  //Funzione che scatta quando arriva un messaggio dal broker
  
  StaticJsonDocument<512> doc;  //Creare un documento temporaneo per decodificare il messaggio JSON ricevuto
  if (deserializeJson(doc, payload, length)) return;  //Trasformare il contenuto del messaggio (payload) in un oggetto JSON. Se fallisce, esce
  String t = String(topic);   //Trasformare il topic in una stringa
  //Trovare la posizione del nome tra "parking/" e "/status"
  int start = t.indexOf("parking/") + 8; // Saltare i 8 caratteri di "parking/"
  int end = t.indexOf("/status");        // Trovare dove inizia "/status"
  String rowName = t.substring(start, end); //Estrarre la sottostringa (es. "FilaB") che useremo come chiave nella mappa
  int sIdx = (String((const char*)doc["sensor"]) == "2") ? 1 : 0;   // Controllare se nel JSON c'è scritto "sensor": "2". Se sì, usa l'indice 1, altrimenti 0
  
  dynamicParkingLot[rowName].sensors[sIdx] = { // Inserire i dati ricevuti (distanza, stato, meteo) nella posizione corretta della mappa,Se la fila "rowName" non esiste ancora, viene creata automaticamente.
    doc["distance"], 
    doc["parked"], 
    doc["temperature"], 
    doc["humidity"],
    doc["pressure"]
  };
}

void setup() {
  Serial.begin(115200);
  
  for (int i = 0; i < sensorCount; i++) {   //Per ogni sensore...
    pinMode(localSensors[i].trigPin, OUTPUT);   //...trigPin Output
    pinMode(localSensors[i].echoPin, INPUT);    //...echoPin Input
  }
  attachInterrupt(digitalPinToInterrupt(localSensors[0].echoPin), echoISR0, CHANGE);  //Assegnazione Interrupt
  attachInterrupt(digitalPinToInterrupt(localSensors[1].echoPin), echoISR1, CHANGE);

  bmp.begin(0x77);
  aht.begin();

  WiFi.begin(ssid, password);     //Connesione WiFi e controllo stato
  while (WiFi.status() != WL_CONNECTED) delay(500);

  mqttClient.setServer(broker, 1883);   //Impostato il Server MQTT
  mqttClient.setCallback(mqttCallback);
  
  server.on("/", [](){ server.send(200, "text/html", INDEX_HTML); });   //Host Server
  server.on("/data", handleData);
  server.begin();
}

void loop() {
  if (!mqttClient.connected()) {    //Connessione MQTT come client
    if (mqttClient.connect("ESP32_Central_Hub")) {
        mqttClient.subscribe("FermiModena/Rejeb/parking/+/status");
    }
  }
  mqttClient.loop();  //ontrolla se ci sono pacchetti dati in arrivo nel buffer Wi-Fi
  server.handleClient();

  static unsigned long lastMeasure = 0;   //Misura del tempo
  if (millis() - lastMeasure > 2000) {    //Ogni 2s lettura sensori
    triggerSensors();
    
    //Lettura AHT20
    sensors_event_t humidity, temp;
    aht.getEvent(&humidity, &temp);
    curT = temp.temperature;
    curH = humidity.relative_humidity;
    
    //Lettura BMP280
    curP = bmp.readPressure() / 100.0F;
    
    lastMeasure = millis();
  }
}