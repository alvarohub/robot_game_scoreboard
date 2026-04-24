# Full Control Reference

This file is the detailed reference for the scoreboard command surface as it is
implemented in firmware.

It complements `docs/command_reference.md`.
Use this file when you need:

- the full list of implemented commands
- argument order and examples
- text and particle parameter ranges
- `.game` runtime script keywords and limits

## Transports

All four control paths share the same command set:

| Interface         | Transport     | Notes                                  |
| ----------------- | ------------- | -------------------------------------- |
| OSC over WiFi     | UDP port 9000 | Same addresses and arguments as serial |
| OSC over Ethernet | UDP port 9000 | Same handler as WiFi                   |
| USB serial        | 115200 baud   | Easiest for bench testing              |
| RS485 serial      | 115200 baud   | Same text protocol as USB serial       |

Display numbers are always `1..6` in commands.

## Command Syntax

Serial and RS485 use plain text lines:

```text
/display/1/text "HELLO"
/display/1/color 255 0 0
/brightness 20
```

OSC uses the same address paths, with OSC-typed arguments.

Python OSC example:

```python
from pythonosc import udp_client

client = udp_client.SimpleUDPClient("192.168.1.42", 9000)
client.send_message("/display/1/text", "HELLO")
client.send_message("/display/1/color", [255, 0, 0])
client.send_message("/brightness", 20)
```

## Important Range Notes

- Many integer parameters are stored as `uint8_t` in firmware. In practice, keep them in `0..255`.
- Values above `255` are not safely meaningful for `uint8_t` fields and may wrap.
- Only a few fields are actively clamped by firmware.
- This reference therefore lists two kinds of ranges:

| Label             | Meaning                                           |
| ----------------- | ------------------------------------------------- |
| Firmware range    | What the current code accepts or enforces         |
| Recommended range | What is practical and stable on the 32x8 displays |

## Command Groups

### Display Text And Layer Commands

| Command                        | Args                                          | Example                          |
| ------------------------------ | --------------------------------------------- | -------------------------------- |
| `/display/<N>`                 | `string` or `int`                             | `/display/1 42`                  |
| `/display/<N>/text`            | `string`                                      | `/display/1/text "HI"`           |
| `/display/<N>/mode`            | `text`, `scroll_up`, `scroll_down`, or `0..2` | `/display/1/mode scroll_up`      |
| `/display/<N>/scroll`          | `0..2`                                        | `/display/1/scroll 2`            |
| `/display/<N>/color`           | `r g b`                                       | `/display/1/color 255 80 0`      |
| `/display/<N>/brightness`      | `0..255`                                      | `/display/1/brightness 30`       |
| `/display/<N>/clear`           | none                                          | `/display/1/clear`               |
| `/display/<N>/clearqueue`      | none                                          | `/display/1/clearqueue`          |
| `/display/<N>/text/enable`     | `0` or `1`                                    | `/display/1/text/enable 1`       |
| `/display/<N>/text/brightness` | `0..255`                                      | `/display/1/text/brightness 180` |
| `/display/<N>/text/push`       | `string`                                      | `/display/1/text/push "READY"`   |
| `/display/<N>/text/pop`        | none                                          | `/display/1/text/pop`            |
| `/display/<N>/text/set`        | `index string`                                | `/display/1/text/set 0 "READY"`  |
| `/display/<N>/text/clear`      | none                                          | `/display/1/text/clear`          |
| `/display/<N>/text/list`       | none                                          | `/display/1/text/list`           |

### Particle Commands Per Display

