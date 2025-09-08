const int mq6Pin = 34;  // Analog pin connected to A0 of MQ-6

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);  // ESP32 ADC range: 0–4095
}

void loop() {
  int rawValue = analogRead(mq6Pin);

  // Map raw sensor value to 0–100% gas concentration
  // Calibrate these values by testing in clean and gas-exposed air
  int gasPercent = map(rawValue, 300, 3000, 0, 100);  // Example calibration
  gasPercent = constrain(gasPercent, 0, 100);         // Clamp to 0–100%

  float voltage = rawValue * (3.3 / 4095.0);  // Optional: get voltage

  Serial.print("Raw: ");
  Serial.print(rawValue);
  

  delay(1000);
}
