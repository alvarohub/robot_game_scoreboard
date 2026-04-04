#pragma once
// ═══════════════════════════════════════════════════════════════
//  ParticleSystem — physics simulation for 2D particles
//
//  Uses Velocity Verlet integration with explicit velocity for
//  energy conservation.  Supports constraints (walls, collisions,
//  future springs) and Langevin temperature jitter.
//  Rendering is NOT done here — the display reads particle state.
// ═══════════════════════════════════════════════════════════════

#include "Vec2f.h"
#include <stdint.h>

// ── Configuration ────────────────────────────────────────────

struct ParticleSystemConfig {
    uint8_t count        = 6;
    uint8_t renderMs     = 20;     // canvas redraw interval (cosmetic)
    uint8_t substepMs    = 20;     // max physics sub-step (stability vs CPU)
    float   radius       = 0.45f;  // collision & rendering radius (pixels)
    float   gravityScale = 18.0f;  // multiplier for IMU g-force input
    float   elasticity   = 0.92f;  // inter-particle bounce (0–1)
    float   wallElasticity = 0.78f;
    float   damping      = 0.9998f; // per-substep velocity multiplier (1 = none)
    float   temperature  = 0.0f;   // Langevin jitter magnitude
    float   attractStrength = 0.0f; // inter-particle attraction (0 = off)
    float   attractRange = 3.0f;    // interaction range (× sum-of-radii)
    bool    gravityEnabled = true;
};

// ── Single particle ──────────────────────────────────────────

struct Particle {
    Vec2f pos;        // current position
    Vec2f vel;        // velocity (explicit for Velocity Verlet)
    Vec2f accel;      // accumulated acceleration this step
    float radius;     // individual radius
    uint16_t color;   // RGB565 per-particle colour
};

// ── System ───────────────────────────────────────────────────

class ParticleSystem {
public:
    static constexpr uint8_t MAX_PARTICLES = 64;

    ParticleSystem() = default;

    // ── Setup ────────────────────────────────────────────────
    /// Initialise particles in a grid within the given bounds.
    void init(float boundsW, float boundsH, uint16_t defaultColor);
    bool isInitialised() const { return _initialised; }
    void reset() { _initialised = false; }

    // ── Configuration ────────────────────────────────────────
    void setConfig(const ParticleSystemConfig& cfg);
    const ParticleSystemConfig& config() const { return _config; }

    /// Low-pass filtered gravity input (call every frame from IMU).
    void setGravity(float gx, float gy);
    Vec2f gravity() const { return _gravity; }

    // ── Simulation step ──────────────────────────────────────
    /// Advance physics by `dt` seconds. Internally sub-steps if needed.
    void step(float dt);

    // ── Particle access (for rendering) ──────────────────────
    uint8_t        count()                  const { return _count; }
    const Particle& particle(uint8_t i)     const { return _particles[i]; }
    Particle&       particle(uint8_t i)           { return _particles[i]; }

    /// Bounds used by the system
    float boundsW() const { return _boundsW; }
    float boundsH() const { return _boundsH; }

private:
    ParticleSystemConfig _config;
    Particle _particles[MAX_PARTICLES];
    uint8_t  _count = 0;
    bool     _initialised = false;

    // World
    float _boundsW = 32.0f;
    float _boundsH = 8.0f;
    Vec2f _gravity{0, 0};

    // Internal physics sub-step
    void _substep(float dt);
    void _applyGravity();
    void _integrate(float dt);
    void _constrainWalls();
    void _resolveCollisions();
};