| Command                                 | Args                           | Example                                                                                                                |
| --------------------------------------- | ------------------------------ | ---------------------------------------------------------------------------------------------------------------------- |
| `/display/<N>/particles/enable`         | `0` or `1`                     | `/display/1/particles/enable 1`                                                                                        |
| `/display/<N>/particles/brightness`     | `0..255`                       | `/display/1/particles/brightness 255`                                                                                  |
| `/display/<N>/particles/color`          | `r g b`                        | `/display/1/particles/color 0 100 255`                                                                                 |
| `/display/<N>/particles`                | up to 26 positional args       | `/display/1/particles 24 20 18.0 0.92 0.78 0.45 4 1.2 0.0 0.0 3.0 1 20 0.9998 0.0 0 0.0 5.0 0 0.0 10.0 0 0.0 10.0 0 1` |
| `/display/<N>/particles/pause`          | `0` or `1`                     | `/display/1/particles/pause 1`                                                                                         |
| `/display/<N>/particles/restore`        | none                           | `/display/1/particles/restore`                                                                                         |
| `/display/<N>/particles/restorecolors`  | none                           | `/display/1/particles/restorecolors`                                                                                   |
| `/display/<N>/text2particles`           | none                           | `/display/1/text2particles`                                                                                            |
| `/display/<N>/screen2particles`         | none                           | `/display/1/screen2particles`                                                                                          |
| `/display/<N>/particles/transform`      | `angleDeg scaleX scaleY tx ty` | `/display/1/particles/transform 20 1.0 1.0 2 0`                                                                        |
| `/display/<N>/particles/rotate`         | `angleDeg`                     | `/display/1/particles/rotate 45`                                                                                       |
| `/display/<N>/particles/scale`          | `sx [sy]`                      | `/display/1/particles/scale 1.5 0.8`                                                                                   |
| `/display/<N>/particles/translate`      | `tx ty`                        | `/display/1/particles/translate 3 -1`                                                                                  |
| `/display/<N>/particles/resettransform` | none                           | `/display/1/particles/resettransform`                                                                                  |

### Animation Commands

| Command                       | Args | Example                     |
| ----------------------------- | ---- | --------------------------- |
| `/display/<N>/animation`      | `id` | `/display/1/animation 1`    |
| `/display/<N>/animation/stop` | none | `/display/1/animation/stop` |
| `/animation`                  | `id` | `/animation 1`              |
| `/animation/stop`             | none | `/animation/stop`           |

Animation id `0` means off.

### Global Display Commands

| Command                     | Args                                          | Example                     |
| --------------------------- | --------------------------------------------- | --------------------------- |
| `/brightness`               | `0..255`                                      | `/brightness 20`            |
| `/mode`                     | `text`, `scroll_up`, `scroll_down`, or `0..2` | `/mode text`                |
| `/scroll`                   | `0..2`                                        | `/scroll 1`                 |
| `/scrollspeed`              | `1..255` practical                            | `/scrollspeed 25`           |
| `/scrollcontinuous`         | `0` or `1`                                    | `/scrollcontinuous 1`       |
| `/text/enable`              | `0` or `1`                                    | `/text/enable 1`            |
| `/text/brightness`          | `0..255`                                      | `/text/brightness 200`      |
| `/particles/enable`         | `0` or `1`                                    | `/particles/enable 1`       |
| `/particles/brightness`     | `0..255`                                      | `/particles/brightness 180` |
| `/particles/color`          | `r g b`                                       | `/particles/color 0 80 255` |
| `/text/push`                | `string`                                      | `/text/push "GO"`           |
| `/text/pop`                 | none                                          | `/text/pop`                 |
| `/text/set`                 | `index string`                                | `/text/set 0 "READY"`       |
| `/text/clear`               | none                                          | `/text/clear`               |
| `/text/list`                | none                                          | `/text/list`                |
| `/text2particles`           | none                                          | `/text2particles`           |
| `/screen2particles`         | none                                          | `/screen2particles`         |
| `/particles/pause`          | `0` or `1`                                    | `/particles/pause 0`        |
| `/particles/restore`        | none                                          | `/particles/restore`        |
| `/particles/restorecolors`  | none                                          | `/particles/restorecolors`  |
| `/particles/rotate`         | `angleDeg`                                    | `/particles/rotate 15`      |
| `/particles/scale`          | `sx [sy]`                                     | `/particles/scale 1.2`      |
| `/particles/translate`      | `tx ty`                                       | `/particles/translate 2 0`  |
| `/particles/resettransform` | none                                          | `/particles/resettransform` |
| `/clearqueue`               | none                                          | `/clearqueue`               |
| `/clearall`                 | none                                          | `/clearall`                 |
| `/clear`                    | none                                          | `/clear`                    |
| `/defaults`                 | none                                          | `/defaults`                 |
| `/status`                   | none                                          | `/status`                   |
| `/rasterscan`               | optional `delayMs`                            | `/rasterscan 20`            |

