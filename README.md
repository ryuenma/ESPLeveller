# ESP Leveller

A real-time steadiness game built on an **ESP32-C3** and an accelerometer sensor pod. Hold the device level; the moment it tilts past your threshold, the timer stops and you lose. Your phone is the controller *and* the display, over a self-hosted web interface with no app and no internet connection.

## Features

- **Real-time web UI**: timer, live tilt bar, and threshold control (slider and ± buttons), pushed over **Server-Sent Events at 10 Hz**
- **Random countdown** of 2 to 5 s, so you cannot pre-empt the start
- **Audio cues** generated on the phone itself (WebAudio): start beep, proximity ticking as you approach the limit, and a fail alarm
- **Threshold from 5 to 90°**, anywhere from "steady surgeon" to "anything goes"
- **Calibration**: whatever orientation you are holding becomes level, saved to flash
- **Settings persist** in NVS (threshold and calibration base), so they survive power cycles
- **Auto-detecting sensor pod**: detects a GY-521 (MPU6050) *or* a BMI160 breakout (e.g. GY-BMI160) at boot and picks the right driver. Marketplace "GY-LSM6DS3" boards often actually carry a BMI160, since the chips are pin-compatible; the firmware identifies the real chip by its ID register, so the label does not matter. See [Tested and proven](#tested-and-proven) before choosing a pod
- **Spectral spectator mode**: a second phone or laptop on the same network sees the round live

## Tested and proven

Honest status per path, because the two sensor paths are not equally proven and the README used to imply they were.

| Path | Status |
|---|---|
| **GY-521 (MPU6050)** | **Field-tested.** This is the path that ran on real devices, and it is where the drift bug was found and fixed. |
| **GY-BMI160** | **Written and committed, never run on hardware.** Do not trust it yet. |

Details:

- **The GY-521 path is proven in the field.** Commit `8781fb7` ("QC round 1", 2026-09-06) fixed a
  threshold bypass that turned out to be gyro bias accumulating over hours, which the commit
  records as *field-confirmed*. Tilt is now computed with `atan2` against gravity from the
  accelerometer, so it is absolute and does not drift.
- **The BMI160 path has never been on a real BMI160.** It landed in `823e704` on 2026-09-08,
  two days *after* the last field QC, and nothing since has put it on a sensor. The driver is
  written from the datasheet and reads plausibly, but "written" and "verified" are different
  claims and only the first one is true here. The soft reset and its dummy-read quirk, the
  100 Hz and ±2 g configuration, and the `CHIP_ID` detection are all unexercised.
- **No firmware release has been compiled in CI.** There is no build step in this repository
  and no `arduino-cli` on the machine that produced the release commits, so the firmware has
  never been built automatically. It was compiled and flashed by hand during development.
- **The web UI is verified against a mock, not a device.** The click-through in `anti-slop/`
  drives the real embedded page in headless Chromium against a Python mock of the firmware's
  HTTP surface. That proves the page, the state machine, and the SSE contract. It cannot prove
  real accelerometer noise, the WiFi access point, or the I2C bus. See
  [`anti-slop/README.md`](anti-slop/README.md).

If you are wiring this up for the first time, **start with a GY-521 (MPU6050)**. If you only have
a BMI160 pod, treat it as an untested code path and expect to debug it yourself. Reporting what
you find would be genuinely useful.

## Hardware

| Part | Role | Status |
|------|------|--------|
| ESP32-C3 (Super Mini / LuatOS C3-CORE or clone) | MCU + WiFi access point | Field-tested |
| **GY-521 (MPU6050)** | Tilt sensing | **Field-tested, use this one** |
| GY-BMI160 breakout | Tilt sensing | Driver written, **never run on hardware** |

Both pods wire up the same way and both are auto-detected, but see
[Tested and proven](#tested-and-proven) before choosing. The short version: the MPU6050 path
has run on real devices, the BMI160 path has not run on any.

### Wiring

```
ESP32-C3          Sensor pod
--------          ----------
3V3      ----->   VCC
GND      ----->   GND
IO4      ----->   SDA
IO5      ----->   SCL
```

> **Board notes (LuatOS ESP32C3-CORE and clones):** external SPI flash uses GPIO11-17, so
> do not use those pins for peripherals. Onboard LEDs are D4=GPIO12 and D5=GPIO13
> (active-high); D4 is the game-status LED here. USB is a CH343 bridge, so set
> **USB CDC On Boot: Disabled** and **Flash Mode: DIO** in the IDE.
>
> **Power note:** the Super Mini's onboard regulator is small. TX power is capped to
> 8.5 dBm in firmware for stability. If you still see brownouts or disconnects, add a
> **100 µF low-ESR capacitor** across the board's 5V and GND.

## How to use

1. Power the device over USB.
2. Connect your phone to the WiFi network `ESPLevellerAP` (password `levelup123`).
3. Open **http://192.168.4.1**. On iOS you can also try `http://leveller.local`; most
   Android builds dropped `.local` name resolution, so use the IP there.
4. **CALIBRATE**: hold the device the way you will hold it during a round, then tap
   CALIBRATE. That orientation becomes level.
5. Set the **threshold** with the slider or the ± buttons. This is only accepted while
   IDLE, so you cannot change the rules mid-round.
6. Tap **START**. A random 2 to 5 s countdown runs, then the timer starts.
7. **Keep it level.** The tilt bar fills as you tilt, turning amber at 75% of the
   threshold and red at the threshold.
8. Tilt past the threshold and the round ends, showing your final time.
9. **STOP** ends a countdown or a running round early and shows the elapsed time.
   **BACK TO MENU** returns to the idle screen.

The page is responsive and works on a second phone or laptop as a live spectator.

## Building and flashing

1. Open `ESP_Leveller.ino` in the **Arduino IDE** (or build it with `arduino-cli`).
2. Install the libraries (Library Manager). [Credits and dependencies](#credits-and-dependencies)
   names each one and pins the versions this release was built against:
   - **ESP Async WebServer** 3.6.0, by Mathieu Carbou, a fork of Me-No-Dev's
   - **Async TCP** 3.3.2, same maintainer. The web server requires it; the sketch never includes it directly
   - **MPU6050_tockn** 1.5.2, **only** if your pod is a GY-521 / MPU6050. A BMI160 pod needs no sensor library at all
3. Select **ESP32-C3 Dev Module** from the Espressif `esp32` platform (3.3.11 is what this
   release was verified against). On a LuatOS C3-CORE or any CH343 board, set
   **USB CDC On Boot: Disabled** and use **Flash Mode: DIO**.
4. Upload.

> **Serial logging** is compiled out by default (`ENABLE_SERIAL 0`) so a release build
> stays quiet on a headless kiosk. Define `ENABLE_SERIAL 1` at the top of the sketch to
> watch the boot sequence, sensor detection, and AP IP during bring-up.

## API

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Web UI |
| `/api/events` | GET | SSE stream of `{"state","tilt","thr","t"}` at ~10 Hz |
| `/api/state` | GET | One-shot JSON state, used by the poll fallback |
| `/api/start` | POST | Start a countdown, or return to the menu from GAMEOVER |
| `/api/calibrate` | POST | Treat the current orientation as level |
| `/api/abort` | POST | Cancel a countdown, or end a running round and show the elapsed time |
| `/api/threshold?val=N` | POST | Set the threshold, clamped 5 to 90, accepted only while IDLE |

## Project layout

- `ESP_Leveller.ino`, the whole firmware in one sketch
- `DESIGN.md`, the web UI's design decisions and the reason behind each
- `anti-slop/`, the release audit and its click-through evidence
- `LICENSE`, MIT
- A custom carrier **PCB design** for a compact hardware build lives on the `pcb-design` branch.

## Credits and dependencies

### Firmware

Everything the device runs. Versions are the ones in the build this release was verified against, read from each library's own `library.properties`.

| Component | Version | Author | License | Role here |
|---|---|---|---|---|
| [ESP32 Arduino](https://github.com/espressif/arduino-esp32), the `esp32` platform | 3.3.11 | Espressif Systems | LGPL-2.1 | The MCU framework. Supplies `Wire.h`, `WiFi.h`, `Preferences.h`, `ESPmDNS.h`, `esp_wifi.h`, and the RISC-V toolchain, `esptool.py` and flasher that come with the package. |
| [ESP Async WebServer](https://github.com/mathieucarbou/ESPAsyncWebServer) | 3.6.0 | Me-No-Dev, maintained by Mathieu Carbou | LGPL-3.0 | `ESPAsyncWebServer.h`. The HTTP routes, and the `AsyncEventSource` behind `/api/events`. |
| [AsyncTCP](https://github.com/mathieucarbou/AsyncTCP) | 3.3.2 | Hristo Gochkov, maintained by Mathieu Carbou | LGPL-3.0 | Required by ESP Async WebServer, which declares `^3.3.2`. The sketch never includes it directly, but it does not build without it. |
| [MPU6050_tockn](https://github.com/tockn/MPU6050_tockn) | 1.5.2 | tockn, forked from [ElectronicCats/MPU6050](https://github.com/ElectronicCats/MPU6050) | **none declared**, see the note below | GY-521 / MPU6050 pods only. |

Two dependencies this project deliberately does **not** have:

- **No BMI160 driver.** The sketch reads the BMI160 configuration and data registers
  directly over I2C and identifies the chip from `CHIP_ID`. There is no Bosch library
  and no third-party BMI160 code anywhere in the project.
- **No ArduinoJson.** ESP Async WebServer advertises support for it, but the sketch
  formats its JSON with `snprintf` in `buildStateJson()`, so nothing pulls ArduinoJson in.

> **A caveat worth knowing about MPU6050_tockn.** The fork ships no `LICENSE` file and
> declares no `license=` field in its `library.properties`, so it carries no explicit
> grant to use or redistribute it. Its upstream, ElectronicCats' MPU6050, is MIT. Only
> the GY-521 path needs this library at all; a BMI160 pod builds without it. If you would
> rather sit on a plainly licensed library, ElectronicCats' original is API-compatible
> apart from its header name, so the swap is a one-line `#include` change.

### Web UI: none

The page embedded in `ESP_Leveller.ino` ships **no third-party code to the phone**. No
JavaScript framework, no CSS framework, no web fonts (the timer uses the browser's own
`monospace`, everything else uses `system-ui`), no CDN, no analytics, and no vendored
files. Audio is the browser's built-in WebAudio, and the live updates are the browser's
built-in `EventSource`. Every byte of the interface is written by hand in the sketch.

### Tooling

Build and audit only. None of this is on the device, and none of it is needed to run the firmware.

| Tool | License | Used for |
|---|---|---|
| Arduino IDE or `arduino-cli`, with Espressif's board package | LGPL-2.1 (with the package) | Compiling and flashing |
| [Node.js](https://nodejs.org) | MIT | Running the click-through harness |
| [playwright-core](https://github.com/microsoft/playwright) | Apache-2.0 | Driving headless Chromium |
| Chromium, Playwright build | BSD-3-Clause | Rendering the page under test |
| Python 3, standard library only | PSF | `anti-slop/mock-firmware-server.py` |

The dependencies above keep their own licences and are not redistributed here. Only the
firmware and the page in this repository are covered by [LICENSE](LICENSE).

## License

MIT, see [LICENSE](LICENSE).
