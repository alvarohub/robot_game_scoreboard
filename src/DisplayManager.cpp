#include "DisplayManager.h"
#include <cstring>
#if SCOREBOARD_HAS_M5UNIFIED
  #include <M5Unified.h>
#endif

// Simple RGB565 pack (same as Adafruit_GFX::color565)
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static void color565ToRgb(uint16_t c, uint8_t* r, uint8_t* g, uint8_t* b) {
    uint8_t rr = (uint8_t)((c >> 11) & 0x1F);
    uint8_t gg = (uint8_t)((c >> 5) & 0x3F);
    uint8_t bb = (uint8_t)(c & 0x1F);
    if (r) *r = (uint8_t)((rr << 3) | (rr >> 2));
    if (g) *g = (uint8_t)((gg << 2) | (gg >> 4));
    if (b) *b = (uint8_t)((bb << 3) | (bb >> 2));
}

static const char* modeName(DisplayMode mode) {
    switch (mode) {
        case DISPLAY_MODE_TEXT: return "text";
        case DISPLAY_MODE_SCROLL_UP: return "scroll_up";
        case DISPLAY_MODE_SCROLL_DOWN: return "scroll_down";
        default: return "text";
    }
}

static void appendJsonString(String& out, const char* value) {
    out += '"';
    if (value) {
        while (*value) {
            char c = *value++;
            switch (c) {
                case '\\': out += "\\\\"; break;
                case '"': out += "\\\""; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default: out += c; break;
            }
        }
    }
    out += '"';
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
#if SCOREBOARD_HAS_M5UNIFIED
    _imuAvailable = M5.Imu.isEnabled();
#endif
}

// ── Per-display forwarding ───────────────────────────────────
void DisplayManager::setText(uint8_t idx, const char* text) {
    if (idx >= NUM_DISPLAYS) return;
    _vDisplays[idx]->setText(text);
    _onTextSet(idx);
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
    _anim[idx].selectedId = 0;
    _stopAnimation(idx);
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
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
        clear(i);
    }
    _matrix.fillScreen(0);
    _needsUpdate = true;
}

