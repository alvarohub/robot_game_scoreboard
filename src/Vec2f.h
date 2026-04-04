#pragma once
// ═══════════════════════════════════════════════════════════════
//  Vec2f — lightweight 2D float vector for physics simulations
// ═══════════════════════════════════════════════════════════════

#include <math.h>

struct Vec2f {
    float x = 0.0f;
    float y = 0.0f;

    Vec2f() = default;
    Vec2f(float x_, float y_) : x(x_), y(y_) {}

    // ── Arithmetic ───────────────────────────────────────────
    Vec2f  operator+(Vec2f v)  const { return {x + v.x, y + v.y}; }
    Vec2f  operator-(Vec2f v)  const { return {x - v.x, y - v.y}; }
    Vec2f  operator*(float s)  const { return {x * s, y * s}; }
    Vec2f  operator/(float s)  const { return {x / s, y / s}; }
    Vec2f  operator-()         const { return {-x, -y}; }

    Vec2f& operator+=(Vec2f v) { x += v.x; y += v.y; return *this; }
    Vec2f& operator-=(Vec2f v) { x -= v.x; y -= v.y; return *this; }
    Vec2f& operator*=(float s) { x *= s; y *= s; return *this; }

    // ── Geometry ─────────────────────────────────────────────
    float dot(Vec2f v)      const { return x * v.x + y * v.y; }
    float lengthSq()        const { return x * x + y * y; }
    float length()          const { return sqrtf(lengthSq()); }

    Vec2f normalized() const {
        float len = length();
        return (len > 1e-7f) ? Vec2f{x / len, y / len} : Vec2f{0, 0};
    }

    // ── Utility ──────────────────────────────────────────────
    void clamp(float minX, float minY, float maxX, float maxY) {
        if (x < minX) x = minX; else if (x > maxX) x = maxX;
        if (y < minY) y = minY; else if (y > maxY) y = maxY;
    }
};

// scalar * vec
inline Vec2f operator*(float s, Vec2f v) { return {s * v.x, s * v.y}; }
