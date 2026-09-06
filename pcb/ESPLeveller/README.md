# ESP Leveller — Custom PCB (P1: Schematic)

**Status:** schematic done, ERC connectivity-clean, netlist machine-verified. Next: PCB layout (P2).

## Design

Drop-in carrier board — all active electronics are cheap replaceable modules, everything else is through-hole. **Zero SMD soldering required.**

```
2x 18650 (parallel) ──► TP4056 module (charge+protect) ──► slide SW ──► MT3608 boost (5.0V) ──► ESP32-C3 Super Mini (5V pin)
                                                              │
                              100µF bulk cap on +5V rail ◄────┘
ESP32-C3 SuperMini IO4 (SDA) ──┬── GY-521 / LSM6DS3 pod socket (8-pin, 2.54mm)
                IO5 (SCL) ─────┤   + 4.7k pull-ups to 3V3
                IO6 ───────────┴── optional operator button (to GND)
                IO7 ── 330R ── green THT status LED ── GND
```

## Files

| File | What |
|---|---|
| `ESPLeveller.kicad_sch` | Schematic (KiCad 10) |
| `ESPLeveller.kicad_pro` | Project file |
| `symbols/ESPLeveller.kicad_sym` | Module symbols (SuperMini socket, TP4056, MT3608) |
| `symbols/Generic.kicad_sym` | R/C/LED/SW/conn/power symbols |
| `footprints/ESPLeveller.pretty/` | Custom footprints (socket + module pads) |
| `ESPLeveller.pdf` | Rendered schematic for quick review |
| `ESPLeveller.net` | Exported netlist |

## Assembly notes (future BOM)

| Part | Source | ~Cost |
|---|---|---|
| ESP32-C3 Super Mini | AliExpress | ~$2 |
| TP4056 module (with protection) | AliExpress | ~$0.50 |
| MT3608 boost module — **set to 5.0V BEFORE install** | AliExpress | ~$0.50 |
| 2× 18650 + holders (parallel) | local | ~$6 |
| GY-521 now / LSM6DS3TR-C breakout later | AliExpress | $1–3 |
| THT: 100µF cap, 2× 4.7k, 330R, green 5mm LED, SPST slide switch, 8-pin female socket, 2-pin headers | — | ~$1 |

## Before first power-up

1. **Verify MT3608 is set to 5.0V** with a multimeter (no load), using its own pot — overvoltage kills the Super Mini.
2. TP4056 module must be the **protected** version (DW01+FS8205) — with raw 18650s this is not optional.
3. Cells in parallel: charge both to the same voltage BEFORE connecting in parallel.

## Firmware pin map (matches `Barrett_Leveller` sketch)

| Function | GPIO |
|---|---|
| SDA | IO4 |
| SCL | IO5 |
| Optional STOP button | IO6 |
| Status LED | IO7 |
