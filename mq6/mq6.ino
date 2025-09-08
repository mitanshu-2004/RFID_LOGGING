#include <WiFi.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// WiFi credentials
const char* ssid = "COMPUTER LAB 2.4";
const char* password = "IIPDELHI@1234";

// Server IP and port
const char* server_ip = "192.168.1.101";
const uint16_t server_port = 1234;
WiFiClient client;

// OLED display dimensions
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// ESP32 pin connections to SH1106 OLED 
#define OLED_DC     2   // DC pin
#define OLED_RESET  4   // RES pin  
#define OLED_CS     21  // CS pin
// SDA → GPIO 23 (Hardware MOSI)
// SCK → GPIO 18 (Hardware SCK)

// RFID RC522 pin connections
#define RFID_SS_PIN    5    // SDA/SS pin for RC522
#define RFID_RST_PIN   22   // RST pin for RC522
// MOSI → GPIO 23 (shared with OLED)
// MISO → GPIO 19 (Hardware MISO)
// SCK → GPIO 18 (shared with OLED)

// Create display and RFID objects
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &SPI, OLED_DC, OLED_RESET, OLED_CS);
MFRC522 mfrc522(RFID_SS_PIN, RFID_RST_PIN);

// Variables for RFID management
String lastCardUID = "";
unsigned long lastCardTime = 0;
const unsigned long CARD_TIMEOUT = 5000; // Increased timeout
const unsigned long CARD_COOLDOWN = 3000; // Cooldown period before same card can be processed again
boolean cardPresent = false;
boolean processingCard = false;

// ID counter for writing to cards
uint32_t currentID = 1000; // Starting ID number
const byte BLOCK_NUMBER = 8; // Block to write ID to

// RFID Key (default key for most cards)
MFRC522::MIFARE_Key key;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("Starting SH1106 OLED + RFID RC522 + WiFi System...");
  
  // Initialize SPI
  SPI.begin();
  
  // Initialize SH1106 display
  if (!display.begin(0x3C)) {
    Serial.println("SH1106 allocation failed!");
    for(;;);
  }
  
  // Initialize RFID reader
  mfrc522.PCD_Init();
  delay(100);
  
  // Check if RFID reader is properly connected
  byte version = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
  if (version == 0x00 || version == 0xFF) {
    Serial.println("WARNING: Communication failure with RFID reader!");
    Serial.println("Check wiring connections.");
  } else {
    Serial.print("RFID Reader Version: 0x");
    Serial.println(version, HEX);
    mfrc522.PCD_DumpVersionToSerial();
  }
  
  // Prepare the security key (default key)
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }
  
  Serial.println("SH1106 Display and RFID RC522 initialized successfully!");
  
  // Show startup screen
  showStartupScreen();
  delay(2000);
  
  // Connect to WiFi
  connectToWiFi();
  
  // Connect to server
  connectToServer();
  
  // Show ready screen
  showReadyScreen();
}

void loop() {
  // Maintain server connection
  if (!client.connected()) {
    connectToServer();
  }
  
  // Check for new RFID cards only if not currently processing
  if (!processingCard && mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    // Read card UID first
    String currentCardUID = "";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      if (mfrc522.uid.uidByte[i] < 0x10) currentCardUID += "0";
      currentCardUID += String(mfrc522.uid.uidByte[i], HEX);
    }
    currentCardUID.toUpperCase();
    
    // Check if this is the same card within cooldown period
    if (currentCardUID == lastCardUID && (millis() - lastCardTime) < CARD_COOLDOWN) {
      Serial.println("Same card detected within cooldown period - ignoring");
      mfrc522.PICC_HaltA();
      mfrc522.PCD_StopCrypto1();
      delay(100);
      return;
    }
    
    // Process the card
    handleRFIDCard(currentCardUID);
  }
  
  // Check if card timeout has occurred
  if (cardPresent && (millis() - lastCardTime > CARD_TIMEOUT)) {
    cardPresent = false;
    processingCard = false;
    showReadyScreen();
  }
  
  delay(100); // Increased delay to reduce unnecessary scanning
}

void connectToWiFi() {
  showConnectingWiFi();
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi connected. IP: " + WiFi.localIP().toString());
    showWiFiConnected();
  } else {
    Serial.println("\n❌ WiFi connection failed!");
    showWiFiError();
  }
  delay(2000);
}

void connectToServer() {
  Serial.print("🔌 Connecting to server... ");
  if (client.connect(server_ip, server_port)) {
    Serial.println("✅ Connected to server.");
    sendToServer("SYSTEM_READY");
  } else {
    Serial.println("❌ Connection failed. Retrying in 5 seconds...");
    delay(5000);
  }
}

void sendToServer(String message) {
  if (client.connected()) {
    client.println(message);
    Serial.println("Sent to server: " + message);
  } else {
    Serial.println("❌ Not connected to server.");
  }
}

