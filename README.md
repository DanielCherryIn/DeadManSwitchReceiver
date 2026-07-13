# DeadManSwitchReceiver — Receiver / Relay Controller

Receiver half of a proof-of-concept **wheelchair dead-man's switch**. It listens for ESP-NOW telemetry from the handheld transmitter (`../DeadManSwitch`) and drives a relay in the chair's enable path. **Fail-safe by construction**: the relay is energized only while the link is alive *and* the transmitter reports its switch held. Anything else — release, radio loss, transmitter death, this board rebooting or hanging — de-energizes the relay and the chair stops.

## Hardware

- **Board**: DFRobot FireBeetle ESP32 (`firebeetle32`)
- **DFRobot motor shield** (I2C `0x18`): channel **M1** at 100% PWM acts as the switch for the **12 V relay coil**. De-energized coil = chair disabled. A missing shield is detected at boot and the relay becomes a no-op (fail-safe).
- **Development bypass button** between GPIO 27 and GND — see below.

| GPIO | Function |
|---|---|
| 27 | Dev bypass button (INPUT_PULLUP, active-LOW) |
| 21/22 | I2C to motor shield (relay coil driver) |

## Source layout

| File | Contents |
|---|---|
| `src/main.cpp` | `setup()` + `loop()` only: boot order, bypass handling, 500 ms link timeout, channel scanning, the single relay decision |
| `src/Relay.cpp/.h` | Relay shield driver, `relayInit()`/`relaySetActive()`, bypass button debounce |
| `src/Wireless.cpp/.h` | Paired transmitter MAC, ESP-NOW init + encrypted peer, receive callback (auth guards + per-packet-type handlers), channel-table NVS storage, stats |
| `src/State.h` | `ReceiverState` enum (SEARCHING / WORKING / CALIBRATING) |
| `src/Cli.cpp/.h` | Serial CLI |
| `../common/DeadManProtocol/Protocol.h` | **Shared with the transmitter**: payload struct, packet types, encryption keys, channel count. Edit ⇒ reflash BOTH devices. |

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

While the MAC is unset (all zeros) the receiver rejects every packet and prints a loud `[AUTH]` warning at boot — the relay can never energize.

## Safety mechanisms (know these before modifying)

- **500 ms link watchdog**: no accepted packet for 500 ms ⇒ relay off, resume channel scan.
- **Task watchdog (3 s)**: if `loop()` ever hangs, the chip panics, resets, and boots with the relay de-energized. Armed at the end of `setup()`, fed at the top of `loop()` — keep that ordering.
- **Auth guards** in the receive callback, in order: sender-MAC filter → frame-length check → replay guard (monotonic packet counter; re-baselines only from SEARCHING). Rejections are counted in `status`.
- **Single relay owner**: only the last line of `loop()` and the timeout/bypass paths call `relaySetActive()`. The radio callback records intent only — never add blocking I2C to it.

## Development bypass (GPIO 27)

Holding the hidden chassis button forces the relay ACTIVE and suspends link checks, so the robot can be worked on without someone holding the transmitter. **It intentionally defeats the dead-man's switch** — it exists for bench/debug work by the other teams, and must never be reachable by the seated user.

## CLI (115200 baud)

| Command | Action |
|---|---|
| `status` | State, RF channel, relay state, bypass, packet stats (incl. auth rejections), channel rankings |
| `reset` / `deletetable` | Clear the calibrated channel table from NVS and resume scanning |
| `reboot` | Restart the board |

Full architecture, timing contract, and safety review: see `../DeadManSwitch Technical Note.md`.
