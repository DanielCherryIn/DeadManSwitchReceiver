#include "Wireless.h"
#include <esp_now.h>
#include <WiFi.h>
#include "esp_wifi.h"

// Link authentication.
// The ONLY transmitter this relay obeys. Read the address from the transmitter's
// boot log ("[BOOT] Transmitter MAC ...") or its CLI `status`, and fill it in here.
// While left at all-zeros, EVERY packet is rejected and the relay can never
// energize, so the link is down until this is set.
const uint8_t transmitterMacAddress[6] = {0x24,0x6F,0x28,0xF6,0xDA,0x90};

// ESP-NOW encryption keys (espNowPmk/espNowLmk) come from the shared Protocol.h.


// Wireless Data Structures
// PacketType and PayloadData come from the shared Protocol.h (../common), so the
// layout can't drift from the transmitter's.

PayloadData receiverData;

// Non Volatile Storage variables
Preferences preferences;
const char* radioConfigNamespace = "radioConfig";
const char* channelTableStorageKey = "chanTable";
uint8_t rankedChannelTable[channelTableSize];


// States and timing variables.

volatile ReceiverState systemState = STATE_SEARCHING;
volatile uint8_t currentChannel = 1;
volatile unsigned long lastPacketReceivedTime = 0;
const unsigned long TimeoutMs = 500;

// This watchdog MUST stay shorter than the
// transmitter's CALIB_DISCONNECT_WAIT_MS (1000ms, in its Diagnostics.cpp), so that
// during a calibration sweep the transmitter can starve just long enough for this
// timeout to fire and return to scanning before it tests the next channel.

// True while the development bypass button is held (forces the relay ACTIVE).
// Written from loop(), read from the ESP-NOW receive callback, hence volatile.
volatile bool bypassActive = false;

// Last dead-man switch state received from the transmitter (telemetry). The radio
// callback only records state here.
volatile bool txSwitchActive = false;

// Scanning variables for the system.
unsigned long lastChannelSwitchTime = 0;

// How long the receiver will dwell in a channel to see if it matches the
// Current one of the transmitters.
// Cross-device timing contract: one full sweep is 13 * channelDwellTimeMs (~390ms).
// The transmitter's CALIB_HANDSHAKE_TIMEOUT_MS (1200ms) must comfortably exceed this
// so we pass through its channel a few times while it beacons. Keep this close to the
// transmitter's CALIB_BEACON_INTERVAL_MS (~25ms) so a beacon lands during each dwell.
const unsigned long channelDwellTimeMs = 30;

// Diagnostic Statistics.
// Used during Serial connections.
volatile uint32_t statsPacketsReceived = 0;
volatile uint32_t statsPacketsDropped = 0;
volatile uint32_t statsScanSweeps = 0;
volatile uint32_t expectedPacketCounter = 0;
volatile bool lastSwitchState = false;

// Auth rejections: frames dropped by the sender-MAC filter or the replay guard.
volatile uint32_t statsPacketsRejected = 0;

// Replay guard baseline. The transmitter's payloadPacketCounter is global
// across ALL packet types, so any accepted packet must carry a higher
// counter than the last one, except when we are (re)syncing from SEARCHING,
// which is the only state a rebooted transmitter (counter reset to 0) can reach
// us from, since its multi-second boot always trips our 500ms link timeout first.
volatile uint32_t lastAcceptedCounter = 0;

