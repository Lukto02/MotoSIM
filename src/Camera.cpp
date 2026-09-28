#include "Terrain.h"   // (incluye Jolt: siempre antes que raylib)

#include "Camera.h"

#include "MathUtil.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>

namespace {

// Resorte críticamente amortiguado que sigue a un objetivo EN MOVIMIENTO: con la velocidad del
// objetivo como feed-forward no queda retraso a velocidad constante, sólo al acelerar/frenar,
// que es justo lo que da sensación de empuje.
void Spring(float& x, float& v, float target, float targetVel, float omega, float dt)
{
    float a = omega * omega * (target - x) + 2.0f * omega * (targetVel - v);
    v += a * dt;
    x += v * dt;
}

} // namespace

void ChaseCamera::Reset(Vector3 bikePos, Vector3 bikeForward)
{
    Vector3 flat = {bikeForward.x, 0.0f, bikeForward.z};
    heading = Vector3Length(flat) > 0.1f ? Vector3Normalize(flat) : Vector3{0.0f, 0.0f, 1.0f};
    pos = Vector3Add(Vector3Subtract(bikePos, Vector3Scale(heading, distance)), {0.0f, height, 0.0f});
    look = Vector3Add(bikePos, {0.0f, lookHeight, 0.0f});
    vel = lookVel = {0.0f, 0.0f, 0.0f};
    smoothVy = 0.0f;
    fov = fovMin;
    trauma = fovKick = 0.0f;
    orbitYaw = orbitPitch = orbitYawS = orbitPitchS = orbitYawVel = orbitPitchVel = 0.0f;
    orbitIdle = 10.0f;
    cam.position = pos;
    cam.target = look;
    cam.up = {0.0f, 1.0f, 0.0f};
    cam.fovy = fov;
    cam.projection = CAMERA_PERSPECTIVE;
}

void ChaseCamera::Update(float dt, Vector3 bikePos, Vector3 bikeForward, Vector3 bikeVel, const Terrain& terrain)
{
    dt = std::min(dt, 0.05f);
    const Vector3 flatVel = {bikeVel.x, 0.0f, bikeVel.z};
    const float speed = Vector3Length(flatVel);

    // Rumbo: dirección de la moto a baja velocidad, dirección de la velocidad al ir rápido.
    Vector3 flatFwd = {bikeForward.x, 0.0f, bikeForward.z};
    Vector3 desired = Vector3Length(flatFwd) > 0.2f ? Vector3Normalize(flatFwd) : heading;
    if (speed > 2.0f)
        desired = Vector3Normalize(Vector3Lerp(desired, Vector3Scale(flatVel, 1.0f / speed), 0.65f * mu::Smoothstep(2.0f, 9.0f, speed)));
    heading = Vector3Normalize(Vector3Lerp(heading, desired, 1.0f - std::exp(-3.2f * dt)));

    // El resorte de persecución trabaja siempre detrás de la moto; la órbita del mouse se aplica
    // después, girando esa posición alrededor del piloto (ver UpdateOrbit).
    Vector3 targetPos = Vector3Add(Vector3Subtract(bikePos, Vector3Scale(heading, distance)), {0.0f, height, 0.0f});
    Vector3 lead = Vector3Scale(flatVel, 0.10f);
    if (Vector3Length(lead) > 2.5f) lead = Vector3Scale(Vector3Normalize(lead), 2.5f);
    lead = Vector3Scale(lead, std::max(0.0f, std::cos(orbitYawS)));   // mirando de costado o de frente, al piloto
    Vector3 targetLook = Vector3Add(Vector3Add(bikePos, {0.0f, lookHeight, 0.0f}), lead);
    if (mode == Mode::Side) {
        // Vista lateral para debug: ver trabajar la suspensión y el cabeceo.
        const Vector3 rightDir = {-heading.z, 0.0f, heading.x};
        targetPos = Vector3Add(Vector3Add(bikePos, Vector3Scale(rightDir, 5.0f)), {0.0f, 0.9f, 0.0f});
        targetLook = Vector3Add(bikePos, {0.0f, 0.3f, 0.0f});
    }

    // En vertical la cámara no copia cada golpe del chasis: sigue una velocidad vertical filtrada.
    smoothVy = mu::Damp(smoothVy, bikeVel.y, 4.0f, dt);
    const int steps = std::max(1, (int)std::ceil(dt / 0.005f));
    const float h = dt / (float)steps;
    for (int i = 0; i < steps; ++i) {
        Spring(pos.x, vel.x, targetPos.x, bikeVel.x, stiffness, h);
        Spring(pos.z, vel.z, targetPos.z, bikeVel.z, stiffness, h);
        Spring(pos.y, vel.y, targetPos.y, smoothVy, verticalStiffness, h);
        Spring(look.x, lookVel.x, targetLook.x, bikeVel.x, stiffness * 1.6f, h);
        Spring(look.z, lookVel.z, targetLook.z, bikeVel.z, stiffness * 1.6f, h);
        Spring(look.y, lookVel.y, targetLook.y, smoothVy, verticalStiffness * 1.3f, h);
    }

    // No meterse dentro del terreno.
    const float ground = terrain.Height(pos.x, pos.z) + 0.6f;
    if (pos.y < ground) {
        pos.y = ground;
        vel.y = std::max(vel.y, 0.0f);
    }

    // Órbita: la posición de persecución se gira alrededor del piloto en ángulos, con el mismo radio.
    // Girando en línea recta (como antes) la cámara cortaba camino y pasaba pegada a la moto.
    UpdateOrbit(dt);
    const Vector3 pivot = Vector3Add(bikePos, {0.0f, lookHeight, 0.0f});
    const Vector3 off = Vector3Subtract(pos, pivot);
    const float radius = Vector3Length(off) * zoomS;
    const float yaw = std::atan2(off.x, off.z) + orbitYawS;
    const float elev = mu::Clamp(std::atan2(off.y, std::sqrt(off.x * off.x + off.z * off.z)) + orbitPitchS, -0.2f, 1.35f);
    Vector3 eye = Vector3Add(pivot, {std::sin(yaw) * std::cos(elev) * radius, std::sin(elev) * radius, std::cos(yaw) * std::cos(elev) * radius});
    eye.y = std::max(eye.y, terrain.Height(eye.x, eye.z) + 0.6f);

    fov = mu::Damp(fov, mu::Lerp(fovMin, fovMax, mu::Smoothstep(3.0f, 28.0f, speed)), 2.0f, dt);
    fovKick = mu::Damp(fovKick, 0.0f, 5.0f, dt);

    // Sacudón (aterrizajes, choques): ruido suave que decae, proporcional a trauma^2, en posición y
    // en giro alrededor de la mirada. Rápido, además, una vibración fina constante.
    trauma = std::max(0.0f, trauma - 1.5f * dt);
    shakeTime += dt;
    auto wobble = [&](float freq, float phase) {
        return 0.6f * std::sin(shakeTime * freq + phase) + 0.4f * std::sin(shakeTime * freq * 2.3f + phase * 1.7f);
    };
    const float k = trauma * trauma;
    const float buzz = shakeEnabled ? mu::Smoothstep(14.0f, 30.0f, speed) * 0.004f : 0.0f;
    const Vector3 shake = {k * 0.12f * wobble(31.0f, 0.0f) + buzz * wobble(71.0f, 0.3f),
                           k * 0.12f * wobble(37.0f, 1.3f) + buzz * wobble(83.0f, 1.1f),
                           k * 0.12f * wobble(29.0f, 2.1f) + buzz * wobble(67.0f, 2.9f)};
    const float roll = k * mu::Rad(4.5f) * wobble(23.0f, 0.7f);

    // Corrimiento lateral (menú): cámara y mirada se mueven juntas hacia la izquierda de la vista, así
    // la moto queda a la derecha de la pantalla y el menú no la tapa.
    shiftS = mu::Damp(shiftS, shift, 3.0f, dt);
    const Vector3 viewDir = Vector3Normalize(Vector3Subtract(look, eye));
    const Vector3 side = Vector3Scale(Vector3Normalize(Vector3CrossProduct(viewDir, {0.0f, 1.0f, 0.0f})), -shiftS);
    cam.position = Vector3Add(Vector3Add(eye, shake), side);
    cam.target = Vector3Add(Vector3Add(look, Vector3Scale(shake, 0.5f)), side);
    cam.up = Vector3RotateByAxisAngle({0.0f, 1.0f, 0.0f}, Vector3Normalize(Vector3Subtract(cam.target, cam.position)), roll);
    cam.fovy = fov + fovKick;
    cam.projection = CAMERA_PERSPECTIVE;
}

