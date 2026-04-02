#include "VirtualDisplay.h"
#include <cstring>

// RGB565 pack — same formula as Adafruit_SPITFT::color565()
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// ── Constructor ──────────────────────────────────────────────
VirtualDisplay::VirtualDisplay(uint16_t w, uint16_t h)
    : GFXcanvas16(w, h),
      _color(0xFFFF),          // white in RGB565
      _dirty(true),
      _scrollMode(SCROLL_NONE),
      _scrollOffset(0),
      _scrollLastStep(0),
      _scrollStepMs(SCROLL_STEP_MS),
      _scrollBlank(false),
      _scrollJustDone(false),
      _queueHead(0),
      _queueTail(0),
      _queueCount(0)
{
    _text[0]    = '\0';
    _oldText[0] = '\0';
    setTextWrap(false);
    setTextSize(1);
    fillScreen(0);
}

// ── High-level text / color setters ──────────────────────────
void VirtualDisplay::setText(const char* text) {
    // Nothing to do if text is identical to current target
    if (strcmp(_text, text) == 0) return;

    if (_scrollMode != SCROLL_NONE) {
        if (_scrollOffset > 0) {
            // Scroll in progress — queue if room
            if (_queueCount < SCROLL_QUEUE_SIZE) {
                strncpy(_queue[_queueTail], text, 31);
                _queue[_queueTail][31] = '\0';
                _queueTail = (_queueTail + 1) % SCROLL_QUEUE_SIZE;
                _queueCount++;
            }
            return;
        }
        _startScroll(text);
    } else {
        strncpy(_text, text, sizeof(_text) - 1);
        _text[sizeof(_text) - 1] = '\0';
        _dirty = true;
    }
}

void VirtualDisplay::setColor(uint8_t r, uint8_t g, uint8_t b) {
    setColor(rgb565(r, g, b));
}

void VirtualDisplay::setColor(uint16_t color565) {
    _color = color565;
    _dirty = true;
}

void VirtualDisplay::clear() {
    setText("");
}

// ── Scroll mode ──────────────────────────────────────────────
void VirtualDisplay::setScrollMode(uint8_t mode) {
    if (mode > SCROLL_DOWN) mode = SCROLL_NONE;
    _scrollMode = mode;

    if (mode == SCROLL_NONE) {
        // Flush the queue — show last queued value instantly
        if (_queueCount > 0) {
            uint8_t lastIdx = (_queueTail + SCROLL_QUEUE_SIZE - 1)
                              % SCROLL_QUEUE_SIZE;
            strncpy(_text, _queue[lastIdx], 31);
            _text[31] = '\0';
        }
        _queueHead  = 0;
        _queueTail  = 0;
        _queueCount = 0;
        _scrollOffset = 0;
        _oldText[0] = '\0';
        _dirty = true;
    }
}

void VirtualDisplay::setScrollSpeed(uint8_t ms) {
    _scrollStepMs = ms > 0 ? ms : 1;
}

void VirtualDisplay::setScrollBlank(bool enabled) {
    _scrollBlank = enabled;
}

void VirtualDisplay::clearQueue() {
    _queueHead  = 0;
    _queueTail  = 0;
    _queueCount = 0;
}

// ── Per-frame update (call from DisplayManager::update) ──────
bool VirtualDisplay::update() {
    unsigned long now = millis();

    // ── Scroll animation in progress? ────────────────────────
    if (_scrollOffset > 0) {
        if (now - _scrollLastStep >= _scrollStepMs) {
            _scrollLastStep = now;
            _renderScrollFrame();
            _scrollOffset++;

            if (_scrollOffset > (int8_t)height()) {
                // Animation complete
                _scrollOffset = 0;
                _oldText[0] = '\0';
                _scrollJustDone = true;

                // Dequeue next item if available
                if (_queueCount > 0) {
                    char next[32];
                    strncpy(next, _queue[_queueHead], 31);
                    next[31] = '\0';
                    _queueHead = (_queueHead + 1) % SCROLL_QUEUE_SIZE;
                    _queueCount--;

                    if (_scrollBlank) {
                        fillScreen(0);
                        strncpy(_text, next, sizeof(_text) - 1);
                        _text[sizeof(_text) - 1] = '\0';
                        _dirty = true;
                    }
                    _startScroll(next);
                } else {
                    _render();
                }
            }
            return true;
        }
        return false;   // waiting for next step
    }

    // ── Instant redraw ───────────────────────────────────────
    if (_dirty) {
        _render();
        _dirty = false;
        return true;
    }
    return false;
}

// ── Query ────────────────────────────────────────────────────
bool VirtualDisplay::scrollFinished() {
    if (_scrollJustDone) {
        _scrollJustDone = false;
        return true;
    }
    return false;
}

// ══════════════════════════════════════════════════════════════
//  Private helpers
// ══════════════════════════════════════════════════════════════

void VirtualDisplay::_startScroll(const char* newText) {
    strncpy(_oldText, _text, sizeof(_oldText) - 1);
    _oldText[sizeof(_oldText) - 1] = '\0';

    strncpy(_text, newText, sizeof(_text) - 1);
    _text[sizeof(_text) - 1] = '\0';

    _scrollOffset   = 1;
    _scrollLastStep = millis();
}

void VirtualDisplay::_render() {
    fillScreen(0);
    if (_text[0] == '\0') return;

    const char* visible = _fitTextRight(_text);
    if (*visible == '\0') return;

    setTextColor(_color);
    int16_t x = _centerTextX(visible);
    setCursor(x, 0);
    print(visible);
}

void VirtualDisplay::_renderScrollFrame() {
    fillScreen(0);
    setTextColor(_color);

    int8_t offset = _scrollOffset;
    int16_t h = (int16_t)height();

    int16_t oldY, newY;
    if (_scrollMode == SCROLL_UP) {
        oldY = -offset;
        newY = h - offset;
    } else {  // SCROLL_DOWN
        oldY = offset;
        newY = -(h - offset);
    }

    // Draw old text
    if (_oldText[0] != '\0') {
        const char* visOld = _fitTextRight(_oldText);
        if (*visOld) {
            setCursor(_centerTextX(visOld), oldY);
            print(visOld);
        }
    }

    // Draw new text
    if (_text[0] != '\0') {
        const char* visNew = _fitTextRight(_text);
        if (*visNew) {
            setCursor(_centerTextX(visNew), newY);
            print(visNew);
        }
    }
}

const char* VirtualDisplay::_fitTextRight(const char* text) {
    int16_t  x1, y1;
    uint16_t tw, th;
    getTextBounds(text, 0, 0, &x1, &y1, &tw, &th);
    if ((int16_t)tw <= (int16_t)width()) return text;

    const char* p = text;
    while (*p) {
        p++;
        getTextBounds(p, 0, 0, &x1, &y1, &tw, &th);
        if ((int16_t)tw <= (int16_t)width()) return p;
    }
    return p;
}

int16_t VirtualDisplay::_centerTextX(const char* text) {
    int16_t  x1, y1;
    uint16_t tw, th;
    getTextBounds(text, 0, 0, &x1, &y1, &tw, &th);
    int16_t off = ((int16_t)width() - (int16_t)tw) / 2;
    if (off < 0) off = 0;
    return off;
}
