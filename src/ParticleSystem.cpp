#include "ParticleSystem.h"
#include <Arduino.h>   // random()
#include <math.h>

// ── Setup ────────────────────────────────────────────────────

void ParticleSystem::init(float boundsW, float boundsH, uint16_t defaultColor) {
    _boundsW = boundsW;
    _boundsH = boundsH;
    _count   = _config.count;
    if (_count > MAX_PARTICLES) _count = MAX_PARTICLES;

    if (_count == 0) { _initialised = true; return; }

    // Distribute in a grid that respects the aspect ratio
    float aspect = boundsW / boundsH;
    uint8_t cols = (uint8_t)ceilf(sqrtf(_count * aspect));
    if (cols == 0) cols = 1;
    uint8_t rows = (_count + cols - 1) / cols;
    if (rows == 0) rows = 1;

    float margin = _config.radius + 0.25f;
    float spanX  = boundsW - 2.0f * margin;
    float spanY  = boundsH - 2.0f * margin;
    float dx     = (cols > 1) ? (spanX / (cols - 1)) : 0.0f;
    float dy     = (rows > 1) ? (spanY / (rows - 1)) : 0.0f;

    for (uint16_t i = 0; i < _count; i++) {
        uint8_t col = i % cols;
        uint8_t row = i / cols;

        Particle& p = _particles[i];
        p.pos    = Vec2f(margin + dx * col, margin + dy * row);
        p.vel = Vec2f(
            ((int)random(-100, 101)) * 0.003f,
            ((int)random(-100, 101)) * 0.003f
        );
        p.accel  = Vec2f{0, 0};
        p.radius = _config.radius;
        p.color  = defaultColor;
    }

    _initialised = true;
}

void ParticleSystem::initFromPositions(const Vec2f* positions, uint16_t count,
                                       float boundsW, float boundsH,
                                       float radius, uint16_t color) {
    _boundsW = boundsW;
    _boundsH = boundsH;
    _count   = count;
    if (_count > MAX_PARTICLES) _count = MAX_PARTICLES;

    for (uint16_t i = 0; i < _count; i++) {
        Particle& p = _particles[i];
        p.pos    = positions[i];
        p.vel    = Vec2f{0, 0};
        p.accel  = Vec2f{0, 0};
        p.radius = radius;
        p.color  = color;
    }
    _initialised = true;
}

// ── Configuration ────────────────────────────────────────────

void ParticleSystem::setConfig(const ParticleSystemConfig& cfg) {
    bool countChanged = (cfg.count != _config.count);
    _config = cfg;
    if (_config.renderMs == 0) _config.renderMs = 1;
    if (_config.substepMs == 0) _config.substepMs = 1;
    if (_config.count > MAX_PARTICLES) _config.count = MAX_PARTICLES;

    // Update radii on live particles
    for (uint16_t i = 0; i < _count; i++) {
        _particles[i].radius = _config.radius;
    }

    if (countChanged && _initialised) {
        _initialised = false;   // trigger re-init
    }
}

void ParticleSystem::setGravity(float gx, float gy) {
    static constexpr float alpha = 0.18f;
    _gravity.x += (gx - _gravity.x) * alpha;
    _gravity.y += (gy - _gravity.y) * alpha;
}

// ── Simulation step ──────────────────────────────────────────

void ParticleSystem::step(float dt) {
    if (_count == 0) return;

    // Cap large dt to avoid explosion
    if (dt > 0.10f) dt = 0.10f;

    // Sub-step at configured max interval for stability
    float maxSub = _config.substepMs * 0.001f;
    if (maxSub < 0.005f) maxSub = 0.005f;
    while (dt > 0.0f) {
        float sub = (dt > maxSub) ? maxSub : dt;
        _substep(sub);
        dt -= sub;
    }
}

// ── Internal ─────────────────────────────────────────────────

void ParticleSystem::_substep(float dt) {
    _applyGravity();
    _integrate(dt);
    _constrainWalls();
    _resolveCollisions();
}

