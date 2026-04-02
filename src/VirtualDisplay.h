#pragma once
// ═══════════════════════════════════════════════════════════════
//  VirtualDisplay — a self-contained logical display that IS a
//  full Adafruit_GFX canvas (GFXcanvas16).
//
//  Inherits every GFX drawing primitive: drawLine, drawCircle,
//  setRotation, setFont, print, etc. — all in local coordinates.
//
//  On top of the raw canvas it adds: setText/setColor for the
//  scoreboard use-case, plus vertical scroll animation & queue.
//
//  The parent DisplayManager composites these canvases onto the
//  physical NeoPixel strip.
// ═══════════════════════════════════════════════════════════════

#include <Adafruit_GFX.h>

// Scroll mode values (shared with DisplayManager for convenience)
#ifndef SCROLL_NONE
  #define SCROLL_NONE 0    // instant update (default)
  #define SCROLL_UP   1    // old text scrolls up, new enters from bottom
  #define SCROLL_DOWN 2    // old text scrolls down, new enters from top
#endif

#ifndef SCROLL_STEP_MS
  #define SCROLL_STEP_MS 50
#endif

#ifndef SCROLL_QUEUE_SIZE
  #define SCROLL_QUEUE_SIZE 10
#endif

class VirtualDisplay : public GFXcanvas16 {
public:
    /// Construct a virtual display of the given pixel dimensions.
    /// All Adafruit_GFX drawing methods work immediately.
    VirtualDisplay(uint16_t w, uint16_t h);

    // ── High-level text/score operations ────────────────────────
    void setText(const char* text);
    void setColor(uint8_t r, uint8_t g, uint8_t b);
    void setColor(uint16_t color565);
    uint16_t color() const { return _color; }
    void clear();

    // ── Scroll mode ─────────────────────────────────────────────
    void    setScrollMode(uint8_t mode);
    uint8_t scrollMode() const { return _scrollMode; }

    void setScrollSpeed(uint8_t ms);
    void setScrollBlank(bool enabled);

    // ── Scroll queue ────────────────────────────────────────────
    void clearQueue();

    // ── Per-frame update (drives scroll animations) ─────────────
    /// Returns true if the canvas was modified this call.
    bool update();

    // ── Query ───────────────────────────────────────────────────
    bool isAnimating() const { return _scrollOffset > 0; }

    /// Returns true (once) when a scroll animation finishes.
    bool scrollFinished();

    /// Expected scroll duration in ms (height × step time).
    unsigned long scrollDurationMs() const {
        return (unsigned long)height() * _scrollStepMs;
    }

private:
    // Text / display state
    char     _text[32];
    char     _oldText[32];
    uint16_t _color;
    bool     _dirty;

    // Scroll animation
    uint8_t  _scrollMode;
    int8_t   _scrollOffset;
    unsigned long _scrollLastStep;
    uint8_t  _scrollStepMs;
    bool     _scrollBlank;
    bool     _scrollJustDone;

    // Scroll queue (ring buffer)
    char     _queue[SCROLL_QUEUE_SIZE][32];
    uint8_t  _queueHead;
    uint8_t  _queueTail;
    uint8_t  _queueCount;

    // Internal helpers
    void        _render();
    void        _renderScrollFrame();
    void        _startScroll(const char* newText);
    const char* _fitTextRight(const char* text);
    int16_t     _centerTextX(const char* text);
};
