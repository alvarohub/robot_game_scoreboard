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
#include "Animations.h"

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
    /// Save current params to NVS bank [1..NVS_BANK_COUNT].
    void saveParams(uint8_t bank);
    /// Load display params from NVS and apply them.
    void loadParams();
    /// Load params from NVS bank [1..NVS_BANK_COUNT].
    void loadParams(uint8_t bank);
    /// Set which bank is loaded at startup.
    void setStartupBank(uint8_t bank);
    /// Current startup bank index [1..NVS_BANK_COUNT].
    uint8_t startupBank() const;
    /// Load startup bank. Called during setup once boot visuals are done.
    void loadStartupParams();
    /// Debounced autosave for the startup bank after live state changes.
    void schedulePersist();

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

    /// Serialize one display's current runtime state as JSON.
    void printDisplayState(uint8_t displayIndex, Print& out) const;

    /// Emit one display's current runtime state on Serial as DISPLAY_STATE JSON.
    void notifyDisplayState(uint8_t displayIndex) const;

    /// Select animation script for one/all displays (0 = off).
    void setAnimation(uint8_t displayIndex, uint8_t animationId);
    uint8_t animationId(uint8_t displayIndex) const;
    const char* animationName(uint8_t displayIndex) const;
    void startAnimation(uint8_t displayIndex);
    void setAnimationAll(uint8_t animationId);
    void stopAnimation(uint8_t displayIndex);
    void stopAnimationAll();

private:
    Adafruit_NeoMatrix _matrix;
    VirtualDisplay*    _vDisplays[NUM_DISPLAYS];
    int16_t            _offsets[NUM_DISPLAYS];   // X-offset of each display on the strip
    bool               _needsUpdate;
    uint8_t            _brightness = DEFAULT_BRIGHTNESS;
    Preferences        _prefs;
    bool               _persistDirty = false;
    bool               _persistenceSuspended = false;
    unsigned long      _persistDueMs = 0;
#if SCOREBOARD_HAS_M5UNIFIED
    bool               _imuAvailable = false;
#endif

    static constexpr uint8_t NVS_BANK_COUNT = 5;
    static constexpr unsigned long PERSIST_DEBOUNCE_MS = 800;

    struct AnimationRuntime {
        uint8_t selectedId = 0;      // script selected for this display
        bool running = false;
        uint8_t step = 0;
        unsigned long stepStartedMs = 0;
        uint8_t triggeredTextIndex = 0;
        int16_t gotoSourceStep = -1; // step currently managing finite goto count
        int16_t gotoRemaining = 0;   // remaining finite jumps for gotoSourceStep
    };
    AnimationRuntime _anim[NUM_DISPLAYS];

    /// Blit all VirtualDisplay canvases onto the physical NeoMatrix.
    void _render();
    void _persistIfDue();
    bool _saveAutoState();
    bool _loadAutoState();

    uint8_t _clampBank(uint8_t bank) const;
    bool _makeBankKey(const char* base, uint8_t bank, char* out, size_t outLen) const;

    void _onTextSet(uint8_t displayIndex);
    void _startAnimation(uint8_t displayIndex);
    void _stopAnimation(uint8_t displayIndex);
    void _tickAnimations();
    void _applyAnimationStep(uint8_t displayIndex, const AnimationStep& step);
};
