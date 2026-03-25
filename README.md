# Robot Game Scoreboard

Six-display NeoPixel scoreboard driven by **OSC over UDP** or
**serial text commands**, built on ESP32 with PlatformIO / Arduino.

```
┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐
│  32 × 8  │→ │  32 × 8  │→ │  32 × 8  │→ │  32 × 8  │→ │  32 × 8  │→ │  32 × 8  │
│ Display 1│  │ Display 2│  │ Display 3│  │ Display 4│  │ Display 5│  │ Display 6│
└──────────┘  └──────────┘  └──────────┘  └──────────┘  └──────────┘  └──────────┘
      ↑ NeoPixel data (single GPIO)
  ┌────────┐
  │  ESP32 │◄── OSC / UDP (WiFi or Ethernet PoE)
  │        │◄── Serial text commands (USB)
  └────────┘
```

---

## Hardware

| Part                                                | Role            | Notes                                                             |
| --------------------------------------------------- | --------------- | ----------------------------------------------------------------- |
| M5Stack **AtomS3** (ESP32-S3)                       | Microcontroller | Any ESP32 board works                                             |
| 6 × WS2812B 32×8 NeoPixel matrix                    | Score displays  | Daisy-chained via single data line                                |
| M5Stack **ESP32 Ethernet Unit with PoE** (optional) | Wired network   | W5500 SPI chip, for production use                                |
| 5 V PSU (≥ 15 A recommended)                        | LED power       | 1 536 LEDs × ~60 mA max = 92 A absolute max; real use is far less |

> **Power warning:** Never power 1 536 LEDs from USB alone.
> Use an external 5 V supply connected directly to the matrix
> power rails with adequate wiring gauge.

See [`docs/hardware_setup.md`](docs/hardware_setup.md) for wiring details.

---

## Quick start

### 1. Prerequisites

- [PlatformIO CLI](https://docs.platformio.org/en/latest/core/installation.html)
  or the **PlatformIO IDE** VS Code extension.
- Python 3 + `python-osc` (for the test script): `pip install python-osc`

### 2. Clone & configure

```bash
git clone https://github.com/alvarohub/robot_game_scoreboard.git
cd robot_game_scoreboard

# Create your WiFi credentials file (not committed to git)
cp src/credentials.h.example src/credentials.h
# Edit src/credentials.h with your SSID and password
```

### 3. Build & flash

```bash
# For AtomS3 + WiFi (default)
pio run -e atoms3-wifi -t upload

# For AtomS3 + Ethernet
pio run -e atoms3-ethernet -t upload

# For generic ESP32 DevKit + WiFi
pio run -e esp32dev-wifi -t upload
```

### 4. Monitor serial output

```bash
pio device monitor
```

On boot the board will:

1. Run a quick colour self-test across all six displays.
2. Show its IP address on displays 1 & 2 for 4 seconds.
3. Start listening for OSC messages.

### 5. Send a test score

```bash
cd test
python test_osc_send.py 192.168.1.42   # replace with the board's IP
```

---

## OSC protocol

Displays are numbered **1 – 6** in OSC messages.

| Address                   | Arguments                        | Example                      | Effect                |
| ------------------------- | -------------------------------- | ---------------------------- | --------------------- |
| `/display/<N>`            | `string` or `int`                | `/display/1` `"1234"`        | Set display 1 text    |
| `/display/<N>/text`       | `string`                         | `/display/3/text` `"HI"`     | Same as above         |
| `/display/<N>/color`      | `int` `int` `int` (R G B, 0-255) | `/display/2/color` `255 0 0` | Red text on display 2 |
| `/display/<N>/clear`      | —                                | `/display/4/clear`           | Clear display 4       |
| `/display/<N>/brightness` | `int` (0-255)                    | `/display/1/brightness` `80` | Set global brightness |
| `/brightness`             | `int` (0-255)                    | `/brightness` `40`           | Set global brightness |
| `/clearall` or `/clear`   | —                                | `/clearall`                  | Clear all displays    |

All commands also work over **USB-Serial** as plain text lines (same syntax,
newline-terminated). No network required — great for testing and standalone use.

Full command reference (all interfaces, examples, C++ API):
[`docs/command_reference.md`](docs/command_reference.md)

OSC-specific protocol details: [`docs/osc_protocol.md`](docs/osc_protocol.md)

---

## Configuration

All tunables live in [`src/config.h`](src/config.h) with sensible defaults.
Most can also be overridden via `-D` build flags in `platformio.ini`.

| Define               | Default      | Description                 |
| -------------------- | ------------ | --------------------------- |
| `NEOPIXEL_PIN`       | `2`          | GPIO for NeoPixel data      |
| `NUM_DISPLAYS`       | `6`          | Number of 32×8 tiles        |
| `MATRIX_TILE_WIDTH`  | `32`         | Pixels per tile (width)     |
| `MATRIX_TILE_HEIGHT` | `8`          | Pixels per tile (height)    |
| `DEFAULT_BRIGHTNESS` | `20`         | Start-up brightness (0-255) |
| `OSC_PORT`           | `9000`       | UDP port for OSC            |
| `MATRIX_LAYOUT`      | see config.h | NeoMatrix wiring flags      |

> If your panels show garbled output, you likely need to change
> `MATRIX_LAYOUT` (row vs. column, progressive vs. zigzag).
> The [Adafruit NeoMatrix guide](https://learn.adafruit.com/adafruit-neopixel-uberguide/neomatrix-library)
> explains each flag.

---

## Project structure

```
robot_game_scoreboard/
├── platformio.ini            Build configuration (multiple envs)
├── src/
│   ├── main.cpp              Entry point — setup / loop
│   ├── config.h              All compile-time settings
│   ├── credentials.h.example Template for WiFi credentials
│   ├── DisplayManager.h/cpp  NeoMatrix wrapper, per-display state
│   └── OSCHandler.h/cpp      UDP reception + OSC parsing
├── docs/
│   ├── hardware_setup.md     Wiring diagrams & power notes
│   └── osc_protocol.md       Full OSC API reference
├── test/
│   └── test_osc_send.py      Python helper to send test scores
├── include/                   (reserved for shared headers)
└── lib/                       (reserved for local libraries)
```

---

## Future ideas

- [ ] Scrolling text for messages longer than 5 characters
- [ ] Custom large-pixel digit font (full 8 px height)
- [ ] Per-display brightness control
- [ ] HTTP status / config page
- [ ] mDNS (`scoreboard.local`)
- [ ] OTA firmware updates
- [ ] Show status on the AtomS3 built-in LCD
- [ ] Animation effects (fade, flash on score change)

---

## License

MIT