// ── Per-frame update ─────────────────────────────────────────
void DisplayManager::update() {
#if SCOREBOARD_HAS_M5UNIFIED
    if (_imuAvailable && M5.Imu.update()) {
        auto imu = M5.Imu.getImuData();
        setGravity(-imu.accel.x, -imu.accel.y);
    }
#endif

    _tickAnimations();

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

    _persistIfDue();
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

void DisplayManager::printDisplayState(uint8_t idx, Print& out) const {
    if (idx >= NUM_DISPLAYS) return;

    VirtualDisplay* vd = _vDisplays[idx];
    if (!vd) return;

    const DisplayModeConfig& mc = vd->modeConfig();
    const ParticleModeConfig& pc = mc.particles;

    uint16_t textColor565 = vd->color();
    uint8_t tr = 255, tg = 255, tb = 255;
    color565ToRgb(textColor565, &tr, &tg, &tb);

    uint16_t particleColor565 = vd->particleColor();
    uint8_t pr = 255, pg = 255, pb = 255;
    color565ToRgb(particleColor565, &pr, &pg, &pb);

    float angleDeg = pc.viewTransform.angle * 57.2957795f;

    String json;
    json.reserve(1280);
    json += "{\"display\":" + String((int)(idx + 1));
    json += ",\"animationSlot\":" + String((int)animationId(idx));
    json += ",\"animationName\":";
    appendJsonString(json, animationName(idx));
    json += ",\"animationRunning\":" + String(_anim[idx].running ? "true" : "false");
    json += ",\"mode\":\"" + String(modeName(mc.mode)) + "\"";
    json += ",\"textEnabled\":" + String(mc.textEnabled ? "true" : "false");
    json += ",\"particlesEnabled\":" + String(mc.particlesEnabled ? "true" : "false");
    json += ",\"textBrightness\":" + String((int)mc.textBrightness);
    json += ",\"particleBrightness\":" + String((int)mc.particleBrightness);
    json += ",\"textColor\":[" + String((int)tr) + "," + String((int)tg) + "," + String((int)tb) + "]";
    json += ",\"particleColor\":[" + String((int)pr) + "," + String((int)pg) + "," + String((int)pb) + "]";
    json += ",\"textCount\":" + String((int)vd->textCount());
    json += ",\"textItems\":[";
    for (uint8_t textIndex = 0; textIndex < vd->textCount(); textIndex++) {
        if (textIndex > 0) {
            json += ',';
        }
        appendJsonString(json, vd->textGet(textIndex));
    }
    json += "]";
    json += ",\"scroll\":{";
    json += "\"stepMs\":" + String((int)mc.scroll.scrollStepMs);
    json += ",\"continuous\":" + String(mc.scroll.continuous ? "true" : "false");
    json += "}";
    json += ",\"particles\":{";
    json += "\"count\":" + String((int)pc.count);
    json += ",\"renderMs\":" + String((int)pc.renderMs);
    json += ",\"substepMs\":" + String((int)pc.substepMs);
    json += ",\"gravityScale\":" + String(pc.gravityScale, 3);
    json += ",\"gravityEnabled\":" + String(pc.gravityEnabled ? "true" : "false");
    json += ",\"collisionEnabled\":" + String(pc.collisionEnabled ? "true" : "false");
    json += ",\"elasticity\":" + String(pc.elasticity, 3);
    json += ",\"wallElasticity\":" + String(pc.wallElasticity, 3);
    json += ",\"damping\":" + String(pc.damping, 6);
    json += ",\"radius\":" + String(pc.radius, 3);
    json += ",\"renderStyle\":" + String((int)pc.renderStyle);
    json += ",\"glowSigma\":" + String(pc.glowSigma, 3);
    json += ",\"glowWavelength\":" + String(pc.glowWavelength, 3);
    json += ",\"temperature\":" + String(pc.temperature, 3);
    json += ",\"attractStrength\":" + String(pc.attractStrength, 3);
    json += ",\"attractRange\":" + String(pc.attractRange, 3);
    json += ",\"attractEnabled\":" + String(pc.attractEnabled ? "true" : "false");
    json += ",\"speedColor\":" + String(pc.speedColor ? "true" : "false");
    json += ",\"springStrength\":" + String(pc.springStrength, 3);
    json += ",\"springRange\":" + String(pc.springRange, 3);
    json += ",\"springEnabled\":" + String(pc.springEnabled ? "true" : "false");
    json += ",\"coulombStrength\":" + String(pc.coulombStrength, 3);
    json += ",\"coulombRange\":" + String(pc.coulombRange, 3);
    json += ",\"coulombEnabled\":" + String(pc.coulombEnabled ? "true" : "false");
    json += ",\"scaffoldStrength\":" + String(pc.scaffoldStrength, 3);
    json += ",\"scaffoldRange\":" + String(pc.scaffoldRange, 3);
    json += ",\"scaffoldEnabled\":" + String(pc.scaffoldEnabled ? "true" : "false");
    json += ",\"physicsPaused\":" + String(pc.physicsPaused ? "true" : "false");
    json += ",\"viewRotation\":" + String(angleDeg, 3);
    json += ",\"viewScaleX\":" + String(pc.viewTransform.scaleX, 3);
    json += ",\"viewScaleY\":" + String(pc.viewTransform.scaleY, 3);
    json += ",\"viewTx\":" + String(pc.viewTransform.tx, 3);
    json += ",\"viewTy\":" + String(pc.viewTransform.ty, 3);
    json += "}";
    json += "}";

    out.print(json);
}

void DisplayManager::notifyDisplayState(uint8_t idx) const {
    if (idx >= NUM_DISPLAYS) return;
    Serial.print("DISPLAY_STATE ");
    printDisplayState(idx, Serial);
    Serial.println();
}

void DisplayManager::setAnimation(uint8_t idx, uint8_t animationId) {
    if (idx >= NUM_DISPLAYS) return;
    _anim[idx].selectedId = animationId;
    _stopAnimation(idx);
}

uint8_t DisplayManager::animationId(uint8_t idx) const {
    if (idx >= NUM_DISPLAYS) return 0;
    return _anim[idx].selectedId;
}

const char* DisplayManager::animationName(uint8_t idx) const {
    if (idx >= NUM_DISPLAYS) return "default";

    uint8_t animationId = _anim[idx].selectedId;
    if (animationId == 0) {
        return "default";
    }

    const AnimationScript* script = findAnimationScript(animationId);
    return script ? script->name : "empty";
}

void DisplayManager::startAnimation(uint8_t idx) {
    if (idx >= NUM_DISPLAYS) return;
    _startAnimation(idx);
}

void DisplayManager::setAnimationAll(uint8_t animationId) {
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
        setAnimation(i, animationId);
    }
}

