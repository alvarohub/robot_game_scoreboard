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
    uint16_t count       = 6;
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
    bool    attractEnabled = true;
    bool    gravityEnabled = true;
    bool    collisionEnabled = true;  // hard-sphere collision (position correction + bounce)

    // Spring force (linear, charge-dependent)
    float   springStrength = 0.0f;  // positive + same-sign charges → repulsion
    float   springRange    = 5.0f;  // cutoff distance in absolute pixels
    bool    springEnabled   = false;

    // Coulomb force (1/r², charge-dependent)
    float   coulombStrength = 0.0f; // positive + same-sign charges → repulsion
    float   coulombRange    = 10.0f; // cutoff distance in absolute pixels
    bool    coulombEnabled  = false;

    // Scaffold attraction (pull particles toward their origin positions)
    float   scaffoldStrength = 0.0f; // spring-like pull toward scaffold pos
    float   scaffoldRange    = 10.0f; // max effective range (pixels)
    bool    scaffoldEnabled  = false;
};

// ── Single particle ──────────────────────────────────────────

struct Particle {
    Vec2f pos;        // current position
    Vec2f vel;        // velocity (explicit for Velocity Verlet)
    Vec2f accel;      // accumulated acceleration this step
    float radius;     // individual radius
    uint16_t color;   // RGB565 per-particle colour
    float charge;     // sensitivity to charge-based forces (0 = insensitive)
    int16_t originIdx; // index into scaffold (-1 = none)
};

// ── Scaffold: snapshot of initial particle state ─────────────
//  Saved automatically on every init / initFromPositions call.
//  Useful for: restoring colours after speedColor, attracting
//  particles back to original positions, reset effects, etc.

struct ParticleScaffold {
    Vec2f    pos;
    float    radius;
    uint16_t color;
    float    charge;
};

// ── System ───────────────────────────────────────────────────

class ParticleSystem {
public:
    static constexpr uint16_t MAX_PARTICLES = 256;

    ParticleSystem() = default;

    // ── Setup ────────────────────────────────────────────────
    /// Initialise particles in a grid within the given bounds.
    void init(float boundsW, float boundsH, uint16_t defaultColor);

    /// Initialise from explicit positions (for text-to-particles etc.).
    /// Positions array must have `count` entries.  Velocities start at zero.
    void initFromPositions(const Vec2f* positions, uint16_t count,
                           float boundsW, float boundsH,
                           float radius, uint16_t color);

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
    uint16_t       count()                  const { return _count; }
    const Particle& particle(uint16_t i)    const { return _particles[i]; }
    Particle&       particle(uint16_t i)          { return _particles[i]; }

    // ── Scaffold (initial state snapshot) ────────────────────
    /// Read-only access to the scaffold saved at init time.
    bool hasScaffold() const { return _hasScaffold; }
    const ParticleScaffold& scaffold(uint16_t i) const { return _scaffold[i]; }

    /// Re-snapshot current particle state as the scaffold
    /// (call after externally modifying colors, e.g. screenToParticles).
    void saveScaffold() { _saveScaffold(); }

    /// Restore per-particle colours from the scaffold.
    void restoreColorsFromScaffold();

    /// Restore per-particle positions (and zero velocities) from scaffold.
    void restorePositionsFromScaffold();

    /// Bounds used by the system
    float boundsW() const { return _boundsW; }
    float boundsH() const { return _boundsH; }

private:
    ParticleSystemConfig _config;
    Particle _particles[MAX_PARTICLES];
    ParticleScaffold _scaffold[MAX_PARTICLES];
    uint16_t _count = 0;
    bool     _initialised = false;
    bool     _hasScaffold = false;

    // World
    float _boundsW = 32.0f;
    float _boundsH = 8.0f;
    Vec2f _gravity{0, 0};

    // Internal physics sub-step
    void _substep(float dt);
    void _applyGravity();
    void _integrate(float dt);
    void _constrainWalls();
    void _interParticleInteraction();
    void _scaffoldInteraction();
    void _saveScaffold();

    // ── Per-force methods (called from interaction loops) ─────
    // Each takes pre-computed pair geometry so distances are computed once.
    // Can also be called for particle→scaffold or particle→point forces.

    /// Hard-sphere collision: position correction + elastic bounce.
    void _applyCollision(Particle& a, Particle& b,
                         Vec2f normal, float dist, float minDist);

    /// Short-range attraction: linear pull strongest at contact, zero at attractDist.
    void _applyAttraction(Particle& a, Particle& b,
                          Vec2f normal, float dist, float minDist,
                          float attractDist);

    /// Spring force: linear, charge-dependent.
    /// F = springStrength × qA × qB × (1 − dist/range).
    void _applySpringForce(Particle& a, Particle& b,
                           Vec2f normal, float dist);

    /// Coulomb force: 1/r², charge-dependent.
    /// F = coulombStrength × qA × qB / dist².
    void _applyCoulombForce(Particle& a, Particle& b,
                            Vec2f normal, float dist);
};