### Preset And NVS Commands

| Command        | Args            | Example          |
| -------------- | --------------- | ---------------- |
| `/saveparams`  | optional `bank` | `/saveparams 2`  |
| `/save`        | optional `bank` | `/save 2`        |
| `/loadparams`  | optional `bank` | `/loadparams 2`  |
| `/load`        | optional `bank` | `/load 2`        |
| `/startupbank` | optional `bank` | `/startupbank 3` |

Bank range is `1..5`.

### Runtime Script Transport Commands

| Command          | Args          | Example                          |
| ---------------- | ------------- | -------------------------------- |
| `/script/begin`  | none          | `/script/begin`                  |
| `/script/append` | `string line` | `/script/append "step intro"`    |
| `/script/commit` | none          | `/script/commit`                 |
| `/script/cancel` | none          | `/script/cancel`                 |
| `/script/save`   | `file`        | `/script/save "demo1_runtime"`   |
| `/script/load`   | `file`        | `/script/load "demo1_runtime"`   |
| `/script/delete` | `file`        | `/script/delete "demo1_runtime"` |
| `/script/files`  | none          | `/script/files`                  |
| `/script/list`   | none          | `/script/list`                   |
| `/script/unload` | `id`          | `/script/unload 1`               |
| `/script/status` | none          | `/script/status`                 |

Typical live upload sequence:

```text
/script/begin
/script/append "id 1"
/script/append "name demo"
/script/append "step intro"
/script/append "wait 1000"
/script/append "text on"
/script/commit
/display/1/animation 1
/display/1/text "GO"
```

The helper uploader already automates this:

```bash
python3 test/load_game_serial.py games/demo1.game --display 1 --trigger-text GO
```

## Text Parameter Reference

### Text Storage And Capacity

| Item                             | Value                    | Notes                                                     |
| -------------------------------- | ------------------------ | --------------------------------------------------------- |
| Displays                         | `1..6`                   | Fixed hardware layout                                     |
| Text stack entries per display   | `8`                      | `TEXT_STACK_MAX`                                          |
| Stored characters per text entry | up to `31` visible chars | `TEXT_MAX_LEN = 32`, one byte is the terminator           |
| Scroll queue size                | `10`                     | Pending scroll items                                      |
| Tile size                        | `32x8` pixels            | Default font usually fits about 4 to 5 characters cleanly |

### Text-Related Parameters