void ParticleSystem::_applyGravity() {
    if (!_config.gravityEnabled) {
        for (uint16_t i = 0; i < _count; i++)
            _particles[i].accel = Vec2f{0, 0};
        return;
    }
    Vec2f g = _gravity * _config.gravityScale;
    for (uint16_t i = 0; i < _count; i++) {
        _particles[i].accel = g;
    }
}

void ParticleSystem::_integrate(float dt) {
    // Velocity Verlet: pos += v*dt + ½a*dt²; v += a*dt
    float halfDt2 = 0.5f * dt * dt;

    for (uint16_t i = 0; i < _count; i++) {
        Particle& p = _particles[i];
        p.pos += p.vel * dt + p.accel * halfDt2;
        p.vel += p.accel * dt;
        p.vel *= _config.damping;

        // Langevin jitter (thermal noise)
        if (_config.temperature > 0.0f) {
            float t = _config.temperature;
            p.vel.x += ((int)random(-1000, 1001)) * 0.001f * t;
            p.vel.y += ((int)random(-1000, 1001)) * 0.001f * t;
        }

        p.accel = Vec2f{0, 0};
    }
}

void ParticleSystem::_constrainWalls() {
    for (uint16_t i = 0; i < _count; i++) {
        Particle& p = _particles[i];
        float minX = p.radius;
        float maxX = _boundsW - 1.0f - p.radius;
        float minY = p.radius;
        float maxY = _boundsH - 1.0f - p.radius;
        float we   = _config.wallElasticity;

        if (p.pos.x < minX) {
            p.pos.x = minX;
            p.vel.x = -p.vel.x * we;
        } else if (p.pos.x > maxX) {
            p.pos.x = maxX;
            p.vel.x = -p.vel.x * we;
        }

        if (p.pos.y < minY) {
            p.pos.y = minY;
            p.vel.y = -p.vel.y * we;
        } else if (p.pos.y > maxY) {
            p.pos.y = maxY;
            p.vel.y = -p.vel.y * we;
        }
    }
}

void ParticleSystem::_resolveCollisions() {
    // Interaction range for attraction (in absolute pixels)
    bool doAttract = (_config.attractStrength > 0.0f);

    for (uint16_t i = 0; i < _count; i++) {
        for (uint16_t j = i + 1; j < _count; j++) {
            Particle& a = _particles[i];
            Particle& b = _particles[j];

            Vec2f delta = b.pos - a.pos;
            float minDist = a.radius + b.radius;
            float distSq  = delta.lengthSq();

            // Attraction range = minDist * attractRange
            float attractDist = minDist * _config.attractRange;
            float attractDistSq = attractDist * attractDist;

            // Skip if beyond both collision and attraction range
            if (distSq >= attractDistSq && distSq >= minDist * minDist) continue;

            // Degenerate case: particles exactly overlapping
            if (distSq < 1e-6f) {
                delta = Vec2f(0.01f, 0.0f);
                distSq = delta.lengthSq();
            }

            float dist    = sqrtf(distSq);
            Vec2f normal  = delta / dist;

            if (dist < minDist) {
                // ── Contact: position correction + elastic bounce ──
                float overlap = minDist - dist;
                a.pos -= normal * (overlap * 0.5f);
                b.pos += normal * (overlap * 0.5f);

                Vec2f relVel = b.vel - a.vel;
                float normSpeed = relVel.dot(normal);

                if (normSpeed < 0.0f) {
                    float impulse = -(1.0f + _config.elasticity) * normSpeed * 0.5f;
                    Vec2f imp = normal * impulse;
                    a.vel -= imp;
                    b.vel += imp;
                }
            }
            else if (doAttract && dist < attractDist) {
                // ── Attraction: linear spring pull ──
                // Force strongest at contact (t=0), fades to 0 at attractDist (t=1)
                float t = (dist - minDist) / (attractDist - minDist);
                float force = _config.attractStrength * (1.0f - t);
                // Apply as velocity impulse (equal mass, symmetric)
                Vec2f pull = normal * force;
                a.vel += pull;
                b.vel -= pull;
            }
        }
    }
}
