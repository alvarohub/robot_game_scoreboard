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