| Parameter           | Used By                                                        | Firmware range                                | Recommended range                  | Default       | Notes                                                                                               |
| ------------------- | -------------------------------------------------------------- | --------------------------------------------- | ---------------------------------- | ------------- | --------------------------------------------------------------------------------------------------- |
| `display`           | `/display/<N>/...`                                             | `1..6`                                        | `1..6`                             | none          | Out-of-range displays are ignored                                                                   |
| `text string`       | `/display/<N>`, `/display/<N>/text`, `/text/push`, `/text/set` | up to 31 stored chars                         | 1 to 8 chars for clear readability | empty         | Longer text is still stored, but the 32-pixel width is the real visual limit                        |
| `text index`        | `/text/set`, `/display/<N>/text/set`                           | `0..7` meaningful                             | `0..7`                             | `0`           | `textSet` can fill sparse indices up to 7                                                           |
| `mode`              | `/mode`, `/display/<N>/mode`                                   | `text`, `scroll_up`, `scroll_down`, or `0..2` | same                               | `text`        | There is no standalone `particles` display mode in the current enum; particles are a separate layer |
| `scroll mode`       | `/scroll`, `/display/<N>/scroll`                               | `0..2`                                        | same                               | `0`           | `0=text`, `1=scroll_up`, `2=scroll_down`                                                            |
| `scroll speed`      | `/scrollspeed`                                                 | minimum `1`, effectively `1..255`             | `10..80`                           | `50`          | Stored as `uint8_t`; firmware forces `0` to `1`                                                     |
| `scroll continuous` | `/scrollcontinuous`                                            | `0` or `1`                                    | same                               | `0`           | Cycles through the text stack                                                                       |
| `text brightness`   | `/text/brightness`, `/display/<N>/text/brightness`             | `0..255`                                      | `32..255`                          | `255`         | Layer brightness                                                                                    |
| `text enable`       | `/text/enable`, `/display/<N>/text/enable`                     | `0` or `1`                                    | same                               | `1`           | Turns only the text layer on or off                                                                 |
| `text color`        | `/display/<N>/color`                                           | each channel `0..255`                         | same                               | `255 255 255` | RGB is converted to RGB565                                                                          |
| `global brightness` | `/brightness`, `/display/<N>/brightness`                       | `0..255`                                      | `5..80` for normal use             | `10`          | Affects the whole physical strip                                                                    |

### Text Examples

Immediate text:

```text
/display/1/text "123"
```

Scroll new values upward:

```text
/display/1/scroll 1
/display/1/text "124"
```

Build a per-display text stack:

```text
/display/1/text/push "READY"
/display/1/text/push "SET"
/display/1/text/push "GO"
/display/1/text/list
```

Enable continuous cycling through the stack:

```text
/display/1/scroll 1
/scrollcontinuous 1
```

## Particle Parameter Reference

### Bulk Positional Order For `/display/<N>/particles`

The per-display particle config command uses this exact argument order:

```text
/display/<N>/particles \
  count renderMs gravityScale elasticity wallElasticity \
  radius renderStyle glowSigma temperature attractStrength attractRange \
  gravityEnabled substepMs damping glowWavelength \
  speedColor springStrength springRange springEnabled \
  coulombStrength coulombRange coulombEnabled \
  scaffoldStrength scaffoldRange scaffoldEnabled \
  collisionEnabled
```

Missing trailing arguments keep the current value.

### Particle Fields, Defaults And Ranges

