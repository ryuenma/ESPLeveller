# ESP Leveller 🛡️

A real-time reaction/steadiness game built on an **ESP32-C3** and an accelerometer/gyro sensor pod. Hold your device level — the moment it tilts past the threshold, the timer stops and you lose. Your phone is the controller *and* the display via a self-hosted web interface (no app, no internet needed).

![states](https://img.shields.io/badge/states-IDLE%20%7C%20COUNTDOWN%20%7C%20PLAYING%20%7C%20GAMEOVER-blue)

## Features

- **Real-time web UI** — timer, live tilt bar, threshold control (slider + ± buttons), all pushed over **Server-Sent Events at 10 Hz** (no polling lag)
- **Random countdown** start (2–5 s) — keeps you honest, reaction-game style
- **Audio cues** generated on the phone itself (WebAudio: countdown beep, proximity ticking, fail alarm)
- **Threshold 5–90°** — from "steady surgeon" to "anything goes"
- **Calibration** — set your current orientation as the level baseline, saved to flash
- **Settings persist** in NVS (threshold, calibration base) — survive power cycles
- **mDNS** — reachable at `http://leveller.local` on most phones
- **Auto-detecting sensor pod** — works with a GY-521 (MPU6050) *or* a BMI160 breakout (e.g. GY-BMI160). Marketplace "GY-LSM6DS3" boards often actually carry a BMI160 (the chips are pin-compatible); the firmware identifies the real chip by its ID register, so the label doesn't matter.

## Hardware

| Part | Role |
|------|------|
| ESP32-C3 (Super Mini / LuatOS C3-CORE or clone) | MCU + WiFi access point |
| GY-521 (MPU6050) **or** GY-BMI160 breakout | Tilt sensing |

### Wiring

```
ESP32-C3          Sensor pod
--------          ----------
3V3      ----->   VCC
GND      ----->   GND
IO4      ----->   SDA
IO5      ----->   SCL
```

> **Board notes (LuatOS ESP32C3-CORE and clones):** external SPI flash uses GPIO11–17 —
> don't use those for peripherals. Onboard LEDs D4=GPIO12, D5=GPIO13 (active-high); D4
> is used here as the game-status LED. USB is a CH343 bridge: set **USB CDC On Boot: Disabled**
> and **Flash Mode: DIO** in the IDE.
>
> **Power note:** the Super Mini's onboard regulator is small. TX power is capped to
> 8.5 dBm in firmware for stability. If you still see brownouts/disconnects, add a
> **100 µF low-ESR capacitor** across the board's 5V and GND.

## How to use

1. **Power the device** (USB).
2. On your phone, connect to the WiFi network:
   - **SSID:** `ESPLevellerAP`
   - **Password:** `levelup123`
3. Open **http://192.168.4.1** (or `http://leveller.local`).
4. **CALIBRATE** — hold the device the way you'll hold it during the game (this becomes "level"), then tap CALIBRATE.
   - **Threshold** — set the fail angle with the slider or ± buttons (only while IDLE).
   - **START** — a random 2–5 s countdown begins, then the timer runs.
   - **Keep it level.** The tilt bar fills as you tilt; it turns amber at 75% of the threshold, red at the threshold.
   - Tilt too far and the round ends — **GAMEOVER** shows your final time.
   - **ABORT** bails out of a countdown/round mid-run.

The interface is fully responsive — one phone is the player, a second phone/laptop can spectate in real time.

## Building & flashing

1. Open `ESP_Leveller.ino` in **Arduino IDE**.
2. Install libraries (Library Manager):
   - **ESP Async WebServer** (ESP32Async / mathieucarbou fork)
   - **Async TCP** (same publisher)
   - **MPU6050 by Electronic Cats** (tockn fork — `MPU6050_tockn.h`; only needed for a GY-521 pod)
3. Board: **ESP32-C3 Dev Module** (for a LuatOS C3-CORE / CH343 board set **USB CDC On Boot: Disabled**; use **Flash Mode: DIO**).
4. Upload.

> **Serial logging** is compiled out by default (`ENABLE_SERIAL 0`). To watch the boot
> sequence / sensor detection / AP IP during bring-up, define `ENABLE_SERIAL 1` at the top
> of the sketch. Release builds stay silent on a headless kiosk.

## API (for the curious)

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Web UI |
| `/api/events` | GET | SSE stream: `{"state","tilt","thr","t"}` at ~10 Hz |
| `/api/state` | GET | One-shot JSON state (poll fallback) |
| `/api/start` | POST | Start countdown / return to menu after GAMEOVER |
| `/api/calibrate` | POST | Set current orientation as level |
| `/api/abort` | POST | Cancel countdown or running round |
| `/api/threshold?val=N` | POST | Set threshold, clamped 5–90 (IDLE only) |

## Project layout

- `ESP_Leveller.ino` — the entire firmware (single-file sketch)
- `LICENSE` — MIT
- A custom carrier **PCB design** for a compact hardware build lives on the `pcb-design` branch.

## License

MIT — see [LICENSE](LICENSE).