void DisplayManager::stopAnimation(uint8_t idx) {
    if (idx >= NUM_DISPLAYS) return;
    _stopAnimation(idx);
}

void DisplayManager::stopAnimationAll() {
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
        _stopAnimation(i);
    }
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
//  Version bytes detect format changes for manual bank saves and compact autosave.
// ══════════════════════════════════════════════════════════════
static const uint8_t NVS_PARAM_VERSION = 7;
static const uint8_t AUTO_STATE_VERSION = 2;
static constexpr const char* AUTO_STATE_NAMESPACE = "dispauto";
static constexpr const char* AUTO_STATE_KEY = "snapshot";

struct AutoPersistDisplayState {
    uint8_t animationId = 0;
    bool animationRunning = false;
};

struct AutoPersistState {
    uint8_t version = AUTO_STATE_VERSION;
    AutoPersistDisplayState displays[NUM_DISPLAYS];
};

static void _clearBankKeys(Preferences& prefs, uint8_t bank) {
    char key[16];

    auto clearSimple = [&](const char* base) {
        int written = snprintf(key, sizeof(key), "%sb%d", base, (int)bank);
        if (written > 0 && written < (int)sizeof(key)) {
            prefs.remove(key);
        }
    };

    clearSimple("ver");
    clearSimple("bright");
    clearSimple("valid");

    for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
        char base[16];
        snprintf(base, sizeof(base), "mode%d", i);  clearSimple(base);
        snprintf(base, sizeof(base), "color%d", i); clearSimple(base);
        snprintf(base, sizeof(base), "pcfg%d", i);  clearSimple(base);
        snprintf(base, sizeof(base), "sms%d", i);   clearSimple(base);
        snprintf(base, sizeof(base), "scnt%d", i);  clearSimple(base);
        snprintf(base, sizeof(base), "tidx%d", i);  clearSimple(base);
        snprintf(base, sizeof(base), "pen%d", i);   clearSimple(base);
        snprintf(base, sizeof(base), "ani%d", i);   clearSimple(base);
        snprintf(base, sizeof(base), "ar%d", i);    clearSimple(base);
        snprintf(base, sizeof(base), "ten%d", i);   clearSimple(base);
        snprintf(base, sizeof(base), "tbr%d", i);   clearSimple(base);
        snprintf(base, sizeof(base), "pbr%d", i);   clearSimple(base);
        snprintf(base, sizeof(base), "pcl%d", i);   clearSimple(base);
        snprintf(base, sizeof(base), "tsn%d", i);   clearSimple(base);

        for (uint8_t j = 0; j < TEXT_STACK_MAX; j++) {
            snprintf(base, sizeof(base), "ts%d_%d", i, j);
            clearSimple(base);
        }
    }
}

uint8_t DisplayManager::_clampBank(uint8_t bank) const {
    if (bank < 1) return 1;
    if (bank > NVS_BANK_COUNT) return NVS_BANK_COUNT;
    return bank;
}

bool DisplayManager::_makeBankKey(const char* base, uint8_t bank, char* out, size_t outLen) const {
    if (!base || !out || outLen == 0) return false;
    int written = snprintf(out, outLen, "%sb%d", base, (int)_clampBank(bank));
    return written > 0 && (size_t)written < outLen;
}

uint8_t DisplayManager::startupBank() const {
    Preferences p;
    p.begin("disp", true);
    uint8_t bank = p.getUChar("startup", 1);
    p.end();
    return _clampBank(bank);
}

void DisplayManager::setStartupBank(uint8_t bank) {
    uint8_t b = _clampBank(bank);
    Preferences p;
    p.begin("disp", false);
    p.putUChar("startup", b);
    p.end();
    Serial.printf("Startup bank set to %d\n", b);
}

void DisplayManager::loadStartupParams() {
    uint8_t bank = startupBank();
    loadParams(bank);
    _loadAutoState();

    for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
        if (_anim[i].selectedId != 0 && !_anim[i].running) {
            startAnimation(i);
        }
    }
}