void handleRFIDCard(String cardUID) {
  // Set processing flag
  processingCard = true;
  
  // Update card presence tracking
  lastCardUID = cardUID;
  lastCardTime = millis();
  cardPresent = true;
  
  // Log to serial
  Serial.print("Card detected: ");
  Serial.println(cardUID);
  
  // Show processing screen
  showProcessingCard(cardUID);
  delay(500); // Give user time to see processing screen
  
  // Write ID to card and get result
  bool writeSuccess = writeIDToCard();
  
  if (writeSuccess) {
    // Send success info to server
    String logMessage = "CARD_PROCESSED|UID:" + cardUID + "|ID:" + String(currentID-1) + "|BLOCK:" + String(BLOCK_NUMBER);
    sendToServer(logMessage);
    
    showSuccessScreen(cardUID, currentID-1);
    Serial.println("✅ Card processed successfully! ID: " + String(currentID-1));
  } else {
    // Send error info to server
    String logMessage = "CARD_ERROR|UID:" + cardUID + "|ERROR:Write_Failed";
    sendToServer(logMessage);
    
    showErrorScreen(cardUID);
    Serial.println("❌ Failed to write to card!");
  }
  
  // Halt PICC
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  
  delay(2000); // Show result for 2 seconds
  
  // Reset processing flag
  processingCard = false;
}

bool writeIDToCard() {
  // Check card type
  MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
  if (piccType != MFRC522::PICC_TYPE_MIFARE_MINI &&
      piccType != MFRC522::PICC_TYPE_MIFARE_1K &&
      piccType != MFRC522::PICC_TYPE_MIFARE_4K) {
    Serial.println("This sample only works with MIFARE Classic cards.");
    return false;
  }
  
  // Authenticate block
  MFRC522::StatusCode status = mfrc522.PCD_Authenticate(
    MFRC522::PICC_CMD_MF_AUTH_KEY_A, BLOCK_NUMBER, &key, &(mfrc522.uid));
  
  if (status != MFRC522::STATUS_OK) {
    Serial.print("Authentication failed: ");
    Serial.println(mfrc522.GetStatusCodeName(status));
    return false;
  }
  
  // Prepare data to write (ID as 4-byte integer + padding to 16 bytes)
  byte dataBlock[16];
  
  // Write current ID as 4-byte integer (little-endian)
  dataBlock[0] = (currentID >> 0) & 0xFF;
  dataBlock[1] = (currentID >> 8) & 0xFF;
  dataBlock[2] = (currentID >> 16) & 0xFF;
  dataBlock[3] = (currentID >> 24) & 0xFF;
  
  // Add timestamp (4 bytes)
  uint32_t timestamp = millis() / 1000;
  dataBlock[4] = (timestamp >> 0) & 0xFF;
  dataBlock[5] = (timestamp >> 8) & 0xFF;
  dataBlock[6] = (timestamp >> 16) & 0xFF;
  dataBlock[7] = (timestamp >> 24) & 0xFF;
  
  // Fill remaining bytes with zeros
  for (byte i = 8; i < 16; i++) {
    dataBlock[i] = 0x00;
  }
  
  // Write data to block
  status = mfrc522.MIFARE_Write(BLOCK_NUMBER, dataBlock, 16);
  
  if (status != MFRC522::STATUS_OK) {
    Serial.print("Write failed: ");
    Serial.println(mfrc522.GetStatusCodeName(status));
    return false;
  }
  
  // Verify write by reading back
  byte readBuffer[18];
  byte bufferSize = sizeof(readBuffer);
  
  status = mfrc522.MIFARE_Read(BLOCK_NUMBER, readBuffer, &bufferSize);
  
  if (status == MFRC522::STATUS_OK) {
    // Verify the ID matches
    uint32_t readID = (readBuffer[3] << 24) | (readBuffer[2] << 16) | 
                      (readBuffer[1] << 8) | readBuffer[0];
    
    if (readID == currentID) {
      Serial.print("✅ Write verified! ID ");
      Serial.print(currentID);
      Serial.print(" written to block ");
      Serial.println(BLOCK_NUMBER);
      
      currentID++; // Increment for next card
      return true;
    } else {
      Serial.println("❌ Write verification failed!");
      return false;
    }
  } else {
    Serial.print("Read verification failed: ");
    Serial.println(mfrc522.GetStatusCodeName(status));
    return false;
  }
}

// Display Functions
void showStartupScreen() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SH110X_WHITE);
  displayCenteredText("RFID", 10, 2);
  displayCenteredText("WRITER", 30, 2);
  display.setTextSize(1);
  displayCenteredText("Initializing...", 50, 1);
  display.display();
}

