#include "Engine.h"

#include "MathUtil.h"

#include <cmath>

namespace {
constexpr float kRadPerSecToRPM = 60.0f / (2.0f * mu::kPi);

// Curva aproximada de un 450 cuatro tiempos: poco abajo, lleno en medios, pico ~7000.
struct CurvePoint { float rpm, torque; };
constexpr CurvePoint kCurve[] = {
    {0.0f, 16.0f},   {1000.0f, 20.0f}, {3000.0f, 29.0f}, {5000.0f, 39.0f},
    {7000.0f, 47.0f}, {8500.0f, 46.0f}, {9000.0f, 44.0f}, {10000.0f, 39.0f},
    {11000.0f, 30.0f},
};
} // namespace

float Engine::TorqueCurve(float r)
{
    constexpr int n = sizeof(kCurve) / sizeof(kCurve[0]);
    if (r <= kCurve[0].rpm) return kCurve[0].torque;
    for (int i = 1; i < n; ++i) {
        if (r <= kCurve[i].rpm) {
            float t = (r - kCurve[i - 1].rpm) / (kCurve[i].rpm - kCurve[i - 1].rpm);
            return mu::Lerp(kCurve[i - 1].torque, kCurve[i].torque, t);
        }
    }
    return kCurve[n - 1].torque;
}

float Engine::Ratio(int g) const
{
    g = g < 1 ? 1 : (g > 5 ? 5 : g);
    return p->gear[g - 1] * p->finalDrive;
}

float Engine::Update(float wheelOmega, float throttle, float dt, float& outInertia)
{
    const float ratio = TotalRatio();
    const float wheelRPM = wheelOmega * ratio * kRadPerSecToRPM;
    outInertia = 0.0f;

    if (reverse) {
        // Marcha atrás: el motor sube de vueltas con el gas (embrague patinando) y empuja la rueda
        // hacia atrás con poco torque, que se corta al acercarse al tope de velocidad.
        clutchSlipping = true;
        limiter = false;
        rpm = mu::Damp(rpm, mu::Lerp(p->idleRPM, 4500.0f, throttle), 8.0f, dt);
        crankTorque = 0.0f;
        const float governor = mu::Clamp((p->reverseWheelSpeed + wheelOmega) / p->reverseWheelSpeed * 4.0f, 0.0f, 1.0f);
        return -throttle * p->reverseTorque * governor;
    }

    if (shiftTimer > 0.0f) {
        // Embrague abierto durante el cambio: el motor gira libre, sin torque a la rueda.
        shiftTimer -= dt;
        float target = mu::Lerp(p->idleRPM, p->revLimit * 0.8f, throttle);
        rpm = mu::Damp(rpm, target, 6.0f, dt);
        crankTorque = 0.0f;
        return 0.0f;
    }

    const float launchRPM = mu::Lerp(p->idleRPM, p->clutchRPM, throttle);
    if (wheelRPM < launchRPM) {
        // Embrague patinando (arranque / muy baja velocidad): el motor sostiene launchRPM y
        // transmite su torque. Con throttle 0 el embrague está abierto: no hay creep.
        clutchSlipping = true;
        rpm = mu::Damp(rpm, launchRPM, 10.0f, dt);
        limiter = false;
        crankTorque = Torque(rpm) * p->torqueScale * throttle;
    } else {
        clutchSlipping = false;
        rpm = wheelRPM;
        outInertia = p->engineInertia * ratio * ratio;
        if (limiter) {
            if (rpm < p->revLimit - 250.0f) limiter = false;
        } else if (rpm > p->revLimit) {
            limiter = true;
        }
        float drive = limiter ? 0.0f : Torque(rpm) * p->torqueScale * throttle;
        float braking = p->engineBrake * (rpm / p->revLimit) * (1.0f - throttle);
        crankTorque = drive - braking;
    }
    return crankTorque * ratio * p->efficiency;
}

void Engine::ShiftUp()
{
    if (reverse) {                   // de R a primera
        reverse = false;
        shiftTimer = p->shiftTime;
        return;
    }
    if (gear >= 5 || shiftTimer > 0.0f) return;
    ++gear;
    shiftTimer = p->shiftTime;
}

void Engine::ShiftDown(float wheelOmega)
{
    if (shiftTimer > 0.0f || reverse) return;
    if (gear == 1) {                 // de primera a R, sólo casi parado (~1 m/s)
        if (std::fabs(wheelOmega) < 3.0f) {
            reverse = true;
            shiftTimer = p->shiftTime;
        }
        return;
    }
    // No permitir una reducción que pase el motor muy por encima del limitador.
    float newRPM = wheelOmega * Ratio(gear - 1) * kRadPerSecToRPM;
    if (newRPM > p->revLimit + 500.0f) return;
    --gear;
    shiftTimer = p->shiftTime * 0.8f;
}

void Engine::Reset()
{
    rpm = p ? p->idleRPM : 1800.0f;
    gear = 1;
    reverse = false;
    shiftTimer = 0.0f;
    limiter = false;
    clutchSlipping = true;
    crankTorque = 0.0f;
}
