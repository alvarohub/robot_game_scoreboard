# OSC Protocol Reference

The scoreboard listens for **OSC messages over UDP** on port **9000**
(configurable via `OSC_PORT`).

Displays are numbered **1 – 6** (1-based) in all OSC addresses.

---

## Message catalogue

### Set display text

```
/display/<N>       <string|int>
/display/<N>/text  <string>
```

Sets the text shown on display _N_.

- A **string** argument is shown as-is.
- An **int** argument is converted to its decimal representation.
- Text is automatically centred within the 32-pixel-wide panel.
- Maximum ~5 characters fit at the default font size; longer text is clipped.

**Examples:**

| Address           | Argument | Result                 |
| ----------------- | -------- | ---------------------- |
| `/display/1`      | `"1234"` | Display 1 shows `1234` |
| `/display/3`      | `42`     | Display 3 shows `42`   |
| `/display/6/text` | `"GO!"`  | Display 6 shows `GO!`  |

---

### Set display colour

```
/display/<N>/color  <int R> <int G> <int B>
```

Sets the text colour for display _N_. Values 0 – 255.
Default colour is white (255, 255, 255).

**Examples:**

| Address            | Arguments   | Result      |
| ------------------ | ----------- | ----------- |
| `/display/1/color` | `255 0 0`   | Red text    |
| `/display/2/color` | `0 255 0`   | Green text  |
| `/display/3/color` | `0 100 255` | Bluish text |

---

### Clear a single display

```
/display/<N>/clear
```

No arguments needed. Turns off all LEDs on display _N_.

---

### Set brightness

```
/brightness         <int>      (global)
/display/<N>/brightness <int>  (also sets global for now)
```

Sets the overall NeoPixel brightness (0 – 255).
Default is **20**. Higher values increase current draw significantly.

---

### Clear all displays

```
/clearall
/clear
```

Turns off all LEDs on all six displays.

---

### Set scroll mode

```
/display/<N>/scroll  <int mode>
/scroll              <int mode>       (all displays)
```

| Mode | Constant      | Behaviour                               |
| ---- | ------------- | --------------------------------------- |
| `0`  | `SCROLL_NONE` | Instant replacement (default)           |
| `1`  | `SCROLL_UP`   | Old text scrolls up, new enters below   |
| `2`  | `SCROLL_DOWN` | Old text scrolls down, new enters above |

Switching to mode `0` automatically flushes the scroll queue and shows
the last queued value instantly.

---

### Set scroll speed

```
/scrollspeed  <int ms>
```

Milliseconds per pixel step of the scroll animation (default `25`).
With 8-pixel-high tiles: `25 ms × 8 = 200 ms` per transition.
Minimum 1.

---

### Set scroll blank

```
/scrollblank  <int 0|1>
```

When `1`, a blank (all-dark) frame is shown between consecutive queued
scroll items. Prevents visual “bleeding” at high scroll speeds.
Default `0` (off).

---

### Manage scroll queue

```
/display/<N>/clearqueue          (one display)
/clearqueue                      (all displays)
```

Discards pending queued text without affecting the current display.

---

### Query animation state

```
/status
```

Replies on serial with `ANIMATING 0` or `ANIMATING 1`.
Firmware also emits `SCROLL_DONE <N>` (1-based) when each display
finishes a scroll animation.

---

## Particle system

### Layer controls

```
/display/<N>/particles/enable  <0|1>       (per display)
/display/<N>/text/enable       <0|1>       (per display)
/display/<N>/particles/brightness <int>    (0-255)
/display/<N>/text/brightness      <int>    (0-255)
/display/<N>/particles/color   <R> <G> <B>
```

Global forms: `/particles/enable`, `/text/enable`, `/particles/brightness`,
`/text/brightness`, `/particles/color`

---

### Convert to particles

```
/display/<N>/text2particles      (text → frozen particles)
/display/<N>/screen2particles    (screen pixels → particles with colours)
```