void showConnectingWiFi() {
  display.clearDisplay();
  display.setTextSize(1);
  displayCenteredText("Connecting WiFi", 25, 1);
  displayCenteredText("Please wait...", 40, 1);
  display.display();
}

void showWiFiConnected() {
  display.clearDisplay();
  display.setTextSize(1);
  displayCenteredText("WiFi Connected!", 20, 1);
  String ip = WiFi.localIP().toString();
  displayCenteredText(ip, 35, 1);
  display.display();
}

void showWiFiError() {
  display.clearDisplay();
  display.setTextSize(1);
  displayCenteredText("WiFi Failed!", 20, 1);
  displayCenteredText("Check credentials", 35, 1);
  display.display();
}

void showReadyScreen() {
  display.clearDisplay();
  
  // Draw border
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SH110X_WHITE);
  
  // Card icon
  drawCardIcon(54, 8);
  
  // Main text
  display.setTextSize(1);
  displayCenteredText("SCAN CARD", 30, 1);
  displayCenteredText("TO WRITE ID", 42, 1);
  
  // Status info
  display.setCursor(5, 54);
  display.setTextColor(SH110X_WHITE);
  display.print("Next ID: ");
  display.print(currentID);
  display.display();
}

void showProcessingCard(String cardUID) {
  display.clearDisplay();
  
  // Processing animation or indicator
  display.drawRect(10, 10, SCREEN_WIDTH-20, SCREEN_HEIGHT-20, SH110X_WHITE);
  
  display.setTextSize(1);
  displayCenteredText("PROCESSING...", 20, 1);
  displayCenteredText("Writing ID: " + String(currentID), 32, 1);
  
  // Show shortened UID
  String shortUID = cardUID.substring(0, 8);
  displayCenteredText("UID: " + shortUID, 44, 1);
  display.display();
}

void showSuccessScreen(String cardUID, uint32_t writtenID) {
  display.clearDisplay();
  
  // Success border
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SH110X_WHITE);
  display.drawRect(2, 2, SCREEN_WIDTH-4, SCREEN_HEIGHT-4, SH110X_WHITE);
  
  // Checkmark
  drawCheckmark(54, 5);
  
  display.setTextSize(1);
  displayCenteredText("SUCCESS!", 28, 1);
  displayCenteredText("ID: " + String(writtenID), 40, 1);
  
  // Show UID at bottom
  display.setCursor(5, 54);
  display.setTextColor(SH110X_WHITE);
  display.print("UID: ");
  display.print(cardUID.substring(0, 8));
  display.display();
}

void showErrorScreen(String cardUID) {
  display.clearDisplay();
  
  // Error border
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SH110X_WHITE);
  
  // X mark
  drawXMark(54, 5);
  
  display.setTextSize(1);
  displayCenteredText("WRITE FAILED!", 28, 1);
  displayCenteredText("Try Again", 40, 1);
  
  // Show UID at bottom
  display.setCursor(5, 54);
  display.setTextColor(SH110X_WHITE);
  display.print("UID: ");
  display.print(cardUID.substring(0, 8));
  display.display();
}

// Helper Functions
void displayCenteredText(String text, int y, int textSize) {
  display.setTextSize(textSize);
  display.setTextColor(SH110X_WHITE);
  
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  
  int x = (SCREEN_WIDTH - w) / 2;
  display.setCursor(x, y);
  display.println(text);
}

void drawCardIcon(int x, int y) {
  display.drawRect(x, y, 20, 14, SH110X_WHITE);
  display.drawRect(x+1, y+1, 18, 12, SH110X_WHITE);
  display.drawLine(x+3, y+4, x+17, y+4, SH110X_WHITE);
  display.drawLine(x+3, y+6, x+13, y+6, SH110X_WHITE);
  display.drawLine(x+3, y+8, x+15, y+8, SH110X_WHITE);
  display.drawLine(x+3, y+10, x+11, y+10, SH110X_WHITE);
}

void drawCheckmark(int x, int y) {
  display.drawLine(x+5, y+8, x+8, y+11, SH110X_WHITE);
  display.drawLine(x+8, y+11, x+15, y+4, SH110X_WHITE);
  display.drawLine(x+4, y+8, x+8, y+12, SH110X_WHITE);
  display.drawLine(x+8, y+12, x+16, y+4, SH110X_WHITE);
  display.drawCircle(x+10, y+8, 12, SH110X_WHITE);
}

void drawXMark(int x, int y) {
  display.drawLine(x+4, y+4, x+16, y+16, SH110X_WHITE);
  display.drawLine(x+16, y+4, x+4, y+16, SH110X_WHITE);
  display.drawLine(x+5, y+4, x+17, y+16, SH110X_WHITE);
  display.drawLine(x+17, y+4, x+5, y+16, SH110X_WHITE);
  display.drawCircle(x+10, y+10, 12, SH110X_WHITE);
}