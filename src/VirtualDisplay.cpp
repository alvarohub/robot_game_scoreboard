#include "VirtualDisplay.h"
#include <cstring>
#include <math.h>

// RGB565 pack — same formula as Adafruit_SPITFT::color565()
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// ── Constructor ──────────────────────────────────────────────
VirtualDisplay::VirtualDisplay(uint16_t w, uint16_t h)
    : GFXcanvas16(w, h),
      _color(0xFFFF),          // white in RGB565
      _dirty(true),
    _modeConfig(),
      _scrollOffset(0),
      _scrollLastStep(0),
      _scrollJustDone(false),
    _lastParticleStep(0),
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
    if (!text) text = "";

    // In static text mode, skip if text is identical (no visible change)
    if (strcmp(_text, text) == 0 && !_isScrollMode()) return;

    if (_modeConfig.mode == DISPLAY_MODE_PARTICLES) {
        strncpy(_text, text, sizeof(_text) - 1);
        _text[sizeof(_text) - 1] = '\0';
        return;
    }

    if (_isScrollMode()) {
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
    // Propagate to live particles
    if (_modeConfig.mode == DISPLAY_MODE_PARTICLES && _particleSys.isInitialised()) {
        for (uint8_t i = 0; i < _particleSys.count(); i++) {
            _particleSys.particle(i).color = color565;
        }
    }
    _dirty = true;
}

void VirtualDisplay::clear() {
    _text[0] = '\0';
    _oldText[0] = '\0';
    _scrollOffset = 0;
    _scrollJustDone = false;
    _particleSys.reset();
    _lastParticleStep = 0;
    _resetQueue();
    _modeConfig.mode = DISPLAY_MODE_TEXT;
    fillScreen(0);
    _dirty = true;
}

// ── Display mode ─────────────────────────────────────────────
void VirtualDisplay::setMode(DisplayMode mode) {
    DisplayModeConfig config = _modeConfig;
    config.mode = mode;
    setMode(config);
}

void VirtualDisplay::setMode(const DisplayModeConfig& config) {
    _modeConfig = config;

    if (_modeConfig.scrollStepMs == 0) {
        _modeConfig.scrollStepMs = 1;
    }
    if (_modeConfig.particles.renderMs == 0) {
        _modeConfig.particles.renderMs = 1;
    }

    if (_modeConfig.mode == DISPLAY_MODE_TEXT) {
        // Flush the queue — show last queued value instantly
        if (_queueCount > 0) {
            uint8_t lastIdx = (_queueTail + SCROLL_QUEUE_SIZE - 1)
                              % SCROLL_QUEUE_SIZE;
            strncpy(_text, _queue[lastIdx], 31);
            _text[31] = '\0';
        }
        _resetQueue();
        _scrollOffset = 0;
        _oldText[0] = '\0';
        _dirty = true;
        return;
    }

    _scrollOffset = 0;
    _oldText[0] = '\0';
    _resetQueue();

    if (_modeConfig.mode == DISPLAY_MODE_PARTICLES) {
        _particleSys.setConfig(_modeConfig.particles.toSystemConfig());
        _particleSys.reset();
        _lastParticleStep = millis();
        _lastParticleRender = _lastParticleStep;
    }

    _dirty = true;
}

void VirtualDisplay::setScrollMode(uint8_t mode) {
    if (mode > DISPLAY_MODE_SCROLL_DOWN) mode = DISPLAY_MODE_TEXT;
    setMode((DisplayMode)mode);
}

void VirtualDisplay::setScrollSpeed(uint8_t ms) {
    _modeConfig.scrollStepMs = ms > 0 ? ms : 1;
}

void VirtualDisplay::setScrollBlank(bool enabled) {
    _modeConfig.scrollBlank = enabled;
}

void VirtualDisplay::setGravity(float gx, float gy) {
    _particleSys.setGravity(gx, gy);
}

void VirtualDisplay::setParticleConfig(const ParticleModeConfig& cfg) {
    _modeConfig.particles = cfg;
    if (_modeConfig.particles.renderMs == 0) _modeConfig.particles.renderMs = 1;
    _particleSys.setConfig(cfg.toSystemConfig());
    _dirty = true;
}

void VirtualDisplay::clearQueue() {
    _resetQueue();
}

// ── Per-frame update (call from DisplayManager::update) ──────
bool VirtualDisplay::update() {
    switch (_modeConfig.mode) {
        case DISPLAY_MODE_PARTICLES:
            return _updateParticles();

        case DISPLAY_MODE_SCROLL_UP:
        case DISPLAY_MODE_SCROLL_DOWN:
            if (_scrollOffset > 0) return _updateScroll();
            return _updateText();   // not scrolling → simple redraw

        case DISPLAY_MODE_TEXT:
        default:
            return _updateText();
    }
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
    _render();
    _dirty = false;
    return true;
}

