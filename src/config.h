#pragma once
// ═══════════════════════════════════════════════════════════════
//  Robot Game Scoreboard — Configuration
// ═══════════════════════════════════════════════════════════════
//
//  Most values can be overridden from platformio.ini build_flags
//  (e.g. -DNEOPIXEL_PIN=5). Defaults are provided here.
// ═══════════════════════════════════════════════════════════════

// ── Network mode (set ONE via build_flags) ───────────────────
#if !defined(USE_WIFI) && !defined(USE_ETHERNET_W5500)
  #define USE_WIFI                 // fallback: WiFi
#endif

// ── OSC ──────────────────────────────────────────────────────
#ifndef OSC_PORT
  #define OSC_PORT 9000
#endif

// ── NeoPixel / Matrix ────────────────────────────────────────
#ifndef NEOPIXEL_PIN
  #define NEOPIXEL_PIN 2           // GPIO for NeoPixel data line
#endif

#define NUM_DISPLAYS        6      // logical score displays
#define MATRIX_TILE_WIDTH  32      // pixels per tile (width)
#define MATRIX_TILE_HEIGHT  8      // pixels per tile (height)
#define TOTAL_WIDTH  (NUM_DISPLAYS * MATRIX_TILE_WIDTH)  // 192
#define TOTAL_HEIGHT MATRIX_TILE_HEIGHT                  // 8

// Brightness 0-255.  Keep LOW during development to limit current draw!
// 6 panels × 256 LEDs = 1536 LEDs → up to ~90 A at full white/full bright.
#define DEFAULT_BRIGHTNESS 20

// ── NeoMatrix layout flags ───────────────────────────────────
// Adjust to match your physical panel wiring.
// Reference: https://learn.adafruit.com/adafruit-neopixel-uberguide/neomatrix-library
//
//  Within each 32×8 tile:
//    NEO_MATRIX_TOP / BOTTOM      first pixel at top or bottom
//    NEO_MATRIX_LEFT / RIGHT      first pixel at left or right
//    NEO_MATRIX_ROWS / COLUMNS    pixels arranged in rows or columns
//    NEO_MATRIX_PROGRESSIVE / ZIGZAG  same direction each row, or alternating
//
//  Tile arrangement (how the 6 tiles are daisy-chained):
//    NEO_TILE_TOP / BOTTOM
//    NEO_TILE_LEFT / RIGHT
//    NEO_TILE_ROWS / COLUMNS
//    NEO_TILE_PROGRESSIVE / ZIGZAG

#define MATRIX_LAYOUT ( \
  NEO_MATRIX_TOP  + NEO_MATRIX_LEFT + NEO_MATRIX_COLUMNS + NEO_MATRIX_PROGRESSIVE + \
  NEO_TILE_TOP    + NEO_TILE_LEFT   + NEO_TILE_ROWS      + NEO_TILE_PROGRESSIVE     \
)

#define LED_TYPE (NEO_GRB + NEO_KHZ800)

// ── Ethernet W5500 pins (M5Stack Ethernet Unit) ─────────────
#ifdef USE_ETHERNET_W5500
  #ifndef ETH_CS_PIN
    #define ETH_CS_PIN  6
  #endif
  #ifndef ETH_RST_PIN
    #define ETH_RST_PIN 7
  #endif
#endif