void DisplayManager::schedulePersist() {
    if (_persistenceSuspended) return;
    _persistDirty = true;
    _persistDueMs = millis() + PERSIST_DEBOUNCE_MS;
}

void DisplayManager::saveParams() {
    saveParams(startupBank());
}

void DisplayManager::saveParams(uint8_t bank) {
    uint8_t b = _clampBank(bank);
    bool saved = false;

    for (uint8_t attempt = 0; attempt < 2 && !saved; attempt++) {
        _prefs.begin("disp", false);

        if (attempt == 1) {
            _clearBankKeys(_prefs, b);
        }

        bool ok = true;
        char key[16];

        auto putUCharKey = [&](const char* k, uint8_t v) {
            ok = ok && _prefs.putUChar(k, v) == 1;
        };
        auto putBoolKey = [&](const char* k, bool v) {
            ok = ok && _prefs.putBool(k, v) == 1;
        };
        auto putUShortKey = [&](const char* k, uint16_t v) {
            ok = ok && _prefs.putUShort(k, v) == 2;
        };

        if (_makeBankKey("ver", b, key, sizeof(key))) putUCharKey(key, NVS_PARAM_VERSION);
        if (_makeBankKey("bright", b, key, sizeof(key))) putUCharKey(key, _brightness);

        for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
            VirtualDisplay* vd = _vDisplays[i];
            const DisplayModeConfig& mc = vd->modeConfig();
            const ParticleModeConfig& p = mc.particles;
            char base[16];

            snprintf(base, sizeof(base), "mode%d", i);
            if (_makeBankKey(base, b, key, sizeof(key))) putUCharKey(key, (uint8_t)mc.mode);

            snprintf(base, sizeof(base), "color%d", i);
            if (_makeBankKey(base, b, key, sizeof(key))) putUShortKey(key, vd->color());

            snprintf(base, sizeof(base), "pcfg%d", i);
            if (_makeBankKey(base, b, key, sizeof(key))) {
                ok = ok && _prefs.putBytes(key, &p, sizeof(ParticleModeConfig)) == sizeof(ParticleModeConfig);
            }

            snprintf(base, sizeof(base), "sms%d", i);
            if (_makeBankKey(base, b, key, sizeof(key))) putUCharKey(key, mc.scroll.scrollStepMs);

            snprintf(base, sizeof(base), "scnt%d", i);
            if (_makeBankKey(base, b, key, sizeof(key))) putBoolKey(key, mc.scroll.continuous);

            snprintf(base, sizeof(base), "tidx%d", i);
            if (_makeBankKey(base, b, key, sizeof(key))) putUCharKey(key, mc.text.textIndex);

            snprintf(base, sizeof(base), "pen%d", i);
            if (_makeBankKey(base, b, key, sizeof(key))) putBoolKey(key, mc.particlesEnabled);

            snprintf(base, sizeof(base), "ani%d", i);
            if (_makeBankKey(base, b, key, sizeof(key))) putUCharKey(key, _anim[i].selectedId);

            snprintf(base, sizeof(base), "ar%d", i);
            if (_makeBankKey(base, b, key, sizeof(key))) putBoolKey(key, _anim[i].running);

            snprintf(base, sizeof(base), "ten%d", i);
            if (_makeBankKey(base, b, key, sizeof(key))) putBoolKey(key, mc.textEnabled);

            snprintf(base, sizeof(base), "tbr%d", i);
            if (_makeBankKey(base, b, key, sizeof(key))) putUCharKey(key, mc.textBrightness);

            snprintf(base, sizeof(base), "pbr%d", i);
            if (_makeBankKey(base, b, key, sizeof(key))) putUCharKey(key, mc.particleBrightness);

            snprintf(base, sizeof(base), "pcl%d", i);
            if (_makeBankKey(base, b, key, sizeof(key))) putUShortKey(key, mc.particleColor);

            snprintf(base, sizeof(base), "tsn%d", i);
            if (_makeBankKey(base, b, key, sizeof(key))) putUCharKey(key, vd->textCount());

            for (uint8_t j = 0; j < vd->textCount(); j++) {
                snprintf(base, sizeof(base), "ts%d_%d", i, j);
                if (_makeBankKey(base, b, key, sizeof(key))) {
                    ok = ok && _prefs.putString(key, vd->textGet(j)) > 0;
                }
            }
        }

        if (_makeBankKey("valid", b, key, sizeof(key))) putBoolKey(key, true);
        _prefs.end();

        if (ok) {
            saved = true;
        } else if (attempt == 0) {
            Serial.printf("NVS save bank %d hit space/entry limit, retrying after cleanup\n", b);
        }
    }

    if (saved) {
        Serial.printf("Params saved to NVS bank %d (v%d)\n", b, NVS_PARAM_VERSION);
    } else {
        Serial.printf("ERROR saving params to NVS bank %d: insufficient NVS space\n", b);
    }
}