| Field              | Positional arg | `.game` field      | Type  | Default  | Firmware range                           | Recommended range        | Notes                                                                  |
| ------------------ | -------------- | ------------------ | ----- | -------- | ---------------------------------------- | ------------------------ | ---------------------------------------------------------------------- |
| `count`            | 0              | `count`            | int   | `6`      | practical `0..255` from command path     | `4..64` for live control | ParticleSystem hard cap is 256, but the live config field is `uint8_t` |
| `renderMs`         | 1              | `renderMs`         | int   | `20`     | minimum `1`, practical `1..255`          | `10..40`                 | Cosmetic redraw interval                                               |
| `gravityScale`     | 2              | `gravityScale`     | float | `18.0`   | any float                                | `0..40`                  | Multiplies IMU gravity                                                 |
| `elasticity`       | 3              | `elasticity`       | float | `0.92`   | any float                                | `0.0..1.0`               | Particle-particle bounce                                               |
| `wallElasticity`   | 4              | `wallElasticity`   | float | `0.78`   | any float                                | `0.0..1.0`               | Wall bounce                                                            |
| `radius`           | 5              | `radius`           | float | `0.45`   | any positive float works best            | `0.2..1.5`               | Affects rendering and collisions                                       |
| `renderStyle`      | 6              | `renderStyle`      | enum  | `4`      | `0..4`                                   | `0..4`                   | `0=point`, `1=square`, `2=circle`, `3=text`, `4=glow`                  |
| `glowSigma`        | 7              | `glowSigma`        | float | `1.2`    | any positive float works best            | `0.4..2.5`               | Relevant mainly for glow style                                         |
| `temperature`      | 8              | `temperature`      | float | `0.0`    | any float                                | `0.0..1.5`               | Langevin jitter                                                        |
| `attractStrength`  | 9              | `attractStrength`  | float | `0.0`    | any float                                | `0.0..1.0`               | Inter-particle attraction                                              |
| `attractRange`     | 10             | `attractRange`     | float | `3.0`    | any positive float                       | `1.5..8.0`               | Measured in multiples of sum-of-radii                                  |
| `gravityEnabled`   | 11             | `gravityEnabled`   | bool  | `true`   | `0/1`, `true/false`, `on/off` in scripts | same                     | Bulk command uses `0/1`; scripts accept friendly booleans              |
| `substepMs`        | 12             | `substepMs`        | int   | `20`     | minimum `1`, practical `1..255`          | `5..30`                  | Physics stability/CPU tradeoff                                         |
| `damping`          | 13             | `damping`          | float | `0.9998` | any float                                | `0.9900..1.0000`         | Per-substep velocity multiplier                                        |
| `glowWavelength`   | 14             | `glowWavelength`   | float | `0.0`    | any float                                | `0.0..8.0`               | `0` means pure glow                                                    |
| `speedColor`       | 15             | `speedColor`       | bool  | `false`  | `0/1` or bool token in scripts           | same                     | Colors particles by velocity                                           |
| `springStrength`   | 16             | `springStrength`   | float | `0.0`    | any float                                | `-5.0..5.0`              | Charge-dependent linear force                                          |
| `springRange`      | 17             | `springRange`      | float | `5.0`    | any positive float                       | `0.5..20.0`              | Absolute pixels                                                        |
| `springEnabled`    | 18             | `springEnabled`    | bool  | `false`  | `0/1` or bool token in scripts           | same                     |                                                                        |
| `coulombStrength`  | 19             | `coulombStrength`  | float | `0.0`    | any float                                | `-5.0..5.0`              | Charge-dependent inverse-square force                                  |
| `coulombRange`     | 20             | `coulombRange`     | float | `10.0`   | any positive float                       | `0.5..30.0`              | Absolute pixels                                                        |
| `coulombEnabled`   | 21             | `coulombEnabled`   | bool  | `false`  | `0/1` or bool token in scripts           | same                     |                                                                        |
| `scaffoldStrength` | 22             | `scaffoldStrength` | float | `0.0`    | any float                                | `0.0..5.0`               | Pull back toward saved origin positions                                |
| `scaffoldRange`    | 23             | `scaffoldRange`    | float | `10.0`   | any positive float                       | `0.5..30.0`              | Absolute pixels                                                        |
| `scaffoldEnabled`  | 24             | `scaffoldEnabled`  | bool  | `false`  | `0/1` or bool token in scripts           | same                     |                                                                        |
| `collisionEnabled` | 25             | `collisionEnabled` | bool  | `true`   | `0/1` or bool token in scripts           | same                     | Hard-sphere correction and bounce                                      |

### Particle Fields Available Only Through Dedicated Commands Or `.game`

| Field                   | Where                                                                                              | Default | Range            | Notes                                              |
| ----------------------- | -------------------------------------------------------------------------------------------------- | ------- | ---------------- | -------------------------------------------------- |
| `physicsPaused`         | `/particles/pause`, `/display/<N>/particles/pause`, script `physics`, script field `physicsPaused` | `false` | bool             | Not part of the 26-arg positional particle command |
| `textIndex`             | script field `textIndex`                                                                           | `0`     | practical `0..7` | Used by `RENDER_TEXT` to select a text-stack entry |
| `replaceParticleConfig` | script keyword `replace_particle_config`                                                           | `false` | bool             | Runtime script only                                |

