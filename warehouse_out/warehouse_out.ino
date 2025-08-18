#include <WiFi.h>
#include <SPI.h>
#include <MFRC522.h>

// WiFi credentials
const char* ssid = "mitanshu";
const char* password = "qwerty123";

// Server details
const char* server_ip = "10.102.191.109";
const uint16_t server_port = 1234;
WiFiClient client; 

// RFID pins
#define RFID_SS_PIN    5
#define RFID_RST_PIN   22

// LED pins
#define LED_RED_PIN     25
#define LED_GREEN_PIN   26
#define LED_BLUE_PIN    27

MFRC522 mfrc522(RFID_SS_PIN, RFID_RST_PIN);
MFRC522::MIFARE_Key key;

// Simple data to write to Block 9 (16 bytes)
String block9_string = "WAREHOUSE_OUT"; // 16 characters exactly

// Variables
String lastCardUID = "";
unsigned long lastCardTime = 0;
const unsigned long CARD_COOLDOWN = 3000;
uint16_t sequenceCounter = 1;
bool serverConnected = false;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("=== Simple RFID Reader-Writer ===");
  
  // Initialize pins
  pinMode(LED_RED_PIN, OUTPUT);
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_BLUE_PIN, OUTPUT);
  
  // Test LED
  setColor(255, 0, 0); delay(500);  // Red
  setColor(0, 255, 0); delay(500);  // Green
  setColor(0, 0, 255); delay(500);  // Blue
  setColor(0, 0, 0);                // Off
  
  // Initialize RFID
  SPI.begin();
  mfrc522.PCD_Init();
  
  // Set RFID key (default)
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }
  
  // Connect WiFi
  WiFi.begin(ssid, password);
  setColor(0, 0, 255); // Blue for connecting
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi connected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  
  // Connect to server
  connectToServer();
  
  Serial.println("System ready - place card near reader");
  setColor(0, 255, 0); // Green for ready
}

void connectToServer() {
  Serial.print("Connecting to server...");
  
  if (client.connect(server_ip, server_port)) {
    Serial.println(" Connected!");
    client.println("READER_WRITER_READY");
    
    // Wait for response
    unsigned long timeout = millis() + 5000;
    while (millis() < timeout) {
      if (client.available()) {
        String response = client.readStringUntil('\n');
        response.trim();
        Serial.println("Server: " + response);
        
        if (response == "ACK_READER_WRITER_READY") {
          serverConnected = true;
          break;
        }
      }
      delay(10);
    }
  } else {
    Serial.println(" Failed!");
    serverConnected = false;
    setColor(255,0,0);
  }
}

void loop() {
  // Check for new card
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    
    // Get card UID
    String cardUID = "";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      if (mfrc522.uid.uidByte[i] < 0x10) cardUID += "0";
      cardUID += String(mfrc522.uid.uidByte[i], HEX);
    }
    cardUID.toUpperCase();
    
    // Check cooldown
    if (cardUID == lastCardUID && (millis() - lastCardTime) < CARD_COOLDOWN) {
      mfrc522.PICC_HaltA();
      mfrc522.PCD_StopCrypto1();
      return;
    }
    
    Serial.println("\n--- Card Detected ---");
    Serial.println("UID: " + cardUID);
    
    lastCardUID = cardUID;
    lastCardTime = millis();
    
    setColor(255, 255, 0); // Yellow for processing
    
    // Read Block 8 and Write Block 9
    String block8_data = "";
    String block9_data = "";
    
    if (readBlock8(block8_data) && writeBlock9(block9_data)) {
      Serial.println("SUCCESS!");
      
      // Send log to server
      if (serverConnected && client.connected()) {
        String logMessage = "RFID_LOG|UID:" + cardUID + 
                           "|BLOCK8:" + block8_data + 
                           "|BLOCK9:" + block9_data + 
                           "|SEQ:" + String(sequenceCounter++);
        
        client.println(logMessage);
        Serial.println("Sent: " + logMessage);
        
        // Wait for server response
        unsigned long timeout = millis() + 3000;
        while (millis() < timeout) {
          if (client.available()) {
            String response = client.readStringUntil('\n');
            response.trim();
            Serial.println("Server: " + response);
            break;
          }
          delay(10);
        }
      }
      
      // Success flash
      for(int i = 0; i < 3; i++) {
        setColor(0, 255, 0);
        delay(200);
        setColor(0, 0, 0);
        delay(200);
      }
      
    } else {
      Serial.println("FAILED!");
      setColor(255,0,0);
      
      // Error flash
      for(int i = 0; i < 3; i++) {
        setColor(255, 0, 0);
        delay(200);
        setColor(0, 0, 0);
        delay(200);
      }
    }
    
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    
    setColor(0, 255, 0); // Back to ready
    delay(1000);
  }
  
  // Reconnect if needed
  if (!client.connected() && serverConnected) {
    serverConnected = false;
    Serial.println("Lost server connection");
  }
  
  if (!serverConnected && WiFi.status() == WL_CONNECTED) {
    static unsigned long lastReconnect = 0;
    if (millis() - lastReconnect > 10000) {
      lastReconnect = millis();
      connectToServer();
    }
  }
  
  delay(100);
}

