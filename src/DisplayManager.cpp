#include "DisplayManager.h"
#include <cstring>
#ifdef USE_M5UNIFIED
  #include <M5Unified.h>
#endif

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
#ifdef USE_M5UNIFIED
    _imuAvailable = M5.Imu.isEnabled();
#endif
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

// ── Modes ────────────────────────────────────────────────────
void DisplayManager::setMode(uint8_t idx, DisplayMode mode) {
    if (idx >= NUM_DISPLAYS) return;
    _vDisplays[idx]->setMode(mode);
}

void DisplayManager::setMode(uint8_t idx, const DisplayModeConfig& config) {
    if (idx >= NUM_DISPLAYS) return;
    _vDisplays[idx]->setMode(config);
}

void DisplayManager::setModeAll(DisplayMode mode) {
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++)
        _vDisplays[i]->setMode(mode);
}

// ── Compatibility scroll API ─────────────────────────────────
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

void DisplayManager::setScrollContinuous(bool enabled) {
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++)
        _vDisplays[i]->setScrollContinuous(enabled);
}

void DisplayManager::setParticlesEnabled(bool enabled) {
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++)
        _vDisplays[i]->setParticlesEnabled(enabled);
}

void DisplayManager::setParticlesEnabled(uint8_t idx, bool enabled) {
    if (idx >= NUM_DISPLAYS) return;
    _vDisplays[idx]->setParticlesEnabled(enabled);
}

void DisplayManager::setTextEnabled(bool enabled) {
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++)
        _vDisplays[i]->setTextEnabled(enabled);
}

void DisplayManager::setTextEnabled(uint8_t idx, bool enabled) {
    if (idx >= NUM_DISPLAYS) return;
    _vDisplays[idx]->setTextEnabled(enabled);
}

void DisplayManager::setTextBrightness(uint8_t b) {
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++)
        _vDisplays[i]->setTextBrightness(b);
}

void DisplayManager::setTextBrightness(uint8_t idx, uint8_t b) {
    if (idx >= NUM_DISPLAYS) return;
    _vDisplays[idx]->setTextBrightness(b);
}

void DisplayManager::setParticleBrightness(uint8_t b) {
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++)
        _vDisplays[i]->setParticleBrightness(b);
}

void DisplayManager::setParticleBrightness(uint8_t idx, uint8_t b) {
    if (idx >= NUM_DISPLAYS) return;
    _vDisplays[idx]->setParticleBrightness(b);
}

void DisplayManager::setParticleColor(uint8_t r, uint8_t g, uint8_t b) {
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++)
        _vDisplays[i]->setParticleColor(r, g, b);
}

void DisplayManager::setParticleColor(uint8_t idx, uint8_t r, uint8_t g, uint8_t b) {
    if (idx >= NUM_DISPLAYS) return;
    _vDisplays[idx]->setParticleColor(r, g, b);
}

void DisplayManager::setGravity(float gx, float gy) {
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++)
        _vDisplays[i]->setGravity(gx, gy);
}

void DisplayManager::setParticleConfig(const ParticleModeConfig& cfg) {
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++)
        _vDisplays[i]->setParticleConfig(cfg);
}

void DisplayManager::setParticleConfig(uint8_t idx, const ParticleModeConfig& cfg) {
    if (idx >= NUM_DISPLAYS) return;
    _vDisplays[idx]->setParticleConfig(cfg);
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
    _brightness = brightness;
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
#ifdef USE_M5UNIFIED
    if (_imuAvailable && M5.Imu.update()) {
        auto imu = M5.Imu.getImuData();
        setGravity(imu.accel.x, -imu.accel.y);
    }
#endif

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
        _vDisplays[i]->setColor(colors[i % 6]);
        // Draw directly without setText (avoid polluting the stack)
        _vDisplays[i]->fillScreen(0);
        _vDisplays[i]->setTextColor(colors[i % 6]);
        _vDisplays[i]->setCursor(0, 0);
        _vDisplays[i]->print(label);
        update();
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
        char label[8];
        snprintf(label, sizeof(label), "DISP%d", i + 1);
        // Draw label directly (no stack pollution)
        _vDisplays[i]->fillScreen(0);
        _vDisplays[i]->setTextColor(cyan);
        _vDisplays[i]->setCursor(0, 0);
        _vDisplays[i]->print(label);
    }
    update();
    delay(durationMs / 2);

    // Show READY on each display
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
        _vDisplays[i]->fillScreen(0);
        _vDisplays[i]->setTextColor(cyan);
        _vDisplays[i]->setCursor(0, 0);
        _vDisplays[i]->print("READY");
    }
    update();
    delay(durationMs / 2);

    // Clear everything including text stack
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
        _vDisplays[i]->textClear();
    }
    update();
}

// ══════════════════════════════════════════════════════════════
//  NVS save / load — persists key display parameters
//  Version byte to detect format changes. Current version: 4
// ══════════════════════════════════════════════════════════════
static const uint8_t NVS_PARAM_VERSION = 4;