### Render Style Enum

| Value | Name     | Meaning                              |
| ----- | -------- | ------------------------------------ |
| `0`   | `point`  | One pixel per particle               |
| `1`   | `square` | Filled square                        |
| `2`   | `circle` | Filled circle                        |
| `3`   | `text`   | Uses text glyphs from the text stack |
| `4`   | `glow`   | Gaussian additive glow               |

### Particle Transform Parameters

These are render-time only. They do not change the physics positions.

| Parameter          | Command                  | Firmware range | Recommended range | Default |
| ------------------ | ------------------------ | -------------- | ----------------- | ------- |
| `angleDeg`         | `rotate`, `transform`    | any float      | `-180..180`       | `0`     |
| `scaleX`, `scaleY` | `scale`, `transform`     | any float      | `0.1..4.0`        | `1.0`   |
| `tx`               | `translate`, `transform` | any float      | `-16..16`         | `0.0`   |
| `ty`               | `translate`, `transform` | any float      | `-8..8`           | `0.0`   |

### Particle Examples

Light gravity glow cloud:

```text
/display/1/particles/enable 1
/display/1/particles/color 0 120 255
/display/1/particles 24 20 10.0 0.92 0.78 0.45 4 1.2 0.0 0.0 3.0 1 20 0.9998 0.0 0 0.0 5.0 0 0.0 10.0 0 0.0 10.0 0 1
```

Exploding text:

```text
/display/1/text "HELLO"
/display/1/text2particles
/display/1/particles 40 20 18.0 0.92 0.78 0.35 4 0.6 1.0 0.0 3.0 1 20 0.9998 0.0 0 0.0 5.0 0 0.0 10.0 0 0.0 10.0 0 1
/display/1/particles/pause 0
```

Wiggling text that holds shape:

```text
/display/1/text "HELLO"
/display/1/text2particles
/display/1/particles 40 20 0.0 0.92 0.78 0.35 4 0.6 0.2 0.0 3.0 0 20 0.9998 0.0 0 0.0 5.0 0 0.0 10.0 0 1.2 10.0 1 0
/display/1/particles/pause 0
```

## Runtime `.game` Script Reference

Runtime scripts are parsed on the ESP32 and installed as animation scripts.

### Script Upload Limits

| Limit                                      | Value                 | Notes                                         |
| ------------------------------------------ | --------------------- | --------------------------------------------- |
| Staged source size                         | `4096` bytes          | Total buffered source text                    |
| Runtime script slots                       | `8`                   | Installed runtime scripts at once             |
| Script id                                  | `1..255`              | `0` is invalid                                |
| Script name length                         | `63` chars            | One extra byte is the terminator              |
| Steps per script                           | `64`                  | Hard parser limit                             |
| Labels per script                          | `64`                  | Hard parser limit                             |
| Goto statements per script                 | `64`                  | Hard parser limit                             |
| Transport line length via `/script/append` | about `159` chars     | Current handler copies into a 160-byte buffer |
| Parsed source line length                  | less than `192` chars | Parser rejects longer lines                   |

### Script Keywords

