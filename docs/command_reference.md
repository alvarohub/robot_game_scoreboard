# Command Reference

The scoreboard accepts commands via **four interfaces** — all sharing
the same message set:

| Interface             | Transport       | When to use                                    |
| --------------------- | --------------- | ---------------------------------------------- |
| **OSC over WiFi**     | UDP port 9000   | Production / wireless control                  |
| **OSC over Ethernet** | UDP port 9000   | Production / wired PoE (low latency, reliable) |
| **Serial text**       | USB 115200 baud | Bench testing, standalone use, no network      |
| **RS485 text**        | Serial2 115200  | Long-distance wired (up to 1200 m), multi-drop |

> **Architecture note:** Serial and RS485 commands are parsed into proxy
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

### `/display/<N>/mode` — set display mode

| Parameter | Type            | Description                                                     |
| --------- | --------------- | --------------------------------------------------------------- |
| arg 0     | `int`\|`string` | `0`/`text`, `1`/`scroll_up`, `2`/`scroll_down`, `3`/`particles` |

Selects the active rendering mode for display _N_.

| Mode | Name          | Behaviour                                    |
| ---- | ------------- | -------------------------------------------- |
| `0`  | `text`        | Static text rendering                        |
| `1`  | `scroll_up`   | Text changes scroll upward                   |
| `2`  | `scroll_down` | Text changes scroll downward                 |
| `3`  | `particles`   | Tilt-driven particle system using AtomS3 IMU |

**Serial:**

