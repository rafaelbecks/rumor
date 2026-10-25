
#include <WiFi.h>
#include <WebSocketsServer.h>
#include <ESPmDNS.h>

const char* ssid = "Personal-256";
const char* password = "";

const int TRIG_PIN = 25;
const int ECHO_PIN = 26;

WebSocketsServer webSocket(81);

unsigned long lastReading = 0;
const unsigned long interval = 100;

void onWebSocketEvent(
  uint8_t client,
  WStype_t type,
  uint8_t* payload,
  size_t length
) {
  if (type == WStype_CONNECTED) {
    Serial.println("WebSocket client connected");
  } else if (type == WStype_DISCONNECTED) {
    Serial.println("WebSocket client disconnected");
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.print("Connecting to Wi-Fi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }


  Serial.println();
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());

  if (MDNS.begin("rumor-sensor")) {
    MDNS.addService("ws", "tcp", 81);
    Serial.println("Device: rumor-sensor.local");
    Serial.println("WebSocket: ws://rumor-sensor.local:81/");
  } else {
    Serial.println("mDNS failed to start");
  }

  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);

  Serial.println("WebSocket server started on port 81");
}

void loop() {
  webSocket.loop();

  if (millis() - lastReading >= interval) {
    lastReading = millis();

    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    unsigned long duration = pulseIn(ECHO_PIN, HIGH, 30000);
    float distance = duration * 0.0343f / 2.0f;

    String json = "{";
    json += "\"duration_us\":" + String(duration) + ",";
    json += "\"distance_cm\":" + String(distance, 2);
    json += "}";

    webSocket.broadcastTXT(json);
  }
}