| Keyword                   | Arguments                               | Example                         | Notes                                                                                                                       |
| ------------------------- | --------------------------------------- | ------------------------------- | --------------------------------------------------------------------------------------------------------------------------- | ------------ |
| `id`                      | `1..255`                                | `id 1`                          | Required                                                                                                                    |
| `name`                    | free text                               | `name burst_demo`               | Optional but strongly recommended                                                                                           |
| `step`                    | optional label                          | `step intro`                    | Starts a new animation step                                                                                                 |
| `end`                     | none                                    | `end`                           | Ends the current step block                                                                                                 |
| `wait`                    | milliseconds                            | `wait 2000`                     | Per-step delay                                                                                                              |
| `mode`                    | `text`, `scroll_up`, `scroll_down`      | `mode text`                     | Same display mode enum as runtime commands                                                                                  |
| `text`                    | bool-like token or legacy shorthand     | `text off` or `text hello`      | Controls the text layer state. A non-bool token currently behaves like `text on`; it does not store per-step text payloads. |
| `particles`               | bool-like token                         | `particles on`                  | Toggles particle layer                                                                                                      |
| `text_to_particles`       | none                                    | `text_to_particles`             | Converts text to particles                                                                                                  |
| `screen_to_particles`     | none                                    | `screen_to_particles`           | Captures current screen to particles                                                                                        |
| `physics`                 | `paused`, `running`, or bool-like token | `physics running`               | Controls particle motion                                                                                                    |
| `replace_particle_config` | bool-like token                         | `replace_particle_config true`  | Runtime script only                                                                                                         |
| `particle`                | `field value`                           | `particle scaffoldStrength 2.2` | Patch one particle parameter                                                                                                |
| `goto`                    | `label [forever                         | repeat N]`                      | `goto intro forever`                                                                                                        | Flow control |

Accepted bool-like tokens in scripts:

```text
1 0
true false
on off
yes no
enabled disabled
```

### Script Example

Valid minimal runtime-script style:

```text
id 1
name text_hold_burst_reassemble

step intro
wait 2000
mode text
text on
particles off

step burst
wait 5000
text_to_particles
text off
particle gravityEnabled true
particle collisionEnabled true
particle scaffoldEnabled false
particle scaffoldStrength 0.0
particle temperature 1.0
particles on
physics running

step rebuild
wait 2500
particle gravityEnabled false
particle collisionEnabled false
particle scaffoldEnabled true
particle scaffoldStrength 2.2
particle scaffoldRange 10.0
particle temperature 0.0
```

At the moment, `.game` steps do not carry an arbitrary text payload. `text hello`
is accepted as compatibility shorthand for `text on`, but the `hello` part is not
stored in the animation step. Set the display text separately with a normal text
command, then use the runtime script to animate layer state and particle behavior
around that text.

### Script File Operations

Save the last staged script to SPIFFS:

```text
/script/save "demo1_runtime"
```

Load it later:

```text
/script/load "demo1_runtime"
```

List stored files and loaded scripts:

```text
/script/files
/script/list
/script/status
```

## Practical Defaults

These are the main compiled defaults that are useful to remember:

| Setting             | Default  |
| ------------------- | -------- |
| Global brightness   | `10`     |
| Scroll step         | `50 ms`  |
| Text brightness     | `255`    |
| Particle brightness | `255`    |
| Particle count      | `6`      |
| Particle renderMs   | `20`     |
| Particle substepMs  | `20`     |
| Radius              | `0.45`   |
| Gravity scale       | `18.0`   |
| Elasticity          | `0.92`   |
| Wall elasticity     | `0.78`   |
| Damping             | `0.9998` |
| Temperature         | `0.0`    |
| Attract strength    | `0.0`    |
| Attract range       | `3.0`    |
| Spring enabled      | `false`  |
| Coulomb enabled     | `false`  |
| Scaffold enabled    | `false`  |
| Render style        | `glow`   |
| Glow sigma          | `1.2`    |
| Glow wavelength     | `0.0`    |
| Speed color         | `false`  |

## Current Caveats

- The current detailed particle bulk-config command exists only per display. There is no global `/particles` command that sets all 26 fields in one call.
- The live uploader `test/load_game_serial.py` currently rejects script source lines containing a literal double quote.
- The text and particle layers are independent. In the current firmware, particles are not a separate `DisplayMode`; they are enabled as an overlay.
- `text2particles` and `screen2particles` temporarily overwrite several particle settings to produce a stable starting state.
