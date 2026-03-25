#pragma once
// ═══════════════════════════════════════════════════════════════
//  DisplayManager — drives 6 logical 32×8 NeoPixel displays
//  that are physically daisy-chained into one long strip.
//  Supports optional scroll-transition when values change.
// ═══════════════════════════════════════════════════════════════

#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>
#include "config.h"

// Scroll mode values
#define SCROLL_NONE 0    // instant update (default)
#define SCROLL_UP   1    // old text scrolls up, new enters from bottom
#define SCROLL_DOWN 2    // old text scrolls down, new enters from top

// Speed of scroll animation (ms per pixel step).  8 pixels → ~120 ms total.
#ifndef SCROLL_STEP_MS
  #define SCROLL_STEP_MS 15
#endif

class DisplayManager {
public:
    DisplayManager();

    void begin();

    // ── Per-display operations (displayIndex: 0 … NUM_DISPLAYS-1) ──
    void setText(uint8_t displayIndex, const char* text);
    void setColor(uint8_t displayIndex, uint8_t r, uint8_t g, uint8_t b);
    void setColor(uint8_t displayIndex, uint16_t color565);
    void clear(uint8_t displayIndex);

    // ── Scroll mode ─────────────────────────────────────────────────
    /// Set per-display scroll mode: SCROLL_NONE (0), SCROLL_UP (1), SCROLL_DOWN (2).
    void setScrollMode(uint8_t displayIndex, uint8_t mode);
    /// Set scroll mode for ALL displays at once.
    void setScrollModeAll(uint8_t mode);

    // ── Global operations ───────────────────────────────────────────
    void setBrightness(uint8_t brightness);
    void clearAll();

    /// Call every loop iteration — drives scroll animations and pushes pixels.
    void update();

    /// Runs a short self-test that lights each display in sequence.
    void showTestPattern();

    /// Returns true if any display is currently mid-scroll.
    bool isAnimating() const;

private:
    Adafruit_NeoMatrix _matrix;

    struct DisplayState {
        char     text[32];       // current (target) text
        char     oldText[32];    // previous text (for scroll animation)
        uint16_t color;
        uint8_t  scrollMode;     // SCROLL_NONE / SCROLL_UP / SCROLL_DOWN
        int8_t   scrollOffset;   // animation progress: 0 = done, ±1…8 = in transit
        unsigned long scrollLastStep;  // millis() of last animation tick
        bool     dirty;          // needs instant redraw (non-scroll)
    };
    DisplayState _displays[NUM_DISPLAYS];
    bool _needsUpdate;

    void    _drawDisplay(uint8_t index);
    void    _drawDisplayScrollFrame(uint8_t index);
    int16_t _centerTextX(uint8_t index, const char* text);
};
