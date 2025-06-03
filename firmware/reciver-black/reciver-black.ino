#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <ESP32Servo.h>
#include <AccelStepper.h>

// Pins dedicated to stepper driver
#define IN1 5
#define IN2 18
#define IN3 19
#define IN4 22
#define IR_PIN 15

// Internal LED pin
#define LED_PIN 2
// Servo motor pin
#define SERVO_PIN 27 
// WiFi channel which will be used
#define WIFI_CHANNEL 1

// Debounce time in milliseconds(IR related)
#define DEBOUNCE_TIME 50          

// Debounce variables for IR sensor
bool lastStableState = HIGH;
unsigned long lastDebounceTime = 0;
unsigned long current_time = 0;

// Create a servo object
Servo myServo;
// Create AccelStepper object (type 8 for half-step)
AccelStepper stepper(AccelStepper::HALF4WIRE, IN1, IN3, IN2, IN4);

// Servo control variables
bool servoActive = false;
unsigned long servoStartTime = 0;

// Structure that is sent over ESP32 Mesh
typedef struct struct_message {
    char text[20];
    int value;
} struct_message;

// Transmitter MAC Address(Red ESP)
uint8_t transmitterAddress[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

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

    // Locking in a WiFi channel
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

    if (!strcmp(message.text, "GARAZA_VRATA")) {
        myServo.attach(SERVO_PIN);
        if (message.value == HIGH) {
            myServo.write(100);
            servoStartTime = millis();
            servoActive = true;
        } else {
            myServo.write(87);
            servoStartTime = millis();
            servoActive = true;
        }
    }

    if (!strcmp(message.text, "ZAVJESA")) {
        stepper.enableOutputs();
        if (message.value == HIGH) {
            stepper.moveTo(stepper.currentPosition() - 2048 * 7);
        } else {
            stepper.moveTo(stepper.currentPosition() + 2048 * 7);
        }
    }
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

    // Stepper motor settings
    stepper.setMaxSpeed(1000);    // Steps per second
    stepper.setAcceleration(500); // Steps per second²
    
    // IR pin mode setting
    pinMode(IR_PIN, INPUT_PULLUP);
}

void loop() {
    // Stepper motor control
    if (stepper.distanceToGo() != 0) {
        stepper.run();
    } else {
        stepper.disableOutputs(); 
    }

    // Check if servo needs to stop
    if (servoActive && (millis() - servoStartTime >= 1000)) {
        myServo.write(90); // Stop servo
        servoActive = false;
        myServo.detach();
    }

    // IR sensor debounce logic
    int currentReading = digitalRead(IR_PIN);

    // If the reading is different from the last recorded state, start debounce timer
    if (currentReading != lastStableState) {
        // If it's been stable for long enough, accept the new state
        if (millis() - lastDebounceTime > DEBOUNCE_TIME) {
            struct_message message;
            lastStableState = currentReading;
            strcpy(message.text, "IR SENZOR");
            message.value = lastStableState;
            sendData(message, transmitterAddress);
        }
    } else {
        // If state is same, update lastDebounceTime
        lastDebounceTime = millis();
    }
}