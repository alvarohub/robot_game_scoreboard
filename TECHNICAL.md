# Technical Notes

This file collects implementation notes and practical limits that matter when building tests, installations, and future hardware revisions.

## Full-Chain Refresh Limit

The current display chain is:

- 6 logical displays
- 32 x 8 pixels per display
- 1536 WS2812/NeoPixel LEDs total
- one serial LED data line

WS2812-style LEDs use a fixed one-wire protocol. Each LED consumes about 30 us of data, so a full-chain update takes approximately:

```text
1536 pixels x 30 us = 46 ms
```

That is only the LED transfer time. The firmware also needs time for serial/OSC parsing, text rendering, canvas compositing, particle/animation bookkeeping, and main-loop scheduling. In practice, all-display updates were stable until about 16 Hz:

```text
1 / 16 Hz = 62.5 ms per frame
```

That measured result is consistent with a 46 ms hard LED transfer plus roughly 10-20 ms of overhead and safety margin.

## What Was Causing the Test Glitches

The serial count test originally exposed several overlapping issues:

- Direct `/display/N/text` commands stopped the running animation, then restarted the selected animation slot. This could make the display return to particles after the test stopped.
- The test did not clear every hidden state at startup. Text history, selected animations, particle state, physics, and transforms could survive from previous experiments.
- The firmware printed debug output for every incoming serial command. At six commands per frame, this added avoidable USB serial traffic and timing pressure.
- Fast update periods approached the physical WS2812 refresh ceiling, leaving too little time for parsing and rendering between LED pushes.

The current serial tests now:

- enable `/serial/quiet 1` while streaming
- stop and deselect animations
- clear queues and text history
- clear/disable particles and pause physics
- reset particle transforms
- force text mode or fill mode before sending the test frames

This does not make the LED chain faster; it removes avoidable interference around the real refresh limit.

## Serial vs LED Refresh

USB serial at 115200 baud is not the main bottleneck for the current tests. A typical text frame sends six short commands, which is well below what USB serial can deliver at 10-20 Hz.

The harder limit is the LED output. While the firmware is sending the WS2812 waveform, the timing is strict and the device has much less freedom to service other work. If a new display update arrives while the system is already spending most of its time refreshing LEDs, commands can backlog and frame timing becomes irregular.

If a frame appears horizontally shifted, color-garbled, or broken only for one refresh, suspect the LED data stream itself as well as software backlog. One missed or extra WS2812 bit shifts every later LED in the chain until the next complete frame.

Also check hardware:

- 5 V power margin
- common ground between controller and LED supply
- data-line length and routing
- level shifting for the data line if needed
- first-pixel reliability
- connector movement during installation

## FastLED vs Adafruit NeoPixel/NeoMatrix

FastLED may be useful for API style, color utilities, and some platform-specific output backends, but it does not remove the WS2812 protocol limit. A single 1536-pixel WS2812 chain still needs about 46 ms for each full refresh.

Changing libraries may improve robustness or timing behavior in some cases, but it will not turn a single-line WS2812 chain into a high-frame-rate display. To substantially increase refresh rate, the architecture has to change.

## Ways to Increase Refresh Rate

Useful options, from least to most structural:

- Reduce the number of pixels in the chain.
- Reduce how often full-display changes are sent.
- Split the LEDs across multiple data pins and drive shorter chains in parallel.
- Use an ESP32 output backend that supports parallel RMT/I2S-style LED output.
- Use clocked LEDs such as APA102/DotStar-style pixels.

## APA102 / DotStar Notes for POV Displays

APA102-style pixels use separate data and clock lines instead of the strict self-clocked WS2812 waveform. This has several consequences that matter for persistence-of-vision displays:

- The update clock can be much faster than WS2812 timing.
- The controller can choose the clock rate, within the LED and wiring limits.
- Timing is less fragile because data is sampled by an explicit clock.
- Long or fast installations are often easier to reason about than one-wire WS2812 chains.
- POV displays benefit from the higher and more deterministic refresh rate.

Tradeoffs:

- APA102 needs two signal wires instead of one.
- It may cost more or be less available in matrix/tile formats.
- High clock rates still require careful wiring, grounding, and signal integrity.
- Power delivery remains just as important as with WS2812.

For a future POV display, APA102/DotStar-style pixels are a strong candidate because the display rate is central to the illusion. For this scoreboard, the current WS2812 chain is acceptable at moderate rates, especially around 10-16 Hz for all-display full-frame tests.
