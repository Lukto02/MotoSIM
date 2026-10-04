#include "Suspension.h"

#include "MathUtil.h"

#include <algorithm>

float UpdateSuspension(const SuspensionParams& p, SuspensionState& s, float compression, float dt, bool loaded)
{
    const float rawVelocity = (compression - s.compression) / dt;
    s.velocity = mu::Clamp(rawVelocity, -p.maxDamperVelocity, p.maxDamperVelocity);
    s.compression = compression;

    const float c = mu::Clamp(compression, 0.0f, p.travel);
    const float x = c / p.travel;
    const float springForce = p.spring * c * (1.0f + x * x * p.progressivity);

    // Compresión: lineal, con un cono hidráulico en el último 20% del recorrido que frena más
    // para que los aterrizajes grandes no terminen en un golpe seco.
    // Extensión (rebote): mucho más amortiguada, así la energía del aterrizaje no relanza la moto.
    float damperForce = s.velocity >= 0.0f
        ? p.damping * (1.0f + p.bottomingDamping * mu::Smoothstep(0.8f, 1.0f, x)) * s.velocity
        : p.damping * p.reboundRatio * s.velocity;
    // Rebote desde el fondo (reboundBottoming): el resorte progresivo guarda mucha fuerza en el último tramo y,
    // con la extensión amortiguada igual que cerca del sag, después de una caída alta lanzaba el chasis hacia arriba
    // (las cubiertas casi sin carga) en vez de devolverlo despacio. Con 0 no se hace la cuenta (bit a bit lo de antes).
    // Sólo con la cubierta apoyada: la rueda en el aire (pasando un tronco) se estira como siempre, lista para el próximo golpe.
    if (p.reboundBottoming > 0.0f && s.velocity < 0.0f && loaded)
        damperForce *= 1.0f + p.reboundBottoming * mu::Smoothstep(p.reboundBottomingFrom, 1.0f, x);

    // Tope de final de recorrido: muy duro al comprimir y casi sin devolver energía al salir
    // (histéresis). Evita el "rebote trampolín".
    float bump = 0.0f;
    const float over = compression - p.travel;
    if (over > 0.0f) {
        const bool extending = rawVelocity < 0.0f;
        bump = over * p.bumpStop * (extending ? 0.2f : 1.0f) + std::max(0.0f, rawVelocity) * p.bumpDamping;
    }

    s.force = mu::Clamp(springForce + damperForce + bump, 0.0f, 40000.0f);
    return s.force;
}
