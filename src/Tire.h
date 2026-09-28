#pragma once

// Modelo de grip simplificado (nada de Pacejka): curvas de multiplicador 0..1 sobre el grip
// pico y una elipse de fricción que combina longitudinal y lateral.
struct TireParams {
    float radius = 0.33f;
    float inertia = 0.9f;            // kg m^2 (rueda + neumático)
    float longGrip = 1.10f;          // coeficiente de fricción pico longitudinal
    float latGrip = 1.00f;           // coeficiente de fricción pico lateral
    float roundness = 0.06f;         // radio del perfil del neumático (contacto al inclinar)
    float stiffness = 400000.0f;     // N/m, rigidez vertical del neumático
    float damping = 3500.0f;         // N/(m/s), histéresis de la goma
    float unsprungMass = 12.0f;      // kg, rueda + parte de horquilla/basculante
    // Histéresis de la goma: al descomprimirse devuelve la energía frenada. Sin esto, cuando la
    // suspensión hace tope en una caída grande la moto rebota sobre las cubiertas como en un trampolín.
    float reboundDamping = 20000.0f; // N/(m/s) mientras el neumático se descomprime
    // Qué tan bien le va a esta cubierta en cada superficie (multiplica el agarre del suelo). Una lisa de
    // carreras en la tierra o el pasto no tiene tacos que muerdan: patina; una de tacos, al revés.
    float looseGrip = 1.0f;          // tierra y pasto
    float pavedGrip = 1.0f;          // asfalto, cemento y objetos (cajas, escalones, casas)
};

// |slip ratio| -> multiplicador. 0: normal, 0.1-0.3: máximo, >0.4 cae, 1+: mucho patinaje.
float LongitudinalGripCurve(float slipRatio);
// ángulo de deriva (rad) -> multiplicador.
float LateralGripCurve(float slipAngle);

struct TireSolveInput {
    float normalForce;               // N
    float desiredLong;               // N, fuerza que anularía el deslizamiento longitudinal
    float desiredLat;                // N, fuerza que anularía la velocidad lateral
    float slipRatio;
    float slipAngle;
    float surfaceGrip;               // 1 = tierra de pista, <1 = pasto, etc.
};

struct TireSolveOutput {
    float longForce;
    float latForce;
    float usage;                     // demanda / grip disponible (>1 = deslizando)
};

TireSolveOutput SolveTireForces(const TireParams& p, const TireSolveInput& in);
