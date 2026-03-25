# Command Reference

The scoreboard accepts commands via **three interfaces** — all sharing
the same message set:

| Interface             | Transport       | When to use                                    |
| --------------------- | --------------- | ---------------------------------------------- |
| **OSC over WiFi**     | UDP port 9000   | Production / wireless control                  |
| **OSC over Ethernet** | UDP port 9000   | Production / wired PoE (low latency, reliable) |
| **Serial text**       | USB 115200 baud | Bench testing, standalone use, no network      |

> **Architecture note:** Serial commands are parsed into proxy
> `OSCMessage` objects and routed through exactly the same handler
> as network OSC — so behaviour is identical regardless of interface.

---

## Command catalogue

Displays are numbered **1 – 6** (1-based) in all commands.

---

### `/display/<N>` — set display text (short form)

| Parameter | Type            | Description                   |
| --------- | --------------- | ----------------------------- |
| arg 0     | `string`\|`int` | The text or number to display |

Sets the text shown on display _N_. An integer argument is
converted to its decimal string. Text is auto-centred in the
32-pixel-wide tile. Maximum ~5 characters at the default font.

**OSC (Python):**

```python
from pythonosc import udp_client
client = udp_client.SimpleUDPClient("192.168.1.42", 9000)
client.send_message("/display/1", "1234")
client.send_message("/display/2", 42)
```

**OSC (command line — `oscsend` from liblo):**

```bash
oscsend 192.168.1.42 9000 /display/1 s "1234"
oscsend 192.168.1.42 9000 /display/2 i 42
```

**Serial (PlatformIO monitor / any terminal @ 115200 baud):**

```
/display/1 "1234"
/display/2 42
```

---

### `/display/<N>/text` — set display text (explicit)

| Parameter | Type     | Description      |
| --------- | -------- | ---------------- |
| arg 0     | `string` | The text to show |

Identical to `/display/<N>` with a string argument.

**OSC (Python):**

```python
client.send_message("/display/3/text", "HI")
```

**Serial:**

```
/display/3/text "HI"
```

---

### `/display/<N>/color` — set text colour

| Parameter | Type  | Description     |
| --------- | ----- | --------------- |
| arg 0     | `int` | Red (0 – 255)   |
| arg 1     | `int` | Green (0 – 255) |
| arg 2     | `int` | Blue (0 – 255)  |

Changes the foreground text colour on display _N_.
Default colour is white (255, 255, 255).
The colour persists until changed again or until a clear.

**OSC (Python):**

```python
client.send_message("/display/1/color", [255, 0, 0])    # red
client.send_message("/display/2/color", [0, 255, 0])    # green
client.send_message("/display/3/color", [0, 100, 255])  # bluish
```

**OSC (command line):**

```bash
oscsend 192.168.1.42 9000 /display/1/color iii 255 0 0
```

**Serial:**

```
/display/1/color 255 0 0
/display/2/color 0 255 0
/display/3/color 0 100 255
```

---

### `/display/<N>/clear` — clear a single display

No arguments. Turns off all LEDs on display _N_ and resets its text.

**OSC (Python):**

```python
client.send_message("/display/4/clear", [])
```

**Serial:**

```
/display/4/clear
```

---

### `/display/<N>/brightness` — set brightness (via display address)

| Parameter | Type  | Description          |
| --------- | ----- | -------------------- |
| arg 0     | `int` | Brightness (0 – 255) |

Sets the **global** NeoPixel strip brightness. (Per-display brightness
is a future feature; for now this is equivalent to `/brightness`.)

**OSC (Python):**

```python
client.send_message("/display/1/brightness", 80)
```

**Serial:**

```
/display/1/brightness 80
```

---

### `/display/<N>/scroll` — set scroll transition mode

| Parameter | Type  | Description                                |
| --------- | ----- | ------------------------------------------ |
| arg 0     | `int` | `0` = instant, `1` = scroll up, `2` = down |

Controls how text changes are animated on display _N_:

| Mode | Constant      | Behaviour                               |
| ---- | ------------- | --------------------------------------- |
| `0`  | `SCROLL_NONE` | Instant replacement (default)           |
| `1`  | `SCROLL_UP`   | Old text scrolls up, new enters below   |
| `2`  | `SCROLL_DOWN` | Old text scrolls down, new enters above |

