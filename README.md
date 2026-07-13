# DeadManSwitchReceiver: Receiver

Receiver half of a proof-of-concept wireless Dead Man's Switch. It listens for ESP-NOW telemetry from the handheld transmitter (`../DeadManSwitch`) and drives a relay that allows movement. The relay is energized only while the link between the Transmitter and Receiver is alive *and* the transmitter reports its switch held. Anything else from release, radio loss, transmitter death, this board rebooting or hanging, and it de-energizes the relay and the movement stops.

## Hardware

- **Board**: DFRobot FireBeetle ESP32 (`firebeetle32`)
- **DFRobot motor shield** (I2C `0x18`): channel **M1** at 100% PWM acts as the switch for the **12 V relay coil**. De-energized coil = movement disabled. A missing shield is detected at boot and the relay turns off.
- **Bypass button** between GPIO 27 and GND. See below.

| GPIO | Function |
|---|---|
| 27 | Bypass button (INPUT_PULLUP, active-LOW) |
| 21/22 | I2C to motor shield (relay coil driver) |

## Source layout

| File | Contents |
|---|---|
| `src/main.cpp` | `setup()` + `loop()` only: boot order, bypass handling, 500 ms link timeout, channel scanning, relay state switching |
| `src/Relay.cpp/.h` | Relay shield driver, `relayInit()`/`relaySetActive()`, bypass button debounce |
| `src/Wireless.cpp/.h` | Paired transmitter MAC, ESP-NOW init + encrypted peer, receive callback (auth guards + per-packet-type handlers), channel-table NVS storage, stats |
| `src/State.h` | `ReceiverState` enum (SEARCHING / WORKING / CALIBRATING) |
| `src/Cli.cpp/.h` | Serial CLI |
| `../common/DeadManProtocol/Protocol.h` | **Shared with the transmitter**: payload struct, packet types, encryption keys, channel count. If you Edit, reflash BOTH devices. |

## Build & flash

```
platformio run                    # build
platformio run -t upload          # flash
platformio device monitor         # serial console @ 115200
```

## Pairing (required once per transmitter board)

This receiver only obeys **one** transmitter, identified by MAC and encrypted with keys from `Protocol.h`:

1. Flash the transmitter; read `[BOOT] Transmitter MAC (for receiver pairing): ...` from its serial log.
2. Set `transmitterMacAddress` in `src/Wireless.cpp` to that address.
3. Flash this receiver.

While the MAC is unset (all zeros) the receiver rejects every packet and prints a `[AUTH]` warning at boot and the relay can never energize.

MAC also visible in the 'status' command through serial comms.

## Safety mechanisms

- No accepted packet for 500 ms and the relay is turned off, resume channel scan.
- If `loop()` ever hangs, the chip resets, and boots with the relay de-energized. Armed at the end of `setup()`, fed at the top of `loop()` Shouldn't change that ordering ordering.
- In the receive callback, in order: sender-MAC filter > frame-length check > replay guard. Rejections are counted in `status`.
- Only the last line of `loop()` and the timeout/bypass paths call `relaySetActive()`.

## Development bypass (GPIO 27)

Turning on the button forces the relay ACTIVE and suspends link checks, so the robot can be worked on without someone holding the transmitter. **It intentionally defeats the dead-man's switch** and exists for bench/debug work.

## CLI (115200 baud)

| Command | Action |
|---|---|
| `status` | State, RF channel, relay state, bypass, packet stats (incl. auth rejections), channel rankings |
| `reset` / `deletetable` | Clear the calibrated channel table from NVS and resume scanning |
| `reboot` | Restart the board |

