# Robot Game Scoreboard — Development Journal

## Platform

- **MCU**: ESP32-S3 (M5Stack AtomS3)
- **Display**: Adafruit NeoMatrix 32×8 (single tile)
- **Framework**: Arduino via PlatformIO, M5Unified
- **Build env**: `atoms3-wifi` (primary)
- **GUI**: `test/gui_control.py` — tkinter, requires conda Python (`/opt/anaconda3/envs/ML/bin/python`)

---

## Session: Early Work (prior sessions)

### Dead LED Compensation

- Dead LED at index 172, skipped during render in `DisplayManager::_render()`.

### Display Mode System

- Created `DisplayMode` enum: `DISPLAY_MODE_TEXT`, `DISPLAY_MODE_SCROLL_UP`, `DISPLAY_MODE_SCROLL_DOWN`, `DISPLAY_MODE_PARTICLES`.
- `ParticleModeConfig` / `DisplayModeConfig` structs for per-mode settings.
- Refactored `update()` with switch/case, extracted `_updateText()`, `_updateScroll()`, `_updateParticles()`.
- Moved IMU polling from `main.cpp` into `DisplayManager::update()`.

### GUI Control Panel

- Created `test/gui_control.py` — tkinter serial GUI for all display parameters.
- Added float parsing to serial command parser (peek for `.` → `strtof`).

---

## Session: Particle Physics (prior sessions)

### ParticleSystem Class

- Created `Vec2f.h` — header-only 2D float vector with operators, dot, length, normalize, clamp.
- Created `ParticleSystem.h/.cpp` — standalone physics engine decoupled from rendering.
- Initial integration: Störmer-Verlet (position-based).

### Velocity Verlet Migration

- **Problem**: Störmer-Verlet suffered "energy death" — particles sharing coordinates against walls caused `pos - oldPos → 0`, losing velocity permanently.
- **Fix**: Switched to Velocity Verlet with explicit `vel` field replacing `oldPos`. Particle struct: `pos`, `vel`, `accel`, `radius`, `color`.

### Glow Rendering

- Gaussian kernel splat per particle with additive RGB float accumulation buffer (`_glowBuf`).
- Configurable `glowSigma` for kernel width.
- `RenderStyle` enum to switch between point and glow.

### Temperature (Langevin Jitter)

- Added `temperature` config: random velocity kicks per substep for Brownian-like motion.

### Color Propagation Fix

- **Problem**: `setColor()` didn't update live particle colors.
- **Fix**: Iterate `_particleSys.particle(i).color = color565` when in particle mode.

### MAX_PARTICLES Increase

- Increased from 12 to 64.

---

## Session: Attraction & Gravity Toggle

### Inter-particle Attraction (van der Waals-like)

- Extended `_resolveCollisions()` with attraction range check + linear spring pull.
- For particles beyond contact but within `attractRange × diameter`: force = `attractStrength * (1 - t)` where `t` normalizes distance.
- New config fields: `attractStrength` (0 = off), `attractRange` (× sum-of-radii).

### Gravity Enable/Disable

- Added `gravityEnabled` bool to config.
- `_applyGravity()` conditionalised.
- GUI: checkbox next to gravity scale slider.

---

## Session: Timing Split (renderMs / substepMs)

### Problem

- `frameMs` controlled both physics and rendering — confusing name, and physics & render couldn't run at independent rates.

### Solution

- Renamed `frameMs` → `renderMs` (canvas redraw interval, cosmetic).
- Added `substepMs` (max physics sub-step for stability, replaces hardcoded `0.02f`).
- Default for both: 20ms.
- In `_updateParticles()`: two independent timestamps `_lastParticleStep` and `_lastParticleRender`.
  - Physics advances at real elapsed dt, sub-stepped at `substepMs`.
  - Render redraws at `renderMs` interval (can animate visuals even when physics is slow/paused).
- In `ParticleSystem::step()`: `float maxSub = _config.substepMs * 0.001f;`

### Files Changed

- `ParticleSystem.h`: `renderMs`, `substepMs` in config.
- `ParticleSystem.cpp`: configurable sub-step.
- `VirtualDisplay.h`: `ParticleModeConfig` fields + `_lastParticleRender`.
- `VirtualDisplay.cpp`: split timing logic.
- `OSCHandler.cpp`: renamed arg, added `substepMs` as arg 13.
- `gui_control.py`: renamed variable, added substep spinbox.

---

## Session: Damping & Restitution Labels

### Damping Exposed

- Was hardcoded at `0.998` in `toSystemConfig()` — too aggressive (~10%/sec velocity loss).
- Lowered default to `0.9998` (~1%/sec loss).
- Added `damping` field to `ParticleModeConfig`, wired through `toSystemConfig()`.
- OSC: arg index 14 (float).
- GUI: spinbox with 0.0001 increment (range 0.99–1.0).

### GUI Label Clarity

- "Elasticity:" → **"Restit (p-p):"** (coefficient of restitution, particle-particle).
- "Wall elast:" → **"Restit (wall):"** (coefficient of restitution, wall bounce).
- These are coefficients of restitution: 1.0 = perfectly elastic, 0.0 = fully inelastic.

---

## Session: Preset Save/Load

### JSON Presets (GUI-side)

- "Save to JSON…" / "Load from JSON…" buttons in new Presets frame.
- `_gather_params()` collects all GUI state into a dict.
- `_apply_params(d)` restores GUI from dict, then pushes all values to device.

### ESP32 NVS Persistence (device-side)

- "Save to ESP32" / "Load from ESP32" buttons → `/saveparams` and `/loadparams` serial commands.
- `DisplayManager::saveParams()` / `loadParams()` using ESP32 `Preferences` library (NVS flash).
- Persists: brightness, per-display color, mode, scroll settings, full `ParticleModeConfig` struct.
- Survives power cycles.