The scroll animation is non-blocking (driven by `update()` in the
main loop) at `SCROLL_STEP_MS` (default 15 ms) per pixel step —
so 8 pixels of height ≈ 120 ms for a full transition.

**OSC (Python):**

```python
client.send_message("/display/2/scroll", 1)    # enable scroll-up
client.send_message("/display/2", "999")        # will scroll in
```

**Serial:**

```
/display/2/scroll 1
/display/2 "999"
```

---

### `/brightness` — set global brightness

| Parameter | Type  | Description          |
| --------- | ----- | -------------------- |
| arg 0     | `int` | Brightness (0 – 255) |

Default is **20**. At this level, typical text display draws ~1–2 A.
Higher values increase current draw proportionally (a 10 A PSU
covers any realistic use case).

**OSC (Python):**

```python
client.send_message("/brightness", 40)
```

**OSC (command line):**

```bash
oscsend 192.168.1.42 9000 /brightness i 40
```

**Serial:**

```
/brightness 40
```

---

### `/scroll` — set scroll mode for all displays

| Parameter | Type  | Description                                |
| --------- | ----- | ------------------------------------------ |
| arg 0     | `int` | `0` = instant, `1` = scroll up, `2` = down |

Same as `/display/<N>/scroll` but applies to every display at once.

**OSC (Python):**

```python
client.send_message("/scroll", 1)    # all displays scroll up
```

**Serial:**

```
/scroll 1
```

---

### `/scrollspeed` — set scroll animation speed

| Parameter | Type  | Description                                |
| --------- | ----- | ------------------------------------------ |
| arg 0     | `int` | Milliseconds per pixel step (default `25`) |

Controls how fast scroll-transitions run. With the default 8-pixel-high
tiles, `25 ms × 8 = 200 ms` per transition. Lower values = faster.
Minimum 1.

**Serial:**

```
/scrollspeed 10      # fast scrolling
/scrollspeed 50      # slow, easy-to-read scrolling
```

---

### `/scrollblank` — blank frame between scroll items

| Parameter | Type  | Description                   |
| --------- | ----- | ----------------------------- |
| arg 0     | `int` | `0` = off (default), `1` = on |

When enabled, a blank (all-dark) frame is briefly shown between
consecutive queued scroll items. This prevents visual “bleeding” /
ghosting that can occur at high scroll speeds.

**Serial:**

```
/scrollblank 1       # enable blank frame
/scrollblank 0       # disable (default)
```

---

### `/display/<N>/clearqueue` — clear scroll queue for one display

No arguments. Discards all pending queued text for display _N_
without affecting the currently shown value or in-progress animation.

**Serial:**

```
/display/1/clearqueue
```

---

### `/clearqueue` — clear all scroll queues

No arguments. Discards pending queued text on all displays.

**Serial:**

```
/clearqueue
```

---

### `/status` — query animation state

No arguments. Replies on serial with `ANIMATING 0` or `ANIMATING 1`.

**Serial:**

```
/status
```

---

### `/clearall` or `/clear` — clear all displays

No arguments. Turns off all LEDs on all six displays.

**OSC (Python):**

```python
client.send_message("/clearall", [])
```

**OSC (command line):**

```bash
oscsend 192.168.1.42 9000 /clearall
```

**Serial:**

```
/clearall
```

---

## Serial interface details

### Enabling / disabling

Serial commands are enabled by default (`SERIAL_CMD_ENABLED=1` in
`config.h`). To disable, add to your build flags:

```ini
build_flags = -DSERIAL_CMD_ENABLED=0
```

### Line format

```
/address [arg1] [arg2] …
```

- Lines must be terminated with `\n` (LF) or `\r\n` (CR+LF).
- The first token must start with `/`.
- **Quoted tokens** are string arguments: `"hello world"`.
- **Unquoted numeric tokens** are parsed as integers: `42`, `255`.
- **Unquoted non-numeric tokens** are treated as string arguments.
- Lines starting with `#` are ignored (comments).
- Maximum line length: 127 characters.

