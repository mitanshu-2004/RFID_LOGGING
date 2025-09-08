#include <WiFi.h>

// ================== Wi-Fi Credentials ==================
const char* ssid = "COMPUTER LAB 2.4";
const char* password = "IIPDELHI@1234";

// ================== Server Setup ==================
const char* host = "192.168.1.106";
const uint16_t gasPort = 5005;     // For MQ-4 + MQ-6
const uint16_t soilPort = 5004;    // For soil sensor

// ================== Gas Sensors ==================
#define MQ4_PIN 34   // MQ-4 sensor (methane/natural gas)
#define MQ6_PIN 35   // MQ-6 sensor (LPG/butane)

int mq4Value = 0;
int mq6Value = 0;

// Calibration (adjust after testing)
int mq4_min = 0, mq4_max = 4095;
int mq6_min = 0, mq6_max = 4095;

// ================== Soil Sensor ==================
#define SOIL_PIN 32   // ADC pin (GPIO32) → use different pin than MQ4/6
int dryValue = 3000;  // set after calibration
int wetValue = 1200;  // set after calibration

// ==================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Initialize sensor pins
  pinMode(MQ4_PIN, INPUT);
  pinMode(MQ6_PIN, INPUT);
  pinMode(SOIL_PIN, INPUT);

  // Connect to Wi-Fi
  Serial.print("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi.");
  Serial.print("Local IP: ");
  Serial.println(WiFi.localIP());

  Serial.println("MQ-4, MQ-6 and Soil Sensor Ready");
  Serial.println("Warming up gas sensors... (60s)");
  delay(60000); // warm-up
}

void loop() {
  // ================== Gas Sensors ==================
  mq4Value = analogRead(MQ4_PIN);
  mq6Value = analogRead(MQ6_PIN);

  int mq4Percent = map(mq4Value, mq4_min, mq4_max, 0, 100);
  int mq6Percent = map(mq6Value, mq6_min, mq6_max, 0, 100);
  mq4Percent = constrain(mq4Percent, 0, 100);
  mq6Percent = constrain(mq6Percent, 0, 100);

  // Send gas data
  WiFiClient gasClient;
  if (gasClient.connect(host, gasPort)) {
    String gasMessage = String(mq4Percent) + "%," + String(mq6Percent)+"%";
    gasClient.println(gasMessage);
    gasClient.stop();
    Serial.print("Gas Data Sent (5005): ");
    Serial.println(gasMessage + " | Raw: " + String(mq4Value) + "," + String(mq6Value));
  } else {
    Serial.println("Failed to connect to gas server (5005).");
  }

  // ================== Soil Sensor ==================
  int soilValue = analogRead(SOIL_PIN);
  int moisturePercent = map(soilValue, dryValue, wetValue, 0, 100);
  moisturePercent = constrain(moisturePercent, 0, 100);

  String soilMessage =String(moisturePercent)+"%";

  WiFiClient soilClient;
  if (soilClient.connect(host, soilPort)) {
    soilClient.println(soilMessage);
    soilClient.stop();
    Serial.print("Soil Data Sent (5004): ");
    Serial.println(soilMessage);
  } else {
    Serial.println("Failed to connect to soil server (5004).");
  }

  Serial.println("---");
  delay(1000);  // send every 1s
}
