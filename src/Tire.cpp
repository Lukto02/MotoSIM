#include "Tire.h"

#include "MathUtil.h"

#include <algorithm>
#include <cmath>

float LongitudinalGripCurve(float slipRatio)
{
    const float s = std::fabs(slipRatio);
    if (s < 0.12f) return mu::Lerp(0.90f, 1.0f, s / 0.12f);        // sube hacia el pico
    if (s < 0.30f) return 1.0f;                                     // meseta de máximo grip
    if (s < 1.00f) return mu::Lerp(1.0f, 0.70f, mu::Smoothstep(0.30f, 1.0f, s));
    return std::max(0.62f, 0.70f - (s - 1.0f) * 0.03f);             // patinaje fuerte
}

float LateralGripCurve(float slipAngle)
{
    const float a = std::fabs(slipAngle);
    if (a < 0.10f) return mu::Lerp(0.88f, 1.0f, a / 0.10f);
    if (a < 0.22f) return 1.0f;
    return mu::Lerp(1.0f, 0.90f, mu::Smoothstep(0.22f, 0.80f, a));   // deslizando conserva casi todo el grip
}

TireSolveOutput SolveTireForces(const TireParams& p, const TireSolveInput& in)
{
    TireSolveOutput out{0.0f, 0.0f, 0.0f};
    if (in.normalForce <= 1.0f) return out;

    const float maxLong = in.normalForce * in.surfaceGrip * p.longGrip * LongitudinalGripCurve(in.slipRatio);
    const float maxLat = in.normalForce * in.surfaceGrip * p.latGrip * LateralGripCurve(in.slipAngle);

    // Elipse de fricción: si la demanda combinada supera el grip, se escala manteniendo la
    // dirección. Así el patinaje de la trasera "se come" el grip lateral (la cola se abre).
    const float ex = in.desiredLong / maxLong;
    const float ey = in.desiredLat / maxLat;
    const float usage = std::sqrt(ex * ex + ey * ey);
    const float scale = usage > 1.0f ? 1.0f / usage : 1.0f;

    out.longForce = in.desiredLong * scale;
    out.latForce = in.desiredLat * scale;
    out.usage = usage;
    return out;
}