### Quick test from PlatformIO monitor

```bash
pio device monitor -b 115200
```

Then type:

```
/display/1 "HELLO"
/display/2 42
/display/1/color 255 0 0
/brightness 40
/clearall
```

### Quick test with Python (pyserial)

```python
import serial, time

ser = serial.Serial("/dev/ttyACM0", 115200, timeout=1)
time.sleep(2)  # wait for ESP32 reset after DTR

ser.write(b'/display/1 "HELLO"\n')
time.sleep(0.1)
ser.write(b'/display/2 42\n')
time.sleep(0.1)
ser.write(b'/display/1/color 255 0 0\n')
```

See `test/test_serial_send.py` for a full smoke-test script.

### Quick test with `screen` or `minicom`

```bash
screen /dev/ttyACM0 115200
# type commands and press Enter
```

---

## Firmware API (C++ classes)

### `OSCHandler`

Declared in `src/OSCHandler.h`, implemented in `src/OSCHandler.cpp`.

| Method                          | Description                                                                                                                                                  |
| ------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `OSCHandler(DisplayManager& d)` | Constructor — takes a reference to the display driver.                                                                                                       |
| `bool begin()`                  | Connects to WiFi or Ethernet, starts UDP listener. Returns `false` on failure.                                                                               |
| `void update()`                 | Polls for incoming UDP/OSC packets and dispatches them. Call every `loop()`.                                                                                 |
| `void processSerial()`          | Reads serial input, assembles lines, parses into proxy `OSCMessage` objects and dispatches. Call every `loop()`. Only available when `SERIAL_CMD_ENABLED=1`. |
| `IPAddress localIP()`           | Returns the device's IP address (WiFi or Ethernet).                                                                                                          |

**Private helpers:**

| Method                           | Description                                                                              |
| -------------------------------- | ---------------------------------------------------------------------------------------- |
| `_processMessage(OSCMessage&)`   | Routes a (real or proxy) OSC message to the display manager.                             |
| `_handleSerialLine(const char*)` | Parses one newline-terminated string into an `OSCMessage` and calls `_processMessage()`. |

### `DisplayManager`

Declared in `src/DisplayManager.h`, implemented in `src/DisplayManager.cpp`.

| Method                                                        | Description                                                                    |
| ------------------------------------------------------------- | ------------------------------------------------------------------------------ | --- | ---------------------------------------- | ----------------------------------------------------------------- |
| `DisplayManager()`                                            | Constructor — sets up the NeoMatrix with layout from `config.h`.               |
| `void begin()`                                                | Initialises the NeoPixel hardware.                                             |
| `void setText(uint8_t idx, const char* text)`                 | Set text on display `idx` (0-based). Triggers scroll if scroll mode is active. |
| `void setColor(uint8_t idx, uint8_t r, uint8_t g, uint8_t b)` | Set RGB foreground colour for display `idx`.                                   |
| `void setColor(uint8_t idx, uint16_t color565)`               | Set colour using a 16-bit 565 value.                                           |
| `void clear(uint8_t idx)`                                     | Clear one display.                                                             |
| `void setScrollMode(uint8_t idx, uint8_t mode)`               | Set scroll transition mode for display `idx` (0/1/2).                          |
| `void setScrollModeAll(uint8_t mode)`                         | Set scroll mode for all displays.                                              |     | `void setScrollSpeed(uint8_t ms)`        | Set scroll speed in ms per pixel step (default `SCROLL_STEP_MS`). |
| `void setScrollBlank(bool enabled)`                           | Enable/disable blank frame between queued scroll items.                        |
| `void clearQueue(uint8_t idx)`                                | Discard pending scroll queue for one display.                                  |
| `void clearQueueAll()`                                        | Discard scroll queues on all displays.                                         |     | `void setBrightness(uint8_t brightness)` | Set global NeoPixel brightness (0-255).                           |
| `void clearAll()`                                             | Clear all displays.                                                            |
| `void update()`                                               | Drive scroll animations and push pixels. Call every `loop()`. No-op when idle. |
| `void showTestPattern()`                                      | Light each display in sequence at start-up.                                    |
| `bool isAnimating() const`                                    | Returns `true` if any display is mid-scroll.                                   |