### Implementation

- Added `Preferences _prefs` member to `DisplayManager`.
- Added `_brightness` member to track current brightness for save.
- `saveParams()`: writes to NVS namespace `"disp"`, sets `"valid"` flag.
- `loadParams()`: reads back, applies via `setMode()` / `setColor()` / `setBrightness()`.

---

## Session: Particle Render Shapes

### Expanded RenderStyle Enum

From 2 values to 5:

| Value | Style  | Description                                           |
| ----- | ------ | ----------------------------------------------------- |
| 0     | Point  | Single pixel per particle                             |
| 1     | Square | Filled square, side = `2×radius+1`                    |
| 2     | Circle | Filled circle, r = radius (pixel fallback if r<1)     |
| 3     | Text   | Display text (`_text`) drawn centred on each particle |
| 4     | Glow   | Gaussian additive glow (unchanged)                    |

- Renamed `_renderParticlesPoint()` → `_renderParticlesShape()` with switch/case.
- Fixed broken `/ drawCircle(...)` syntax error from manual edit.
- **Text mode**: each particle carries a copy of `_text`, rendered at particle position using 6×8 GFX font. Text floats around with physics.
- GUI: 5 radio buttons in row 3.
- **Note**: Glow default changed from value `1` to `4`.

---

## Session: GUI Responsiveness Fix

### Problem

GUI would freeze briefly when clicking buttons or spinboxes. Sliders worked fine.

### Root Causes

1. **`ser.write()` blocking main thread** — synchronous serial write could stall if OS buffer full.
2. **`_serial_reader` busy-loop** — no sleep when no data available, eating CPU.
3. **Slider flood** — every slider tick fired `_send_particle_config()`, flooding serial.

### Fixes

1. **Write queue + background writer thread**: `_send()` enqueues to `queue.Queue`, drained by `_serial_writer` daemon thread. GUI never blocks on `ser.write()`.
2. **Reader sleep**: Added `time.sleep(0.02)` when `ser.in_waiting` is 0.
3. **Debounced slider sends**: `_send_particle_config()` uses `root.after(40ms)` to coalesce rapid drags. Labels update immediately; serial command fires once after 40ms pause.

---

## Session: Wave Interference Rendering

### Goal

Simulate optical interference patterns — particles act as coherent point sources.

### Physics

Each particle emits a wave. At pixel $(x,y)$, the complex amplitude from particle $i$ is:

$$A_i = E_i \cdot e^{-d^2/2\sigma^2} \cdot e^{i k d}$$

where $d = |\mathbf{r} - \mathbf{r}_i|$, $k = 2\pi/\lambda$.

The total field is the coherent sum $A_{total} = \sum_i A_i$, and the displayed intensity is $|A_{total}|^2 = \text{Re}^2 + \text{Im}^2$.

### First Attempt (Wrong)

- Multiplied real Gaussian by `cos(k·d)`, summed real amplitudes, took `abs()`.
- This is wrong: it treats the sum as incoherent — adding real values and taking absolute value doesn't produce proper constructive/destructive interference.

### Correct Implementation

- Expanded `_glowBuf` from 3 to **6 floats per pixel**: `Re_R, Im_R, Re_G, Im_G, Re_B, Im_B`.
- Accumulate complex amplitudes: `Re += E·envelope·cos(phase)`, `Im += E·envelope·sin(phase)`.
- Final intensity per channel: $I = \text{Re}^2 + \text{Im}^2$.
- Display: `sqrt(I)` for perceptual brightness mapping, clamped to [0,1].
- When `glowWavelength = 0`: falls back to plain additive glow (only uses Re channels, 3 floats/pixel).

### Parameters

- `glowWavelength` (float): 0 = pure glow, >0 = interference wavelength in pixels.
- Kernel radius: 5σ for interference (vs 3.5σ for plain glow) to show outer fringes.

### Memory

- Buffer: `MAX_GLOW_PIXELS * 6` = 1024 × 6 = 6144 floats = 24KB (actual display 32×8 = 256 pixels uses 6KB).

### OSC

- Arg index 15 for `glowWavelength`.

### GUI

- Slider "λ (px):" on glow row, range 0–10, default 0 (off).

---

## OSC Protocol Summary (as of latest)

```
/display/<N>/particles count renderMs gravityScale elasticity wallElasticity
    radius renderStyle glowSigma temperature attractStrength attractRange
    gravityEnabled substepMs damping glowWavelength
```

Arg indices (0-based):
| Idx | Name | Type | Default |
|-----|-----------------|-------|---------|
| 0 | count | int | 6 |
| 1 | renderMs | int | 20 |
| 2 | gravityScale | float | 18.0 |
| 3 | elasticity | float | 0.92 |
| 4 | wallElasticity | float | 0.78 |
| 5 | radius | float | 0.45 |
| 6 | renderStyle | int | 4 (glow)|
| 7 | glowSigma | float | 1.2 |
| 8 | temperature | float | 0.0 |
| 9 | attractStrength | float | 0.0 |
| 10 | attractRange | float | 3.0 |
| 11 | gravityEnabled | int | 1 |
| 12 | substepMs | int | 20 |
| 13 | damping | float | 0.9998 |
| 14 | glowWavelength | float | 0.0 |

All args optional — missing args keep current values.

Additional commands:

- `/saveparams` — persist to ESP32 NVS
- `/loadparams` — restore from ESP32 NVS
- `/brightness <n>` — set LED brightness
- `/display/<N>/color <r> <g> <b>` — set particle/text color
- `/display/<N>/mode <n>` — 0=text, 1=scroll_up, 2=scroll_down, 3=particles