```
/display/1/mode 3
/display/1/mode text
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

### `/display/<N>/scroll` — compatibility alias for text scroll modes

| Parameter | Type  | Description                                    |
| --------- | ----- | ---------------------------------------------- |
| arg 0     | `int` | `0` = text, `1` = scroll up, `2` = scroll down |

Compatibility wrapper around `/display/<N>/mode` for the text-related modes.
It does not expose particle mode; use `/display/<N>/mode 3` for that.

| Mode | Behaviour                               |
| ---- | --------------------------------------- |
| `0`  | Instant replacement (default)           |
| `1`  | Old text scrolls up, new enters below   |
| `2`  | Old text scrolls down, new enters above |

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

### `/mode` — set display mode for all displays

| Parameter | Type            | Description                                                     |
| --------- | --------------- | --------------------------------------------------------------- |
| arg 0     | `int`\|`string` | `0`/`text`, `1`/`scroll_up`, `2`/`scroll_down`, `3`/`particles` |

Same as `/display/<N>/mode` but applies to every display at once.

**Serial:**

```
/mode particles
/mode 0
```

---

### `/scroll` — compatibility alias for text scroll modes

| Parameter | Type  | Description                                    |
| --------- | ----- | ---------------------------------------------- |
| arg 0     | `int` | `0` = text, `1` = scroll up, `2` = scroll down |

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

### `/display/<N>/text2particles` — convert text to frozen particles

No arguments. Renders the current text on display _N_ into a GFX
canvas, scans for lit pixels, and creates a particle at each one.
The text layer is disabled, the particle layer is enabled with
**physics paused** and glow rendering (σ = 0.6).

Physics defaults set at conversion time:

- **Collision disabled** — particles don't bump each other away.
- **Scaffold spring enabled** (strength 1.0, range 10) — each particle
  is tethered to its original position by a spring force.
- This means: if you resume physics with temperature > 0, the text
  will wiggle but hold its shape. Disable the scaffold to let particles
  scatter freely.

To make the text "explode", disable scaffold and resume physics:

```
/display/1/text2particles
/display/1/particles/pause 0
```

To make the text wiggle but hold its shape:

```
/display/1/text2particles
/display/1/particles 50 20 0.0 0.92 0.78 0.35 4 0.6 0.3
/display/1/particles/pause 0
```

**Global form:** `/text2particles` — applies to all displays.

**Serial:**

```
/display/1/text2particles
```

---

### `/display/<N>/particles/pause` — pause / resume particle physics

| Parameter | Type  | Description            |
| --------- | ----- | ---------------------- |
| arg 0     | `int` | `1` = pause, `0` = run |

When paused, particles keep rendering (glow, shapes) but do not
move — gravity, collisions, temperature and attraction are frozen.
Useful for freeze-frame effects or when using `text2particles`.

**Global form:** `/particles/pause 1` — applies to all displays.

**Serial:**

```
/display/1/particles/pause 1
/display/1/particles/pause 0
```

---

### `/display/<N>/screen2particles` — capture screen to particles

No arguments. Scans the current canvas buffer (whatever is rendered —
text, particles, or any GFX drawing). Each lit pixel becomes a particle
whose colour matches the original pixel. Physics is paused, glow
rendering enabled (σ = 0.6). Collision is **disabled** and scaffold
spring is **enabled** (strength 1.0) so particles stay in place.

**Global form:** `/screen2particles` — applies to all displays.

**Serial:**

```
/display/1/screen2particles
```

---

### `/display/<N>/particles/restore` — restore scaffold positions

No arguments. Resets all particles to their saved scaffold positions
(the positions captured at `textToParticles` / `screenToParticles` /
`init` time). Velocities are zeroed and physics is paused.

**Global form:** `/particles/restore` — applies to all displays.

**Serial:**

```
/display/1/particles/restore
```

---

### `/display/<N>/particles/restorecolors` — restore scaffold colours

No arguments. Restores each particle's colour from its scaffold
snapshot. Useful after `speedColor` has overwritten the original
colours.

**Global form:** `/particles/restorecolors` — applies to all displays.

**Serial:**

```
/display/1/particles/restorecolors
```

---

### `/display/<N>/particles/enable` — enable / disable particle layer

| Parameter | Type  | Description                 |
| --------- | ----- | --------------------------- |
| arg 0     | `int` | `1` = enable, `0` = disable |

**Global form:** `/particles/enable 1` — applies to all displays.

**Serial:**

```
/display/1/particles/enable 1
```

---

### `/display/<N>/text/enable` — enable / disable text layer

| Parameter | Type  | Description                 |
| --------- | ----- | --------------------------- |
| arg 0     | `int` | `1` = enable, `0` = disable |

**Global form:** `/text/enable 1` — applies to all displays.

**Serial:**

```
/display/1/text/enable 0
```

---

### `/display/<N>/particles/brightness` — particle layer brightness

| Parameter | Type  | Description          |
| --------- | ----- | -------------------- |
| arg 0     | `int` | Brightness (0 – 255) |

**Global form:** `/particles/brightness 128` — applies to all displays.

**Serial:**

```
/display/1/particles/brightness 128
```

---

### `/display/<N>/text/brightness` — text layer brightness

| Parameter | Type  | Description          |
| --------- | ----- | -------------------- |
| arg 0     | `int` | Brightness (0 – 255) |

**Global form:** `/text/brightness 200` — applies to all displays.

**Serial:**

```
/display/1/text/brightness 200
```

---

### `/display/<N>/particles/color` — particle colour

| Parameter | Type  | Description     |
| --------- | ----- | --------------- |
| arg 0     | `int` | Red (0 – 255)   |
| arg 1     | `int` | Green (0 – 255) |
| arg 2     | `int` | Blue (0 – 255)  |

Sets the default colour for new particles on display _N_.

**Global form:** `/particles/color 255 128 0` — applies to all displays.

**Serial:**

```
/display/1/particles/color 255 128 0
```

---

### `/display/<N>/particles` — particle physics configuration

Up to **26 positional arguments**, all optional. Missing args keep
their current values. This is the main command for tuning the
particle physics engine at runtime.

| Arg | Name             | Type    | Default | Description                                           |
| --- | ---------------- | ------- | ------- | ----------------------------------------------------- |
| 0   | count            | `int`   | 6       | Number of particles                                   |
| 1   | renderMs         | `int`   | 20      | Canvas redraw interval (ms)                           |
| 2   | gravityScale     | `float` | 18.0    | Multiplier for IMU gravity input                      |
| 3   | elasticity       | `float` | 0.92    | Particle-particle bounce coefficient (0–1)            |
| 4   | wallElasticity   | `float` | 0.78    | Wall bounce coefficient (0–1)                         |
| 5   | radius           | `float` | 0.45    | Collision & rendering radius (pixels)                 |
| 6   | renderStyle      | `int`   | 4       | 0=point, 1=square, 2=circle, 3=text, 4=glow           |
| 7   | glowSigma        | `float` | 1.2     | Gaussian glow envelope sigma (pixels)                 |
| 8   | temperature      | `float` | 0.0     | Langevin jitter magnitude                             |
| 9   | attractStrength  | `float` | 0.0     | Inter-particle attraction strength (0 = off)          |
| 10  | attractRange     | `float` | 3.0     | Attraction range (× sum-of-radii)                     |
| 11  | gravityEnabled   | `int`   | 1       | 0 = off, 1 = on                                       |
| 12  | substepMs        | `int`   | 20      | Max physics sub-step (ms, lower = more stable)        |
| 13  | damping          | `float` | 0.9998  | Per-substep velocity multiplier (1 = none)            |
| 14  | glowWavelength   | `float` | 0.0     | Interference wavelength (0 = pure glow, >0 = ripples) |
| 15  | speedColor       | `int`   | 0       | Colour from velocity heatmap: 0/1                     |
| 16  | springStrength   | `float` | 0.0     | Linear spring force strength (charge-dependent)       |
| 17  | springRange      | `float` | 5.0     | Spring force cutoff distance (pixels)                 |
| 18  | springEnabled    | `int`   | 0       | Enable spring force: 0/1                              |
| 19  | coulombStrength  | `float` | 0.0     | Coulomb 1/r² force strength (charge-dependent)        |
| 20  | coulombRange     | `float` | 10.0    | Coulomb force cutoff distance (pixels)                |
| 21  | coulombEnabled   | `int`   | 0       | Enable Coulomb force: 0/1                             |
| 22  | scaffoldStrength | `float` | 0.0     | Spring pull toward scaffold origin positions          |
| 23  | scaffoldRange    | `float` | 10.0    | Scaffold force max effective range (pixels)           |
| 24  | scaffoldEnabled  | `int`   | 0       | Enable scaffold attraction: 0/1                       |
| 25  | collisionEnabled | `int`   | 1       | Enable hard-sphere collision: 0/1                     |

#### Force model summary

| Force     | Law            | Scope           | Depends on charge? |
| --------- | -------------- | --------------- | ------------------ |
| Collision | Hard-sphere    | Inter-particle  | No                 |
| Attract   | Linear (fades) | Inter-particle  | No                 |
| Spring    | Linear         | Inter-particle  | Yes (q₁ × q₂)      |
| Coulomb   | Inverse-square | Inter-particle  | Yes (q₁ × q₂)      |
| Scaffold  | Linear spring  | Particle→origin | No                 |

**Serial examples:**

```
# Create 50 particles, glow render, radius 0.3
/display/1/particles 50 20 18.0 0.92 0.78 0.3 4 1.2

