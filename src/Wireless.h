#ifndef WIRELESS_H
#define WIRELESS_H

#include <Arduino.h>
#include <Preferences.h>
#include <Protocol.h>
#include "State.h"

// Link authentication.
// The ONLY transmitter this relay obeys. Read the address from the transmitter's
// boot log ("[BOOT] Transmitter MAC ...") or its CLI `status`, and fill it in here.
// While left at all-zeros, EVERY packet is rejected and the relay can never
// energize, so the link is down until this is set.
extern const uint8_t transmitterMacAddress[6];

// Wireless Data Structures
// PacketType and PayloadData come from the shared Protocol.h (../common), so the
// layout can't drift from the transmitter's.
extern PayloadData receiverData;

// Non Volatile Storage variables
extern Preferences preferences;
extern const char* radioConfigNamespace;
extern const char* channelTableStorageKey;
extern uint8_t rankedChannelTable[channelTableSize];

// States and timing variables.
extern volatile ReceiverState systemState;
extern volatile uint8_t currentChannel;
extern volatile unsigned long lastPacketReceivedTime;
extern const unsigned long TimeoutMs;
extern volatile bool bypassActive;
extern volatile bool txSwitchActive;
extern unsigned long lastChannelSwitchTime;
extern const unsigned long channelDwellTimeMs;

// Diagnostic Statistics.
extern volatile uint32_t statsPacketsReceived;
extern volatile uint32_t statsPacketsDropped;
extern volatile uint32_t statsScanSweeps;
extern volatile uint32_t expectedPacketCounter;
extern volatile bool lastSwitchState;
extern volatile uint32_t statsPacketsRejected;
extern volatile uint32_t lastAcceptedCounter;

// Set the receiver's RF channel (second channel intentionally left empty).
void updateReceiverChannel(uint8_t channel);

bool wirelessInit();

#endif // WIRELESS_H
