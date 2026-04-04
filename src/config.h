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

#define NUM_DISPLAYS        1      // logical score displays
#define MATRIX_TILE_WIDTH   32      // pixels per tile (width)
#define MATRIX_TILE_HEIGHT  8      // pixels per tile (height)
#define TOTAL_WIDTH  (NUM_DISPLAYS * MATRIX_TILE_WIDTH)
#define TOTAL_HEIGHT MATRIX_TILE_HEIGHT

// Brightness 0-255.  Keep LOW during development to limit current draw!
// 6 panels × 256 LEDs = 1536 LEDs.  Text at brightness 20 → ~1–2 A.
#define DEFAULT_BRIGHTNESS 10

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
  NEO_MATRIX_TOP  + NEO_MATRIX_LEFT + NEO_MATRIX_ROWS + NEO_MATRIX_PROGRESSIVE + \
  NEO_TILE_TOP    + NEO_TILE_LEFT   + NEO_TILE_ROWS      + NEO_TILE_PROGRESSIVE     \
)

// #define MATRIX_LAYOUT ( \
//   NEO_MATRIX_TOP  + NEO_MATRIX_LEFT + NEO_MATRIX_COLUMNS + NEO_MATRIX_PROGRESSIVE + \
//   NEO_TILE_TOP    + NEO_TILE_LEFT   + NEO_TILE_COLUMNS     + NEO_TILE_PROGRESSIVE     \
// )


#define LED_TYPE (NEO_GRB + NEO_KHZ800)

// ── Dead-LED compensation ────────────────────────────────────
// If a physical LED in the strip is dead (bypassed with a jumper),
// set DEAD_LED_INDEX to its raw strip index (0-based).  The renderer
// simply skips that pixel's content — it can't be displayed anyway.
// Every other physical LED shows its correct pixel.
// Set to -1 to disable (no dead LEDs).
#ifndef DEAD_LED_INDEX
  #define DEAD_LED_INDEX 172
#endif

// ── Text stack ───────────────────────────────────────────────
// Each VirtualDisplay owns a text stack (shared across modes).
// TEXT_STACK_MAX = max entries, TEXT_MAX_LEN = max chars per entry.
#ifndef TEXT_STACK_MAX
  #define TEXT_STACK_MAX  8
#endif
#ifndef TEXT_MAX_LEN
  #define TEXT_MAX_LEN   32
#endif

// ── Scroll step default ─────────────────────────────────────
#ifndef SCROLL_STEP_MS
  #define SCROLL_STEP_MS 50
#endif

// ── Serial command interface ─────────────────────────────────
// Enable text-based commands over USB-Serial (same syntax as OSC addresses).
// Disable with -DSERIAL_CMD_ENABLED=0 in build_flags if not needed.
#ifndef SERIAL_CMD_ENABLED
  #define SERIAL_CMD_ENABLED 1
#endif

// ── Ethernet W5500 pins (M5Stack Ethernet Unit) ─────────────
#ifdef USE_ETHERNET_W5500
  #ifndef ETH_CS_PIN
    #define ETH_CS_PIN  6
  #endif
  #ifndef ETH_RST_PIN
    #define ETH_RST_PIN 7
  #endif
#endif

// ── RS485 (Atomic RS485 Base — SP3485EE) ─────────────────────
// Uses Serial2 via the Atom bottom-pad connector.
// Direction control is automatic (hardware-managed on the base).
// Default pins match AtomS3 / AtomS3R + Atomic RS485 Base wiring.
#ifdef USE_RS485
  #ifndef RS485_RX_PIN
    #define RS485_RX_PIN 5
  #endif
  #ifndef RS485_TX_PIN
    #define RS485_TX_PIN 6
  #endif
  #ifndef RS485_BAUD
    #define RS485_BAUD 115200
  #endif
#endif
