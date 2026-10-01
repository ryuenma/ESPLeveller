# anti-slop audit

Release audit of the ESP Leveller web UI and firmware, run before archiving the project.

| File | What it is |
|---|---|
| `audit-001-2026-10-01.md` | The 16 findings, tiered by rule, each with the evidence that produced it |
| `audit-001-followup.md` | What was fixed, the Delivery Gate result, and what could not be verified |
| `clickthrough-output.txt` | Recorded click-through of every interactive element, after the fixes |
| `clickthrough.js` | The harness that produced that transcript |
| `mock-firmware-server.py` | A stand-in for the sketch's HTTP surface, so the page can be driven off-hardware |

## Reproducing the click-through

The sketch cannot be built on a machine without the ESP32 core, so the UI was verified by extracting the
page straight out of `ESP_Leveller.ino` and serving it from a mock that reproduces the firmware's
endpoints, the SSE stream, the state machine, and the IDLE-only threshold rule. Nothing is hand-copied, so
the harness cannot drift away from the firmware.

```sh
# 1. dependencies
npm install playwright-core
pip install playwright && playwright install chromium

# 2. serve the page (the mock reads index.html from its own directory)
python -c "import re,io; s=io.open('../ESP_Leveller.ino',encoding='utf-8').read(); \
io.open('index.html','w',encoding='utf-8').write(re.search(r'R\"rawliteral\(\n(.*?)\n\)rawliteral\"',s,re.S).group(1))"
python mock-firmware-server.py

# 3. drive it
node clickthrough.js
```

`clickthrough.js` reads the page from the sketch itself, so step 2's extraction is only needed for the mock
server to serve.

The mock reproduces the HTTP contract, not the hardware. It does not model accelerometer noise, the access
point, or the I2C bus, so it cannot stand in for a bench test of the tilt sensing itself.
