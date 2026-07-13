#ifndef RELAY_H
#define RELAY_H

#include <Arduino.h>

// Relay bookkeeping: detect the shield once at boot, and remember the last commanded
// state so we only hit the (slow, blocking) I2C bus on real changes and the CLI can
// report state without touching hardware.
extern bool relayHardwarePresent;
extern bool relayActiveState;

// Development bypass button pin (see Relay.cpp for the full rationale/warnings).
extern const uint8_t pinBypassButtonInput;

// Initialize the relay motor shield (probes I2C first; fail-safe if absent).
void relayInit();

// Energize / de-energize the relay. Edge-triggered on the commanded state.
void relaySetActive(bool active);

// Debounced read of the development bypass button (true while held).
bool getDebouncedBypassState();

#endif // RELAY_H
