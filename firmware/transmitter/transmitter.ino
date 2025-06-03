#include "thingProperties.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <time.h>

// Internal ESP32 LED pin
#define LED_PIN 2
// Passive buzzer pin 
#define ZVONO_PIN 17
// WiFi channel which will be used
#define WIFI_CHANNEL 1

// Structure that is sent over ESP32 Mesh
typedef struct struct_message {
    char text[20];
    int value;
} struct_message;

// Reciver MAC Address(Black ESP) 
uint8_t reciverAddress_black[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
// Reciver MAC Address(Yellow ESP)
uint8_t reciverAddress_yellow[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// Buzzer variables
bool alarm_zvuk = false;
bool poruka_poslata = false;
bool tone1Played = false;
bool tone2Played = false;
unsigned long tone1StartTime = 0;
unsigned long tone2StartTime = 0;

// Ringtone functions
void srecnoZvono() {
    noTone(ZVONO_PIN);
    tone(ZVONO_PIN, 262, 150); // C4
    delay(150);
    tone(ZVONO_PIN, 330, 150); // E4
    delay(150);
    tone(ZVONO_PIN, 524, 150); // C5
    delay(150);
}

void nesrecnoZvono() {
    noTone(ZVONO_PIN);
    tone(ZVONO_PIN, 524, 150); // C4
    delay(150);
    tone(ZVONO_PIN, 330, 150); // E4
    delay(150);
    tone(ZVONO_PIN, 262, 150); // C5
    delay(150);
}

void ljutoZvono() {
    unsigned long currentMillis = millis();

    // Start first tone
    if (!tone1Played) {
        tone(ZVONO_PIN, 262);  // C4
        tone1StartTime = currentMillis;
        tone1Played = true;
    }

    // After 150ms, play second tone
    if (tone1Played && !tone2Played && currentMillis - tone1StartTime >= 150) {
        noTone(ZVONO_PIN);
        tone(ZVONO_PIN, 370);  // F#4
        tone2StartTime = currentMillis;
        tone2Played = true;
    }

    // After another 150ms, stop everything
    if (tone2Played && currentMillis - tone2StartTime >= 150) {
        noTone(ZVONO_PIN);
        tone1Played = false;
        tone2Played = false;
        // Melody finished — ready to be triggered again if needed
    }
}

// Function that returns current local time as a String type
String trenutnoVrijeme() {
    time_t currentTime;
    struct tm *localTime;

    // Get current time
    time(&currentTime);

    // Convert to local time format
    localTime = localtime(&currentTime);

    // Convert to string
    return String(asctime(localTime));
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

// Function that is called every time data is sent on mesh
void OnDataSent(const uint8_t *mac, esp_now_send_status_t status) {
    Serial.print("[TX] ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
    digitalWrite(LED_PIN, status == ESP_NOW_SEND_SUCCESS);
    delay(50);
    digitalWrite(LED_PIN, LOW);
}

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

    // Code for when the data is recived
    if (!strcmp(message.text, "IR SENZOR")) {
        if ((bezbjednosni_sistem) && !(message.value)) {
            alarm_zvuk = true;
        }
    }
    if (!strcmp(message.text, "RFID SENZOR")) {
        bezbjednosni_sistem = !bezbjednosni_sistem;
        onBezbjednosniSistemChange();
    }
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
    delay(100);  // Crucial delay

    // Verify MAC address
    Serial.print("Device MAC: ");
    Serial.println(WiFi.macAddress());
    
    // Locking in a WiFi channel - adjust depending on WiFi connection
    esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
}

// Function that is called every time data needs to be sent 
void sendData(struct_message message, uint8_t *address) {
    Serial.print("[SEND] ");
    Serial.print(getMac(address));
    Serial.print(" ");
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

void setup() {
    Serial.begin(115200);
    while (!Serial);
    Serial.println("\n=== ESP-NOW TRANSMITTER ===");
    blinkLED(3, 200);

    // WiFi Init
    Serial.println("[INIT] Starting WiFi...");
    initializeWiFi();

    // Defined in thingProperties.h
    initProperties();
    // Connect to Arduino IoT Cloud
    ArduinoCloud.begin(ArduinoIoTPreferredConnection);
    setDebugMessageLevel(2);
    ArduinoCloud.printDebugInfo();
    
    // ESP-NOW Init
    Serial.println("[INIT] Starting ESP-NOW...");
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ERROR] ESP-NOW init failed");
        while (1) blinkLED(10, 100);
    }

    // Register callbacks
    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataRecv);

    // Add peer (Reciver node - Yellow ESP)
    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, reciverAddress_yellow, 6);
    peerInfo.channel = WiFi.channel();
    peerInfo.encrypt = false;
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("[ERROR] Failed to add peer (Yellow ESP32)");
        while (1) blinkLED(5, 200);
    }

    // Add peer (Reciver node - Black ESP)
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, reciverAddress_black, 6);
    peerInfo.channel = WiFi.channel();
    peerInfo.encrypt = false;
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("[ERROR] Failed to add peer (Black ESP32)");
        while (1) blinkLED(5, 200);
    }

    Serial.println("\n[READY] Transmitter initialized");
    Serial.print("Broadcasting on channel ");
    Serial.println(WiFi.channel());
    blinkLED(2, 250);

    // Pin mode settings
    pinMode(ZVONO_PIN, OUTPUT);
}

