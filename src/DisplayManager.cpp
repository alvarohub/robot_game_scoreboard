#include "DisplayManager.h"
#include <cstring>

// Simple RGB565 pack (same as Adafruit_GFX::color565)
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// ── Constructor ──────────────────────────────────────────────
DisplayManager::DisplayManager()
    : _matrix(
          MATRIX_TILE_WIDTH, MATRIX_TILE_HEIGHT,  // single-tile size
          NUM_DISPLAYS, 1,                        // tile grid (N × 1)
          NEOPIXEL_PIN,
          MATRIX_LAYOUT,
          LED_TYPE),
      _needsUpdate(false)
{
    // Create VirtualDisplays and compute sequential offsets
    int16_t xOff = 0;
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
        _vDisplays[i] = new VirtualDisplay(MATRIX_TILE_WIDTH, MATRIX_TILE_HEIGHT);
        _offsets[i]    = xOff;
        xOff += MATRIX_TILE_WIDTH;
    }
}

DisplayManager::~DisplayManager() {
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
        delete _vDisplays[i];
    }
}

// ── Initialise hardware ──────────────────────────────────────
void DisplayManager::begin() {
    _matrix.begin();
    _matrix.setTextWrap(false);
    _matrix.setBrightness(DEFAULT_BRIGHTNESS);
    _matrix.fillScreen(0);
    _matrix.show();
}

// ── Per-display forwarding ───────────────────────────────────
void DisplayManager::setText(uint8_t idx, const char* text) {
    if (idx >= NUM_DISPLAYS) return;
    _vDisplays[idx]->setText(text);
}

void DisplayManager::setColor(uint8_t idx, uint8_t r, uint8_t g, uint8_t b) {
    if (idx >= NUM_DISPLAYS) return;
    _vDisplays[idx]->setColor(r, g, b);
}

void DisplayManager::setColor(uint8_t idx, uint16_t color565) {
    if (idx >= NUM_DISPLAYS) return;
    _vDisplays[idx]->setColor(color565);
}

void DisplayManager::clear(uint8_t idx) {
    if (idx >= NUM_DISPLAYS) return;
    _vDisplays[idx]->clear();
}

// ── Scroll ───────────────────────────────────────────────────
void DisplayManager::setScrollMode(uint8_t idx, uint8_t mode) {
    if (idx >= NUM_DISPLAYS) return;
    _vDisplays[idx]->setScrollMode(mode);
}

void DisplayManager::setScrollModeAll(uint8_t mode) {
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++)
        _vDisplays[i]->setScrollMode(mode);
}

void DisplayManager::setScrollSpeed(uint8_t ms) {
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++)
        _vDisplays[i]->setScrollSpeed(ms);
}

void DisplayManager::setScrollBlank(bool enabled) {
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++)
        _vDisplays[i]->setScrollBlank(enabled);
}

void DisplayManager::clearQueue(uint8_t idx) {
    if (idx >= NUM_DISPLAYS) return;
    _vDisplays[idx]->clearQueue();
}

void DisplayManager::clearQueueAll() {
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++)
        _vDisplays[i]->clearQueue();
}

// ── Global ───────────────────────────────────────────────────
void DisplayManager::setBrightness(uint8_t brightness) {
    _matrix.setBrightness(brightness);
    _needsUpdate = true;
}

void DisplayManager::clearAll() {
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++)
        _vDisplays[i]->clear();
    _matrix.fillScreen(0);
    _needsUpdate = true;
}

// ── Per-frame update ─────────────────────────────────────────
void DisplayManager::update() {
    bool anyChanged = false;

    for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
        if (_vDisplays[i]->update())
            anyChanged = true;
    }

    if (anyChanged || _needsUpdate) {
        _render();
        _matrix.show();
        _needsUpdate = false;
    }
}

// ── Query ────────────────────────────────────────────────────
bool DisplayManager::isAnimating() const {
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++)
        if (_vDisplays[i]->isAnimating()) return true;
    return false;
}

bool DisplayManager::scrollFinished(uint8_t idx) {
    if (idx >= NUM_DISPLAYS) return false;
    return _vDisplays[idx]->scrollFinished();
}