void DisplayManager::loadParams() {
    loadParams(startupBank());
}

void DisplayManager::loadParams(uint8_t bank) {
    uint8_t b = _clampBank(bank);
    _persistenceSuspended = true;
    _prefs.begin("disp", true);

    char key[16];
    if (_makeBankKey("valid", b, key, sizeof(key)) && !_prefs.getBool(key, false)) {
        _prefs.end();
        _persistenceSuspended = false;
        Serial.printf("No saved params in NVS bank %d\n", b);
        return;
    }

    uint8_t ver = 1;
    if (_makeBankKey("ver", b, key, sizeof(key))) ver = _prefs.getUChar(key, 1);

    if (_makeBankKey("bright", b, key, sizeof(key))) {
        setBrightness(_prefs.getUChar(key, DEFAULT_BRIGHTNESS));
    }

    for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
        VirtualDisplay* vd = _vDisplays[i];
        char base[16];
        uint8_t animationId = 0;
        bool animationRunning = false;

        snprintf(base, sizeof(base), "color%d", i);
        if (_makeBankKey(base, b, key, sizeof(key))) vd->setColor(_prefs.getUShort(key, 0xFFFF));

        snprintf(base, sizeof(base), "pcfg%d", i);
        ParticleModeConfig pcfg;
        memset(&pcfg, 0, sizeof(pcfg));
        if (_makeBankKey(base, b, key, sizeof(key))) {
            if (_prefs.getBytes(key, &pcfg, sizeof(ParticleModeConfig)) != sizeof(ParticleModeConfig)) {
                pcfg = ParticleModeConfig();
            }
        } else {
            pcfg = ParticleModeConfig();
        }

        DisplayModeConfig mc;
        snprintf(base, sizeof(base), "mode%d", i);
        if (_makeBankKey(base, b, key, sizeof(key))) {
            mc.mode = (DisplayMode)_prefs.getUChar(key, (uint8_t)DISPLAY_MODE_TEXT);
        }

        snprintf(base, sizeof(base), "sms%d", i);
        if (_makeBankKey(base, b, key, sizeof(key))) mc.scroll.scrollStepMs = _prefs.getUChar(key, SCROLL_STEP_MS);

        if (ver >= 2) {
            snprintf(base, sizeof(base), "scnt%d", i);
            if (_makeBankKey(base, b, key, sizeof(key))) mc.scroll.continuous = _prefs.getBool(key, false);

            snprintf(base, sizeof(base), "tidx%d", i);
            if (_makeBankKey(base, b, key, sizeof(key))) mc.text.textIndex = _prefs.getUChar(key, 0);

            uint8_t tsCount = 0;
            snprintf(base, sizeof(base), "tsn%d", i);
            if (_makeBankKey(base, b, key, sizeof(key))) tsCount = _prefs.getUChar(key, 0);
            vd->textClear();
            for (uint8_t j = 0; j < tsCount && j < TEXT_STACK_MAX; j++) {
                snprintf(base, sizeof(base), "ts%d_%d", i, j);
                String s = "";
                if (_makeBankKey(base, b, key, sizeof(key))) s = _prefs.getString(key, "");
                if (s.length() > 0) vd->textPush(s.c_str());
            }
        }

        if (ver >= 3) {
            snprintf(base, sizeof(base), "pen%d", i);
            if (_makeBankKey(base, b, key, sizeof(key))) mc.particlesEnabled = _prefs.getBool(key, false);
        }

        if (ver >= 4) {
            snprintf(base, sizeof(base), "ten%d", i);
            if (_makeBankKey(base, b, key, sizeof(key))) mc.textEnabled = _prefs.getBool(key, true);

            snprintf(base, sizeof(base), "tbr%d", i);
            if (_makeBankKey(base, b, key, sizeof(key))) mc.textBrightness = _prefs.getUChar(key, 255);

            snprintf(base, sizeof(base), "pbr%d", i);
            if (_makeBankKey(base, b, key, sizeof(key))) mc.particleBrightness = _prefs.getUChar(key, 255);

            snprintf(base, sizeof(base), "pcl%d", i);
            if (_makeBankKey(base, b, key, sizeof(key))) mc.particleColor = _prefs.getUShort(key, 0xFFFF);
        }

        if (ver >= 6) {
            snprintf(base, sizeof(base), "ani%d", i);
            if (_makeBankKey(base, b, key, sizeof(key))) animationId = _prefs.getUChar(key, 0);
        }

        if (ver >= 7) {
            snprintf(base, sizeof(base), "ar%d", i);
            if (_makeBankKey(base, b, key, sizeof(key))) animationRunning = _prefs.getBool(key, false);
        }

        mc.particles = pcfg;
        vd->setMode(mc);
        setAnimation(i, animationId);
        if (animationRunning && animationId != 0) {
            startAnimation(i);
        }
    }

    _prefs.end();
    _persistDirty = false;
    _persistenceSuspended = false;
    Serial.printf("Params loaded from NVS bank %d (v%d)\n", b, ver);
}

