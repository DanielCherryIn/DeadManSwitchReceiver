#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>

// Simulated Relay Pin
const uint8_t pinRelaySimulatorOutput = 26;

// Data frame structure (Must exactly match the transmitter)
typedef struct PayloadData {
  uint32_t payloadPacketCounter; 
  bool payloadIsSwitchActive;
} s_PayloadData;

s_PayloadData receiverData;

// Watchdog Timer Variables
unsigned long lastPacketReceivedTime = 0;
const unsigned long watchdogTimeoutMs = 500; 

// Callback function executing automatically when a packet arrives
void onDataReceivedCallback(const uint8_t *senderMacAddress, const uint8_t *incomingData, int dataLength) {
  
  // Verify the incoming byte count matches our struct to prevent memory corruption
  if (dataLength == sizeof(receiverData)) {
    
    // Transfer the raw bytes into our local struct memory
    memcpy(&receiverData, incomingData, sizeof(receiverData));
    
    // Update the watchdog timestamp immediately upon receiving valid data
    lastPacketReceivedTime = millis();

    // Actuate the simulated relay
    if (receiverData.payloadIsSwitchActive) {
      digitalWrite(pinRelaySimulatorOutput, HIGH);
    } else {
      digitalWrite(pinRelaySimulatorOutput, LOW);
    }
  }
}

void setup() {
  // Initialize output and force it to a safe low state at boot
  pinMode(pinRelaySimulatorOutput, OUTPUT);
  digitalWrite(pinRelaySimulatorOutput, LOW);

  // Initialize Wi-Fi in Station mode
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    // In a final build, you would trigger a local error LED here
    return;
  }

  // Bind the receive callback function
  esp_now_register_recv_cb(onDataReceivedCallback);
}

void loop() {
  // Continuous Watchdog Check
  if ((millis() - lastPacketReceivedTime) > watchdogTimeoutMs) {
    // The threshold has elapsed without a new heartbeat packet. 
    // Force the relay low to ensure the system fails safely.
    digitalWrite(pinRelaySimulatorOutput, LOW);
  }
}