Both commands pause physics, enable glow rendering, and configure
**scaffold spring ON** (strength 1.0) + **collision OFF** so particles
stay anchored to their original positions. Global forms:
`/text2particles`, `/screen2particles`.

---

### Physics control

```
/display/<N>/particles/pause     <0|1>      (pause/resume physics)
/display/<N>/particles/restore              (reset to scaffold positions)
/display/<N>/particles/restorecolors        (restore original colours)
```

Global forms available for all three.

---

### Particle configuration

```
/display/<N>/particles  <arg0> <arg1> … <arg25>
```

Up to **26 positional arguments**, all optional (missing args keep
current values):

| Arg | Name              | Type    | Default |
| --- | ----------------- | ------- | ------- |
| 0   | count             | int     | 6       |
| 1   | renderMs          | int     | 20      |
| 2   | gravityScale      | float   | 18.0    |
| 3   | elasticity        | float   | 0.92    |
| 4   | wallElasticity    | float   | 0.78    |
| 5   | radius            | float   | 0.45    |
| 6   | renderStyle       | int     | 4       |
| 7   | glowSigma         | float   | 1.2     |
| 8   | temperature       | float   | 0.0     |
| 9   | attractStrength   | float   | 0.0     |
| 10  | attractRange      | float   | 3.0     |
| 11  | gravityEnabled    | int     | 1       |
| 12  | substepMs         | int     | 20      |
| 13  | damping           | float   | 0.9998  |
| 14  | glowWavelength    | float   | 0.0     |
| 15  | speedColor        | int     | 0       |
| 16  | springStrength    | float   | 0.0     |
| 17  | springRange       | float   | 5.0     |
| 18  | springEnabled     | int     | 0       |
| 19  | coulombStrength   | float   | 0.0     |
| 20  | coulombRange      | float   | 10.0    |
| 21  | coulombEnabled    | int     | 0       |
| 22  | scaffoldStrength  | float   | 0.0     |
| 23  | scaffoldRange     | float   | 10.0    |
| 24  | scaffoldEnabled   | int     | 0       |
| 25  | collisionEnabled  | int     | 1       |

Render styles: `0`=point, `1`=square, `2`=circle, `3`=text, `4`=glow.

---

### View transform

```
/display/<N>/particles/transform  <angle> <sx> <sy> <tx> <ty>
/display/<N>/particles/rotate     <angle>       (degrees)
/display/<N>/particles/scale      <sx> [sy]
/display/<N>/particles/translate  <tx> <ty>     (pixels)
/display/<N>/particles/resettransform
```

Render-time only — physics coordinates are unaffected.
Global forms available for rotate, scale, translate, resettransform.

---

### Text stack

```
/display/<N>/text/push  "STRING"
/display/<N>/text/pop
/display/<N>/text/set   <index> "STRING"
/display/<N>/text/clear
/display/<N>/text/list
```

Global forms: `/text/push`, `/text/pop`, `/text/set`, `/text/clear`, `/text/list`

---

### Miscellaneous

```
/scrollcontinuous <0|1>    — auto-cycle text stack in scroll mode
/defaults                  — reset all params to compiled defaults
```

---

## Sending from common tools

### Python (`python-osc`)

```python
from pythonosc import udp_client

client = udp_client.SimpleUDPClient("192.168.1.42", 9000)
client.send_message("/display/1", "1234")
client.send_message("/display/2/color", [255, 0, 0])
```

### Max/MSP

```
[udpsend 192.168.1.42 9000]
    |
[prepend /display/1]
    |
[1234(
```

### TouchOSC / Open Stage Control

Configure a fader or button to send to the appropriate `/display/N`
address on the board's IP and port 9000.

### Pure Data

```
[netsend -u -b]
|
[oscformat /display/1]
|
[1234(
```

### Command line (`oscsend` from liblo)

```bash
oscsend 192.168.1.42 9000 /display/1 s "1234"
oscsend 192.168.1.42 9000 /display/2/color iii 255 0 0
oscsend 192.168.1.42 9000 /brightness i 40
oscsend 192.168.1.42 9000 /clearall
```