# Enable scaffold spring (args 22-24) and disable collision (arg 25)
/display/1/particles 50 20 18.0 0.92 0.78 0.3 4 1.2 0.0 0.0 3.0 1 20 0.9998 0.0 0 0.0 5.0 0 0.0 10.0 0 1.0 10.0 1 0
```

---

### `/display/<N>/particles/transform` — set full view transform

| Parameter | Type    | Description              |
| --------- | ------- | ------------------------ |
| arg 0     | `float` | Rotation angle (degrees) |
| arg 1     | `float` | Scale X                  |
| arg 2     | `float` | Scale Y                  |
| arg 3     | `float` | Translate X (pixels)     |
| arg 4     | `float` | Translate Y (pixels)     |

Render-time only — physics coordinates are not affected.
Transform is applied as scale → rotate → translate around display centre.

**Serial:**

```
/display/1/particles/transform 45.0 1.5 1.5 0.0 0.0
```

---

### `/display/<N>/particles/rotate` — rotation only

| Parameter | Type    | Description      |
| --------- | ------- | ---------------- |
| arg 0     | `float` | Angle in degrees |

**Global form:** `/particles/rotate 30.0` — applies to all displays.

**Serial:**

```
/display/1/particles/rotate 45.0
```

---

### `/display/<N>/particles/scale` — scale only

| Parameter | Type    | Description              |
| --------- | ------- | ------------------------ |
| arg 0     | `float` | Scale X (and Y if 1 arg) |
| arg 1     | `float` | Scale Y (optional)       |

If only one argument is given, it applies to both axes.

**Global form:** `/particles/scale 2.0` — applies to all displays.

**Serial:**

```
/display/1/particles/scale 1.5 1.5
```

---

### `/display/<N>/particles/translate` — translate only

| Parameter | Type    | Description       |
| --------- | ------- | ----------------- |
| arg 0     | `float` | X offset (pixels) |
| arg 1     | `float` | Y offset (pixels) |

**Global form:** `/particles/translate 5.0 0.0` — all displays.

**Serial:**

```
/display/1/particles/translate 3.0 -1.0
```

---

### `/display/<N>/particles/resettransform` — reset view to identity

No arguments. Resets rotation, scale, and translation to defaults.

**Global form:** `/particles/resettransform` — applies to all displays.

**Serial:**

```
/display/1/particles/resettransform
```

---

### `/display/<N>/text/push` — push text to stack

| Parameter | Type     | Description  |
| --------- | -------- | ------------ |
| arg 0     | `string` | Text to push |

Appends text to the display's text stack (used by RENDER_TEXT particle mode
and scroll continuous mode).

**Global form:** `/text/push "HELLO"` — applies to all displays.

**Serial:**

```
/display/1/text/push "HELLO"
```

---

### `/display/<N>/text/pop` — pop last text from stack

No arguments. Removes the last entry from the text stack.

**Global form:** `/text/pop` — applies to all displays.

---

### `/display/<N>/text/set` — set stack entry at index

| Parameter | Type     | Description           |
| --------- | -------- | --------------------- |
| arg 0     | `int`    | Stack index (0-based) |
| arg 1     | `string` | Text to set           |

**Global form:** `/text/set 0 "NEW"` — applies to all displays.

---

### `/display/<N>/text/clear` — clear text stack

No arguments. Removes all entries from the text stack.

**Global form:** `/text/clear` — applies to all displays.

---

### `/display/<N>/text/list` — print text stack to serial

No arguments. Outputs the text stack contents to serial for debugging.

**Global form:** `/text/list` — applies to all displays.

---

### `/scrollcontinuous` — auto-cycle text stack in scroll mode

| Parameter | Type  | Description                   |
| --------- | ----- | ----------------------------- |
| arg 0     | `int` | `0` = off (default), `1` = on |

When enabled, scroll mode automatically cycles through all text
stack entries in sequence.

**Serial:**

```
/scrollcontinuous 1
```

---

### `/defaults` — reset all parameters

No arguments. Resets all runtime parameters (all displays) back to
their compiled defaults.

**Serial:**

```
/defaults
```

---

### `/script/begin` — start a staged runtime script upload

No arguments. Clears the staged upload buffer and starts a new
runtime `.game` script upload session.

---

### `/script/append` — append one `.game` source line

| Parameter | Type     | Description            |
| --------- | -------- | ---------------------- |
| arg 0     | `string` | One source line to add |

Use this repeatedly to send a `.game` script line by line.
The firmware appends a newline after each call.

**Serial:**

```
/script/append "step intro"
/script/append "wait 1000"
```

---

### `/script/commit` — parse and install staged script

No arguments. Parses the staged `.game` text, installs it as a
runtime animation script, and makes it available through the existing
`/animation N` and `/display/<N>/animation N` commands.

---

### `/script/cancel` — discard staged script text

No arguments. Clears the current staged upload buffer.

---

### `/script/save` — save staged script text to flash

| Parameter | Type     | Description                   |
| --------- | -------- | ----------------------------- |
| arg 0     | `string` | File name or path for `.game` |

Stores the staged script source in SPIFFS. If the name has no leading
slash, it is stored under `/games/` and `.game` is appended if missing.

---

### `/script/load` — load and install a stored script

| Parameter | Type     | Description                   |
| --------- | -------- | ----------------------------- |
| arg 0     | `string` | File name or path for `.game` |

Reads a stored `.game` file from SPIFFS, parses it, and installs it as
runtime animation script data.

---

### `/script/bank/reseed` — restore a built-in preset into its bank slot

| Parameter | Type  | Description               |
| --------- | ----- | ------------------------- |
| arg 0     | `int` | Bank slot number (`1..5`) |

Overwrites a mutable bank slot from its matching built-in preset source,
without requiring a full flash erase. This is intended for built-in slots
such as the water preset in slot 4.

**Serial:**

```
/script/bank/reseed 4
```

---

### `/script/builtin/list` — list built-in preset templates

No arguments. Prints the compiled-in preset templates as:
`BUILTIN_BANK <builtinId> <defaultSlot> <name>`.

**Serial:**

```
/script/builtin/list
```

---

### `/script/builtin/load` — copy a built-in preset into a bank slot

| Parameter | Type  | Description                        |
| --------- | ----- | ---------------------------------- |
| arg 0     | `int` | Built-in preset id                 |
| arg 1     | `int` | Optional target bank slot (`1..5`) |

Copies a compiled-in preset template into a mutable bank slot and installs it
immediately. If the second argument is omitted, the preset is copied into its
default slot.

**Serial:**

```
/script/builtin/load 1
/script/builtin/load 1 2
```

---

### `/script/delete` — delete a stored script file

| Parameter | Type     | Description                   |
| --------- | -------- | ----------------------------- |
| arg 0     | `string` | File name or path for `.game` |

Deletes a stored `.game` file from SPIFFS.

---

### `/script/files` — list stored script files

No arguments. Prints stored `.game` files from SPIFFS to serial.

---

### `/script/list` — list loaded runtime scripts

No arguments. Prints runtime-installed scripts to serial as
`RUNTIME_SCRIPT <id> <name> <stepCount>`.

---

### `/script/unload` — unload a runtime script by id

| Parameter | Type  | Description       |
| --------- | ----- | ----------------- |
| arg 0     | `int` | Runtime script id |

Removes one runtime-installed script from the in-memory registry.

---

### `/script/status` — show runtime script status

No arguments. Prints whether SPIFFS storage is available, the number of
staged upload bytes, and the number of runtime-installed scripts.

---

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
- Maximum line length: 255 characters.

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

| Define                       | Default                           | Description                                |
| ---------------------------- | --------------------------------- | ------------------------------------------ |
| `SCOREBOARD_NETWORK_BACKEND` | `SCOREBOARD_NETWORK_BACKEND_WIFI` | Network transport backend                  |
| `SCOREBOARD_WIFI_ENABLED`    | `1`                               | Enable or disable WiFi startup entirely    |
| `SCOREBOARD_WIFI_MODE`       | `SCOREBOARD_WIFI_MODE_AP`         | WiFi startup mode: off / station / AP      |
| `SCOREBOARD_HAS_M5UNIFIED`   | `0`                               | Enable M5Unified-specific hardware support |
| `SCOREBOARD_RS485_ENABLED`   | `0`                               | Enable RS485 text-command interface        |
| `SERIAL_CMD_ENABLED`         | `1`                               | Enable serial text command input           |
| `NEOPIXEL_PIN`               | `2`                               | GPIO for NeoPixel data line                |
| `NUM_DISPLAYS`               | `6`                               | Number of 32×8 tiles                       |
| `MATRIX_TILE_WIDTH`          | `32`                              | Pixels per tile (width)                    |
| `MATRIX_TILE_HEIGHT`         | `8`                               | Pixels per tile (height)                   |
| `DEFAULT_BRIGHTNESS`         | `10`                              | Start-up brightness (0-255)                |
| `OSC_PORT`                   | `9000`                            | UDP port for OSC messages                  |
| `SCROLL_STEP_MS`             | `50`                              | Milliseconds per scroll pixel step         |
| `WIFI_CONNECT_TIMEOUT_MS`    | `15000`                           | Station-mode WiFi connection timeout       |
| `WIFI_AP_SSID`               | `"Scoreboard"`                    | Access-point SSID                          |
| `WIFI_AP_PASS`               | `"12345678"`                      | Access-point password                      |
| `MATRIX_LAYOUT`              | see config.h                      | NeoMatrix wiring flags                     |
| `LED_TYPE`                   | `NEO_GRB + NEO_KHZ800`            | NeoPixel colour order + speed              |
| `ETH_CS_PIN`                 | `6`                               | SPI chip-select for W5500                  |
| `ETH_RST_PIN`                | `7`                               | Reset pin for W5500                        |
| `RS485_RX_PIN`               | `5`                               | RS485 RX pin                               |
| `RS485_TX_PIN`               | `6`                               | RS485 TX pin                               |
| `RS485_BAUD`                 | `115200`                          | RS485 baud rate                            |

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