void DisplayManager::saveParams() {
    _prefs.begin("disp", false);           // read-write
    _prefs.putUChar("version", NVS_PARAM_VERSION);
    _prefs.putUChar("bright", _brightness);

    for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
        VirtualDisplay* vd = _vDisplays[i];
        const DisplayModeConfig& mc = vd->modeConfig();
        char key[16];

        snprintf(key, sizeof(key), "mode%d", i);
        _prefs.putUChar(key, (uint8_t)mc.mode);

        snprintf(key, sizeof(key), "color%d", i);
        _prefs.putUShort(key, vd->color());

        // Particle config — store as raw bytes
        snprintf(key, sizeof(key), "pcfg%d", i);
        const ParticleModeConfig& p = mc.particles;
        _prefs.putBytes(key, &p, sizeof(ParticleModeConfig));

        // Scroll config
        snprintf(key, sizeof(key), "sms%d", i);
        _prefs.putUChar(key, mc.scroll.scrollStepMs);

        snprintf(key, sizeof(key), "scnt%d", i);
        _prefs.putBool(key, mc.scroll.continuous);

        // Text mode config
        snprintf(key, sizeof(key), "tidx%d", i);
        _prefs.putUChar(key, mc.text.textIndex);

        // Particles enabled (overlay)
        snprintf(key, sizeof(key), "pen%d", i);
        _prefs.putBool(key, mc.particlesEnabled);

        // v4: layer controls
        snprintf(key, sizeof(key), "ten%d", i);
        _prefs.putBool(key, mc.textEnabled);

        snprintf(key, sizeof(key), "tbr%d", i);
        _prefs.putUChar(key, mc.textBrightness);

        snprintf(key, sizeof(key), "pbr%d", i);
        _prefs.putUChar(key, mc.particleBrightness);

        snprintf(key, sizeof(key), "pcl%d", i);
        _prefs.putUShort(key, mc.particleColor);

        // Text stack
        snprintf(key, sizeof(key), "tsn%d", i);
        _prefs.putUChar(key, vd->textCount());
        for (uint8_t j = 0; j < vd->textCount(); j++) {
            snprintf(key, sizeof(key), "ts%d_%d", i, j);
            _prefs.putString(key, vd->textGet(j));
        }
    }

    _prefs.putBool("valid", true);
    _prefs.end();
    Serial.println("Params saved to NVS (v4)");
}

void DisplayManager::loadParams() {
    _prefs.begin("disp", true);            // read-only
    if (!_prefs.getBool("valid", false)) {
        _prefs.end();
        Serial.println("No saved params in NVS");
        return;
    }

    uint8_t ver = _prefs.getUChar("version", 1);

    setBrightness(_prefs.getUChar("bright", DEFAULT_BRIGHTNESS));

    for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
        VirtualDisplay* vd = _vDisplays[i];
        char key[16];

        snprintf(key, sizeof(key), "color%d", i);
        vd->setColor(_prefs.getUShort(key, 0xFFFF));

        // Read particle config
        snprintf(key, sizeof(key), "pcfg%d", i);
        ParticleModeConfig pcfg;
        if (_prefs.getBytes(key, &pcfg, sizeof(ParticleModeConfig)) == sizeof(ParticleModeConfig)) {
            // loaded OK
        }

        DisplayModeConfig mc;
        snprintf(key, sizeof(key), "mode%d", i);
        mc.mode = (DisplayMode)_prefs.getUChar(key, (uint8_t)DISPLAY_MODE_TEXT);

        snprintf(key, sizeof(key), "sms%d", i);
        mc.scroll.scrollStepMs = _prefs.getUChar(key, SCROLL_STEP_MS);

        if (ver >= 2) {
            snprintf(key, sizeof(key), "scnt%d", i);
            mc.scroll.continuous = _prefs.getBool(key, false);

            snprintf(key, sizeof(key), "tidx%d", i);
            mc.text.textIndex = _prefs.getUChar(key, 0);

            // Text stack
            snprintf(key, sizeof(key), "tsn%d", i);
            uint8_t tsCount = _prefs.getUChar(key, 0);
            vd->textClear();
            for (uint8_t j = 0; j < tsCount && j < TEXT_STACK_MAX; j++) {
                snprintf(key, sizeof(key), "ts%d_%d", i, j);
                String s = _prefs.getString(key, "");
                if (s.length() > 0) vd->textPush(s.c_str());
            }
        }

        if (ver >= 3) {
            snprintf(key, sizeof(key), "pen%d", i);
            mc.particlesEnabled = _prefs.getBool(key, false);
        }

        if (ver >= 4) {
            snprintf(key, sizeof(key), "ten%d", i);
            mc.textEnabled = _prefs.getBool(key, true);

            snprintf(key, sizeof(key), "tbr%d", i);
            mc.textBrightness = _prefs.getUChar(key, 255);

            snprintf(key, sizeof(key), "pbr%d", i);
            mc.particleBrightness = _prefs.getUChar(key, 255);

            snprintf(key, sizeof(key), "pcl%d", i);
            mc.particleColor = _prefs.getUShort(key, 0xFFFF);
        }

        mc.particles = pcfg;
        vd->setMode(mc);
    }

    _prefs.end();
    Serial.printf("Params loaded from NVS (v%d)\n", ver);
}

// ══════════════════════════════════════════════════════════════
//  Render — blit VirtualDisplay canvases onto the physical strip
// ══════════════════════════════════════════════════════════════

void DisplayManager::_render() {
    _matrix.fillScreen(0);

    uint16_t stripIdx = 0;   // continuous index across ALL displays

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
        for (uint16_t px = 0; px < totalPixels; px++) {
#if DEAD_LED_INDEX >= 0
            uint16_t globalPx = (uint16_t)(i * totalPixels) + px;
            if (globalPx == (uint16_t)DEAD_LED_INDEX)
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


