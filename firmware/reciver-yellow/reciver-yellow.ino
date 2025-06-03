#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <MFRC522.h>

// Internal LED pin
#define LED_PIN 2 
// LED pin definitions
#define LED_KUPATILO_PIN 0
#define LED_SPAVACA_PIN 4
#define LED_DNEVNI_PIN 13
#define LED_TRPEZARIJA_PIN 15
// WiFi channel which will be used
#define WIFI_CHANNEL 1

// RFID pins
#define SS_PIN 21    // SDA pin
#define RST_PIN 22   // RST pin

// Authorized card UIDs
#define CARD_UID_1 "EB0A1705"
#define CARD_UID_2 "43FEA800"

// Create MFRC522 instance
MFRC522 mfrc522(SS_PIN, RST_PIN);  

// Structure that is sent over ESP32 Mesh
typedef struct struct_message {
    char text[20];
    int value;
} struct_message;

// Transmitter MAC Address(Red ESP)
uint8_t transmitterAddress[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// RFID related restart time
unsigned long reset_time = 0;

// Function for internal led blinking (Debugging)
void blinkLED(int times, int duration) {
    pinMode(LED_PIN, OUTPUT);
    for (int i = 0; i < times; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(duration);
        digitalWrite(LED_PIN, LOW);
        delay(duration);
    }
}

// Function that gets device MAC address
String getMac(const uint8_t *mac) {
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return macStr;
}

// Function that prints device MAC address
void printMac(const uint8_t *mac) {
    Serial.print(getMac(mac));
}

// WiFi initialization function
void initializeWiFi() {
    // Complete WiFi stack reset
    WiFi.disconnect(true, true);
    delay(100);
    WiFi.mode(WIFI_OFF);
    delay(100);
    
    // Initialize with persistent storage disabled
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    delay(100);

    // Verify MAC address
    Serial.print("Device MAC: ");
    Serial.println(WiFi.macAddress());

    // Locking in a WiFi channel - adjust depending on WiFi connection
    esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
}

// Function that is called every time data needs to be sent 
void sendData(struct_message message, uint8_t *address) {
    Serial.print("[SEND] ");
    Serial.print(message.text);
    Serial.print(" ");
    Serial.println(message.value);
    
    esp_err_t result = esp_now_send(address, (uint8_t*)&message, sizeof(message));
    
    if (result != ESP_OK) {
        Serial.print("[ERROR] Send failed (0x");
        Serial.print(result, HEX);
        Serial.println(")");
        blinkLED(10, 50);
    }
}

// Function that gets called every time data from mesh is received
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    struct_message message;
    
    memcpy(&message, data, sizeof(message));

    Serial.print("[RX] From ");
    printMac(info->src_addr);
    Serial.print(": ");
    Serial.print(message.text);
    Serial.print(" ");
    Serial.println(message.value);
    blinkLED(2, 100);

    if (!strcmp(message.text, "LED_KUPATILO")) {
        analogWrite(LED_KUPATILO_PIN, message.value);
    }
    if (!strcmp(message.text, "LED_SPAVACA")) {
        analogWrite(LED_SPAVACA_PIN, message.value);
    }
    if (!strcmp(message.text, "LED_TRPEZARIJA")) {
        analogWrite(LED_TRPEZARIJA_PIN, message.value);
    }
    if (!strcmp(message.text, "LED_DNEVNI")) {
        analogWrite(LED_DNEVNI_PIN, message.value);
    }
}

// Function that returns RFID Sensor UID as a String
String getUIDString(MFRC522::Uid *uid) {
    String uidString = "";
    for (byte i = 0; i < uid->size; i++) {
        if (uid->uidByte[i] < 0x10) {
            uidString += "0";
        }
        uidString += String(uid->uidByte[i], HEX);
    }
    uidString.toUpperCase();
    return uidString;
}

void setup() {
    Serial.begin(115200);

    while (!Serial);
    Serial.println("\n=== ESP-NOW RECEIVER ===");
    blinkLED(3, 200);

    // WiFi Initialization
    Serial.println("[INIT] Starting WiFi...");
    initializeWiFi();

    // ESP-NOW Init
    Serial.println("[INIT] Starting ESP-NOW...");
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ERROR] ESP-NOW init failed");
        while (1) blinkLED(10, 100);
    }

    // Add peer (Red ESP)
    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, transmitterAddress, 6);
    peerInfo.channel = WiFi.channel();
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("[ERROR] Failed to add peer");
        while (1) blinkLED(5, 200);
    }

    // Register callback
    esp_now_register_recv_cb(OnDataRecv);

    Serial.println("\n[READY] Receiver initialized");
    Serial.print("Listening on channel ");
    Serial.println(WiFi.channel());
    blinkLED(1, 1000);

    // RFID settings
    SPI.begin();
    mfrc522.PCD_Init();

    // Pin mode settings
    pinMode(LED_KUPATILO_PIN, OUTPUT);
    pinMode(LED_SPAVACA_PIN, OUTPUT);
    pinMode(LED_TRPEZARIJA_PIN, OUTPUT);
    pinMode(LED_DNEVNI_PIN, OUTPUT);
}

void loop() {
    struct_message message;

    // Reads card UID every three seconds and sends message - if card is present
    if (millis() - reset_time > 3000) {
        if (!mfrc522.PICC_IsNewCardPresent()) return;
        if (!mfrc522.PICC_ReadCardSerial()) return;
        
        String cardID = getUIDString(&mfrc522.uid);
        Serial.println("Card UID: " + cardID);
        
        if (cardID == CARD_UID_1 || cardID == CARD_UID_2) {
            strcpy(message.text, "RFID SENZOR");
            message.value = 0;
            sendData(message, transmitterAddress);
            reset_time = millis();
        }
        
        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();
    }
}