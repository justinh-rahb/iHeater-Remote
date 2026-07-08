# iHeater-Remote

iHeater-Remote is a local-only ESP32-C3 companion firmware for the
iHeater controller. It drives the iHeater pulse input over one GPIO,
exposes a small local web UI, and recreates the standalone iHeater mode
button behavior on an ESP32-C3 Super Mini.

This is a downstream fork of iHeater Link. It intentionally removes the
cloud/portal workflow from the active firmware build while keeping the
upstream source layout close enough to make future syncs practical.

## Credits

This project is based on:

- [pavluchenkor/iHeater-Link](https://github.com/pavluchenkor/iHeater-Link)

The iHeater controller firmware and safety behavior remain the
controller's responsibility. iHeater-Remote only sends the setpoint pulse
train.

## What It Does

- Runs without iDryer cloud, portal MQTT, OTA, Bambu, Moonraker, or Home
  Assistant integrations.
- Serves a local web UI for selecting the heater mode.
- Creates a captive-portal access point if it cannot join configured
  Wi-Fi.
- Reuses the ESP32-C3 Super Mini BOOT button for standalone-style mode
  selection.
- Uses the ESP32-C3 Super Mini blue LED to indicate the selected mode by
  flashing the mode number in quick bursts.
- Keeps the upstream iHeater Link code present behind its original build
  environments so upstream changes can still be pulled and reviewed.

## Hardware

Tested target:

| Board | Status |
| --- | --- |
| ESP32-C3 Super Mini | Supported |

The firmware assumes:

| ESP32-C3 Super Mini | iHeater | Purpose |
| --- | --- | --- |
| `5V` | `5V` | controller power |
| `GND` | `GND` | common ground |
| `GPIO3` | signal input | pulse setpoint |

The default BOOT button input is `GPIO9`, and the blue status LED is
`GPIO8` active-low.

## Build

The downstream build target is:

```bash
pio run -e esp32c3-super-mini-remote
```

To flash a connected ESP32-C3 Super Mini:

```bash
pio run -e esp32c3-super-mini-remote -t upload --upload-port /dev/tty.usbmodem101
```

Replace the upload port with the port for your board.

## Wi-Fi

For LAN mode, create `include/secrets.h`:

```cpp
#pragma once

#define WIFI_SSID "Your 2.4 GHz SSID"
#define WIFI_PASSWORD "Your password"
```

`include/secrets.h` is intentionally ignored by git.

If the configured Wi-Fi is unavailable, iHeater-Remote starts an open
access point:

```text
iHeater-Remote-XXXX
```

The web UI is served at:

```text
http://192.168.4.1/
```

Captive portal DNS is enabled in AP mode, so most phones and laptops
should offer to open the UI automatically.

## Controls

The mode table follows the standalone iHeater firmware behavior:

| Mode | Target |
| --- | --- |
| `MODE_TEMP_0` | Off |
| `MODE_TEMP_1` | 55 C |
| `MODE_TEMP_2` | 60 C |
| `MODE_TEMP_3` | 65 C |
| `MODE_TEMP_4` | 70 C |
| `MODE_TEMP_5` | 75 C |
| `MODE_TEMP_6` | 80 C |
| `MODE_TEMP_7` | 85 C |

BOOT button:

- 1 to 7 quick presses selects `MODE_TEMP_1` through `MODE_TEMP_7`.
- Two quick presses selects `MODE_TEMP_2` / 60 C.
- Holding BOOT for about 2 seconds selects `MODE_TEMP_0` / Off.

Web UI:

- Open the device IP, or `http://192.168.4.1/` in AP mode.
- Tap a mode button or Off.
- The UI polls status and shows mode, target, network mode, and pulse
  code.

LED:

- Off mode leaves the blue LED off.
- Active modes flash the mode number, pause, then repeat.

## Keeping Sync With Upstream

This fork keeps the upstream project shape mostly intact:

- Original integration firmware remains in `src/main.cpp` behind the
  normal upstream build environments.
- iHeater-Remote is selected by the `IHEATER_REMOTE_LOCAL` build flag.
- The local-only implementation lives under `src/standalone/`.
- The downstream PlatformIO environment is isolated as
  `esp32c3-super-mini-remote`.

Suggested sync workflow:

```bash
git remote add upstream https://github.com/pavluchenkor/iHeater-Link.git
git fetch upstream
git merge upstream/main
```

Expect most downstream conflicts to be limited to `README.md`,
`platformio.ini`, `src/main.cpp`, and `src/standalone/`.

## Notes

- Do not use `erase_flash` if you want to preserve NVS data from another
  firmware build.
- ESP32 Wi-Fi is 2.4 GHz only.
- The iHeater controller should remain responsible for heater safety and
  fault handling.
