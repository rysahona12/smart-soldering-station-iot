#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <SoftwareSerial.h>

// --- Hardware & Pins ---
SoftwareSerial STM32Serial(D5, D6);
const int LED_PIN = D1;

// --- Wi-Fi Credentials ---
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// --- MQTT Configuration (For GitHub Live Website) ---
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;
const char* mqtt_topic = "smart_soldering_station/telemetry";

// --- Google Sheets Configuration (For 10-Min Logging) ---
// Replace this with your Google Apps Script Web App URL once deployed
const char* google_script_url = "https://script.google.com/macros/s/YOUR_DEPLOYMENT_ID/exec";

// --- Clients ---
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// --- Timers & State ---
unsigned long lastLiveTime = 0;
unsigned long lastLogTime = 0;

const unsigned long liveInterval = 1000;   // 1 second for real-time web dashboard
const unsigned long logInterval = 600000;  // 10 minutes (600,000 ms) for Google Sheets

String latestTelemetry = "{\"t\":0, \"f\":0, \"d\":1, \"m\":0}";

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnectMQTT() {
  // Loop until we're reconnected
  while (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a unique client ID
    String clientId = "SmartSolderingClient-";
    clientId += String(random(0xffff), HEX);
    
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" retrying in 5 seconds...");
      delay(5000);
    }
  }
}

void sendToGoogleSheets(String data) {
  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(LED_PIN, HIGH);
    
    WiFiClientSecure client;
    client.setInsecure(); // Bypass strict SSL certificate validation for Google Apps Script

    HTTPClient http;
    http.begin(client, google_script_url);
    http.addHeader("Content-Type", "application/json");

    Serial.print("Sending log to Google Sheets: ");
    Serial.println(data);

    int httpResponseCode = http.POST(data);

    if (httpResponseCode > 0) {
      Serial.printf("Sheet Log Success! Response Code: %d\n", httpResponseCode);
    } else {
      Serial.printf("Sheet Log Failed. Error: %s\n", http.errorToString(httpResponseCode).c_str());
    }
    
    http.end();
    digitalWrite(LED_PIN, LOW);
  }
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.begin(9600);
  STM32Serial.begin(9600);
  delay(1000);

  setup_wifi();
  mqttClient.setServer(mqtt_server, mqtt_port);
}

void loop() {
  // Ensure MQTT connection remains active
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop();

  // 1. Constantly check SoftwareSerial buffer so data from STM32 is never dropped
  if (STM32Serial.available()) {
    latestTelemetry = STM32Serial.readStringUntil('\n');
    latestTelemetry.trim();
  }

  unsigned long currentMillis = millis();

  // 2. Channel 1: Push live telemetry to MQTT broker every 1 second
  if (currentMillis - lastLiveTime >= liveInterval) {
    if (latestTelemetry.length() > 0) {
      mqttClient.publish(mqtt_topic, latestTelemetry.c_str());
    }
    lastLiveTime = currentMillis;
  }

  // 3. Channel 2: Push log entry to Google Sheets every 10 minutes
  if (currentMillis - lastLogTime >= logInterval) {
    if (latestTelemetry.length() > 0) {
      sendToGoogleSheets(latestTelemetry);
    }
    lastLogTime = currentMillis;
  }
}