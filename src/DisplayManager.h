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
  #define SCROLL_STEP_MS 25
#endif

// Maximum pending items in the per-display scroll queue.
#ifndef SCROLL_QUEUE_SIZE
  #define SCROLL_QUEUE_SIZE 10
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
    /// Switching to SCROLL_NONE automatically flushes the queue and shows
    /// the last queued value instantly.
    void setScrollMode(uint8_t displayIndex, uint8_t mode);
    /// Set scroll mode for ALL displays at once.
    void setScrollModeAll(uint8_t mode);

    /// Set scroll animation speed (ms per pixel step).  Default = SCROLL_STEP_MS.
    void setScrollSpeed(uint8_t ms);

    /// Enable/disable a brief blank frame between consecutive scroll items.
    /// Helps prevent visual "bleeding" at high scroll speeds. Default = off.
    void setScrollBlank(bool enabled);

    // ── Scroll queue ────────────────────────────────────────────────
    /// Discard all pending queued text for one display.
    void clearQueue(uint8_t displayIndex);
    /// Discard pending queued text for ALL displays.
    void clearQueueAll();

    // ── Global operations ───────────────────────────────────────────
    void setBrightness(uint8_t brightness);
    void clearAll();

    /// Call every loop iteration — drives scroll animations and pushes pixels.
    void update();

    /// Runs a short self-test that lights each display in sequence.
    void showTestPattern();

    /// Splash screen shown once at startup (e.g. "GAME" on all panels).
    void startDisplay(unsigned long durationMs = 2000);

    /// Returns true if any display is currently mid-scroll.
    bool isAnimating() const;

    /// Returns true (once) if display `idx` just finished a scroll animation.
    /// The flag auto-clears after reading, so call once per loop iteration.
    bool scrollFinished(uint8_t idx);

    /// Expected scroll duration in milliseconds (based on current settings).
    unsigned long scrollDurationMs() const {
        return (unsigned long)MATRIX_TILE_HEIGHT * _scrollStepMs;
    }

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
        bool     scrollJustDone; // set once when scroll completes, cleared by scrollFinished()
        // ── Scroll queue (ring buffer) ──
        char     queue[SCROLL_QUEUE_SIZE][32];
        uint8_t  queueHead;      // next slot to dequeue
        uint8_t  queueTail;      // next slot to enqueue
        uint8_t  queueCount;     // items in queue
    };
    DisplayState _displays[NUM_DISPLAYS];
    uint8_t _scrollStepMs;
    bool    _scrollBlank;     // insert blank frame between queued scrolls
    bool _needsUpdate;

    void        _drawDisplay(uint8_t index);
    void        _drawDisplayScrollFrame(uint8_t index);
    void        _startScroll(uint8_t idx, const char* newText);
    const char* _fitTextRight(const char* text);
    int16_t     _centerTextX(uint8_t index, const char* text);
};