bool readBlock8(String &data) {
  // Authenticate Block 8
  if (mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, 8, &key, &(mfrc522.uid)) != MFRC522::STATUS_OK) {
    Serial.println("Block 8 auth failed");
    return false;
  }
  
  // Read Block 8
  byte buffer[18];
  byte size = sizeof(buffer);
  
  if (mfrc522.MIFARE_Read(8, buffer, &size) != MFRC522::STATUS_OK) {
    Serial.println("Block 8 read failed");
    return false;
  }
  
  // Interpret Block 8 data - assuming it contains structured data
  // First 4 bytes as card ID (little endian)
  uint32_t cardID = (buffer[3] << 24) | (buffer[2] << 16) | (buffer[1] << 8) | buffer[0];
  
  // Next 4 bytes as timestamp (little endian)
  uint32_t timestamp = (buffer[7] << 24) | (buffer[6] << 16) | (buffer[5] << 8) | buffer[4];
  
  // Create readable format
  data = "CardID:" + String(cardID) + ",Timestamp:" + String(timestamp);
  
  // Add any readable text from remaining bytes
  String textPart = "";
  for (byte i = 8; i < 16; i++) {
    if (buffer[i] >= 32 && buffer[i] <= 126) {
      textPart += (char)buffer[i];
    } else if (buffer[i] == 0) {
      break;
    }
  }
  
  if (textPart.length() > 0) {
    data += ",Text:" + textPart;
  }
  
  Serial.println("Block 8 decoded: " + data);
  Serial.print("Raw bytes: ");
  for (byte i = 0; i < 16; i++) {
    Serial.print("0x");
    if (buffer[i] < 0x10) Serial.print("0");
    Serial.print(buffer[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
  
  return true;
}

bool writeBlock9(String &data) {
  // Authenticate Block 9
  if (mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, 9, &key, &(mfrc522.uid)) != MFRC522::STATUS_OK) {
    Serial.println("Block 9 auth failed");
    return false;
  }
  
  // Prepare data to write (16 bytes)
  byte writeBuffer[16];
  
  // Make sure string is exactly 16 characters
  String writeString = block9_string;
  while (writeString.length() < 16) {
    writeString += " "; // Pad with spaces
  }
  if (writeString.length() > 16) {
    writeString = writeString.substring(0, 16); // Truncate
  }
  
  // Convert string to bytes
  for (int i = 0; i < 16; i++) {
    writeBuffer[i] = writeString.charAt(i);
  }
  
  // Write to Block 9
  if (mfrc522.MIFARE_Write(9, writeBuffer, 16) != MFRC522::STATUS_OK) {
    Serial.println("Block 9 write failed");
    return false;
  }
  
  // Return the readable text that was written
  data = writeString;
  
  Serial.println("Block 9 written: " + writeString);
  return true;
}

void setColor(int red, int green, int blue) {
  // Common positive RGB LED - invert values
  analogWrite(LED_RED_PIN, 255 - red);
  analogWrite(LED_GREEN_PIN, 255 - green);
  analogWrite(LED_BLUE_PIN, 255 - blue);
}