# Tests

This file collects manual and scripted tests for the robot game scoreboard. Add new tests here as they become useful.

## Setup

Use the AtomS3 WiFi/serial firmware build unless a test says otherwise.

Build firmware:

```bash
pio run -e atoms3-wifi
```

Upload firmware:

```bash
pio run -e atoms3-wifi --target upload
```

Python serial tests require `pyserial`:

```bash
pip install pyserial
```

If PlatformIO Monitor or another serial program is open, close it before running a serial test. Only one program can own the USB serial port at a time.

## Serial Count Test: All Displays

Script: `test/demo_serial_count_all.py`

Purpose: verify that direct serial text updates can drive all six displays continuously without using WiFi or firmware-side animation loops.

What it does:

- Auto-detects the USB serial port, unless a port is provided.
- Enables `/serial/quiet 1` while running, to avoid per-command debug output competing with the test stream.
- Initializes all six displays for text output.
- Stops and deselects animations, clears text history, clears particles, disables particles, resets particle transforms, and forces text mode.
- Sets a different text color on each display.
- Sends the same four-digit value to displays 1-6 as one serial batch per frame.
- Counts `0000 -> 9999 -> 0000` continuously.

Run with auto-detected port:

```bash
python demo_serial_count_all.py
```

Run with an explicit port:

```bash
python demo_serial_count_all.py cu.usbmodem2101
```

Run with a custom update period:

```bash
python demo_serial_count_all.py cu.usbmodem2101 --period-ms 100
```

Run a short finite smoke test:

```bash
python demo_serial_count_all.py cu.usbmodem2101 --period-ms 100 --frames 50
```

Expected result:

- All six displays show the same four-digit count.
- Each display has a distinct text color.
- The count goes up to `9999`, then down to `0000`, then repeats.
- Stop the test with `Ctrl+C`.

Failure signs to watch for:

- One display freezes while others continue.
- One display updates out of order or skips many values compared with the others.
- Text disappears after another mode or animation was previously active.
- Serial errors or disconnects appear in the terminal.

Timing note:

- The full chain is 6 displays x 32 x 8 = 1536 NeoPixels.
- At WS2812/NeoPixel timing, one full-chain `show()` is about 46 ms before rendering and command parsing overhead.
- Therefore all-display updates near 20 Hz are already at the physical refresh ceiling. Use 100 ms first, then try 75 ms and 50 ms. Periods below 50 ms are expected to jitter or backlog unless the LED output backend is changed.
- If the image appears shifted, horizontally broken, or color-garbled for a single refresh, suspect LED data timing or power/ground integrity as well as serial backlog. A missed NeoPixel bit shifts every later LED in the chain until the next frame.

## Serial Flash Test: All Displays

Script: `test/demo_serial_flash_all.py`

Purpose: verify that direct serial fill commands can flash every full display between two colors without using WiFi or firmware-side animation loops.

What it does:

- Auto-detects the USB serial port, unless a port is provided.
- Enables `/serial/quiet 1` while running.
- Stops and deselects animations, clears text history, clears particles, disables particles, resets particle transforms, and disables text.
- Sends six `/display/N/fill R G B` commands as one serial batch per frame.
- Alternates between two selected colors continuously.

Run with default red/blue colors:

```bash
python demo_serial_flash_all.py
```

Run with an explicit port and period:

```bash
python demo_serial_flash_all.py cu.usbmodem2101 --period-ms 100
```

Run with custom colors:

```bash
python demo_serial_flash_all.py cu.usbmodem2101 --a 255,255,255 --b 0,0,0 --period-ms 100
```

Run a short finite smoke test:

```bash
python demo_serial_flash_all.py cu.usbmodem2101 --period-ms 100 --frames 50
```

Expected result:

- All six displays flash together between the two selected colors.
- No text or particles should reappear during or after the test.
- Stop the test with `Ctrl+C`.

Timing note:

- This test also refreshes the full 1536-pixel chain every frame.
- Use the same practical timing expectations as the serial count test: start at 100 ms, then try 75 ms and 50 ms. Faster periods are stress tests for the current WS2812 backend.

## WiFi GUI Display Test

Source: embedded web UI in `src/WebInterface.h`

Purpose: verify browser-over-WiFi command delivery to one selected display.

Procedure:

1. Connect to the scoreboard WiFi/AP or the configured network.
2. Open the scoreboard web UI in a browser.
3. Choose a target display.
4. In **Display Test**, choose an update period.
5. Start **Count 9999** or **Colour Flash**.

Expected result:

- The active test button turns red.
- Count test updates the selected display repeatedly.
- Colour flash alternates the full display between the two selected colors.
- If a command fails, the relevant test button turns error-red and the test stops.

Notes:

- The browser sends every value. The microcontroller does not run the counter.
- The GUI serializes test commands to avoid browser request bursts.
- Starting an animation from the GUI stops the active display test first.
- Direct `/display/N/text` and `/display/N/fill` commands stop any active animation on that display before taking over.

## WiFi Disable Test

Command:

```text
/wifi/off
```

Purpose: disable WiFi until reboot, useful when checking whether display behavior changes without WiFi traffic or RF activity.

Ways to run:

- Press **Disable WiFi** in the web GUI.
- Send `/wifi/off` over USB serial.

Expected result:

- WiFi access point/network service stops.
- OSC UDP over WiFi stops.
- Reboot the controller to re-enable WiFi.

## Adding More Tests

For each new test, add:

- Test name.
- Script or command path.
- Purpose.
- Setup requirements.
- Steps to run.
- Expected result.
- Failure signs to watch for.