void DisplayManager::_persistIfDue() {
    if (_persistenceSuspended || !_persistDirty) return;
    unsigned long now = millis();
    if ((long)(now - _persistDueMs) < 0) return;
    if (_saveAutoState()) {
        _persistDirty = false;
        return;
    }
    _persistDueMs = now + PERSIST_DEBOUNCE_MS;
}

bool DisplayManager::_saveAutoState() {
    Preferences prefs;
    if (!prefs.begin(AUTO_STATE_NAMESPACE, false)) {
        Serial.println("ERROR saving auto state: failed to open NVS namespace");
        return false;
    }

    AutoPersistState state;
    state.version = AUTO_STATE_VERSION;

    for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
        AutoPersistDisplayState& saved = state.displays[i];
        saved.animationId = _anim[i].selectedId;
        saved.animationRunning = _anim[i].running;
    }

    prefs.clear();
    bool ok = prefs.putBytes(AUTO_STATE_KEY, &state, sizeof(state)) == sizeof(state);
    prefs.end();
    if (!ok) {
        Serial.println("ERROR saving auto state: insufficient NVS space");
    }
    return ok;
}

bool DisplayManager::_loadAutoState() {
    Preferences prefs;
    if (!prefs.begin(AUTO_STATE_NAMESPACE, true)) {
        return false;
    }

    AutoPersistState state;
    size_t bytes = prefs.getBytes(AUTO_STATE_KEY, &state, sizeof(state));
    prefs.end();

    if (bytes != sizeof(state) || state.version != AUTO_STATE_VERSION) {
        return false;
    }

    _persistenceSuspended = true;

    for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
        const AutoPersistDisplayState& saved = state.displays[i];
        setAnimation(i, saved.animationId);
        if (saved.animationRunning && saved.animationId != 0) {
            startAnimation(i);
        }
    }

    _persistDirty = false;
    _persistenceSuspended = false;
    Serial.println("Auto animation state restored from NVS");
    return true;
}

void DisplayManager::_onTextSet(uint8_t idx) {
    if (idx >= NUM_DISPLAYS) return;
    if (_anim[idx].selectedId == 0) return;
    _startAnimation(idx);
}

void DisplayManager::_startAnimation(uint8_t idx) {
    if (idx >= NUM_DISPLAYS) return;
    const AnimationScript* script = findAnimationScript(_anim[idx].selectedId);
    if (!script || script->stepCount == 0) {
        _stopAnimation(idx);
        return;
    }
    _anim[idx].running = true;
    _anim[idx].step = 0;
    _anim[idx].stepStartedMs = millis();
    _anim[idx].triggeredTextIndex++;
    _anim[idx].gotoSourceStep = -1;
    _anim[idx].gotoRemaining = 0;

    _applyAnimationStep(idx, script->steps[0]);
    schedulePersist();
    Serial.printf("D%d animation %d started (%s)\n", idx + 1, script->id, script->name);
}

