#include "VirtualDisplay.h"
#include <cstring>
#include <math.h>

// RGB565 pack — same formula as Adafruit_SPITFT::color565()
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// ── Heat-map: velocity magnitude → RGB565 (dim white→blue→cyan→green→yellow→red) ─
static uint16_t _heatColor(float speed, float maxSpeed) {
    float t = speed / maxSpeed;
    if (t > 1.0f) t = 1.0f;
    // Floor: stationary particles show as dim white so they stay visible
    const float floor = 0.12f;   // ~30/255 per channel
    // 5-stop heat ramp mapped to 0..1
    float r, g, b;
    if (t < 0.2f)      { float s = t / 0.2f;       r = floor*(1-s);  g = floor*(1-s);  b = floor + (1-floor)*s; }  // dim white→blue
    else if (t < 0.4f) { float s = (t-0.2f)/0.2f;  r = 0;    g = s;    b = 1;    }   // blue→cyan
    else if (t < 0.6f) { float s = (t-0.4f)/0.2f;  r = 0;    g = 1;    b = 1-s;  }   // cyan→green
    else if (t < 0.8f) { float s = (t-0.6f)/0.2f;  r = s;    g = 1;    b = 0;    }   // green→yellow
    else               { float s = (t-0.8f)/0.2f;  r = 1;    g = 1-s;  b = 0;    }   // yellow→red
    return rgb565((uint8_t)(r*255), (uint8_t)(g*255), (uint8_t)(b*255));
}

// Update all particle colours from their velocity magnitude
static void _applySpeedColors(ParticleSystem& ps) {
    uint16_t n = ps.count();
    // Find max speed to normalise
    float maxSpd = 0.001f; // avoid div-by-zero
    for (uint16_t i = 0; i < n; i++) {
        float spd = ps.particle(i).vel.length();
        if (spd > maxSpd) maxSpd = spd;
    }
    for (uint16_t i = 0; i < n; i++) {
        Particle& p = ps.particle(i);
        p.color = _heatColor(p.vel.length(), maxSpd);
    }
}

// ── Constructor ──────────────────────────────────────────────
VirtualDisplay::VirtualDisplay(uint16_t w, uint16_t h)
    : GFXcanvas16(w, h),
      _textStackCount(0),
      _color(0xFFFF),          // white in RGB565
      _dirty(true),
      _modeConfig(),
      _scrollOffset(0),
      _scrollLastStep(0),
      _scrollJustDone(false),
      _continuousIdx(0),
      _lastParticleStep(0),
      _queueHead(0),
      _queueTail(0),
      _queueCount(0)
{
    _text[0]    = '\0';
    _oldText[0] = '\0';
    memset(_textStack, 0, sizeof(_textStack));
    setTextWrap(false);
    setTextSize(1);
    fillScreen(0);
}