### `DisplayState` (internal struct)

| Field            | Type            | Description                                 |
| ---------------- | --------------- | ------------------------------------------- |
| `text[32]`       | `char[]`        | Current (target) text                       |
| `oldText[32]`    | `char[]`        | Previous text (used during scroll)          |
| `color`          | `uint16_t`      | Foreground colour (565 format)              |
| `scrollMode`     | `uint8_t`       | `SCROLL_NONE` / `SCROLL_UP` / `SCROLL_DOWN` |
| `scrollOffset`   | `int8_t`        | Animation progress (0 = done)               |
| `scrollLastStep` | `unsigned long` | `millis()` timestamp of last tick           |
| `dirty`          | `bool`          | Needs instant (non-scroll) redraw           |
| `scrollJustDone` | `bool`          | Set once when scroll completes (one-shot)   |
| `queue[10][32]`  | `char[][]`      | Ring buffer of pending scroll items         |
| `queueHead`      | `uint8_t`       | Next slot to dequeue                        |
| `queueTail`      | `uint8_t`       | Next slot to enqueue                        |
| `queueCount`     | `uint8_t`       | Items currently in queue                    |

---

## Compile-time configuration

All defines live in `src/config.h` and can be overridden with
`-D` build flags in `platformio.ini`.

| Define               | Default                | Description                         |
| -------------------- | ---------------------- | ----------------------------------- |
| `USE_WIFI`           | (fallback)             | Use WiFi for network                |
| `USE_ETHERNET_W5500` | —                      | Use W5500 Ethernet instead of WiFi  |
| `SERIAL_CMD_ENABLED` | `1`                    | Enable serial text command input    |
| `NEOPIXEL_PIN`       | `2`                    | GPIO for NeoPixel data line         |
| `NUM_DISPLAYS`       | `6`                    | Number of 32×8 tiles                |
| `MATRIX_TILE_WIDTH`  | `32`                   | Pixels per tile (width)             |
| `MATRIX_TILE_HEIGHT` | `8`                    | Pixels per tile (height)            |
| `DEFAULT_BRIGHTNESS` | `20`                   | Start-up brightness (0-255)         |
| `OSC_PORT`           | `9000`                 | UDP port for OSC messages           |
| `SCROLL_STEP_MS`     | `25`                   | Milliseconds per scroll pixel step  |
| `SCROLL_QUEUE_SIZE`  | `10`                   | Max queued scroll items per display |
| `MATRIX_LAYOUT`      | see config.h           | NeoMatrix wiring flags              |
| `LED_TYPE`           | `NEO_GRB + NEO_KHZ800` | NeoPixel colour order + speed       |
| `ETH_CS_PIN`         | `6`                    | SPI chip-select for W5500           |
| `ETH_RST_PIN`        | `7`                    | Reset pin for W5500                 |

---

## Typical workflows

### Scoreboard during a match

```
# Setup (once)
/display/1/color 255 50 50
/display/2/color 255 50 50
/display/3/color 50 50 255
/display/4/color 50 50 255
/display/5/color 0 255 0
/display/6/color 0 255 0
/display/2/scroll 1
/display/4/scroll 1
/display/6/scroll 1
/brightness 60

# Labels
/display/1/text "P1"
/display/3/text "P2"
/display/5/text "TIME"

# Live updates from game engine
/display/2 0
/display/4 0
/display/6 300

# ... score happens ...
/display/2 10
# ... another score ...
/display/4 5
# ... timer ticks ...
/display/6 299
```

### Countdown timer (serial, no network needed)

```
/display/1/text "GO!"
/display/2 10
/display/2 9
/display/2 8
...
/display/2 0
/display/1/text "DONE"
```

### Test from laptop over serial

```bash
# Install pyserial
pip install pyserial

# Run the smoke test
python test/test_serial_send.py

# Or with explicit port
python test/test_serial_send.py /dev/cu.usbmodem14101
```

### Test from laptop over network

```bash
# Install python-osc
pip install python-osc

# Run the smoke test
python test/test_osc_send.py 192.168.1.42
```
