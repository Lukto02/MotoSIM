#pragma once

// Resorte con una pizca de progresividad + amortiguador lineal + tope (bump stop).
struct SuspensionParams {
    float travel = 0.30f;            // m de recorrido en el eje de la rueda
    float spring = 9000.0f;          // N/m
    float damping = 900.0f;          // N/(m/s) al comprimir
    float reboundRatio = 4.0f;       // el rebote (extensión) se frena más: evita que la moto "patee"
    float bottomingDamping = 1.5f;   // amortiguación extra en el último 20% del recorrido (x veces)
    float progressivity = 3.0f;      // force *= 1 + x^2 * progressivity  (x = compresión / recorrido)
    float bumpStop = 150000.0f;      // N/m pasado el recorrido total
    float bumpDamping = 12000.0f;    // N/(m/s) del tope al comprimir
    float maxDamperVelocity = 6.0f;  // recorte de seguridad de la velocidad que ve el amortiguador (m/s)
    // Las de abajo, siempre al final: Bike.h inicializa las de arriba por posición ({travel, spring, ...}).
    float reboundBottoming = 0.0f;   // rebote: amortiguación extra (x veces) cuanto más hundida está (0 = como siempre)
    float reboundBottomingFrom = 0.5f; // desde qué fracción del recorrido empieza (llega entera al fondo)
};

struct SuspensionState {
    float compression = 0.0f;        // m, 0 = totalmente extendida, puede superar travel (tope)
    float velocity = 0.0f;           // m/s, positiva = comprimiendo
    float force = 0.0f;              // N
};

// Actualiza el estado con la compresión geométrica nueva y devuelve la fuerza (>= 0). loaded: la cubierta apoya en el
// piso (reboundBottoming sólo frena el rebote que levanta el chasis, no la rueda que se estira en el aire).
float UpdateSuspension(const SuspensionParams& p, SuspensionState& s, float compression, float dt, bool loaded = true);

