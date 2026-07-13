#include <Arduino.h>
#include <esp_task_wdt.h>
#include <Wire.h>
#include <Protocol.h> // Shared contract: PayloadData, PacketType, keys (../common)
#include "State.h"
#include "Relay.h"
#include "Wireless.h"
#include "Cli.h"

// Hang protection: if loop() stalls for this long, the task watchdog panics and
// hard-resets the chip, and relayInit() then boots the relay de-energized. Without
// this, a hung MCU leaves the motor shield latched on its last (possibly energized)
// command forever. Must comfortably exceed the longest legitimate loop stall
// (blocking CLI prints ~60ms, NVS writes a few ms).
const uint32_t loopWatchdogTimeoutSeconds = 3;

// SYSTEM INITIALIZATION

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n[BOOT] Booting. . .");

  // I2C bus for the relay motor shield, then bring the relay up de-energized.
  // (relayInit() can block ~2s inside the DFRobot driver when the shield is present.)
  Wire.begin();
  Wire.setClock(100000);
  Wire.setTimeOut(50);
  relayInit();

  // Development bypass button: internal pull-up, the button pulls the pin to GND.
  pinMode(pinBypassButtonInput, INPUT_PULLUP);

  // Bring up WiFi/ESP-NOW, register the encrypted peer + recv callback, and load the
  // saved channel table. Bail on ESP-NOW init failure exactly as the monolith did
  // (watchdog stays unarmed and the CLI prompt is not printed).
  if (!wirelessInit()) {
    return;
  }

  // Hang protection, armed LAST so the blocking init work above (DFRobot ~2s)
  // can't trip it. If loop() ever stalls past the timeout, the chip panics and
  // resets, and relayInit() brings the relay back up de-energized. This bounds a
  // hung-MCU stuck-relay at roughly timeout + boot time instead of "forever".
  esp_task_wdt_init(loopWatchdogTimeoutSeconds, true);
  esp_task_wdt_add(NULL); // watch this task (the Arduino loopTask)

  // Print CLI command prompt
  Serial.print("\n> ");
}

// SYSTEM MAIN RUNTIME LOOP

void loop() {
  // Feed the hang-protection watchdog first thing, so it also covers the early
  // return taken while the development bypass is engaged.
  esp_task_wdt_reset();

  uint32_t now = millis();

  // Handle serial CLI inputs and actions asynchronously
  handleSerialCLI();

  // Development bypass: a local button on the receiver that forces the relay
  // ACTIVE regardless of link state or the transmitter's dead-man's switch. Lets
  // us bench-test the relay without holding the transmitter switch. NOT a
  // production safety path -- it intentionally defeats the dead-man's switch.
  bool bypassPressed = getDebouncedBypassState();
  if (bypassPressed != bypassActive) {
    bypassActive = bypassPressed;
    if (bypassActive) {
      Serial.println("\n[BYPASS] Engaged. Relay forced ACTIVE, link checks suspended.");
    } else {
      // On release we just fall through; the actuation at the end of loop() returns
      // the relay to normal dead-man control (de-energized unless linked + switched).
      Serial.println("\n[BYPASS] Released. Relay returned to normal dead-man control.");
    }
    Serial.print("> ");
  }

  if (bypassActive) {
    relaySetActive(true);
    return; // While bypassed, skip the link timeout and channel scanning entirely.
  }

  if (systemState == STATE_WORKING || systemState == STATE_CALIBRATING) {
    // Continuous Timeout Check for Active Connection.
    // Recompute time here -- a slow CLI print earlier in the loop can leave the
    // top-of-loop 'now' tens of ms stale -- and snapshot the volatile timestamp,
    // which the RX callback updates from another task. Use a SIGNED difference:
    // if a packet arrives mid-loop, lastRx can be a hair ahead of our time sample,
    // and an unsigned (now - lastRx) would underflow to ~4.29e9 and fire a *false*
    // timeout (this was the real "disconnect" storm). Signed diff reads that as a
    // small negative instead, and stays correct across the millis() rollover.
    uint32_t nowMs = millis();
    uint32_t lastRx = lastPacketReceivedTime;
    int32_t sinceRxMs = (int32_t)(nowMs - lastRx);
    if (sinceRxMs > (int32_t)TimeoutMs) {
      // Timeout: safety fault triggered. Drop the relay and forget stale switch intent.
      relaySetActive(false);
      txSwitchActive = false;
      systemState = STATE_SEARCHING;
      expectedPacketCounter = 0; // Reset tracking
      lastChannelSwitchTime = nowMs;
      Serial.printf("\n[Timeout] Connection lost after %dms gap. Relay de-energized. Resuming channel scan...\n", sinceRxMs);
      Serial.print("> ");
    }
  } else {
    //  Channel Sweep Scanning Loop
    if (now - lastChannelSwitchTime >= channelDwellTimeMs) {
      lastChannelSwitchTime = now;

      // Step to next channel (1 through 13)
      currentChannel = (currentChannel % 13) + 1;
      updateReceiverChannel(currentChannel);

      // Track sweeps
      if (currentChannel == 1) {
        statsScanSweeps++;
      }
    }
  }

  // Single owner of the relay (edge-triggered inside relaySetActive). Energize only
  // while connected AND the transmitter's dead-man switch is held; any other state
  // (searching, calibrating, timed out) de-energizes -- fail-safe by default.
  relaySetActive(systemState == STATE_WORKING && txSwitchActive);
}
