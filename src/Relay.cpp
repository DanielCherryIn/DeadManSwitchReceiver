#include "Relay.h"
#include <Wire.h>
#include <DFRobot_MotorStepper.h>

// comment next line out when you don't need to debug
//#define DEBUG_RELAY 1

// Pins and Hardware config.

// Relay output, driven for real through a DFRobot motor shield used as a makeshift
// 12V switch: the channel runs at 100% PWM to energize the relay coil from a battery.
// Defaults: channel M1, I2C address 0x18 (the shield's A0 default). Change these if
// your relay coil is wired to a different motor terminal or the shield is re-addressed.
static constexpr uint8_t  RELAY_SHIELD_CHANNEL = M1;
static constexpr uint8_t  RELAY_SHIELD_ADDR    = 0x18;   // A0 == 0x30>>1
static constexpr uint16_t RELAY_PWM_FULL       = 4096;   // 100% duty (library: higher = harder)
DFRobot_Motor relayDriver(RELAY_SHIELD_CHANNEL, RELAY_SHIELD_ADDR);

// Relay bookkeeping: detect the shield once at boot, and remember the last commanded
// state so we only hit the (slow, blocking) I2C bus on real changes and the CLI can
// report state without touching hardware.
bool relayHardwarePresent = false;
bool relayActiveState = false;

// Development bypass button.
// Wired between this pin and GND, read as INPUT_PULLUP (active-LOW: pressed = LOW).
// Holding it engages a bypass that forces the relay ACTIVE regardless of the
// dead-man's-switch / link state, so the receiver and relay can be bench-tested
// without holding the transmitter's switch. FOR DEVELOPMENT ONLY 
// Change this constant to relocate the button.
extern const uint8_t pinBypassButtonInput = 27;

// Debounced read of the development bypass button (active-LOW via INPUT_PULLUP).
// Returns true while the button is held down. Mirrors the transmitter's debounce
// so a noisy contact can't chatter the relay on press/release.
bool getDebouncedBypassState() {
  static bool stableState = false;     // debounced result: true = pressed
  static bool lastReading = false;
  static unsigned long lastDebounceTime = 0;
  const uint8_t debounceDelayMs = 40;

  bool currentReading = (digitalRead(pinBypassButtonInput) == LOW); // pressed pulls to GND

  if (currentReading != lastReading) {
    lastDebounceTime = millis();
  }
  if ((millis() - lastDebounceTime) > debounceDelayMs) {
    stableState = currentReading;
  }
  lastReading = currentReading;
  return stableState;
}

// Initialize the relay motor shield. Probes the I2C address first because the DFRobot
// init() blocks (~2s) and then spins forever if the shield never answers -- a missing
// or dead shield must not hang boot. If absent, the relay simply stays a no-op, which
// is fail-safe (a dead-man relay defaults de-energized anyway).
void relayInit() {
  Wire.beginTransmission(RELAY_SHIELD_ADDR);
  if (Wire.endTransmission() != 0) {
    relayHardwarePresent = false;
    #ifdef DEBUG_RELAY
      Serial.printf("[RELAY] Motor shield NOT found at 0x%02X. Relay output disabled.\n", RELAY_SHIELD_ADDR);
    #endif
    return;
  }
  relayDriver.init();
  relayDriver.speed(RELAY_PWM_FULL); // set duty once; start()/stop() gate it thereafter
  relayDriver.stop();                // boot de-energized
  relayHardwarePresent = true;
  relayActiveState = false;
  #ifdef DEBUG_RELAY
    Serial.printf("[RELAY] Motor shield found at 0x%02X. Relay ready at 100%% drive.\n", RELAY_SHIELD_ADDR);
  #endif
}

// Energize / de-energize the relay. Edge-triggered: only touches the I2C bus when the
// commanded state actually changes. Call only from the main task (loop()/CLI), never
// from the radio callback. Flip CW->CCW below if your relay's flyback diode needs it.
void relaySetActive(bool active) {
  if (!relayHardwarePresent || active == relayActiveState) {
    return;
  }
  relayActiveState = active;
  if (active) {
    relayDriver.speed(RELAY_PWM_FULL);
    relayDriver.start(CW);
  } else {
    relayDriver.stop();
  }
}