// Again, remember to keep the second channel empty as we're not using it.
void updateReceiverChannel(uint8_t channel) {
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

// Case 1: Received calibration hierarchy table from transmitter
static void handleCalibTablePacket() {
  Serial.println("\n[CALIB] Calibration Table Received from Transmitter.");

  // Validate every entry is a legal channel (1-13) before trusting the table.
  // ESP-NOW already CRCs the frame, but this is cheap defense in depth so a bad
  // byte can never reach esp_wifi_set_channel() with an illegal channel.
  bool tableValid = true;
  for (size_t i = 0; i < channelTableSize; i++) {
    if (receiverData.rankedChannels[i] < 1 || receiverData.rankedChannels[i] > 13) {
      tableValid = false;
      break;
    }
  }

  if (!tableValid) {
    Serial.println("[CALIB] Rejected: table contains an out-of-range channel. Continuing scan.");
  } else {
    // Save rankings to NVS flash memory
    preferences.begin(radioConfigNamespace, false);
    preferences.putBytes(channelTableStorageKey, receiverData.rankedChannels, channelTableSize);
    preferences.end();

    // Copy to RAM
    memcpy(rankedChannelTable, receiverData.rankedChannels, channelTableSize);

    // Hop to the best channel specified in the table
    uint8_t bestChannel = rankedChannelTable[0];
    currentChannel = bestChannel;
    updateReceiverChannel(currentChannel);

    systemState = STATE_WORKING;
    lastPacketReceivedTime = millis();
    expectedPacketCounter = 0; // Reset tracking on new channel
    Serial.printf("[CALIB] Table saved. Hopped to Best Channel: %u\n", bestChannel);
  }
}

// Case 2: Received Telemetry
static void handleTelemetryPacket() {
  // Transition to WORKING if we were searching
  if (systemState == STATE_SEARCHING) {
    systemState = STATE_WORKING;
    expectedPacketCounter = 0; // Reset tracking
    Serial.printf("\n[SCAN] Scan completed. Connected on Channel %u for telemetry.\n", currentChannel);
  }

  // Count drops only across a continuous telemetry stream. A zero baseline means
  // we just (re)synced after calibration, a channel hop, or a reconnect so
  // adopt the new counter without charging the gap as drops. (The transmitter's
  // packet counter is global and monotonic across every channel it probes, so an
  // un-gated diff would spike the drop count by hundreds.)
  if (expectedPacketCounter != 0 && receiverData.payloadPacketCounter > expectedPacketCounter) {
    statsPacketsDropped += (receiverData.payloadPacketCounter - expectedPacketCounter);
  }
  expectedPacketCounter = receiverData.payloadPacketCounter + 1;

  lastPacketReceivedTime = millis();

  // Log Switch State transitions
  if (receiverData.payloadIsSwitchActive != lastSwitchState) {
    Serial.printf("\n[RELAY] Switch state change: %s -> %s (Relay %s)\n",
                  lastSwitchState ? "ACTIVE" : "INACTIVE",
                  receiverData.payloadIsSwitchActive ? "ACTIVE" : "INACTIVE",
                  receiverData.payloadIsSwitchActive ? "HIGH" : "LOW");
    lastSwitchState = receiverData.payloadIsSwitchActive;
  }

  // Record the transmitter's switch intent only. loop() drives the relay from
  // this, so we never run blocking motor-shield I2C inside this radio callback.
  txSwitchActive = receiverData.payloadIsSwitchActive;
}

// Case 3: Calibration
static void handleCalibBeaconPacket() {
  // Calibration traffic breaks the telemetry sequence; force the next telemetry
  // packet to re-baseline (see telemetry case) instead of counting a huge gap.
  expectedPacketCounter = 0;

  if (systemState != STATE_CALIBRATING) {
    systemState = STATE_CALIBRATING;
    Serial.printf("\n[SCAN] Scan completed. Calibrating on Channel %u\n", receiverData.targetChannel);
  }

  // Force the channel to the correct on in case of adjacency interference.
  if (currentChannel != receiverData.targetChannel) {
    currentChannel = receiverData.targetChannel;
    updateReceiverChannel(currentChannel);
  }
  lastPacketReceivedTime = millis();
}

void onDataReceivedCallback(const uint8_t *senderMacAddress, const uint8_t *incomingData, int dataLength) {

  // Sender filter: only the paired transmitter may command this relay. Encryption
  // already stops others from forging *encrypted* frames, but plain unencrypted
  // ESP-NOW frames from any device still reach this callback -- drop them here.
  if (memcmp(senderMacAddress, transmitterMacAddress, 6) != 0) {
    statsPacketsRejected++;
    return;
  }

  // Verify data length matches struct to prevent memory corruption
  if (dataLength == sizeof(receiverData)) {
    memcpy(&receiverData, incomingData, sizeof(receiverData));

    // Replay guard (see lastAcceptedCounter declaration): once synced, every
    // packet must advance the transmitter's monotonic counter. A captured-and-
    // replayed frame (or a radio duplicate) carries an old counter -> rejected,
    // so it can neither hold the relay energized nor refresh the link watchdog.
    // In SEARCHING we have no trustworthy baseline (transmitter may have
    // rebooted and reset its counter), so we accept and re-baseline instead.
    if (systemState != STATE_SEARCHING && lastAcceptedCounter != 0 &&
        receiverData.payloadPacketCounter <= lastAcceptedCounter) {
      statsPacketsRejected++;
      return;
    }
    lastAcceptedCounter = receiverData.payloadPacketCounter;

    statsPacketsReceived++;

    // Case 1: Received calibration hierarchy table from transmitter
    if (receiverData.packetType == PACKET_TYPE_CALIB_TABLE) {
      handleCalibTablePacket();
    }

    // Case 2: Received Telemetry
    else if (receiverData.packetType == PACKET_TYPE_TELEMETRY) {
      handleTelemetryPacket();
    }
    // Case 3: Calibration
    else if (receiverData.packetType == PACKET_TYPE_CALIB_BEACON) {
      handleCalibBeaconPacket();
    }
  }
}

bool wirelessInit() {
  // Initialize WIFI in Station mode
  WiFi.mode(WIFI_STA);

  // Disable WiFi modem-sleep. The default power-save mode lets the radio doze
  // between beacon intervals and drop bursts of incoming ESP-NOW frames -- the
  // classic cause of intermittent packet loss / timeouts. Critical on the receiver,
  // which must listen continuously.
  esp_wifi_set_ps(WIFI_PS_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("[Fault] ESP-NOW initialization failed.");
    return false;
  }

  // Link authentication. Register the transmitter as an encrypted peer so its
  // CCMP frames decrypt; the callback additionally MAC-filters and replay-guards.
  bool txMacConfigured = false;
  for (uint8_t i = 0; i < 6; i++) {
    if (transmitterMacAddress[i] != 0x00) txMacConfigured = true;
  }
  if (txMacConfigured) {
    esp_now_set_pmk(espNowPmk);
    esp_now_peer_info_t transmitterPeerInfo = {};
    memcpy(transmitterPeerInfo.peer_addr, transmitterMacAddress, 6);
    transmitterPeerInfo.channel = 0; // 0 = follow whatever channel we are on (we hop while scanning)
    transmitterPeerInfo.encrypt = true;
    memcpy(transmitterPeerInfo.lmk, espNowLmk, 16);
    if (esp_now_add_peer(&transmitterPeerInfo) != ESP_OK) {
      Serial.println("[Fault] Failed to register transmitter as encrypted peer.");
    }
  } else {
    Serial.println("*************************************************************");
    Serial.println("[AUTH] transmitterMacAddress is NOT SET (all zeros).");
    Serial.println("[AUTH] All packets will be REJECTED and the relay stays off.");
    Serial.println("[AUTH] Read the MAC from the transmitter's boot log and set it");
    Serial.println("[AUTH] in main.cpp (transmitterMacAddress), then reflash.");
    Serial.println("*************************************************************");
  }

  // Bind receive callback
  esp_now_register_recv_cb(onDataReceivedCallback);

  // Attempt to load saved channel table from NVS
  preferences.begin(radioConfigNamespace, true);
  size_t storedLength = preferences.getBytesLength(channelTableStorageKey);
  bool tableLoaded = false;

  if (storedLength == channelTableSize) {
    preferences.getBytes(channelTableStorageKey, rankedChannelTable, channelTableSize);
    tableLoaded = true;
  }
  preferences.end();

  if (tableLoaded) {
    // Load last known best channel
    currentChannel = rankedChannelTable[0];
    updateReceiverChannel(currentChannel);
    systemState = STATE_WORKING;
    lastPacketReceivedTime = millis();
    Serial.printf("[STORAGE] Channel Table loaded. Working Channel: %u\n", currentChannel);
  } else {
    // Start searching the spectrum from channel 1
    currentChannel = 1;
    updateReceiverChannel(currentChannel);
    systemState = STATE_SEARCHING;
    lastChannelSwitchTime = millis();
    Serial.println("[STORAGE] Channel Table missing. Starting auto-scan...");
  }

  return true;
}