void DisplayManager::_stopAnimation(uint8_t idx) {
    if (idx >= NUM_DISPLAYS) return;
    bool wasRunning = _anim[idx].running;
    _anim[idx].running = false;
    _anim[idx].step = 0;
    _anim[idx].stepStartedMs = 0;
    _anim[idx].gotoSourceStep = -1;
    _anim[idx].gotoRemaining = 0;
    if (wasRunning) {
        schedulePersist();
    }
}

static bool _stepAppliesToDisplay(const AnimationStep& step, uint8_t idx) {
    // 0 means "all displays" for backward compatibility.
    if (step.targetDisplayMask == 0) return true;
    if (idx >= 32) return false;
    return (step.targetDisplayMask & (1ul << idx)) != 0;
}

void DisplayManager::_applyAnimationStep(uint8_t idx, const AnimationStep& step) {
    VirtualDisplay* vd = _vDisplays[idx];
    if (!vd) return;
    if (!_stepAppliesToDisplay(step, idx)) return;

    if (step.setMode) vd->setMode(step.mode);
    if (step.setTextEnabled) vd->setTextEnabled(step.textEnabled);
    if (step.setParticlesEnabled) vd->setParticlesEnabled(step.particlesEnabled);

    if (step.doTextToParticles) vd->textToParticles();
    if (step.doScreenToParticles) vd->screenToParticles();

    if (step.setParticleConfig) {
        ParticleModeConfig cfg = step.replaceParticleConfig
            ? step.particleConfig
            : vd->modeConfig().particles;

        if (!step.replaceParticleConfig) {
            uint32_t mask = step.particleFieldMask;
            if (mask == 0) {
                // Backward compatibility for pre-mask compiled scripts.
                cfg.gravityEnabled = step.particleConfig.gravityEnabled;
                cfg.collisionEnabled = step.particleConfig.collisionEnabled;
                cfg.scaffoldEnabled = step.particleConfig.scaffoldEnabled;
                cfg.scaffoldStrength = step.particleConfig.scaffoldStrength;
                cfg.scaffoldRange = step.particleConfig.scaffoldRange;
                cfg.temperature = step.particleConfig.temperature;
            } else {
                if (mask & AnimationStep::PARTICLE_COUNT) cfg.count = step.particleConfig.count;
                if (mask & AnimationStep::PARTICLE_RENDER_MS) cfg.renderMs = step.particleConfig.renderMs;
                if (mask & AnimationStep::PARTICLE_SUBSTEP_MS) cfg.substepMs = step.particleConfig.substepMs;
                if (mask & AnimationStep::PARTICLE_RADIUS) cfg.radius = step.particleConfig.radius;
                if (mask & AnimationStep::PARTICLE_GRAVITY_SCALE) cfg.gravityScale = step.particleConfig.gravityScale;
                if (mask & AnimationStep::PARTICLE_ELASTICITY) cfg.elasticity = step.particleConfig.elasticity;
                if (mask & AnimationStep::PARTICLE_WALL_ELASTICITY) cfg.wallElasticity = step.particleConfig.wallElasticity;
                if (mask & AnimationStep::PARTICLE_DAMPING) cfg.damping = step.particleConfig.damping;
                if (mask & AnimationStep::PARTICLE_TEMPERATURE) cfg.temperature = step.particleConfig.temperature;
                if (mask & AnimationStep::PARTICLE_ATTRACT_STRENGTH) cfg.attractStrength = step.particleConfig.attractStrength;
                if (mask & AnimationStep::PARTICLE_ATTRACT_RANGE) cfg.attractRange = step.particleConfig.attractRange;
                if (mask & AnimationStep::PARTICLE_GRAVITY_ENABLED) cfg.gravityEnabled = step.particleConfig.gravityEnabled;
                if (mask & AnimationStep::PARTICLE_COLLISION_ENABLED) cfg.collisionEnabled = step.particleConfig.collisionEnabled;
                if (mask & AnimationStep::PARTICLE_SPRING_STRENGTH) cfg.springStrength = step.particleConfig.springStrength;
                if (mask & AnimationStep::PARTICLE_SPRING_RANGE) cfg.springRange = step.particleConfig.springRange;
                if (mask & AnimationStep::PARTICLE_SPRING_ENABLED) cfg.springEnabled = step.particleConfig.springEnabled;
                if (mask & AnimationStep::PARTICLE_COULOMB_STRENGTH) cfg.coulombStrength = step.particleConfig.coulombStrength;
                if (mask & AnimationStep::PARTICLE_COULOMB_RANGE) cfg.coulombRange = step.particleConfig.coulombRange;
                if (mask & AnimationStep::PARTICLE_COULOMB_ENABLED) cfg.coulombEnabled = step.particleConfig.coulombEnabled;
                if (mask & AnimationStep::PARTICLE_SCAFFOLD_STRENGTH) cfg.scaffoldStrength = step.particleConfig.scaffoldStrength;
                if (mask & AnimationStep::PARTICLE_SCAFFOLD_RANGE) cfg.scaffoldRange = step.particleConfig.scaffoldRange;
                if (mask & AnimationStep::PARTICLE_SCAFFOLD_ENABLED) cfg.scaffoldEnabled = step.particleConfig.scaffoldEnabled;
                if (mask & AnimationStep::PARTICLE_RENDER_STYLE) cfg.renderStyle = step.particleConfig.renderStyle;
                if (mask & AnimationStep::PARTICLE_GLOW_SIGMA) cfg.glowSigma = step.particleConfig.glowSigma;
                if (mask & AnimationStep::PARTICLE_GLOW_WAVELENGTH) cfg.glowWavelength = step.particleConfig.glowWavelength;
                if (mask & AnimationStep::PARTICLE_SPEED_COLOR) cfg.speedColor = step.particleConfig.speedColor;
                if (mask & AnimationStep::PARTICLE_PHYSICS_PAUSED) cfg.physicsPaused = step.particleConfig.physicsPaused;
                if (mask & AnimationStep::PARTICLE_TEXT_INDEX) cfg.textIndex = step.particleConfig.textIndex;
            }
        }

        vd->setParticleConfig(cfg);
    }

    if (step.setPhysicsPaused) vd->setPhysicsPaused(step.physicsPaused);

    notifyDisplayState(idx);
}