unsigned long DisplayManager::scrollDurationMs() const {
    if (NUM_DISPLAYS == 0) return 0;
    return _vDisplays[0]->scrollDurationMs();
}

VirtualDisplay* DisplayManager::getDisplay(uint8_t idx) {
    if (idx >= NUM_DISPLAYS) return nullptr;
    return _vDisplays[idx];
}

// ── Test pattern ─────────────────────────────────────────────
void DisplayManager::showTestPattern() {
    const uint16_t colors[] = {
        rgb565(255,   0,   0),
        rgb565(  0, 255,   0),
        rgb565(  0,   0, 255),
        rgb565(255, 255,   0),
        rgb565(255,   0, 255),
        rgb565(  0, 255, 255),
    };

    for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
        char label[8];
        snprintf(label, sizeof(label), "D%d", i + 1);
        uint8_t savedMode = _vDisplays[i]->scrollMode();
        _vDisplays[i]->setScrollMode(SCROLL_NONE);
        _vDisplays[i]->setColor(colors[i % 6]);
        _vDisplays[i]->setText(label);
        update();
        _vDisplays[i]->setScrollMode(savedMode);
        delay(300);
    }

    delay(1500);
    clearAll();
    update();
}

// ── Raster scan — one LED at a time by strip index ───────────
void DisplayManager::showRasterScan(uint16_t delayMs) {
    uint16_t totalLEDs = _matrix.numPixels();
    Serial.printf("Raster scan: %d LEDs, %d ms/step\n", totalLEDs, delayMs);

    _matrix.fillScreen(0);   
    // Let's do the raster scan, but not cleaning the whole matrix when we move one led, just clear that led first:
    for (uint16_t i = 0; i < totalLEDs; i++) {
        _matrix.setPixelColor(i, 255, 255, 255);  // white dot at raw index
        _matrix.show();
        Serial.printf("LED %3d\n", i);
        delay(delayMs);
        _matrix.setPixelColor(i, 0, 0, 0);        // clear that dot before moving on
        _matrix.fillScreen(0); 
    }

    _matrix.fillScreen(0);
    _matrix.show();
    Serial.println("Raster scan complete");
}

// ── Splash screen ────────────────────────────────────────────
void DisplayManager::startDisplay(unsigned long durationMs) {
    uint16_t cyan = rgb565(0, 255, 255);
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
        uint8_t savedMode = _vDisplays[i]->scrollMode();
        _vDisplays[i]->setScrollMode(SCROLL_DOWN);
        _vDisplays[i]->setColor(cyan);
        _vDisplays[i]->setText("GAME");
        _vDisplays[i]->setScrollMode(savedMode);
    }
    update();
    delay(durationMs);
    clearAll();
    update();
}

// ══════════════════════════════════════════════════════════════
//  Render — blit VirtualDisplay canvases onto the physical strip
// ══════════════════════════════════════════════════════════════

void DisplayManager::_render() {
    _matrix.fillScreen(0);

    for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
        const uint16_t* buf = _vDisplays[i]->getBuffer();
        if (!buf) continue;

        uint16_t dw = _vDisplays[i]->width();
        uint16_t dh = _vDisplays[i]->height();
        uint16_t totalPixels = dw * dh;

        // Canvas buffer is row-major; matrix layout is ROW-PROGRESSIVE,
        // so canvas index == raw strip index.  When a dead LED exists
        // (bypassed with a jumper), we skip its canvas pixel — it
        // can't be displayed anyway.  Every subsequent pixel lands on
        // the correct physical LED because the skip keeps strip and
        // canvas indices in sync.
        uint16_t stripIdx = 0;
        for (uint16_t px = 0; px < totalPixels; px++) {
#if DEAD_LED_INDEX >= 0
            if (px == (uint16_t)DEAD_LED_INDEX)
                continue;           // dead LED's pixel — nothing to show
#endif
            uint16_t c565 = buf[px];
            if (c565 != 0) {
                _matrix.setPixelColor(stripIdx,
                    (uint8_t)((c565 >> 8) & 0xF8),   // R
                    (uint8_t)((c565 >> 3) & 0xFC),   // G
                    (uint8_t)((c565 << 3) & 0xF8));  // B
            }
            stripIdx++;
        }
    }
}


