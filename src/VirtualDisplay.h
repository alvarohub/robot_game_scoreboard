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
#include "ParticleSystem.h"

#ifndef SCROLL_STEP_MS
  #define SCROLL_STEP_MS 50
#endif

#ifndef SCROLL_QUEUE_SIZE
  #define SCROLL_QUEUE_SIZE 10
#endif

enum DisplayMode : uint8_t {
    DISPLAY_MODE_TEXT = 0,
    DISPLAY_MODE_SCROLL_UP,
    DISPLAY_MODE_SCROLL_DOWN,
    DISPLAY_MODE_PARTICLES,
};

struct ParticleModeConfig {
    // Physics (forwarded to ParticleSystem)
    uint8_t count        = 6;
    uint8_t renderMs     = 20;     // canvas redraw interval (ms)
    uint8_t substepMs    = 20;     // max physics sub-step (ms)
    float   radius       = 0.45f;
    float   gravityScale = 18.0f;
    float   elasticity   = 0.92f;    // coef. of restitution, particle-particle
    float   wallElasticity = 0.78f;   // coef. of restitution, wall bounce
    float   damping        = 0.9998f;  // per-substep velocity multiplier (1 = none)
    float   temperature    = 0.0f;     // Langevin jitter magnitude
    float   attractStrength = 0.0f;    // inter-particle attraction (0 = off)
    float   attractRange   = 3.0f;     // interaction range (× sum-of-radii)
    bool    gravityEnabled = true;

    // Rendering
    enum RenderStyle : uint8_t {
        RENDER_POINT  = 0,   // single pixel per particle
        RENDER_SQUARE = 1,   // filled square (side = 2*radius+1)
        RENDER_CIRCLE = 2,   // filled circle (r = radius)
        RENDER_TEXT   = 3,   // display text floats with each particle
        RENDER_GLOW   = 4,   // Gaussian additive glow
    };
    RenderStyle renderStyle = RENDER_GLOW;
    float       glowSigma      = 1.2f;   // Gaussian envelope sigma (pixels)
    float       glowWavelength = 0.0f;   // interference wavelength (0 = pure glow, >0 = wave)

    // Convert to ParticleSystemConfig
    ParticleSystemConfig toSystemConfig() const {
        ParticleSystemConfig c;
        c.count         = count;
        c.renderMs      = renderMs;
        c.substepMs     = substepMs;
        c.radius        = radius;
        c.gravityScale  = gravityScale;
        c.elasticity    = elasticity;
        c.wallElasticity = wallElasticity;
        c.damping       = damping;
        c.temperature   = temperature;
        c.attractStrength = attractStrength;
        c.attractRange  = attractRange;
        c.gravityEnabled = gravityEnabled;
        return c;
    }
};

struct DisplayModeConfig {
    DisplayMode mode = DISPLAY_MODE_TEXT;
    uint8_t scrollStepMs = SCROLL_STEP_MS;
    bool scrollBlank = false;
    ParticleModeConfig particles;
};

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

    // ── Display mode ────────────────────────────────────────────
    void setMode(DisplayMode mode);
    void setMode(const DisplayModeConfig& config);
    DisplayMode mode() const { return _modeConfig.mode; }
    const DisplayModeConfig& modeConfig() const { return _modeConfig; }

    // Compatibility wrappers for the previous scroll-only API.
    void    setScrollMode(uint8_t mode);
    uint8_t scrollMode() const { return (uint8_t)_modeConfig.mode; }
    void setScrollSpeed(uint8_t ms);
    void setScrollBlank(bool enabled);
    void setGravity(float gx, float gy);
    void setParticleConfig(const ParticleModeConfig& cfg);

    // ── Scroll queue ────────────────────────────────────────────
    void clearQueue();

    // ── Per-frame update (drives scroll animations) ─────────────
    /// Returns true if the canvas was modified this call.
    bool update();

    // ── Query ───────────────────────────────────────────────────
    bool isAnimating() const {
      return _scrollOffset > 0 || _modeConfig.mode == DISPLAY_MODE_PARTICLES;
    }

    /// Returns true (once) when a scroll animation finishes.
    bool scrollFinished();

    /// Expected scroll duration in ms (height × step time).
    unsigned long scrollDurationMs() const {
      return (unsigned long)height() * _modeConfig.scrollStepMs;
    }

private:
    // Text / display state
    char     _text[32];
    char     _oldText[32];
    uint16_t _color;
    bool     _dirty;

    // Mode / animation state
    DisplayModeConfig _modeConfig;
    int8_t   _scrollOffset;
    unsigned long _scrollLastStep;
    bool     _scrollJustDone;

    // Particle physics engine
    ParticleSystem _particleSys;
    unsigned long  _lastParticleStep;
    unsigned long  _lastParticleRender;

    // Glow / interference accumulation buffer
    // 6 floats per pixel: Re_R, Im_R, Re_G, Im_G, Re_B, Im_B
    // (plain glow only uses Re channels; interference uses all 6)
    static constexpr uint16_t MAX_GLOW_PIXELS = 64 * 16; // generous max
    float _glowBuf[MAX_GLOW_PIXELS * 6];

    // Scroll queue (ring buffer)
    char     _queue[SCROLL_QUEUE_SIZE][32];
    uint8_t  _queueHead;
    uint8_t  _queueTail;
    uint8_t  _queueCount;

    // Per-mode update handlers (called from update())
    bool        _updateText();
    bool        _updateScroll();
    bool        _updateParticles();

    // Rendering & animation helpers
    void        _render();
    void        _renderScrollFrame();
    void        _renderParticlesShape();
    void        _renderParticlesGlow();
    void        _startScroll(const char* newText);
    void        _resetQueue();
    bool        _isScrollMode() const;
    const char* _fitTextRight(const char* text);
    int16_t     _centerTextX(const char* text);
};