bool VirtualDisplay::_updateScroll() {
    unsigned long now = millis();
    if (now - _scrollLastStep < _modeConfig.scrollStepMs) return false;

    _scrollLastStep = now;
    _renderScrollFrame();
    _scrollOffset++;

    if (_scrollOffset > (int8_t)height()) {
        _scrollOffset = 0;
        _oldText[0] = '\0';
        _scrollJustDone = true;

        if (_queueCount > 0) {
            char next[32];
            strncpy(next, _queue[_queueHead], 31);
            next[31] = '\0';
            _queueHead = (_queueHead + 1) % SCROLL_QUEUE_SIZE;
            _queueCount--;

            if (_modeConfig.scrollBlank) {
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

    // Always advance physics at the real elapsed dt
    float dt = _dirty
        ? (_modeConfig.particles.renderMs * 0.001f)
        : ((now - _lastParticleStep) * 0.001f);
    if (dt > 0.0f) {
        _particleSys.step(dt);
        _lastParticleStep = now;
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

void VirtualDisplay::_renderParticlesShape() {
    fillScreen(0);
    uint8_t n = _particleSys.count();
    auto style = _modeConfig.particles.renderStyle;

    for (uint8_t i = 0; i < n; i++) {
        const Particle& p = _particleSys.particle(i);
        int16_t px = (int16_t)lroundf(p.pos.x);
        int16_t py = (int16_t)lroundf(p.pos.y);

        switch (style) {
            case ParticleModeConfig::RENDER_SQUARE: {
                int16_t r = (int16_t)lroundf(p.radius);
                if (r < 1) r = 1;
                fillRect(px - r, py - r, 2 * r + 1, 2 * r + 1, p.color);
                break;
            }
            case ParticleModeConfig::RENDER_CIRCLE: {
                int16_t r = (int16_t)lroundf(p.radius);
                if (r < 1) {
                    drawPixel(px, py, p.color);
                } else {
                    fillCircle(px, py, r, p.color);
                }
                break;
            }
            case ParticleModeConfig::RENDER_TEXT: {
                // Draw the display text centred on the particle position
                if (_text[0] != '\0') {
                    setTextColor(p.color);
                    // 6×8 font: offset by half the string width & half height
                    int16_t tw = (int16_t)(strlen(_text) * 6);
                    setCursor(px - tw / 2, py - 3);
                    print(_text);
                }
                break;
            }
            case ParticleModeConfig::RENDER_POINT:
            default:
                if (px >= 0 && px < (int16_t)width() && py >= 0 && py < (int16_t)height()) {
                    drawPixel(px, py, p.color);
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

    uint8_t n = _particleSys.count();

    if (doInterference) {
        // ── Complex-amplitude interference ────────────────────
        // 6 floats per pixel: Re_R, Im_R, Re_G, Im_G, Re_B, Im_B
        memset(_glowBuf, 0, totalPx * 6 * sizeof(float));

        for (uint8_t i = 0; i < n; i++) {
            const Particle& p = _particleSys.particle(i);
            float pr, pg, pb;
            rgb565_to_float(p.color, pr, pg, pb);

            int x0 = (int)floorf(p.pos.x) - kernelRadius;
            int x1 = (int)ceilf(p.pos.x)  + kernelRadius;
            int y0 = (int)floorf(p.pos.y) - kernelRadius;
            int y1 = (int)ceilf(p.pos.y)  + kernelRadius;
            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            if (x1 >= (int)w) x1 = (int)w - 1;
            if (y1 >= (int)h) y1 = (int)h - 1;

            for (int py = y0; py <= y1; py++) {
                float dy = (float)py - p.pos.y;
                float dy2 = dy * dy;
                for (int px = x0; px <= x1; px++) {
                    float dx = (float)px - p.pos.x;
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

        for (uint8_t i = 0; i < n; i++) {
            const Particle& p = _particleSys.particle(i);
            float pr, pg, pb;
            rgb565_to_float(p.color, pr, pg, pb);

            int x0 = (int)floorf(p.pos.x) - kernelRadius;
            int x1 = (int)ceilf(p.pos.x)  + kernelRadius;
            int y0 = (int)floorf(p.pos.y) - kernelRadius;
            int y1 = (int)ceilf(p.pos.y)  + kernelRadius;
            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            if (x1 >= (int)w) x1 = (int)w - 1;
            if (y1 >= (int)h) y1 = (int)h - 1;

            for (int py = y0; py <= y1; py++) {
                float dy = (float)py - p.pos.y;
                float dy2 = dy * dy;
                for (int px = x0; px <= x1; px++) {
                    float dx = (float)px - p.pos.x;
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
    if (_modeConfig.mode == DISPLAY_MODE_SCROLL_UP) {
        oldY = -offset;
        newY = h - offset;
    } else {  // DISPLAY_MODE_SCROLL_DOWN
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