void DisplayManager::_tickAnimations() {
    unsigned long now = millis();

    for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
        AnimationRuntime& rt = _anim[i];
        if (!rt.running || rt.selectedId == 0) continue;

        const AnimationScript* script = findAnimationScript(rt.selectedId);
        if (!script || script->stepCount == 0) {
            _stopAnimation(i);
            continue;
        }

        if (rt.step >= script->stepCount) {
            _stopAnimation(i);
            continue;
        }

        const AnimationStep& curr = script->steps[rt.step];
        const bool applies = _stepAppliesToDisplay(curr, i);
        const unsigned long effectiveWaitMs = applies ? curr.waitMs : 0;
        if (effectiveWaitMs == 0 || (now - rt.stepStartedMs) >= effectiveWaitMs) {
            // Flow control: optional goto after current step delay.
            if (applies && curr.gotoStep >= 0 && curr.gotoStep < script->stepCount && curr.gotoRepeat != 0) {
                if (curr.gotoRepeat < 0) {
                    rt.step = (uint8_t)curr.gotoStep;
                    _applyAnimationStep(i, script->steps[rt.step]);
                    rt.stepStartedMs = now;
                    continue;
                }

                if (rt.gotoSourceStep != rt.step) {
                    rt.gotoSourceStep = rt.step;
                    rt.gotoRemaining = curr.gotoRepeat;
                }

                if (rt.gotoRemaining > 0) {
                    rt.gotoRemaining--;
                    rt.step = (uint8_t)curr.gotoStep;
                    _applyAnimationStep(i, script->steps[rt.step]);
                    rt.stepStartedMs = now;
                    continue;
                }

                // Finite goto exhausted: fall through to normal next-step progression.
                rt.gotoSourceStep = -1;
                rt.gotoRemaining = 0;
            }

            rt.step++;
            if (rt.step >= script->stepCount) {
                _stopAnimation(i);
                Serial.printf("D%d animation %d complete\n", i + 1, script->id);
                continue;
            }
            _applyAnimationStep(i, script->steps[rt.step]);
            rt.stepStartedMs = now;
        }
    }
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


