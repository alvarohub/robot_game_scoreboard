#include "DisplayManager.h"
#include <cstring>

// ── Constructor ──────────────────────────────────────────────
DisplayManager::DisplayManager()
    : _matrix(
          MATRIX_TILE_WIDTH, MATRIX_TILE_HEIGHT,   // single-tile size
          NUM_DISPLAYS, 1,                          // tile grid (6 × 1)
          NEOPIXEL_PIN,
          MATRIX_LAYOUT,
          LED_TYPE),
      _needsUpdate(false)
{
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
        _displays[i].text[0]    = '\0';
        _displays[i].oldText[0] = '\0';
        _displays[i].color      = 0xFFFF;   // white (RGB565)
        _displays[i].scrollMode = SCROLL_NONE;
        _displays[i].scrollOffset    = 0;
        _displays[i].scrollLastStep  = 0;
        _displays[i].dirty      = true;
    }
}

// ── Initialise hardware ──────────────────────────────────────
void DisplayManager::begin() {
    _matrix.begin();
    _matrix.setTextWrap(false);
    _matrix.setBrightness(DEFAULT_BRIGHTNESS);
    _matrix.setTextSize(1);            // default 5×7 font
    _matrix.fillScreen(0);
    _matrix.show();
}

// ── Per-display setters ──────────────────────────────────────
void DisplayManager::setText(uint8_t idx, const char* text) {
    if (idx >= NUM_DISPLAYS) return;

    // Nothing to do if the text is identical
    if (strcmp(_displays[idx].text, text) == 0) return;

    if (_displays[idx].scrollMode != SCROLL_NONE) {
        // Save old text for the scroll-out animation
        strncpy(_displays[idx].oldText, _displays[idx].text,
                sizeof(_displays[idx].oldText) - 1);
        _displays[idx].oldText[sizeof(_displays[idx].oldText) - 1] = '\0';

        // Copy new text
        strncpy(_displays[idx].text, text, sizeof(_displays[idx].text) - 1);
        _displays[idx].text[sizeof(_displays[idx].text) - 1] = '\0';

        // Start the scroll animation (1 → MATRIX_TILE_HEIGHT)
        _displays[idx].scrollOffset   = 1;
        _displays[idx].scrollLastStep = millis();
        _needsUpdate = true;
    } else {
        // Instant mode
        strncpy(_displays[idx].text, text, sizeof(_displays[idx].text) - 1);
        _displays[idx].text[sizeof(_displays[idx].text) - 1] = '\0';
        _displays[idx].dirty = true;
        _needsUpdate = true;
    }
}

void DisplayManager::setColor(uint8_t idx, uint8_t r, uint8_t g, uint8_t b) {
    setColor(idx, _matrix.Color(r, g, b));
}

void DisplayManager::setColor(uint8_t idx, uint16_t color565) {
    if (idx >= NUM_DISPLAYS) return;
    _displays[idx].color = color565;
    _displays[idx].dirty = true;
    _needsUpdate = true;
}

void DisplayManager::clear(uint8_t idx) {
    setText(idx, "");
}

// ── Scroll mode setters ──────────────────────────────────────
void DisplayManager::setScrollMode(uint8_t idx, uint8_t mode) {
    if (idx >= NUM_DISPLAYS) return;
    if (mode > SCROLL_DOWN) mode = SCROLL_NONE;
    _displays[idx].scrollMode = mode;
}

void DisplayManager::setScrollModeAll(uint8_t mode) {
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++)
        setScrollMode(i, mode);
}

// ── Global setters ───────────────────────────────────────────
void DisplayManager::setBrightness(uint8_t brightness) {
    _matrix.setBrightness(brightness);
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++)
        _displays[i].dirty = true;
    _needsUpdate = true;
}

void DisplayManager::clearAll() {
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
        _displays[i].text[0]    = '\0';
        _displays[i].oldText[0] = '\0';
        _displays[i].scrollOffset = 0;
        _displays[i].dirty = true;
    }
    _matrix.fillScreen(0);
    _needsUpdate = true;
}

// ── Refresh — call every loop() ──────────────────────────────
void DisplayManager::update() {
    bool anyAnimating = false;
    unsigned long now = millis();

    for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
        // ── Scroll animation in progress? ────────────────────
        if (_displays[i].scrollOffset > 0) {
            if (now - _displays[i].scrollLastStep >= SCROLL_STEP_MS) {
                _displays[i].scrollLastStep = now;
                _drawDisplayScrollFrame(i);
                _displays[i].scrollOffset++;

                if (_displays[i].scrollOffset > MATRIX_TILE_HEIGHT) {
                    // Animation complete — snap to final state
                    _displays[i].scrollOffset = 0;
                    _displays[i].oldText[0] = '\0';
                    _drawDisplay(i);
                } else {
                    anyAnimating = true;
                }
                _needsUpdate = true;
            } else {
                anyAnimating = true;   // still waiting for next step
            }
        }
        // ── Instant redraw (no scroll, or color change, etc.) ─
        else if (_displays[i].dirty) {
            _drawDisplay(i);
            _displays[i].dirty = false;
            _needsUpdate = true;
        }
    }

    if (_needsUpdate) {
        _matrix.show();
        _needsUpdate = false;
    }

    // Keep update() returning quickly when nothing is happening,
    // but stay "hot" while any animation is running.
    if (anyAnimating) _needsUpdate = true;
}

