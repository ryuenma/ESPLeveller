# ESP Leveller 🛡️

A real-time reaction/steadiness game built on an **ESP32-C3 Super Mini** and a **GY-521 (MPU6050)** accelerometer/gyro. Hold your device level — the moment it tilts past the threshold, the timer stops and you lose. Your phone is the controller *and* the display via a self-hosted web interface (no app, no internet needed).

![game states](https://img.shields.io/badge/states-IDLE%20%7C%20COUNTDOWN%20%7C%20PLAYING%20%7C%20GAMEOVER-blue)

## Features

- **Real-time web UI** — timer, live tilt bar, threshold control (slider + ± buttons), all pushed over **Server-Sent Events at 10 Hz** (no polling lag)
- **Random countdown** start (2–5 s) — keeps you honest, reaction-game style
- **Audio cues** generated on the phone itself (WebAudio: countdown beep, proximity ticking, fail alarm)
- **Threshold 5–90°** — from "steady surgeon" to "anything goes"
- **Calibration** — set your current orientation as the level baseline, saved to flash
- **Settings persist** in NVS (threshold, calibration base) — survive power cycles
- **mDNS** — reachable at `http://leveller.local` on most phones

## Hardware

| Part | Role |
|------|------|
| ESP32-C3 Super Mini | MCU + WiFi AP |
| GY-521 (MPU6050) | Tilt sensing |

### Wiring

```
ESP32-C3          GY-521
--------          ------
3V3      ----->   VCC
GND      ----->   GND
GPIO1    ----->   SDA
GPIO0    ----->   SCL
```

> **Power note:** the Super Mini's onboard regulator is small. TX power is capped to 8.5 dBm in firmware for stability (plenty for phone-at-desk range). If you still see brownouts/disconnects, add a **100 µF low-ESR capacitor** across the board's 5V and GND.

## How to use

1. **Power the device** (USB). First boot takes ~7 seconds — the gyro auto-calibrates, so keep it **still** during that time.
2. On your phone, connect to the WiFi network:
   - **SSID:** `LevellerAP2`
   - **Password:** `levelup123`
3. Open **http://192.168.4.1** (or `http://leveller.local`).
4. Play:
   - **CALIBRATE** — hold the device the way you'll hold it during the game (this becomes "level"), then tap CALIBRATE. Wait half a second.
   - **Threshold** — set the fail angle with the slider or ± buttons (only while IDLE). Higher = easier.
   - **START** — a random countdown begins (2–5 s). When the UI beeps and flips to PLAYING, the timer runs.
   - **Keep it level.** The tilt bar fills as you tilt; it turns amber at 75% of the threshold (with warning ticks), red at the threshold.
   - Tilting past the threshold ends the round — **GAMEOVER** shows your final time. Tap the button to return to menu.
   - **ABORT** bails out of a countdown/round mid-run.

The interface is fully responsive — one phone is a player, but a second phone/laptop can spectate the same round in real time.

## Building & flashing

1. Open `ESP_Leveller.ino` in **Arduino IDE**.
2. Install libraries (Library Manager):
   - **ESP Async WebServer** (ESP32Async / mathieucarbou)
   - **Async TCP** (same publisher)
   - **MPU6050 by Electronic Cats** (tockn fork — provides `MPU6050_tockn.h`)
3. Board: **ESP32C3 Dev Module**, with **USB CDC On Boot: Enabled**.
4. Upload, open Serial Monitor @ 115200 to watch boot + calibration.

## API (for the curious)

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Web UI |
| `/api/events` | GET | SSE stream: `{"state","tilt","thr","t"}` at ~10 Hz |
| `/api/state` | GET | One-shot JSON state (poll fallback) |
| `/api/start` | POST | Start countdown / return to menu after GAMEOVER |
| `/api/calibrate` | POST | Set current orientation as level (IDLE only) |
| `/api/abort` | POST | Cancel countdown or running round |
| `/api/threshold?val=N` | POST | Set threshold, clamped 5–90 (IDLE only) |

## Architecture notes (why it's built this way)

- **AsyncWebServer + AsyncEventSource** — the stock sync `WebServer` services one client at a time, which starves SSE streams (the timer visibly froze and "DISCONNECTED" flickered). The async stack keeps per-client queues and never blocks `loop()`.
- **I2C ownership** — the MPU6050 is only touched from `loop()`; web handlers set request flags (`calRequested`) instead of touching the bus. Concurrent I2C from the web task corrupted calibrations.
- **TX power cap** — `esp_wifi_set_max_tx_power(WIFI_POWER_8_5dBm)` *after* `softAP()`; the Super Mini's regulator sags at default 19.5 dBm TX peaks.
- **MPU rate-limited to 50 Hz** — full-speed `update()` starved the single-core C3's WiFi task.

## License

MIT
