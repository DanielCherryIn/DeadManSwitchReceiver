#ifndef DEADMAN_PROTOCOL_H
#define DEADMAN_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

// This file tracks everything that must match between
// the DeadManSwitch and DeadManSwitchReceiver project files.
// You can pull this file through `lib_extra_dirs = ../common` in the platformio.ini file
// so the layout, packet types, and encryption keys cannot be different
// Editing this file affects both projects, so both of them need to be
// Reflashed if you do changes, don't forget that.

// Number of 2.4 GHz Wi-Fi channels probed and ranked (channels 1..13)
constexpr size_t channelTableSize = 13;

// ESP-NOW Packet Types
enum PacketType {
  PACKET_TYPE_TELEMETRY = 0,    // Regular dead-man switch state updates
  PACKET_TYPE_CALIB_TABLE = 1,  // Ranked channel table broadcast after a sweep
  PACKET_TYPE_CALIB_BEACON = 2  // Handshake/burst packets during calibration
};

// ESP-NOW Payload Structure.
// The transmitter's payloadPacketCounter is global
struct PayloadData {
  uint32_t payloadPacketCounter;
  uint8_t packetType;            // PACKET_TYPE_* value
  bool payloadIsSwitchActive;    // Dead-man switch state (true = held/ACTIVE)
  uint8_t targetChannel;         // Explicit channel routing for calibration
  uint8_t rankedChannels[channelTableSize]; // Full table (CALIB_TABLE only)
};

// The receiver rejects any frame whose length differs from sizeof(PayloadData),
// which fails safe (relay off) but kills the link entirely. This assert makes
// an accidental layout/ABI change visible if something is different.
static_assert(sizeof(PayloadData) == 20, "PayloadData layout changed -- reflash BOTH devices and revisit the receiver's length check");

// ESP-NOW encryption keys (using hardware CCMP), 16 bytes each. The transmitter sets
// the PMK and encrypts toward the receiver with the LMK. The receiver registers
// the transmitter as an encrypted peer with the same pair. Change these values
// if using them in a different pair of Transmitter Receiver
// For obvious reasons. Anyone with the defaults can talk to the relay.
const uint8_t espNowPmk[16] = {'D','M','S','W','-','P','M','K','-','v','1','-','2','0','2','6'};
const uint8_t espNowLmk[16] = {'D','M','S','W','-','L','M','K','-','v','1','-','2','0','2','6'};

#endif // DEADMAN_PROTOCOL_H