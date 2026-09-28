#pragma once
#include <cmath>

// Helpers escalares. Van en su propio namespace para no chocar con Clamp/Lerp de raymath.
namespace mu {

constexpr float kPi = 3.14159265358979f;

inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float Smoothstep(float e0, float e1, float x)
{
    float t = Clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
inline float Rad(float deg) { return deg * (kPi / 180.0f); }
inline float Deg(float rad) { return rad * (180.0f / kPi); }
inline float Sign(float v) { return v < 0.0f ? -1.0f : 1.0f; }

inline float MoveTowards(float current, float target, float maxDelta)
{
    if (std::fabs(target - current) <= maxDelta) return target;
    return current + Sign(target - current) * maxDelta;
}

// Suavizado exponencial independiente del framerate.
inline float Damp(float current, float target, float rate, float dt)
{
    return target + (current - target) * std::exp(-rate * dt);
}

} // namespace mu