// ── Query animation state ────────────────────────────────────
bool DisplayManager::isAnimating() const {
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++)
        if (_displays[i].scrollOffset > 0) return true;
    return false;
}

// ── Startup self-test ────────────────────────────────────────
void DisplayManager::showTestPattern() {
    const uint16_t colors[] = {
        _matrix.Color(255,   0,   0),
        _matrix.Color(  0, 255,   0),
        _matrix.Color(  0,   0, 255),
        _matrix.Color(255, 255,   0),
        _matrix.Color(255,   0, 255),
        _matrix.Color(  0, 255, 255),
    };

    for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
        char label[8];
        snprintf(label, sizeof(label), "D%d", i + 1);
        _displays[i].color = colors[i % 6];
        uint8_t savedMode = _displays[i].scrollMode;
        _displays[i].scrollMode = SCROLL_NONE;
        setText(i, label);
        update();
        _displays[i].scrollMode = savedMode;
        delay(300);
    }

    delay(1500);
    clearAll();
    update();
}

// ── Splash screen ────────────────────────────────────────────
void DisplayManager::startDisplay(unsigned long durationMs) {
    uint16_t cyan = _matrix.Color(0, 255, 255);
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
        _displays[i].color = cyan;
        uint8_t savedMode = _displays[i].scrollMode;
        _displays[i].scrollMode = SCROLL_NONE;
        setText(i, "GAME");
        _displays[i].scrollMode = savedMode;
    }
    update();
    delay(durationMs);
    clearAll();
    update();
}

// ══════════════════════════════════════════════════════════════
//  Private helpers
// ══════════════════════════════════════════════════════════════

// Return a pointer into `text` at the rightmost substring that fits
// within MATRIX_TILE_WIDTH.  E.g. "1234" on an 8-px tile → "4".
const char* DisplayManager::_fitTextRight(const char* text) {
    int16_t  x1, y1;
    uint16_t w, h;
    _matrix.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    if ((int16_t)w <= MATRIX_TILE_WIDTH) return text;

    // Walk forward, dropping leading chars, until it fits
    const char* p = text;
    while (*p) {
        p++;
        _matrix.getTextBounds(p, 0, 0, &x1, &y1, &w, &h);
        if ((int16_t)w <= MATRIX_TILE_WIDTH) return p;
    }
    return p;   // empty string — nothing fits
}

void DisplayManager::_drawDisplay(uint8_t idx) {
    int16_t x0 = idx * MATRIX_TILE_WIDTH;
    _matrix.fillRect(x0, 0, MATRIX_TILE_WIDTH, MATRIX_TILE_HEIGHT, 0);

    if (_displays[idx].text[0] == '\0') return;

    const char* visible = _fitTextRight(_displays[idx].text);
    if (*visible == '\0') return;

    _matrix.setTextColor(_displays[idx].color);
    int16_t x = _centerTextX(idx, visible);
    _matrix.setCursor(x, 0);
    _matrix.print(visible);
}

void DisplayManager::_drawDisplayScrollFrame(uint8_t idx) {
    int16_t x0 = idx * MATRIX_TILE_WIDTH;
    int8_t  offset = _displays[idx].scrollOffset;   // 1 … MATRIX_TILE_HEIGHT
    uint8_t mode   = _displays[idx].scrollMode;

    // Clear this tile
    _matrix.fillRect(x0, 0, MATRIX_TILE_WIDTH, MATRIX_TILE_HEIGHT, 0);
    _matrix.setTextColor(_displays[idx].color);

    // Y positions for old and new text.
    // SCROLL_UP (1):  old moves up (y decreases), new enters from bottom.
    // SCROLL_DOWN (2): old moves down (y increases), new enters from top.
    int16_t oldY, newY;
    if (mode == SCROLL_UP) {
        oldY = -offset;                         // slides up out of view
        newY = MATRIX_TILE_HEIGHT - offset;     // enters from below
    } else {  // SCROLL_DOWN
        oldY = offset;                          // slides down out of view
        newY = -(MATRIX_TILE_HEIGHT - offset);  // enters from above
    }

    // Draw old text at its scrolled position
    if (_displays[idx].oldText[0] != '\0') {
        const char* visOld = _fitTextRight(_displays[idx].oldText);
        if (*visOld) {
            int16_t xOld = _centerTextX(idx, visOld);
            _matrix.setCursor(xOld, oldY);
            _matrix.print(visOld);
        }
    }

    // Draw new text at its scrolled position
    if (_displays[idx].text[0] != '\0') {
        const char* visNew = _fitTextRight(_displays[idx].text);
        if (*visNew) {
            int16_t xNew = _centerTextX(idx, visNew);
            _matrix.setCursor(xNew, newY);
            _matrix.print(visNew);
        }
    }
}

int16_t DisplayManager::_centerTextX(uint8_t idx, const char* text) {
    // Use getTextBounds for accurate measurement
    int16_t  x1, y1;
    uint16_t w, h;
    _matrix.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);

    int16_t displayStart = idx * MATRIX_TILE_WIDTH;
    int16_t offset = (MATRIX_TILE_WIDTH - (int16_t)w) / 2;
    if (offset < 0) offset = 0;
    return displayStart + offset;
}
