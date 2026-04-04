#pragma once
// ═══════════════════════════════════════════════════════════════
//  DisplayManager — compositor for VirtualDisplay instances
//
//  Owns the physical NeoPixel strip (Adafruit_NeoMatrix) and an
//  array of VirtualDisplay objects.  Each VirtualDisplay renders
//  independently into its own GFXcanvas16; the compositor blits
//  their contents onto the hardware strip.
//
//  Display dimensions are independent — each can be a different
//  size.  They are laid out sequentially (left to right) on the
//  physical strip by default.
// ═══════════════════════════════════════════════════════════════

#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include "config.h"
#include "VirtualDisplay.h"

class DisplayManager {
public:
    DisplayManager();
    ~DisplayManager();

    void begin();

    // ── Per-display operations (displayIndex: 0 … numDisplays()-1) ──
    void setText(uint8_t displayIndex, const char* text);
    void setColor(uint8_t displayIndex, uint8_t r, uint8_t g, uint8_t b);
    void setColor(uint8_t displayIndex, uint16_t color565);
    void clear(uint8_t displayIndex);

    // ── Display modes ───────────────────────────────────────────────
    void setMode(uint8_t displayIndex, DisplayMode mode);
    void setMode(uint8_t displayIndex, const DisplayModeConfig& config);
    void setModeAll(DisplayMode mode);

    // ── Compatibility scroll API ───────────────────────────────────
    void setScrollMode(uint8_t displayIndex, uint8_t mode);
    void setScrollModeAll(uint8_t mode);
    void setScrollSpeed(uint8_t ms);
    void setScrollContinuous(bool enabled);
    void setParticlesEnabled(bool enabled);
    void setParticlesEnabled(uint8_t displayIndex, bool enabled);
    void setTextEnabled(bool enabled);
    void setTextEnabled(uint8_t displayIndex, bool enabled);
    void setTextBrightness(uint8_t brightness);
    void setTextBrightness(uint8_t displayIndex, uint8_t brightness);
    void setParticleBrightness(uint8_t brightness);
    void setParticleBrightness(uint8_t displayIndex, uint8_t brightness);
    void setParticleColor(uint8_t r, uint8_t g, uint8_t b);
    void setParticleColor(uint8_t displayIndex, uint8_t r, uint8_t g, uint8_t b);
    void setGravity(float gx, float gy);
    void setParticleConfig(const ParticleModeConfig& cfg);
    void setParticleConfig(uint8_t displayIndex, const ParticleModeConfig& cfg);

    // ── Scroll queue ────────────────────────────────────────────────
    void clearQueue(uint8_t displayIndex);
    void clearQueueAll();

    // ── Global operations ───────────────────────────────────────────
    void setBrightness(uint8_t brightness);
    uint8_t brightness() const { return _brightness; }
    void clearAll();

    /// Save current display params (brightness, color, mode, particles) to NVS.
    void saveParams();
    /// Load display params from NVS and apply them.
    void loadParams();

    /// Call every loop iteration — updates all VirtualDisplays,
    /// composites them, and pushes pixels to the strip.
    void update();

    /// Runs a short self-test that lights each display in sequence.
    void showTestPattern();

    /// Lights one LED at a time by raw strip index (0…N-1),
    /// printing the index to Serial. Useful for finding dead LEDs.
    void showRasterScan(uint16_t delayMs = 30);

    /// Splash screen shown once at startup.
    void startDisplay(unsigned long durationMs = 2000);

    /// Returns true if any display is currently mid-scroll.
    bool isAnimating() const;

    /// Returns true (once) if display `idx` just finished a scroll animation.
    bool scrollFinished(uint8_t idx);

    /// Expected scroll duration in ms (uses first display's height).
    unsigned long scrollDurationMs() const;

    /// Number of logical displays.
    uint8_t numDisplays() const { return NUM_DISPLAYS; }

    /// Direct access to a VirtualDisplay (for advanced GFX operations).
    VirtualDisplay* getDisplay(uint8_t idx);

private:
    Adafruit_NeoMatrix _matrix;
    VirtualDisplay*    _vDisplays[NUM_DISPLAYS];
    int16_t            _offsets[NUM_DISPLAYS];   // X-offset of each display on the strip
    bool               _needsUpdate;
    uint8_t            _brightness = DEFAULT_BRIGHTNESS;
    Preferences        _prefs;
#ifdef USE_M5UNIFIED
    bool               _imuAvailable = false;
#endif

    /// Blit all VirtualDisplay canvases onto the physical NeoMatrix.
    void _render();
};