void ChaseCamera::Orbit(float dYaw, float dPitch)
{
    // Tope por frame: un tirón del mouse (p. ej. al volver el foco a la ventana) no da vuelta la cámara.
    orbitYaw += mu::Clamp(dYaw, -0.3f, 0.3f);
    orbitPitch = mu::Clamp(orbitPitch + mu::Clamp(dPitch, -0.3f, 0.3f), -0.3f, 1.1f);   // ni bajo el suelo ni cenital
    orbitYawVel = orbitPitchVel = 0.0f;
    orbitIdle = 0.0f;
}

void ChaseCamera::UpdateOrbit(float dt)
{
    // Soltado el mouse, vuelve atrás de la moto con un resorte críticamente amortiguado: arranca y
    // llega suave, por el lado más corto.
    orbitIdle += dt;
    if (fixedView) {                   // pruebas: vista fija pedida por línea de comandos
        orbitYaw = orbitYawS = fixedYaw;
        orbitPitch = orbitPitchS = fixedPitch;
        zoom = zoomS = fixedZoom;
        return;
    }
    if (orbitIdle > 0.35f) {
        orbitYaw = std::remainder(orbitYaw, 2.0f * mu::kPi);
        Spring(orbitYaw, orbitYawVel, 0.0f, 0.0f, 3.0f, dt);
        Spring(orbitPitch, orbitPitchVel, 0.0f, 0.0f, 3.0f, dt);
    }
    // Lo que se ve sigue a lo pedido suavizado, siempre por la diferencia angular más corta: cruzar
    // los 180° no hace que la cámara se dé vuelta por el otro lado.
    const float follow = 1.0f - std::exp(-20.0f * dt);
    orbitYawS = std::remainder(orbitYawS + std::remainder(orbitYaw - orbitYawS, 2.0f * mu::kPi) * follow, 2.0f * mu::kPi);
    orbitPitchS += (orbitPitch - orbitPitchS) * follow;
    zoomS = mu::Damp(zoomS, zoom, 10.0f, dt);
}

void ChaseCamera::Zoom(float steps) { zoom = mu::Clamp(zoom * (1.0f - 0.1f * steps), 0.6f, 2.5f); }

void ChaseCamera::AddShake(float amount)
{
    if (!shakeEnabled) return;
    trauma = std::min(1.0f, trauma + amount);
    fovKick -= amount * 7.0f;          // golpe de zoom hacia adentro que se relaja solo
    vel.y -= amount * 2.5f;            // la cámara acompaña el golpe hacia abajo y vuelve con su resorte
}
