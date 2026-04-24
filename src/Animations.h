#pragma once

#include <Arduino.h>
#include "VirtualDisplay.h"

// Fixed built-in animation catalog. Animations are authored in Animations.cpp
// as timed step sequences that apply display/particle state changes.

static constexpr uint8_t ANIMATION_BANK_SLOT_COUNT = 5;

struct AnimationStep {
    // Target displays bitmask for this step (bit 0 => display 1).
    // 0 means "all displays" for backward compatibility.
    uint32_t targetDisplayMask = 0;
    unsigned long waitMs = 0;

    enum ParticleFieldMask : uint32_t {
        PARTICLE_COUNT              = 1ul << 0,
        PARTICLE_RENDER_MS          = 1ul << 1,
        PARTICLE_SUBSTEP_MS         = 1ul << 2,
        PARTICLE_RADIUS             = 1ul << 3,
        PARTICLE_GRAVITY_SCALE      = 1ul << 4,
        PARTICLE_ELASTICITY         = 1ul << 5,
        PARTICLE_WALL_ELASTICITY    = 1ul << 6,
        PARTICLE_DAMPING            = 1ul << 7,
        PARTICLE_TEMPERATURE        = 1ul << 8,
        PARTICLE_ATTRACT_STRENGTH   = 1ul << 9,
        PARTICLE_ATTRACT_RANGE      = 1ul << 10,
        PARTICLE_GRAVITY_ENABLED    = 1ul << 11,
        PARTICLE_COLLISION_ENABLED  = 1ul << 12,
        PARTICLE_SPRING_STRENGTH    = 1ul << 13,
        PARTICLE_SPRING_RANGE       = 1ul << 14,
        PARTICLE_SPRING_ENABLED     = 1ul << 15,
        PARTICLE_COULOMB_STRENGTH   = 1ul << 16,
        PARTICLE_COULOMB_RANGE      = 1ul << 17,
        PARTICLE_COULOMB_ENABLED    = 1ul << 18,
        PARTICLE_SCAFFOLD_STRENGTH  = 1ul << 19,
        PARTICLE_SCAFFOLD_RANGE     = 1ul << 20,
        PARTICLE_SCAFFOLD_ENABLED   = 1ul << 21,
        PARTICLE_RENDER_STYLE       = 1ul << 22,
        PARTICLE_GLOW_SIGMA         = 1ul << 23,
        PARTICLE_GLOW_WAVELENGTH    = 1ul << 24,
        PARTICLE_SPEED_COLOR        = 1ul << 25,
        PARTICLE_PHYSICS_PAUSED     = 1ul << 26,
        PARTICLE_TEXT_INDEX         = 1ul << 27,
    };

    // Flow control:
    // gotoStep: -1 means no jump. Otherwise jump target step index.
    // gotoRepeat:
    //   -1 = infinite jump loop
    //    N > 0 = jump N times, then continue to next step
    //    0 = disabled (same as no jump)
    int16_t gotoStep = -1;
    int16_t gotoRepeat = 0;

    bool setMode = false;
    DisplayMode mode = DISPLAY_MODE_TEXT;

    bool setTextEnabled = false;
    bool textEnabled = true;

    bool setParticlesEnabled = false;
    bool particlesEnabled = false;

    bool doTextToParticles = false;
    bool doScreenToParticles = false;

    bool setPhysicsPaused = false;
    bool physicsPaused = false;

    bool setParticleConfig = false;
    bool replaceParticleConfig = false;  // true: assign full config from particleConfig
    uint32_t particleFieldMask = 0;
    ParticleModeConfig particleConfig;
};

struct AnimationScript {
    uint8_t id = 0;
    const char* name = "";
    const AnimationStep* steps = nullptr;
    uint8_t stepCount = 0;
};

// Installs a runtime-owned script. Ownership of `name` and `steps` transfers to
// the animation registry on success and will be released by clear functions.
bool installRuntimeAnimationScript(uint8_t id, char* name, AnimationStep* steps, uint8_t stepCount);

// Removes one/all runtime-owned scripts.
bool clearRuntimeAnimationScript(uint8_t id);
const AnimationScript* findRuntimeAnimationScript(uint8_t id);
void clearAllRuntimeAnimationScripts();
uint8_t runtimeAnimationScriptCount();
const AnimationScript* runtimeAnimationScriptAt(uint8_t index);

// Returns nullptr for unknown IDs.
const AnimationScript* findAnimationScript(uint8_t id);
