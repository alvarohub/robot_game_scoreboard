# Hardware Setup

## Components

| #   | Part                                                  | Link / Notes                                                      |
| --- | ----------------------------------------------------- | ----------------------------------------------------------------- |
| 1   | **M5Stack AtomS3** (ESP32-S3)                         | Main controller. Any ESP32 board works — adjust `platformio.ini`. |
| 6   | **WS2812B 32×8 NeoPixel matrix panels**               | Daisy-chained via DIN→DOUT.                                       |
| 1   | **5 V power supply** (≥ 15 A)                         | Powers the LED matrices directly.                                 |
| 1   | **M5Stack ESP32 Ethernet Unit with PoE** _(optional)_ | For wired network via W5500 SPI. Connects to AtomS3 Grove port.   |
| —   | Wires, connectors, capacitor (1000 µF across power)   | Standard NeoPixel best practices.                                 |

---

## Wiring — WiFi mode (simplest)

```
                      ┌──────────────┐
                      │   AtomS3     │
                      │              │
              GPIO 2 ─┤ G2 (Grove)   │
                      │              │
                GND ──┤ GND          │
                      └──────────────┘
                           │  │
                    DATA   │  │ GND
                     ┌─────┘  └──────┐
                     ▼               ▼
  5V ──┬─────────── VCC ┌──────────┐ GND
       │                │ Panel 1  │
       │           DOUT─┤ 32×8     ├─DIN (from AtomS3 GPIO 2)
       │                └──────────┘
       │                     │ DOUT
       │                     ▼ DIN
       ├─────────── VCC ┌──────────┐ GND ──┐
       │                │ Panel 2  │       │
       │           DOUT─┤ 32×8     │       │
       │                └──────────┘       │
       │                  … (×6 total)     │
       │                                   │
       └── (+) 5V PSU (−) ────────────────┘
```

### Key points

- **Data line** goes from AtomS3 GPIO 2 → Panel 1 DIN → Panel 1 DOUT
  → Panel 2 DIN → … → Panel 6 DIN.
- The **GND of the ESP32 must be connected to the same GND** as the
  5 V supply powering the panels.
- Add a **1000 µF capacitor** across the 5 V supply close to the
  first panel.
- A **300–500 Ω resistor** on the data line (close to the first panel's
  DIN) is recommended.
- Keep the data wire **short** (< 30 cm) before the first panel;
  longer runs need a level shifter (3.3 V → 5 V).

---

## Wiring — Ethernet mode

When the **M5Stack Ethernet Unit** is connected to the Grove port,
GPIO 2 is no longer available for NeoPixel data. Use one of the
back-pad GPIOs instead.

```
AtomS3 Grove port ←→ Ethernet Unit (I2C/SPI to W5500)

AtomS3 back pad GPIO 5 → 300 Ω → Panel 1 DIN
                  GND   →        → Panel chain GND
```

The Ethernet build environment (`atoms3-ethernet`) sets
`-DNEOPIXEL_PIN=5` and the W5500 CS/RST pins automatically.
Adjust in `platformio.ini` if your wiring differs.

---

## Power budget

Each WS2812B LED can draw up to ~60 mA at full white / full brightness,
but for a text scoreboard **10 mA per LED is a realistic design budget**
(low brightness, limited pixels lit). That gives:

$$1\,536 \times 10\,\text{mA} = 15.4\,\text{A (absolute worst case at 10 mA budget)}$$

In practice, only ~10–15 % of pixels are lit when showing text, so
actual draw is far below that.

| Scenario                                          | Current draw (approx.)                              |
| ------------------------------------------------- | --------------------------------------------------- |
| All LEDs off                                      | ~0.3 A (ESP32 + quiescent)                          |
| Typical score display (white text, brightness 20) | ~1–2 A                                              |
| Busy display, brightness 100                      | ~5–8 A                                              |
| All LEDs full white, brightness 255               | ~92 A (theoretical max — never happens in practice) |

> **Recommendation:** a **5 V / 5–10 A** supply is more than enough
> for normal scoreboard use. Even a 5 A supply will work fine at
> default brightness.

---

## NeoPixel matrix layout

If text appears garbled, the pixel wiring order of your panels
doesn't match the default `MATRIX_LAYOUT` flags in `config.h`.

Common patterns:

| Panel type                    | Flags                                                                            |
| ----------------------------- | -------------------------------------------------------------------------------- |
| Adafruit NeoPixel 32×8        | `NEO_MATRIX_TOP + NEO_MATRIX_LEFT + NEO_MATRIX_COLUMNS + NEO_MATRIX_PROGRESSIVE` |
| Generic Chinese 32×8 (zigzag) | `NEO_MATRIX_TOP + NEO_MATRIX_LEFT + NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG`      |
| Row-based panel               | `NEO_MATRIX_TOP + NEO_MATRIX_LEFT + NEO_MATRIX_ROWS + NEO_MATRIX_PROGRESSIVE`    |

The tile (inter-panel) flags usually stay:

```
NEO_TILE_TOP + NEO_TILE_LEFT + NEO_TILE_ROWS + NEO_TILE_PROGRESSIVE
```

See the [Adafruit NeoMatrix guide](https://learn.adafruit.com/adafruit-neopixel-uberguide/neomatrix-library)
for a visual explanation of each flag.
