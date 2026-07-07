# iHeater Link — quick guide

iHeater Link is a connectivity module for the **iHeater** controller with firmware [iheater_revХ_Х_pulse](https://github.com/pavluchenkor/iHeater-Standalone-Firmware/releases). It is an ESP32-C3 / ESP32-S3 board that:

- Connects to Wi-Fi and links iHeater to [portal.idryer.org](https://portal.idryer.org/).
- Receives chamber target temperature from the printer through
  **Moonraker (Klipper)**, **Bambu Lab (LAN)**, or **Home Assistant**
  integrations.
- Converts the target into a pulse signal and forwards it to the iHeater
  controller over a single GPIO.

iHeater is controlled "over the wire": one signal pin on the ESP feeds
the iHeater signal input. Wi-Fi and integrations are Link's job; heating
and safety are iHeater's job.

The single-wire link does not constrain where Link is placed. The ESP
board can be installed outside the heat chamber. This avoids:

- chip and peripheral overheating when the chamber runs at 60+ °C;
- thermal lock-ups of the radio section and Wi-Fi session drops during
  long heating cycles;
- accelerated component degradation.

Only the iHeater controller — designed for high-temperature operation —
stays inside the chamber. Signal-wire length to the ESP is limited only
by reasonable line load (tens of centimetres — no concerns).

## Supported boards

| Board                    |       |
| ------------------------ | ----- |
| ESP32-C3 Super Mini      | `✅`  |
| ESP32-C3 DevKitM-1       | `✅`  |
| Seeed XIAO ESP32-S3      | `✅`  |
| Waveshare ESP32-S3-Zero  | `✅`  |

Any other ESP32-C3 or ESP32-S3 board can be used as long as a free GPIO
is available for the signal output. Check the manufacturer's pinout.

## Wiring

!!! warning "Never connect or disconnect wires while power is applied."

Power goes to the ESP through USB-C. The ESP, in turn, powers the
iHeater controller over the 5 V line. This is the simplest setup. If
needed, iHeater power can be sourced separately — Link does not depend
on the power scheme.

![ESP32-C3 Super Mini to iHeater wiring](../../img/iHeaterLink.png)

Connections (same for all supported boards):

| ESP        | iHeater          | Purpose             |
| ---------- | ---------------- | ------------------- |
| `5V`       | `5V`             | controller power    |
| `GND`      | `GND`            | common ground       |
| `GPIO3`    | signal input     | pulse setpoint      |

### Board pinouts

ESP32-C3 Super Mini:

![ESP32-C3 Super Mini pinout](../../img/ESP32-C3-Super-Mini-pinout-low.jpg)

Waveshare ESP32-S3-Zero:

![Waveshare ESP32-S3-Zero pinout](../../img/ESP32-C3-ZERO-Waveshare-pinout-low.jpg)

## Flashing via the web flasher

The web flasher is hosted at [install.idryer.org](https://install.idryer.org/).

1. Connect Link to a USB port on the computer.
2. Open [install.idryer.org](https://install.idryer.org/) and select
   the **iHeater Link** device.
3. Select the board variant.
4. Click **Connect** and pick the serial port (typically `USB JTAG/serial`
   or `CH340`). If the device is not detected, hold `BOOT` on the board
   and briefly press `RST`.
5. Click **Install**. The flasher writes the firmware.
6. When flashing completes, the Wi-Fi setup wizard opens.

## Local standalone respin

The ESP32-C3 Super Mini can also be built as local-only firmware:

```bash
pio run -e esp32c3-super-mini-standalone
```

This build does not start the iDryer cloud, portal, MQTT, OTA, Bambu,
Moonraker, or Home Assistant runtime. It only starts the RMT output,
the BOOT button handler, the blue status LED, Wi-Fi, and a small local
web interface.

Network behavior:

- If `include/secrets.h` defines `WIFI_SSID` and `WIFI_PASSWORD`, the
  device joins that 2.4 GHz LAN and prints its local URL on serial.
- If credentials are not present or connection times out, it creates an
  open access point named `iHeater-Standalone-XXXX` and serves the UI at
  `http://192.168.4.1/`.

Standalone mode table:

| Mode          | Target |
| ------------- | ------ |
| `MODE_TEMP_0` | Off    |
| `MODE_TEMP_1` | 55 C   |
| `MODE_TEMP_2` | 60 C   |
| `MODE_TEMP_3` | 65 C   |
| `MODE_TEMP_4` | 70 C   |
| `MODE_TEMP_5` | 75 C   |
| `MODE_TEMP_6` | 80 C   |
| `MODE_TEMP_7` | 85 C   |

BOOT button behavior:

- 1 to 7 quick presses selects `MODE_TEMP_1` through `MODE_TEMP_7`.
  For example, two quick presses selects `MODE_TEMP_2` / 60 C.
- Holding BOOT for 2 seconds selects `MODE_TEMP_0` / Off.
- The ESP32-C3 Super Mini blue LED flashes the current mode number in
  quick bursts, then pauses. Off leaves the LED off.

## Wi-Fi setup

After flashing, the Improv wizard opens automatically over the serial
port.

1. Enter the SSID and password of your 2.4 GHz network.
2. Wait for the **Connected** status. The Link status LED switches to a
   slow blue "breathing" pattern.

If the wizard does not open, unplug USB and reconnect via **Connect**
without re-flashing.

!!! note "ESP32 supports 2.4 GHz only. 5 GHz networks will not work."

## Pairing with the portal

1. On the flasher page click **Connect and Claim**.
2. The device receives the `START_CLAIM` command. After a few seconds
   a PIN is shown on the page. The PIN is valid for ~5 minutes.
3. Open [portal.idryer.org](https://portal.idryer.org/) → **Add device**
   → enter the PIN.
4. Once paired, the device appears online in the list.

If the response is `CLAIM_ALREADY:DEVICE_…`, the device is already
paired to this or another account. Remove the device from the portal,
then repeat the pairing.

## Connecting to iHeater

1. Power the controller off.
2. Wire ESP to iHeater per the diagram above: `5V`, `GND`, `GPIO3` →
   iHeater signal input.
3. Apply power to the ESP via USB. The controller is powered through
   the 5 V line.

On boot, Link connects to the portal, activates the selected
integration, and starts forwarding the chamber target to iHeater.

## What you should see

- LED 1 stays on, LED 3 blinks once per second — Link-to-iHeater
  communication is established.
- When the link is lost, all LEDs blink at 1 Hz.
- All other errors are unrelated to Link and follow the standalone
  firmware indication.

## Diagnostics

The device menu has a **DIAGNOSTICS → DIAG LOG** entry. When enabled,
the serial port outputs a detailed report once per second: Wi-Fi
status, MQTT status, active integration, current target, connector
errors.

Full diagnostics reference is available in the idryer-core library documentation in the project repository on GitHub.
