#include "Cli.h"
#include <Preferences.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include <Protocol.h> // Shared contract: channelTableSize (../common)

// ==========================================
// EXTERNAL REFERENCES TO RECEIVER STATE
// ==========================================
// Receiver state, timing, NVS and diagnostics globals are declared in Wireless.h;
// the relay state + actuator and the bypass pin are declared in Relay.h.
#include "Relay.h"
#include "Wireless.h"

// ==========================================
// INTERACTIVE COMMAND LINE INTERFACE (SHELL)
// ==========================================

void handleSerialCLI() {
  static String inputBuffer = "";

  while (Serial.available() > 0) {
    char c = Serial.read();

    // 1. Handle Newline (Enter pressed)
    if (c == '\r' || c == '\n') {
      Serial.println();

      String input = inputBuffer;
      input.trim();
      inputBuffer = ""; // Flush buffer for next command

      if (input.length() == 0) {
        Serial.print("> ");
        continue;
      }

      // Execute matched commands
      if (input.equalsIgnoreCase("help")) {
        Serial.println("=================================================");
        Serial.println("            DEADMAN RECEIVER CLI HELP            ");
        Serial.println("=================================================");
        Serial.println("  status               - View system state, rankings & statistics");
        Serial.println("  reset / deletetable  - Clear calibration table & resume scanning");
        Serial.println("  reboot               - Restart the receiver microcontroller");
        Serial.println("=================================================");
      } else if (input.equalsIgnoreCase("status")) {
        Serial.println("------------- RECEIVER CONFIGURATION -------------");
        const char* stateStr = "SEARCHING";
        if (systemState == STATE_WORKING) stateStr = "WORKING";
        else if (systemState == STATE_CALIBRATING) stateStr = "CALIBRATING";

        Serial.printf("System State:         %s\n", stateStr);
        Serial.printf("Current RF Channel:   %u\n", currentChannel);
        Serial.printf("Relay Output State:   %s%s\n",
                      relayActiveState ? "ACTIVE (energized)" : "INACTIVE (open)",
                      relayHardwarePresent ? "" : " [motor shield not detected]");
        Serial.printf("Dev Bypass:           %s (Pin %u, hold to GND)\n",
                      bypassActive ? "ENGAGED" : "off",
                      pinBypassButtonInput);

        Serial.println("---------------- SYSTEM METRICS ----------------");
        Serial.printf("Total Packets Recv:   %u\n", statsPacketsReceived);
        Serial.printf("Total Packets Dropped:%u\n", statsPacketsDropped);
        Serial.printf("Packets Rejected:     %u (auth: wrong sender or replay)\n", statsPacketsRejected);
        if (systemState == STATE_WORKING || systemState == STATE_CALIBRATING) {
          Serial.printf("Last Packet Age:      %lums\n", millis() - lastPacketReceivedTime);
        } else {
          Serial.printf("Scanning Dwell Timer: %lums\n", millis() - lastChannelSwitchTime);
        }

        Serial.println("------------- CHANNEL RANKINGS -------------");
        Preferences preferences;
        preferences.begin(radioConfigNamespace, true);
        size_t storedLength = preferences.getBytesLength(channelTableStorageKey);
        if (storedLength == channelTableSize) {
          uint8_t tempTable[13];
          preferences.getBytes(channelTableStorageKey, tempTable, channelTableSize);
          for (int i = 0; i < 13; i++) {
            Serial.printf("  Rank %2d: Channel %2u\n", i + 1, tempTable[i]);
          }
        } else {
          Serial.println("  [NVS] Channel calibration table is missing/empty.");
        }
        preferences.end();
        Serial.println("------------------------------------------------");
      } else if (input.equalsIgnoreCase("reset")) {
        Preferences preferences;
        preferences.begin(radioConfigNamespace, false);
        preferences.remove(channelTableStorageKey);
        preferences.end();
        
        // Reset state and resume scanning
        systemState = STATE_SEARCHING;
        expectedPacketCounter = 0;
        relaySetActive(false);
        currentChannel = 1;
        esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
        lastChannelSwitchTime = millis();
        
        Serial.println("Success: Calibration table deleted from NVS. Resuming channel scan...");
      } else if (input.equalsIgnoreCase("reboot")) {
        Serial.println("System Rebooting...");
        delay(500);
        esp_restart();
      } else {
        Serial.println("Error: Command '" + input + "' not recognized. Enter 'help' for command list.");
      }

      // Print prompt for next command
      Serial.print("> ");
    }
    // 2. Handle Backspace (ASCII 8 (BS) or 127 (DEL))
    else if (c == '\b' || c == 127) {
      if (inputBuffer.length() > 0) {
        inputBuffer.remove(inputBuffer.length() - 1);
        Serial.print("\b \b");
      }
    }
    // 3. Handle Printable Characters (ASCII 32 to 126)
    else if (c >= 32 && c <= 126) {
      inputBuffer += c;
      Serial.write(c); // Echo character back to user terminal
    }
  }
}