// ── High-level text / color setters ──────────────────────────
void VirtualDisplay::setText(const char* text) {
    if (!text) text = "";

    // In static text mode, skip if text is identical (no visible change)
    if (strcmp(_text, text) == 0 && !_isScrollMode()) return;

    // Always push to the text stack
    textPush(text);

    if (_isScrollMode()) {
        if (_scrollOffset > 0) {
            // Scroll in progress — queue if room
            if (_queueCount < SCROLL_QUEUE_SIZE) {
                strncpy(_queue[_queueTail], text, TEXT_MAX_LEN - 1);
                _queue[_queueTail][TEXT_MAX_LEN - 1] = '\0';
                _queueTail = (_queueTail + 1) % SCROLL_QUEUE_SIZE;
                _queueCount++;
            }
            return;
        }
        _startScroll(text);
    } else {
        // Immediate mode: show the text now
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

void VirtualDisplay::setParticleColor(uint8_t r, uint8_t g, uint8_t b) {
    setParticleColor(rgb565(r, g, b));
}

void VirtualDisplay::setParticleColor(uint16_t c565) {
    _modeConfig.particleColor = c565;
    // Propagate to live particles (unless speedColor overrides)
    if (_modeConfig.particlesEnabled && _particleSys.isInitialised()
        && !_modeConfig.particles.speedColor) {
        for (uint16_t i = 0; i < _particleSys.count(); i++) {
            _particleSys.particle(i).color = c565;
        }
    }
    _dirty = true;
}

void VirtualDisplay::setTextEnabled(bool enabled) {
    _modeConfig.textEnabled = enabled;
    _dirty = true;
}

void VirtualDisplay::setTextBrightness(uint8_t b) {
    _modeConfig.textBrightness = b;
    _dirty = true;
}

void VirtualDisplay::setParticleBrightness(uint8_t b) {
    _modeConfig.particleBrightness = b;
    _dirty = true;
}

// Scale an RGB565 colour by brightness (0-255)
uint16_t VirtualDisplay::dimColor565(uint16_t c, uint8_t brightness) {
    if (brightness == 255) return c;
    if (brightness == 0)   return 0;
    uint8_t r = (c >> 8) & 0xF8;
    uint8_t g = (c >> 3) & 0xFC;
    uint8_t b = (c << 3) & 0xF8;
    r = (uint8_t)(((uint16_t)r * brightness) >> 8);
    g = (uint8_t)(((uint16_t)g * brightness) >> 8);
    b = (uint8_t)(((uint16_t)b * brightness) >> 8);
    return rgb565(r, g, b);
}

void VirtualDisplay::clear() {
    _text[0] = '\0';
    _oldText[0] = '\0';
    _scrollOffset = 0;
    _scrollJustDone = false;
    _continuousIdx = 0;
    _particleSys.reset();
    _lastParticleStep = 0;
    _resetQueue();
    _modeConfig.mode = DISPLAY_MODE_TEXT;
    _modeConfig.particlesEnabled = false;
    fillScreen(0);
    _dirty = true;
}

// ── Text stack ───────────────────────────────────────────────
int8_t VirtualDisplay::textPush(const char* text) {
    if (_textStackCount >= TEXT_STACK_MAX) return -1;
    strncpy(_textStack[_textStackCount], text ? text : "", TEXT_MAX_LEN - 1);
    _textStack[_textStackCount][TEXT_MAX_LEN - 1] = '\0';
    _dirty = true;
    return _textStackCount++;
}

bool VirtualDisplay::textPop() {
    if (_textStackCount == 0) return false;
    _textStackCount--;
    _textStack[_textStackCount][0] = '\0';
    _dirty = true;
    return true;
}

bool VirtualDisplay::textSet(uint8_t index, const char* text) {
    if (index >= TEXT_STACK_MAX) return false;
    strncpy(_textStack[index], text ? text : "", TEXT_MAX_LEN - 1);
    _textStack[index][TEXT_MAX_LEN - 1] = '\0';
    if (index >= _textStackCount) _textStackCount = index + 1;
    _dirty = true;
    return true;
}

const char* VirtualDisplay::textGet(uint8_t index) const {
    if (index >= _textStackCount) return "";
    return _textStack[index];
}

void VirtualDisplay::textClear() {
    _textStackCount = 0;
    memset(_textStack, 0, sizeof(_textStack));
    _text[0] = '\0';
    _oldText[0] = '\0';
    _scrollOffset = 0;
    _continuousIdx = 0;
    _resetQueue();
    fillScreen(0);
    _dirty = true;
}

const char* VirtualDisplay::_activeText() const {
    uint8_t idx = 0;
    switch (_modeConfig.mode) {
        case DISPLAY_MODE_TEXT:
            // Immediate mode: show the last (most recent) stack entry
            if (_textStackCount > 0) return _textStack[_textStackCount - 1];
            return _text;
        default:
            // Scroll modes use _text directly (managed by scroll animation)
            return _text;
    }
    if (idx < _textStackCount) return _textStack[idx];
    return _text;
}

// ── Display mode ─────────────────────────────────────────────
void VirtualDisplay::setMode(DisplayMode mode) {
    DisplayModeConfig config = _modeConfig;
    config.mode = mode;
    setMode(config);
}

void VirtualDisplay::setMode(const DisplayModeConfig& config) {
    _modeConfig = config;

    if (_modeConfig.scroll.scrollStepMs == 0) {
        _modeConfig.scroll.scrollStepMs = 1;
    }
    if (_modeConfig.particles.renderMs == 0) {
        _modeConfig.particles.renderMs = 1;
    }

    if (_modeConfig.mode == DISPLAY_MODE_TEXT) {
        // Flush the queue — show last queued value instantly
        if (_queueCount > 0) {
            uint8_t lastIdx = (_queueTail + SCROLL_QUEUE_SIZE - 1)
                              % SCROLL_QUEUE_SIZE;
            strncpy(_text, _queue[lastIdx], TEXT_MAX_LEN - 1);
            _text[TEXT_MAX_LEN - 1] = '\0';
        }
        _resetQueue();
        _scrollOffset = 0;
        _oldText[0] = '\0';
        // Resolve text from stack
        const char* t = _activeText();
        if (t != _text) {
            strncpy(_text, t, sizeof(_text) - 1);
            _text[sizeof(_text) - 1] = '\0';
        }
        _dirty = true;
    } else {
        // Scroll mode
        _scrollOffset = 0;
        _oldText[0] = '\0';
        _resetQueue();

        // For continuous scroll: kick off first scroll immediately
        if (_isScrollMode() && _modeConfig.scroll.continuous && _textStackCount > 0) {
            _continuousIdx = 0;
            _startScroll(_textStack[0]);
        }
        _dirty = true;
    }

    // Init or reconfigure particles if enabled
    if (_modeConfig.particlesEnabled) {
        _particleSys.setConfig(_modeConfig.particles.toSystemConfig());
        if (!_particleSys.isInitialised()) {
            _particleSys.reset();
            _lastParticleStep = millis();
            _lastParticleRender = _lastParticleStep;
        }
    }
}

void VirtualDisplay::setScrollMode(uint8_t mode) {
    if (mode > DISPLAY_MODE_SCROLL_DOWN) mode = DISPLAY_MODE_TEXT;
    setMode((DisplayMode)mode);
}

void VirtualDisplay::setScrollSpeed(uint8_t ms) {
    _modeConfig.scroll.scrollStepMs = ms > 0 ? ms : 1;
}

void VirtualDisplay::setScrollContinuous(bool enabled) {
    _modeConfig.scroll.continuous = enabled;
    if (enabled && _isScrollMode() && _textStackCount > 0 && _scrollOffset == 0) {
        _continuousIdx = 0;
        _startScroll(_textStack[0]);
    }
}

void VirtualDisplay::setParticlesEnabled(bool enabled) {
    _modeConfig.particlesEnabled = enabled;
    if (enabled) {
        _particleSys.setConfig(_modeConfig.particles.toSystemConfig());
        if (!_particleSys.isInitialised()) {
            _particleSys.init((float)width(), (float)height(), _modeConfig.particleColor);
        }
        _lastParticleStep = millis();
        _lastParticleRender = _lastParticleStep;
    }
    _dirty = true;
}

void VirtualDisplay::setGravity(float gx, float gy) {
    _particleSys.setGravity(gx, gy);
}

void VirtualDisplay::setParticleConfig(const ParticleModeConfig& cfg) {
    bool wasSpeedColor = _modeConfig.particles.speedColor;
    _modeConfig.particles = cfg;
    if (_modeConfig.particles.renderMs == 0) _modeConfig.particles.renderMs = 1;
    _particleSys.setConfig(cfg.toSystemConfig());

    // Restore original colours when speedColor is switched off
    if (wasSpeedColor && !cfg.speedColor) {
        _particleSys.restoreColorsFromScaffold();
    }

    _dirty = true;
}

void VirtualDisplay::setPhysicsPaused(bool paused) {
    _modeConfig.particles.physicsPaused = paused;
    _dirty = true;
}

void VirtualDisplay::restoreScaffoldPositions() {
    _particleSys.restorePositionsFromScaffold();
    _dirty = true;
}

void VirtualDisplay::restoreScaffoldColors() {
    _particleSys.restoreColorsFromScaffold();
    _dirty = true;
}

// ── View transform setters ───────────────────────────────────

void VirtualDisplay::setParticleTransform(const ParticleTransform2D& t) {
    _modeConfig.particles.viewTransform = t;
    _dirty = true;
}

void VirtualDisplay::setParticleRotation(float angleDeg) {
    _modeConfig.particles.viewTransform.angle = angleDeg * (3.14159265f / 180.0f);
    _dirty = true;
}

void VirtualDisplay::setParticleScale(float sx, float sy) {
    _modeConfig.particles.viewTransform.scaleX = sx;
    _modeConfig.particles.viewTransform.scaleY = sy;
    _dirty = true;
}

void VirtualDisplay::setParticleTranslation(float tx, float ty) {
    _modeConfig.particles.viewTransform.tx = tx;
    _modeConfig.particles.viewTransform.ty = ty;
    _dirty = true;
}

void VirtualDisplay::resetParticleTransform() {
    _modeConfig.particles.viewTransform = ParticleTransform2D();
    _dirty = true;
}

uint16_t VirtualDisplay::textToParticles() {
    // 1. Render text to canvas so we can scan the bitmap
    fillScreen(0);
    const char* txt = _activeText();
    if (!txt || txt[0] == '\0') return 0;

    const char* visible = _fitTextRight(txt);
    if (!visible || *visible == '\0') return 0;

    setTextColor(_color);
    int16_t x = _centerTextX(visible);
    setCursor(x, 0);
    print(visible);

    // 2. Scan canvas for lit pixels → collect positions
    uint16_t w = width();
    uint16_t h = height();
    Vec2f positions[ParticleSystem::MAX_PARTICLES];
    uint16_t count = 0;

    const uint16_t* buf = getBuffer();
    for (uint16_t py = 0; py < h && count < ParticleSystem::MAX_PARTICLES; py++) {
        for (uint16_t px = 0; px < w && count < ParticleSystem::MAX_PARTICLES; px++) {
            if (buf[py * w + px] != 0) {
                // Integer coords, not +0.5 centred: lroundf(N+0.5)==N+1
                // which shifts every particle 1 pixel right/down in all modes.
                positions[count++] = Vec2f((float)px, (float)py);
            }
        }
    }

    if (count == 0) return 0;

    // 3. Configure particles for glow, physics paused, scaffold spring active
    _modeConfig.particles.count        = count;
    _modeConfig.particles.renderStyle  = ParticleModeConfig::RENDER_GLOW;
    _modeConfig.particles.glowSigma    = 0.6f;   // tight glow for sharp text
    _modeConfig.particles.physicsPaused = true;
    _modeConfig.particles.gravityEnabled = true;
    _modeConfig.particles.radius       = 0.35f;
    _modeConfig.particles.collisionEnabled = false; // scaffold text: no bumping
    _modeConfig.particles.scaffoldEnabled  = true;  // spring to original positions
    _modeConfig.particles.scaffoldStrength = 1.0f;  // moderate pull
    _modeConfig.particles.scaffoldRange    = 10.0f;
    // Keep other physics params (temperature, damping, etc.) as they are

    // 4. Create particles at the scanned positions
    _particleSys.setConfig(_modeConfig.particles.toSystemConfig());
    _particleSys.initFromPositions(positions, count,
                                    (float)w, (float)h,
                                    _modeConfig.particles.radius,
                                    _modeConfig.particleColor);

    // 5. Enable particles (keep text layer unchanged)
    _modeConfig.particlesEnabled = true;
    _lastParticleStep  = millis();
    _lastParticleRender = _lastParticleStep;
    _dirty = true;

    Serial.printf("TEXT2PARTICLES %d\n", count);
    return count;
}

uint16_t VirtualDisplay::screenToParticles() {
    // Scan the current canvas buffer as-is — whatever was rendered
    // (text, particles, or any GFX drawing).  Each lit pixel becomes
    // a particle whose colour matches the original pixel.
    uint16_t w = width();
    uint16_t h = height();
    Vec2f    positions[ParticleSystem::MAX_PARTICLES];
    uint16_t colors[ParticleSystem::MAX_PARTICLES];
    uint16_t count = 0;

    const uint16_t* buf = getBuffer();
    for (uint16_t py = 0; py < h && count < ParticleSystem::MAX_PARTICLES; py++) {
        for (uint16_t px = 0; px < w && count < ParticleSystem::MAX_PARTICLES; px++) {
            uint16_t c = buf[py * w + px];
            if (c != 0) {
                positions[count] = Vec2f((float)px, (float)py);
                colors[count]    = c;
                count++;
            }
        }
    }

    if (count == 0) return 0;

    // Configure particles for glow, physics paused, scaffold spring active
    _modeConfig.particles.count         = count;
    _modeConfig.particles.renderStyle   = ParticleModeConfig::RENDER_GLOW;
    _modeConfig.particles.glowSigma     = 0.6f;
    _modeConfig.particles.physicsPaused = true;
    _modeConfig.particles.gravityEnabled = true;
    _modeConfig.particles.radius        = 0.35f;
    _modeConfig.particles.collisionEnabled = false; // scaffold: no bumping
    _modeConfig.particles.scaffoldEnabled  = true;  // spring to original positions
    _modeConfig.particles.scaffoldStrength = 1.0f;
    _modeConfig.particles.scaffoldRange    = 10.0f;

    // Create particles at scanned positions (uniform color placeholder)
    _particleSys.setConfig(_modeConfig.particles.toSystemConfig());
    _particleSys.initFromPositions(positions, count,
                                    (float)w, (float)h,
                                    _modeConfig.particles.radius,
                                    0xFFFF);

    // Overwrite each particle's colour with its original pixel colour
    for (uint16_t i = 0; i < count; i++) {
        _particleSys.particle(i).color = colors[i];
    }
    // Re-snapshot scaffold so it captures the per-pixel colours
    _particleSys.saveScaffold();

    // Enable particles
    _modeConfig.particlesEnabled = true;
    _lastParticleStep  = millis();
    _lastParticleRender = _lastParticleStep;
    _dirty = true;

    Serial.printf("SCREEN2PARTICLES %d\n", count);
    return count;
}

void VirtualDisplay::clearQueue() {
    _resetQueue();
}

// ── Per-frame update (call from DisplayManager::update) ──────
bool VirtualDisplay::update() {
    bool changed = false;
    bool particlesRedrawn = false;

    // Step 1: Advance particle physics & check if render is due
    if (_modeConfig.particlesEnabled) {
        particlesRedrawn = _updateParticles();
        changed = particlesRedrawn;
    }

    bool particlesActive = _modeConfig.particlesEnabled && _particleSys.isInitialised();

    // Step 2: Text layer
    if (_modeConfig.textEnabled) {
        if (particlesActive) {
            // ── Compositing mode: rebuild full frame only when needed ──
            bool scrollStep = false;
            if (_isScrollMode() && _scrollOffset > 0) {
                unsigned long now = millis();
                scrollStep = (now - _scrollLastStep >= _modeConfig.scroll.scrollStepMs);
            }

            bool needComposite = particlesRedrawn || _dirty || scrollStep;
            if (needComposite) {
                // Ensure particle layer is freshly drawn (clears canvas)
                if (!particlesRedrawn) _redrawParticleLayer();

                // Overlay text on top (no clear)
                if (_isScrollMode() && _scrollOffset > 0) {
                    if (scrollStep) _scrollLastStep = millis();
                    _renderScrollFrame(false);
                    if (scrollStep) { _scrollOffset++; _checkScrollEnd(); }
                } else {
                    _render(false);
                }
                _dirty = false;
                changed = true;
            }
        } else {
            // ── Text-only mode (original behaviour) ──
            switch (_modeConfig.mode) {
                case DISPLAY_MODE_SCROLL_UP:
                case DISPLAY_MODE_SCROLL_DOWN:
                    if (_scrollOffset > 0)
                        changed |= _updateScroll();
                    else
                        changed |= _updateText();
                    break;
                default:
                    changed |= _updateText();
                    break;
            }
        }
    } else if (!changed && _dirty) {
        // Neither layer active — clear screen once
        fillScreen(0);
        _dirty = false;
        changed = true;
    }

    return changed;
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
//  Private — per-mode update handlers
// ══════════════════════════════════════════════════════════════

bool VirtualDisplay::_updateText() {
    if (!_dirty) return false;
    _render(true);
    _dirty = false;
    return true;
}

bool VirtualDisplay::_updateScroll() {
    unsigned long now = millis();
    if (now - _scrollLastStep < _modeConfig.scroll.scrollStepMs) return false;

    _scrollLastStep = now;
    _renderScrollFrame(true);
    _scrollOffset++;
    return _checkScrollEnd();
}

bool VirtualDisplay::_checkScrollEnd() {
    int16_t totalSteps = (int16_t)height();

    if (_scrollOffset > totalSteps) {
        _scrollOffset = 0;
        _oldText[0] = '\0';
        _scrollJustDone = true;

        if (_queueCount > 0) {
            char next[TEXT_MAX_LEN];
            strncpy(next, _queue[_queueHead], TEXT_MAX_LEN - 1);
            next[TEXT_MAX_LEN - 1] = '\0';
            _queueHead = (_queueHead + 1) % SCROLL_QUEUE_SIZE;
            _queueCount--;
            _startScroll(next);
        } else if (_modeConfig.scroll.continuous && _textStackCount >= 1) {
            // Continuous: advance to next textStack entry and scroll it in
            _startContinuousNext();
        } else {
            _render();
        }
    }
    return true;
}

bool VirtualDisplay::_updateParticles() {
    if (!_particleSys.isInitialised()) {
        _particleSys.init((float)width(), (float)height(), _color);
        _lastParticleStep = millis();
        _lastParticleRender = _lastParticleStep;
        _renderParticlesShape();  // first frame
        _dirty = false;
        return true;
    }

    unsigned long now = millis();

    // Advance physics (skip when paused)
    if (!_modeConfig.particles.physicsPaused) {
        float dt = _dirty
            ? (_modeConfig.particles.renderMs * 0.001f)
            : ((now - _lastParticleStep) * 0.001f);
        if (dt > 0.0f) {
            _particleSys.step(dt);
            _lastParticleStep = now;
        }
    } else {
        _lastParticleStep = now;  // keep timestamp current
    }

    // Render at the (independent) render interval
    if (!_dirty && now - _lastParticleRender < _modeConfig.particles.renderMs) {
        return false;
    }
    _lastParticleRender = now;

    if (_modeConfig.particles.renderStyle == ParticleModeConfig::RENDER_GLOW) {
        _renderParticlesGlow();
    } else {
        _renderParticlesShape();
    }

    _dirty = false;
    return true;
}

// ══════════════════════════════════════════════════════════════
//  Private — rendering & animation helpers
// ══════════════════════════════════════════════════════════════

void VirtualDisplay::_startScroll(const char* newText) {
    strncpy(_oldText, _text, sizeof(_oldText) - 1);
    _oldText[sizeof(_oldText) - 1] = '\0';

    strncpy(_text, newText, sizeof(_text) - 1);
    _text[sizeof(_text) - 1] = '\0';

    _scrollOffset   = 1;
    _scrollLastStep = millis();
}

void VirtualDisplay::_startContinuousNext() {
    _continuousIdx = (_continuousIdx + 1) % _textStackCount;
    const char* next = _textStack[_continuousIdx];
    _startScroll(next);
}

void VirtualDisplay::_renderParticlesShape() {
    if (_modeConfig.particles.speedColor) _applySpeedColors(_particleSys);
    uint8_t pb = _modeConfig.particleBrightness;
    fillScreen(0);
    uint16_t n = _particleSys.count();
    auto style = _modeConfig.particles.renderStyle;

    const ParticleTransform2D& xf = _modeConfig.particles.viewTransform;
    bool hasXf = !xf.isIdentity();
    float pivotX = (float)width()  * 0.5f;
    float pivotY = (float)height() * 0.5f;

    for (uint16_t i = 0; i < n; i++) {
        const Particle& p = _particleSys.particle(i);
        uint16_t col = (pb < 255) ? dimColor565(p.color, pb) : p.color;

        Vec2f pos = hasXf ? xf.apply(p.pos, pivotX, pivotY) : p.pos;
        int16_t px = (int16_t)lroundf(pos.x);
        int16_t py = (int16_t)lroundf(pos.y);

        switch (style) {
            case ParticleModeConfig::RENDER_SQUARE: {
                int16_t r = (int16_t)lroundf(p.radius);
                if (r < 1) r = 1;
                fillRect(px - r, py - r, 2 * r + 1, 2 * r + 1, col);
                break;
            }
            case ParticleModeConfig::RENDER_CIRCLE: {
                int16_t r = (int16_t)lroundf(p.radius);
                if (r < 1) {
                    drawPixel(px, py, col);
                } else {
                    fillCircle(px, py, r, col);
                }
                break;
            }
            case ParticleModeConfig::RENDER_TEXT: {
                // Draw the display text centred on the particle position
                const char* txt = _activeText();
                if (txt[0] != '\0') {
                    setTextColor(col);
                    // 6×8 font: offset by half the string width & half height
                    int16_t tw = (int16_t)(strlen(txt) * 6);
                    setCursor(px - tw / 2, py - 3);
                    print(txt);
                }
                break;
            }
            case ParticleModeConfig::RENDER_POINT:
            default:
                if (px >= 0 && px < (int16_t)width() && py >= 0 && py < (int16_t)height()) {
                    drawPixel(px, py, col);
                }
                break;
        }
    }
}

// ── RGB565 helpers for glow blending ─────────────────────────
static inline void rgb565_to_float(uint16_t c, float& r, float& g, float& b) {
    r = (float)((c >> 8) & 0xF8) / 248.0f;
    g = (float)((c >> 3) & 0xFC) / 252.0f;
    b = (float)((c << 3) & 0xF8) / 248.0f;
}

void VirtualDisplay::_renderParticlesGlow() {
    if (_modeConfig.particles.speedColor) _applySpeedColors(_particleSys);
    float brightScale = _modeConfig.particleBrightness / 255.0f;
    uint16_t w = width();
    uint16_t h = height();
    uint16_t totalPx = w * h;

    float sigma = _modeConfig.particles.glowSigma;
    if (sigma < 0.2f) sigma = 0.2f;
    float invTwoSigmaSq = 1.0f / (2.0f * sigma * sigma);

    float wavelength = _modeConfig.particles.glowWavelength;
    bool  doInterference = (wavelength > 0.1f);
    float kWave = doInterference ? (2.0f * 3.14159265f / wavelength) : 0.0f;

    // Kernel radius — extend further when interference ripples are visible
    int kernelRadius = doInterference
        ? (int)ceilf(sigma * 5.0f)
        : (int)ceilf(sigma * 3.5f);

    uint16_t n = _particleSys.count();

    // View transform
    const ParticleTransform2D& xf = _modeConfig.particles.viewTransform;
    bool hasXf = !xf.isIdentity();
    float pivotX = (float)w * 0.5f;
    float pivotY = (float)h * 0.5f;

    if (doInterference) {
        // ── Complex-amplitude interference ────────────────────
        // 6 floats per pixel: Re_R, Im_R, Re_G, Im_G, Re_B, Im_B
        memset(_glowBuf, 0, totalPx * 6 * sizeof(float));

        for (uint16_t i = 0; i < n; i++) {
            const Particle& p = _particleSys.particle(i);
            float pr, pg, pb;
            rgb565_to_float(p.color, pr, pg, pb);
            pr *= brightScale; pg *= brightScale; pb *= brightScale;

            Vec2f pos = hasXf ? xf.apply(p.pos, pivotX, pivotY) : p.pos;
            int x0 = (int)floorf(pos.x) - kernelRadius;
            int x1 = (int)ceilf(pos.x)  + kernelRadius;
            int y0 = (int)floorf(pos.y) - kernelRadius;
            int y1 = (int)ceilf(pos.y)  + kernelRadius;
            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            if (x1 >= (int)w) x1 = (int)w - 1;
            if (y1 >= (int)h) y1 = (int)h - 1;

            for (int py = y0; py <= y1; py++) {
                float dy = (float)py - pos.y;
                float dy2 = dy * dy;
                for (int px = x0; px <= x1; px++) {
                    float dx = (float)px - pos.x;
                    float distSq = dx * dx + dy2;
                    float envelope = expf(-distSq * invTwoSigmaSq);
                    float dist = sqrtf(distSq);
                    float phase = kWave * dist;
                    float cosP  = cosf(phase);
                    float sinP  = sinf(phase);
                    float re = envelope * cosP;
                    float im = envelope * sinP;

                    uint16_t idx = (uint16_t)(py * w + px) * 6;
                    _glowBuf[idx + 0] += pr * re;   // Re_R
                    _glowBuf[idx + 1] += pr * im;   // Im_R
                    _glowBuf[idx + 2] += pg * re;   // Re_G
                    _glowBuf[idx + 3] += pg * im;   // Im_G
                    _glowBuf[idx + 4] += pb * re;   // Re_B
                    _glowBuf[idx + 5] += pb * im;   // Im_B
                }
            }
        }

        // Convert |A|² to RGB565
        fillScreen(0);
        for (uint16_t py = 0; py < h; py++) {
            for (uint16_t px = 0; px < w; px++) {
                uint16_t idx = (py * w + px) * 6;
                // Intensity = Re² + Im² per channel
                float rI = _glowBuf[idx+0]*_glowBuf[idx+0] + _glowBuf[idx+1]*_glowBuf[idx+1];
                float gI = _glowBuf[idx+2]*_glowBuf[idx+2] + _glowBuf[idx+3]*_glowBuf[idx+3];
                float bI = _glowBuf[idx+4]*_glowBuf[idx+4] + _glowBuf[idx+5]*_glowBuf[idx+5];
                // sqrt for perceptual brightness (intensity → amplitude)
                float r = sqrtf(rI); if (r > 1.0f) r = 1.0f;
                float g = sqrtf(gI); if (g > 1.0f) g = 1.0f;
                float b = sqrtf(bI); if (b > 1.0f) b = 1.0f;

                uint8_t r8 = (uint8_t)(r * 255.0f);
                uint8_t g8 = (uint8_t)(g * 255.0f);
                uint8_t b8 = (uint8_t)(b * 255.0f);
                drawPixel(px, py, rgb565(r8, g8, b8));
            }
        }

    } else {
        // ── Plain additive glow (real only) ──────────────────
        // Use first 3 floats per pixel (Re_R, Re_G, Re_B)
        memset(_glowBuf, 0, totalPx * 3 * sizeof(float));

        for (uint16_t i = 0; i < n; i++) {
            const Particle& p = _particleSys.particle(i);
            float pr, pg, pb;
            rgb565_to_float(p.color, pr, pg, pb);
            pr *= brightScale; pg *= brightScale; pb *= brightScale;

            Vec2f pos = hasXf ? xf.apply(p.pos, pivotX, pivotY) : p.pos;
            int x0 = (int)floorf(pos.x) - kernelRadius;
            int x1 = (int)ceilf(pos.x)  + kernelRadius;
            int y0 = (int)floorf(pos.y) - kernelRadius;
            int y1 = (int)ceilf(pos.y)  + kernelRadius;
            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            if (x1 >= (int)w) x1 = (int)w - 1;
            if (y1 >= (int)h) y1 = (int)h - 1;

            for (int py = y0; py <= y1; py++) {
                float dy = (float)py - pos.y;
                float dy2 = dy * dy;
                for (int px = x0; px <= x1; px++) {
                    float dx = (float)px - pos.x;
                    float distSq = dx * dx + dy2;
                    float intensity = expf(-distSq * invTwoSigmaSq);

                    uint16_t idx = (uint16_t)(py * w + px) * 3;
                    _glowBuf[idx + 0] += pr * intensity;
                    _glowBuf[idx + 1] += pg * intensity;
                    _glowBuf[idx + 2] += pb * intensity;
                }
            }
        }

        fillScreen(0);
        for (uint16_t py = 0; py < h; py++) {
            for (uint16_t px = 0; px < w; px++) {
                uint16_t idx = (py * w + px) * 3;
                float r = _glowBuf[idx + 0]; if (r > 1.0f) r = 1.0f;
                float g = _glowBuf[idx + 1]; if (g > 1.0f) g = 1.0f;
                float b = _glowBuf[idx + 2]; if (b > 1.0f) b = 1.0f;

                uint8_t r8 = (uint8_t)(r * 255.0f);
                uint8_t g8 = (uint8_t)(g * 255.0f);
                uint8_t b8 = (uint8_t)(b * 255.0f);
                drawPixel(px, py, rgb565(r8, g8, b8));
            }
        }
    }
}

void VirtualDisplay::_render(bool clearFirst) {
    if (clearFirst) fillScreen(0);
    const char* txt = _activeText();
    if (txt[0] == '\0') return;

    const char* visible = _fitTextRight(txt);
    if (*visible == '\0') return;

    setTextColor(dimColor565(_color, _modeConfig.textBrightness));
    int16_t x = _centerTextX(visible);
    setCursor(x, 0);
    print(visible);
}

void VirtualDisplay::_renderScrollFrame(bool clearFirst) {
    if (clearFirst) fillScreen(0);
    setTextColor(dimColor565(_color, _modeConfig.textBrightness));

    int16_t offset = _scrollOffset;
    int16_t h = (int16_t)height();

    // Old and new text scroll simultaneously
    int16_t oldY, newY;
    if (_modeConfig.mode == DISPLAY_MODE_SCROLL_UP) {
        oldY = -offset;
        newY = h - offset;
    } else {
        oldY = offset;
        newY = -(h - offset);
    }

    if (_oldText[0] != '\0') {
        const char* visOld = _fitTextRight(_oldText);
        if (*visOld) {
            setCursor(_centerTextX(visOld), oldY);
            print(visOld);
        }
    }
    if (_text[0] != '\0') {
        const char* visNew = _fitTextRight(_text);
        if (*visNew) {
            setCursor(_centerTextX(visNew), newY);
            print(visNew);
        }
    }
}

void VirtualDisplay::_redrawParticleLayer() {
    if (_modeConfig.particles.renderStyle == ParticleModeConfig::RENDER_GLOW) {
        _renderParticlesGlow();
    } else {
        _renderParticlesShape();
    }
    _lastParticleRender = millis();
}

void VirtualDisplay::_resetQueue() {
    _queueHead  = 0;
    _queueTail  = 0;
    _queueCount = 0;
}

bool VirtualDisplay::_isScrollMode() const {
    return _modeConfig.mode == DISPLAY_MODE_SCROLL_UP
        || _modeConfig.mode == DISPLAY_MODE_SCROLL_DOWN;
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