void loop() {
    ArduinoCloud.update(); 

    // Alarm system activation
    if (alarm_zvuk) {
        ljutoZvono();
        
        if (!poruka_poslata) {
            String temp = " ";

            temp += trenutnoVrijeme();
            temp += "ALARM OKINUT : POTENCIJALNI ULJEZ!";
            poruke = temp;

            poruka_poslata = true;
        }
    }
}

void onLedKupatiloChange() { 
    struct_message message;
    
    if (led_kupatilo.getSwitch()) {
        message.value = map(led_kupatilo.getBrightness(), 0, 100, 0, 255);
    } else {
        message.value = 0;
    }
    strcpy(message.text, "LED_KUPATILO");
    sendData(message, reciverAddress_yellow);
}

void onLedSpavacaChange() { 
    struct_message message;
    
    if (led_spavaca.getSwitch()) {
        message.value = map(led_spavaca.getBrightness(), 0, 100, 0, 255);
    } else {
        message.value = 0;
    }
    strcpy(message.text, "LED_SPAVACA");
    sendData(message, reciverAddress_yellow);
}

void onLedDnevniChange() { 
    struct_message message;
    
    if (led_dnevni.getSwitch()) {
        message.value = map(led_dnevni.getBrightness(), 0, 100, 0, 255);
    } else {
        message.value = 0;
    }
    strcpy(message.text, "LED_DNEVNI");
    sendData(message, reciverAddress_yellow);
}

void onLedTrpezarijaChange() { 
    struct_message message;
    
    if (led_trpezarija.getSwitch()) {
        message.value = map(led_trpezarija.getBrightness(), 0, 100, 0, 255);
    } else {
        message.value = 0;
    }
    strcpy(message.text, "LED_TRPEZARIJA");
    sendData(message, reciverAddress_yellow);
}

void onGarazaVrataChange() {
    struct_message message;

    message.value = garaza_vrata;
    strcpy(message.text, "GARAZA_VRATA");

    sendData(message, reciverAddress_black);
}

void onZavjesaChange() {
    struct_message message;

    message.value = zavjesa;
    strcpy(message.text, "ZAVJESA");

    sendData(message, reciverAddress_black);
}

void onBezbjednosniSistemChange() {
    String temp = "";

    if (bezbjednosni_sistem) {
        nesrecnoZvono();
    } else {
        srecnoZvono();
    }

    if (!bezbjednosni_sistem) {
        alarm_zvuk = false;
        noTone(ZVONO_PIN);
        poruka_poslata = true;
    }
    
    temp += trenutnoVrijeme();
    temp += " BEZBJEDNOSNI SISTEM ";
    if (bezbjednosni_sistem == HIGH) {
        temp += "AKTIVIRAN";
    } else {
        temp += "DEAKTIVIRAN";
    }
    poruke = temp;
}

void onPorukeChange() {
}