// Física de la moto: UN rigid body (Jolt) + dos ruedas virtuales por raycast.
// Todo se aplica como fuerzas/torques sobre el cuerpo antes de cada paso de Jolt.
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>

#include "Bike.h"

#include "MathUtil.h"
#include "PhysicsWorld.h"
#include "Terrain.h"

#include <algorithm>
#include <cmath>

using JPH::Body;
using JPH::Mat44;
using JPH::Quat;
using JPH::Vec3;

namespace {
constexpr float kG = 9.81f;
constexpr float kRadPerSecToRPM = 60.0f / (2.0f * mu::kPi);
// Parada sobre la trasera (wheelie pasado), la cubierta apoya mientras el eje de suspensión no quede
// casi paralelo al suelo; más allá la sostiene la cola (Bike::BuildShapes). En cualquier otra posición
// (moto volcada) se deja de usar la rueda antes.
constexpr float kMinAxisCosUpright = 0.1f;
// Una cara de un objeto más empinada que esto (normal con menos de 0.5 hacia arriba: más de 60°) no es
// piso: es la contrahuella de un escalón, un cordón o el costado de una caja.
constexpr float kSteepFaceY = 0.5f;

// Las ruedas sólo apoyan en lo estático (terreno), no en el piloto suelto tras una caída.
class StaticOnlyFilter final : public JPH::ObjectLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer) const override { return layer == Layers::STATIC; }
};
} // namespace

void Bike::UpdateWheelConfig()
{
    Wheel& f = wheels[FRONT];
    Wheel& r = wheels[REAR];
    const float rake = mu::Rad(P->rakeDeg), rearAxis = mu::Rad(P->rearAxisDeg);
    f.mountLocal = Vec3(0.0f, P->frontMountY, P->frontMountZ);
    f.axisLocal = Vec3(0.0f, -std::cos(rake), std::sin(rake));
    r.mountLocal = Vec3(0.0f, P->rearMountY, P->rearMountZ);
    r.axisLocal = Vec3(0.0f, -std::cos(rearAxis), -std::sin(rearAxis));
    f.radius = P->frontTire.radius;
    r.radius = P->rearTire.radius;
    f.travel = P->front.travel;
    r.travel = P->rear.travel;
}

// Freno de mano (handbrake_yaw > 0; ver FISICA.md, "Freno de mano"): doblando y clavando el freno trasero solo
// (Espacio, el botón A), el piloto saca la cola hasta el ángulo que busca y la sostiene ahí con el cuerpo y el
// manubrio, en vez de seguir inclinado con la trasera trabada (que no sostiene nada de costado, pero tampoco empuja la
// cola: el contravolante automático la mantenía derecha). Es un PD sobre la cola afuera: pasado el ángulo, la frena
// (no termina en un trompo). La delantera sigue su camino, apuntada slide_steer_in grados hacia la curva
// (HandbrakeSteer): empuja hacia adentro sin frenar de costado. Al soltar el freno o la dirección, durante
// handbrake_catch segundos el piloto ataja la cola (el mismo PD, hacia la moto derecha): si no, con la cola a ~30° y
// gas, la trasera patinando no agarraba y el contravolante (topado en 30°) no alcanzaba: trompo.
// lowSlide (0..1): cuánto manda el derrape lento de siempre (slideIntent, debajo de slide_speed_max): ahí el freno de
// mano sólo saca la cola (no la frena: el cavalo de pau de la Trilheira llega a 55-60°) y no toca el manubrio.
// Lo usan las dos físicas (BikePhysics.cpp y BikePhysics031.cpp, con lowSlide 0); sólo se llama con handbrake_yaw > 0.
void Bike::UpdateHandbrake(const BikeInput& in, Vec3 linVel, Vec3 fwdFlat, float lowSlide, float dt)
{
    const Vec3 flatVel(linVel.GetX(), 0.0f, linVel.GetZ());
    const Vec3 rightFlat(-fwdFlat.GetZ(), 0.0f, fwdFlat.GetX());          // fwd × up, en el plano
    const float spd = flatVel.Length();
    const float beta = spd > 1.5f ? std::atan2(flatVel.Dot(rightFlat), std::max(flatVel.Dot(fwdFlat), 0.0f) + 0.2f) : 0.0f;   // + = va a la derecha del rumbo
    hbTurn = std::fabs(in.steer) > 0.1f ? mu::Sign(in.steer) : (beta != 0.0f ? -mu::Sign(beta) : hbTurn);
    const float slide = -beta * hbTurn;                                     // + = cola afuera de la curva
    const float rate = hbValid ? (slide - hbSlide) / dt : 0.0f;
    hbRate += (rate - hbRate) * std::min(1.0f, dt * 20.0f);
    hbSlide = slide;
    hbValid = spd > 1.5f;
    hbLow = lowSlide;
    handbrake = handbrakeTorque = hbCatching = 0.0f;
    hbCatch = std::max(0.0f, hbCatch - dt / std::max(P->handbrakeCatch, 0.01f));
    hbUp = std::max(0.0f, hbUp - dt / std::max(2.0f * P->handbrakeCatch, 0.01f));   // levantarla al salir (handbrake_lean)
    // handbrake_ramp: lo que hace el piloto entra y sale en rampa (hbIn sigue a handbrake a 1/handbrake_ramp por segundo);
    // hbW, el peso del manubrio, el contravolante y la inclinación, se queda mientras hbIn baja (sale suave). Con 0, como antes.
    auto ramp = [&]() {
        if (P->handbrakeRamp > 0.0f) {
            hbIn = mu::MoveTowards(hbIn, handbrake, dt / P->handbrakeRamp);
            hbW = std::max(handbrake, hbIn);
        } else {
            hbIn = hbW = handbrake;
        }
    };
    if (crashed || !riderOnBike || !wheels[REAR].grounded) {
        ramp();
        return;
    }
    // Clavado (el bot frena atrás hasta 0.6: no lo usa), sin el de adelante y doblando.
    handbrake = mu::Smoothstep(0.6f, 0.95f, in.rearBrake) * (1.0f - mu::Smoothstep(0.1f, 0.4f, in.frontBrake)) *
                mu::Smoothstep(0.15f, 0.4f, std::fabs(in.steer)) * mu::Smoothstep(1.5f, 3.0f, spd);
    ramp();
    hbCatch = std::max(hbCatch, handbrake);
    const float catching = (hbCatch - handbrake) * mu::Smoothstep(1.5f, 3.0f, spd);
    if (P->handbrakeLean > 0.0f) {
        hbUp = std::max(hbUp, handbrake);
        hbCatching = (hbUp - hbW) * mu::Smoothstep(1.5f, 3.0f, spd);
    }
    if (handbrake <= 0.0f && catching <= 0.0f) return;
    const float K = P->handbrakeYaw;
    auto pd = [&](float target) { return mu::Clamp(K * (target - slide) - 0.25f * K * hbRate, -0.6f * K, 0.6f * K); };
    float tq = 0.0f;
    if (handbrake > 0.0f) {
        hbWant = mu::Rad(mu::Lerp(P->handbrakeAngle, P->handbrakeAngleFast, mu::Smoothstep(8.0f, 20.0f, spd))) *
                 mu::Lerp(0.6f, 1.0f, mu::Smoothstep(0.4f, 1.0f, std::fabs(in.steer)));
        // Inclinación que sostiene mientras colea (handbrake_lean, ×0.6 con media dirección): firme mientras dura el
        // derrape, sin bajar con la velocidad; afloja recién entre ~11 y ~8 km/h, cuando termina (más despacio manda el
        // derrape lento de siempre: el cavalo de pau de la Trilheira no cambia).
        if (P->handbrakeLean > 0.0f)
            hbLean = hbTurn * mu::Rad(P->handbrakeLean) * mu::Lerp(0.6f, 1.0f, mu::Smoothstep(0.4f, 1.0f, std::fabs(in.steer))) *
                     mu::Smoothstep(2.2f, 3.2f, spd);
        if (P->handbrakeRamp > 0.0f) hbWant *= hbIn;                     // la cola sale de a poco hasta el ángulo
        hbAim = hbCatchWant = hbWant;
        float out = pd(hbWant);
        if (out < 0.0f) out *= 1.0f - lowSlide;                            // pasada: con el derrape lento la ataja el pedal
        tq += out * handbrake;
    }
    if (catching > 0.0f) {
        // Al soltar, con handbrake_ramp el piloto lleva la cola a derecho de a poco (el ángulo buscado baja a 0 en
        // handbrake_ramp s) en vez de atajarla de golpe.
        float target = 0.0f;
        if (P->handbrakeRamp > 0.0f) {
            hbCatchWant = mu::MoveTowards(hbCatchWant, 0.0f, dt * mu::Rad(P->handbrakeAngle) / P->handbrakeRamp);
            target = hbCatchWant;
            if (handbrake <= 0.0f) hbAim = hbCatchWant;
        }
        tq += pd(target) * catching;
    }
    handbrakeTorque = -hbTurn * tq;                                         // a la derecha (hbTurn +) = -Y
}

// Freno de mano: la delantera sigue su propio camino (el trail la arrastra) y el piloto la apunta slide_steer_in grados
// hacia la curva mientras la cola no llegó al ángulo buscado. Devuelve el manubrio pedido (sólo con handbrake > 0).
float Bike::HandbrakeSteer(float steerTarget, Vec3 linVel, float yawRate, Vec3 frontOffset, Vec3 fwdFlat, float maxSteerRad) const
{
    const Vec3 rightFlat(-fwdFlat.GetZ(), 0.0f, fwdFlat.GetX());
    // Sólo con la guiñada: el rolido mueve el eje de costado (inclinada ~55°, el bamboleo hacía oscilar el manubrio).
    const Vec3 frontAxleVel = linVel + Vec3(0.0f, yawRate, 0.0f).Cross(Vec3(frontOffset.GetX(), 0.0f, frontOffset.GetZ()));
    const float frontPath = std::atan2(frontAxleVel.Dot(rightFlat), std::max(frontAxleVel.Dot(fwdFlat), 0.5f));   // + = va a la derecha del rumbo
    const float rel = mu::Clamp(mu::Rad(P->slideSteerIn) + 0.35f * (hbAim - hbSlide), -maxSteerRad, 2.0f * mu::Rad(P->slideSteerIn));
    const float aligned = mu::Clamp(frontPath + rel * hbTurn, -maxSteerRad, maxSteerRad);
    return mu::Lerp(steerTarget, aligned, hbW * (1.0f - hbLow));
}

// Freno de mano con handbrake_lean: la inclinación que pide el balance mientras colea. El piloto sostiene la moto
// inclinada hacia la curva (hbLean, de UpdateHandbrake) en vez de dejarla enderezarse con slide_upright; si ya iba más
// acostada (despacio, la Trial), la deja como estaba. Al soltar la levanta (slide_upright, y vuelve a la normal en
// 2 × handbrake_catch) para salir traccionando: saliendo con gas desde 40-45° despacio, la trasera patinaba (trompo).
float Bike::HandbrakeLean(float leanTarget) const
{
    const float hold = hbTurn * std::max(hbTurn * leanTarget, hbTurn * hbLean);
    float lean = mu::Lerp(leanTarget, hold, hbW);
    if (hbCatching > 0.0f) lean *= 1.0f - P->slideUpright * hbCatching;
    return lean;
}

void Bike::PrePhysics(const BikeInput& rawInput, float dt, PhysicsWorld& world, const Terrain& terrain)
{
    // Golpes secos contra el piso (hard_landing_spin, ver PhysicsWorld): con el piloto arriba; sin él, la moto es rígida.
    world.SetHardLandingSpin(riderOnBike && !crashed ? P->hardLandingSpin : 1.0f);
    if (P->original031 >= 0.5f) {
        PrePhysics031(rawInput, dt, world, terrain);
        return;
    }
    UpdateWheelConfig();
    Body& b = *body;

    const Quat rot = b.GetRotation();
    const Vec3 com = b.GetCenterOfMassPosition();
    const Vec3 fwd = rot * Vec3::sAxisZ();
    const Vec3 up = rot * Vec3::sAxisY();
    const Vec3 right = fwd.Cross(up);
    const Vec3 left = -right;                    // eje de las ruedas
    const Vec3 linVel = b.GetLinearVelocity();
    const Vec3 angVel = b.GetAngularVelocity();
    const float invMass = b.GetMotionProperties()->GetInverseMass();
    const Mat44 invInertia = b.GetInverseInertia();

    // ------------------------------------------------------------------ actitud
    pitch = std::asin(mu::Clamp(fwd.GetY(), -1.0f, 1.0f));
    roll = std::atan2(-right.GetY(), up.GetY());
    speed = linVel.Length();
    forwardSpeed = linVel.Dot(fwd);
    const Vec3 fwdFlat = Vec3(fwd.GetX(), 0.0f, fwd.GetZ()).NormalizedOr(Vec3::sAxisZ());
    const float v = std::max(0.0f, linVel.Dot(fwdFlat));
    const bool wasInAir = !wheels[FRONT].grounded && !wheels[REAR].grounded;
    supported += (((!wasInAir || world.AnyContact()) ? 1.0f : 0.0f) - supported) * std::min(1.0f, dt * 6.0f);   // sólo para el dibujo
    // Qué tan rápido gira la dirección de marcha (rad/s, + = a la izquierda como la guiñada), filtrado.
    {
        const float velYaw = std::atan2(linVel.GetX(), linVel.GetZ());
        float turn = 0.0f;
        if (v > 1.0f && pathYawValid) {
            turn = velYaw - pathYawPrev;
            if (turn > mu::kPi) turn -= 2.0f * mu::kPi;
            if (turn < -mu::kPi) turn += 2.0f * mu::kPi;
            turn /= dt;
        }
        pathRate += (turn - pathRate) * std::min(1.0f, dt * 15.0f);
        pathYawPrev = velYaw;
        pathYawValid = v > 1.0f;
    }

    // Caída: demasiado inclinada o volcada un instante MIENTRAS toca el suelo (ruedas, chasis o
    // piloto, del paso anterior). En el aire el contador no cambia: un flip no suma nada, y una moto
    // que rebota dando tumbos tampoco se "recupera" entre golpes. Sólo baja derecha sobre las ruedas.
    // Parada sobre la trasera y la cola (un wheelie pasado) no es caída mientras siga derecha de costado,
    // no pase mucho de la vertical y ande: el piloto sigue arriba mientras raspa. Quieta así un rato, se
    // baja por atrás (inclinarse no la trae: con la moto vertical el peso se corre para arriba).
    const bool onTail = fwd.GetY() > std::sin(mu::Rad(60.0f)) && up.GetY() > -0.25f && std::fabs(right.GetY()) < 0.5f;
    tailStill = onTail && (!wasInAir || world.AnyContact()) && speed < 1.5f ? tailStill + dt : 0.0f;
    const bool standing = onTail && tailStill < 3.0f;
    const bool tipped = up.GetY() < std::cos(mu::Rad(P->crashAngleDeg)) && !standing;
    if (tipped && (!wasInAir || world.AnyContact())) crashTimer += dt;
    else if (!tipped && !wasInAir) crashTimer = std::max(0.0f, crashTimer - dt);
    // De cabeza contra el suelo es caída en el acto.
    if (crashTimer > 0.4f || (up.GetY() < 0.0f && !standing && world.AnyContact())) crashed = true;
    if (crashed) crashedTime += dt;

    BikeInput in = rawInput;
    if (crashed) {
        in.throttle = 0.0f;
        in.steer = 0.0f;
        in.lean = 0.0f;
    }

    const float riderThrottle = in.throttle;                     // lo que gira el piloto (antes de los recortes)
    if (P->handbrakeYaw > 0.0f)                   // freno de mano (antes de que el piloto module el pedal)
        UpdateHandbrake(in, linVel, fwdFlat, 1.0f - mu::Smoothstep(P->slideSpeedMax - 4.0f, P->slideSpeedMax, v), dt);

    // Derrape con el freno trasero: con la trasera trabada la cola da la vuelta alrededor de la
    // delantera (una rueda trabada no sostiene nada de costado). El piloto dosifica el pedal: afloja
    // cuando la cola llegó al ángulo que busca (más cuanto más dobla y más lento va), la cubierta
    // vuelve a rodar y agarrar, y aprieta de nuevo si se endereza. Así no termina en un trompo.
    // Es un derrape buscado sólo con el trasero solo (como en motocross: se suelta el de adelante);
    // frenando con los dos es una frenada normal.
    // (y doblando: derecho, el trasero solo es una frenada; la rueda se clava y la cola baila sola).
    slideIntent = in.rearBrake * (1.0f - mu::Smoothstep(0.1f, 0.4f, in.frontBrake)) * mu::Smoothstep(0.15f, 0.4f, std::fabs(in.steer));
    {
        const Vec3 flatVel(linVel.GetX(), 0.0f, linVel.GetZ());
        const Vec3 rightFlat = Vec3(right.GetX(), 0.0f, right.GetZ()).NormalizedOr(Vec3::sAxisX());
        const float along = flatVel.Dot(fwdFlat), side = flatVel.Dot(rightFlat);
        const float beta = flatVel.Length() > 1.5f ? std::atan2(side, std::max(along, 0.0f) + 0.2f) : 0.0f;   // + = va hacia la derecha del rumbo
        const float turnDir = std::fabs(in.steer) > 0.1f ? mu::Sign(in.steer) : (beta != 0.0f ? -mu::Sign(beta) : 1.0f);
        const float prevSlide = slideAngle;
        slideAngle = -beta * turnDir;                              // + = cola afuera, como corresponde a la curva
        slideRate += ((slideAngle - prevSlide) / dt - slideRate) * std::min(1.0f, dt * 20.0f);
        // Mira hacia dónde va el derrape (ángulo + lo que está girando): una trasera trabada gira rápido.
        const float heading = std::max(std::fabs(beta), slideAngle + std::max(0.0f, slideRate) * 0.35f);
        const float want = mu::Rad(mu::Lerp(P->slideAngleMin, P->slideAngleMax, std::fabs(in.steer))) * (1.0f - 0.6f * mu::Smoothstep(6.0f, 14.0f, v));
        in.rearBrake *= 1.0f - slideIntent * mu::Smoothstep(want - mu::Rad(10.0f), want + mu::Rad(5.0f), heading);
        // Arrastra la rueda en vez de trabarla del todo: patinando cerca del bloqueo afloja un poco el
        // pedal y la cubierta conserva algo de agarre de costado (la cola sale, pero no se escapa).
        if (wheels[REAR].grounded) in.rearBrake *= 1.0f - 0.5f * slideIntent * mu::Smoothstep(0.65f, 0.98f, -wheels[REAR].slipRatio);
        slideWant = want;
        slideTurn = turnDir;
        rearBrakeUsed = in.rearBrake;
    }

    // Limitador de wheelie predictivo: mira hacia dónde va el pitch (no sólo dónde está) y corta
    // gas antes de que la inercia haga imposible bajar la rueda. Deja hacer wheelies, no loops.
    const float pitchRate = angVel.Dot(right);                    // + = nariz arriba
    wheelieCut = 0.0f;
    wheelieKeep = 0.0f;
    if (P->wheelieEndDeg > P->wheelieStartDeg && wheels[REAR].grounded && !wheels[FRONT].grounded) {
        // Con el piloto neutro la rueda sube un poco y baja sola; tirado atrás se puede sostener
        // un wheelie más alto (el stick decide, no la marcha).
        // En 1ª la rueda sube con el torque al arrancar; en las marchas siguientes, con el piloto
        // neutro, apenas se despega (wheelieGearFade achica el ángulo permitido en cada marcha).
        const float leanBack = std::max(0.0f, -riderLean);
        const float gearScale = std::pow(P->wheelieGearFade, (float)(engine.gear - 1));
        const float start = P->wheelieStartDeg * gearScale + P->wheelieLeanBackDeg * leanBack;
        const float end = start + (P->wheelieEndDeg - P->wheelieStartDeg);
        const float predicted = pitch + std::max(0.0f, pitchRate) * std::max(0.0f, P->wheeliePrediction);
        wheelieCut = mu::Smoothstep(mu::Rad(start), mu::Rad(end), predicted);
        // Con wheelie_keep_throttle, en un wheelie de verdad (la trompa arriba de wheelie_keep_from_deg) el gas
        // no se corta: el limitador anula en cambio el cabeceo que mete la tracción (abajo, en la trasera) y
        // la moto acelera en una rueda. Con la trompa apenas despegada (un bache, la salida de una curva)
        // sigue cortando como antes: dejar el gas ahí hacía caer más a los bots en la favela y el circuito.
        wheelieKeep = mu::Clamp(P->wheelieKeepThrottle, 0.0f, 1.0f)
                    * mu::Smoothstep(mu::Rad(P->wheelieKeepFromDeg), mu::Rad(P->wheelieKeepFullDeg), pitch);
        in.throttle *= 1.0f - wheelieCut * (1.0f - wheelieKeep) * (1.0f - mu::Clamp(P->wheelieThrottleFloor, 0.0f, 0.6f));
    }

    // Grau (freestyle de favela): con la delantera en el aire el piloto sostiene la moto con el gas y
    // el pie en el freno trasero. Tirado atrás busca el punto de equilibrio: el centro de masa justo
    // encima del contacto de la trasera (sale de la geometría de este momento; el cuerpo atrás lo
    // corre). Y maneja la velocidad como en la calle: un poco antes del equilibrio, el gas que sostiene
    // la rueda acelera la moto; justo encima no hace falta casi nada, y pasado, el freno trasero que
    // baja la trompa la frena. W pide velocidad (suelto: grau lento, a paso de hombre). Con el piloto
    // neutro la rueda baja despacio. Sólo usa el gas y el freno: puede pasarse o quedarse corto.
    grauToBalance = 0.0f;
    grauClutch = 1.0f;
    if (P->grauAssist > 0.0f && riderOnBike && !crashed && wheels[REAR].grounded && !wheels[FRONT].grounded) {
        const float leanBack = std::max(0.0f, -riderLean);
        const float mass = 1.0f / invMass;
        const Vec3 d = com + comShiftWorld - wheels[REAR].contactPoint;
        const float along = d.Dot(fwdFlat), height = std::max(d.GetY(), 0.2f);
        grauToBalance = std::atan2(along, height);                             // + = le falta para el equilibrio
        // Sostener la rueda con el centro de masa `along` adelante del contacto pide una fuerza en la
        // cubierta de m·g·along/alto, que acelera la moto: la altura buscada sale de la aceleración
        // que hace falta para llegar a la velocidad pedida (más lo que frenan el roce y el aire).
        const float vWant = mu::Lerp(P->grauSpeedMin, P->grauSpeedMax, riderThrottle);
        // Para frenar pasa apenas el equilibrio (más atrás, la cola toca el piso y se da vuelta).
        const float aWant = mu::Clamp((vWant - v) * 1.2f, -1.0f, 1.5f);
        const float resist = kG * P->rollingResistance + 0.6f * P->dragArea * v * v / mass;
        const float balanceTarget = pitch + grauToBalance - std::max(std::atan((aWant + resist) / kG), -mu::Rad(6.0f));
        const float hold = mu::Smoothstep(0.15f, 0.6f, leanBack);
        const float target = mu::Lerp(mu::Rad(P->grauPitchNeutralDeg), balanceTarget, hold);
        // Fuerza en la cubierta trasera: la que equilibra el peso ahora más la corrección hacia la altura
        // buscada (PD alrededor del contacto, amortiguado 0.8).
        const float Ic = P->inertiaPitch + mass * d.LengthSq();
        const float wn = P->grauHoldHz;
        float force = mass * kG * along / height + (wn * wn * (target - pitch) - 1.6f * wn * pitchRate) * Ic / height;
        // Subiendo: el gas queda abierto hasta que la trompa trae el impulso justo para llegar sola a la
        // altura buscada contra el peso (hasta dónde sube si se cierra el gas ahora); ahí empieza a dosificar.
        const float gravityDecel = mass * kG * std::max(along, 0.05f) / Ic;
        const float apex = pitch + std::max(pitchRate, 0.0f) * std::max(pitchRate, 0.0f) / (2.0f * gravityDecel);
        if (hold > 0.5f && apex < target - mu::Rad(4.0f)) force = std::max(force, 1e5f);
        // Con el gas: cuánta fuerza da a fondo ahora (patinando el embrague, a las vueltas de arranque).
        const float r = wheels[REAR].radius;
        const float rpmFull = std::min(std::max(P->engine.clutchRPM, wheels[REAR].omega * engine.TotalRatio() * kRadPerSecToRPM), P->engine.revLimit);
        const float fullForce = engine.Torque(rpmFull) * P->engine.torqueScale * engine.TotalRatio() * P->engine.efficiency / r;
        const float thr = mu::Clamp(force / std::max(fullForce, 1.0f), 0.0f, 1.0f);
        const float brk = mu::Clamp(-force * r / std::max(P->rearBrakeTorque, 1.0f), 0.0f, P->grauBrake);
        // Tirado atrás el gas lo maneja el piloto del grau. Neutro, el gas es el del jugador (el motor sigue
        // arriba de vueltas y gritando) y el piloto patina el embrague lo justo para que la trompa no suba
        // más: a la rueda llega sólo la fuerza que la sostiene ahí (una moto así acelera lo que le deja el
        // wheelie, no más).
        const float neutralClutch = in.throttle > thr ? thr / std::max(in.throttle, 1e-3f) : 1.0f;
        grauClutch = mu::Lerp(1.0f, mu::Lerp(neutralClutch, 1.0f, hold), P->grauAssist);
        in.throttle = mu::Lerp(in.throttle, thr, hold * P->grauAssist);
        in.rearBrake = std::max(in.rearBrake, brk * P->grauAssist);
    }
    // Embrague soltado de golpe: tirado atrás, despacio y abriendo el gas rápido (de casi cerrado a casi
    // a fondo en menos de un cuarto de segundo: se puede con el teclado o el gatillo), el volante del
    // motor, que estaba arriba de vueltas, descarga su energía en la rueda: un pico de torque que
    // levanta la trompa sin ganar tanta velocidad.
    if (rawInput.throttle < 0.35f) {
        sinceThrottleLow = 0.0f;
        popArmed = true;
    } else {
        sinceThrottleLow += dt;
    }
    if (P->grauClutchPop > 0.0f && popArmed && rawInput.throttle > 0.8f) {
        popArmed = false;
        if (sinceThrottleLow < 0.25f && riderOnBike && !crashed && riderLean < -0.25f && v < 9.0f && wheels[REAR].grounded) clutchPop = P->grauPopTime;
    }

    // Control de tracción opcional: recorta gas cuando la trasera patina de más (deja algo de
    // patinaje para que haya derrape y tierra volando).
    if (tractionControl && wheels[REAR].grounded && P->tcSlip > 0.0f) {
        const float excess = wheels[REAR].slipRatio - P->tcSlip;
        const float target = 1.0f - mu::Clamp(excess * 2.5f, 0.0f, 0.85f);
        tcFactor = mu::MoveTowards(tcFactor, target, 12.0f * dt);
    } else {
        tcFactor = mu::MoveTowards(tcFactor, 1.0f, 12.0f * dt);
    }
    in.throttle *= tcFactor;

    // Marcha atrás. Con caja automática: parado, mantener el freno medio segundo pone la R y el freno
    // pasa a ser el gas hacia atrás; dar gas vuelve a primera. En manual la R se elige bajando desde
    // primera casi parado (Engine::ShiftDown) y el gas empuja hacia atrás.
    float engineThrottle = in.throttle;
    if (engine.autoShift && !crashed) {
        if (!engine.reverse) {
            const bool holding = std::fabs(forwardSpeed) < 0.6f && in.frontBrake > 0.5f && in.throttle < 0.05f && !wasInAir;
            reverseHold = holding ? reverseHold + dt : 0.0f;
            if (reverseHold > 0.5f) {
                engine.reverse = true;
                reverseHold = 0.0f;
            }
        } else if (in.throttle > 0.05f) {
            engine.reverse = false;
        }
        if (engine.reverse) {
            engineThrottle = in.frontBrake;
            in.frontBrake = in.rearBrake = 0.0f;
        }
    }

    throttle = engineThrottle;
    gripThrottle = engine.reverse ? engineThrottle : riderThrottle;
    frontBrake = in.frontBrake;
    rearBrake = in.rearBrake;

    // ------------------------------------------------------------------ caja de cambios
    if (in.shiftUp) engine.ShiftUp();
    if (in.shiftDown) engine.ShiftDown(wheels[REAR].omega);
    autoShiftCooldown = std::max(0.0f, autoShiftCooldown - dt);
    if (engine.autoShift && !engine.reverse && !crashed && !wasInAir && autoShiftCooldown <= 0.0f && engine.shiftTimer <= 0.0f) {
        // Umbrales de la curva base (una 450): se estiran con la curva de cada motor (engine_rpm_scale).
        const float rs = P->engine.rpmScale;
        if (engine.gear < 5 && !engine.clutchSlipping && engine.rpm > 9400.0f * rs && in.throttle > 0.3f) {
            engine.ShiftUp();
            autoShiftCooldown = 0.5f;
        } else if (engine.gear > 1 && engine.rpm < 4800.0f * rs) {
            float newRPM = wheels[REAR].omega * engine.Ratio(engine.gear - 1) * kRadPerSecToRPM;
            if (newRPM < 8500.0f * rs) {
                engine.ShiftDown(wheels[REAR].omega);
                autoShiftCooldown = 0.5f;
            }
        }
    }

    // ------------------------------------------------------------------ piloto (weight shift)
    // El piloto no es un cuerpo físico: sólo desplaza el centro de masa efectivo.
    riderLean = mu::MoveTowards(riderLean, in.lean, P->riderSpeed * dt);
    riderSide = mu::MoveTowards(riderSide, in.side, P->riderSpeed * dt);

    // Pata afuera: curva cerrada a velocidad media con la trasera en el piso. Sale la pierna del lado
    // para el que dobla (o para el que está inclinada, si ya soltó la dirección); para cambiar de
    // lado primero la vuelve a la estribera. Sentarse adelante le carga peso a la delantera.
    {
        const float turn = std::max(mu::Smoothstep(0.3f, 0.7f, std::fabs(in.steer)),
                                    mu::Smoothstep(mu::Rad(15.0f), mu::Rad(30.0f), std::fabs(roll)));
        const float speedOk = mu::Smoothstep(P->legOutSpeedMin, P->legOutSpeedMin + 1.0f, forwardSpeed) *
                              (1.0f - mu::Smoothstep(P->legOutSpeedMax - 3.0f, P->legOutSpeedMax, forwardSpeed));
        const bool planted = riderOnBike && !crashed && wheels[REAR].grounded;
        const float dir = std::fabs(in.steer) > 0.15f ? mu::Sign(in.steer) : mu::Sign(roll);
        float target = planted ? dir * turn * speedOk : 0.0f;
        if (target * legOut < 0.0f) target = 0.0f;
        legOut = mu::MoveTowards(legOut, target, (std::fabs(target) > std::fabs(legOut) ? 3.5f : 2.5f) * dt);
    }
    const float legAmount = std::fabs(legOut);
    // El cuerpo de costado corre el centro de masa (+X local = izquierda).
    comShiftWorld = rot * Vec3(-riderSide * P->riderSideShift, P->comHeight, P->comForward + riderLean * P->riderShift + legAmount * P->legOutComForward);
    const Vec3 forceCenter = com + comShiftWorld;

    // Aplicar una fuerza en p equivale a: fuerza en el COM + torque (p - COM) x F. Tomar el torque
    // respecto al COM EFECTIVO simula mover el centro de masa sin tocar el cuerpo de Jolt.
    // El componente de rolido se atenúa (tireRollCoupling) para que sea manejable; normal y
    // lateral se atenúan igual, así que el ángulo de equilibrio en curva no cambia.
    Vec3 totalForce = Vec3::sZero();
    Vec3 totalTorque = Vec3::sZero();
    auto applyAt = [&](Vec3 force, Vec3 point) {
        Vec3 torque = (point - forceCenter).Cross(force);
        torque -= fwd * (torque.Dot(fwd) * (1.0f - P->tireRollCoupling));
        totalForce += force;
        totalTorque += torque;
    };

    // ------------------------------------------------------------------ dirección
    // El input pide una curvatura. A baja velocidad se convierte en ángulo de manubrio; a alta
    // velocidad el manubrio sigue a la inclinación real (como el auto-direccionamiento de una
    // moto de verdad) y el input sólo decide cuánto inclinar.
    const float v2 = v * v;
    // maxLateralAccel es el límite en plano (~ μ·g). En un peralte a favor de la curva (θ) el suelo
    // también empuja hacia adentro y con el mismo grip se dobla más: g·(sen θ + μ cos θ)/(cos θ − μ sen θ).
    // En contra (off-camber) el límite baja. La inclinación máxima lo acota igual.
    const Vec3 groundN = terrain.BankNormal(com.GetX(), com.GetZ());
    const Vec3 rightFlat = Vec3(right.GetX(), 0.0f, right.GetZ()).NormalizedOr(Vec3::sAxisX());
    const float sinBank = mu::Clamp(groundN.Dot(rightFlat) * (in.steer >= 0.0f ? 1.0f : -1.0f) * P->bankTurnGain, -0.5f, 0.5f);
    const float cosBank = std::sqrt(1.0f - sinBank * sinBank);
    float muLat = P->maxLateralAccel / kG * (1.0f + P->legOutGrip * legAmount);
    // Con poco agarre (lisas en la tierra o el pasto) el piloto no pide la curva del asfalto, sino la que dan las cubiertas
    // ahí (suelo × cubierta × agarre lateral, en g): sin esto la de carreras se acostaba 56° en la tierra y doblaba a 0.4 g
    // deslizando de costado (ver FISICA.md, "Velocidad de giro"). Con más agarre que max_lateral_accel no cambia nada.
    if (P->leanSurfaceGrip > 0.0f) {
        float grip = 0.0f, onGround = 0.0f;
        for (const Wheel& w : wheels)
            if (w.grounded) {
                grip += w.gripFactor;
                onGround += 1.0f;
            }
        if (onGround > 0.0f) leanGrip += (std::min(1.0f, grip / onGround / (P->maxLateralAccel / kG)) - leanGrip) * std::min(1.0f, 4.0f * dt);   // ~0.25 s
        muLat *= 1.0f - P->leanSurfaceGrip * (1.0f - leanGrip);
    }
    const float maxAccel = kG * (sinBank + muLat * cosBank) / std::max(cosBank - muLat * sinBank, 0.3f);
    const float kMax = std::min(1.0f / P->minTurnRadius, maxAccel / std::max(v2, 0.01f));
    // Sin tocar el manubrio, el cuerpo de costado dobla (más despacio): la moto sigue al peso. Con el
    // manubrio doblando manda el manubrio y el cuerpo sólo cambia cuánto se acuesta la moto (abajo).
    const float bodySteer = P->riderSideSteer * riderSide * (1.0f - mu::Smoothstep(0.1f, 0.3f, std::fabs(in.steer)));
    const float kCmd = mu::Clamp(in.steer + bodySteer, -1.0f, 1.0f) * kMax;
    const float maxLean = mu::Rad(P->maxLeanDeg);
    leanTarget = mu::Clamp(std::atan(v2 * kCmd / kG), -maxLean, maxLean);
    // Lo que se inclina es el conjunto moto + piloto: con el cuerpo hacia adentro de la curva la moto va
    // más derecha para la misma curva; con el cuerpo afuera (el peso en la estribera de afuera), más
    // acostada. Derecho y con el cuerpo de un lado, la moto se inclina un poco para el otro (equilibrio).
    const float sideLean = std::atan(P->riderSideShift / 0.65f);  // cuánto inclina el conjunto el cuerpo de costado
    leanTarget -= riderSide * sideLean;
    // Frenar fuerte inclinado endereza la moto (geometría de dirección): se abre de la curva en
    // vez de pivotar sobre la delantera por el contacto desplazado del centro de masa.
    leanTarget *= 1.0f - P->brakeStandUp * in.frontBrake * mu::Smoothstep(3.0f, 8.0f, v);
    // Con la trasera derrapando (trabada o patinando) la cubierta empuja menos de costado: el piloto
    // lleva la moto más derecha y la hace girar con el derrape, no inclinándola.
    const Wheel& rearPrev = wheels[REAR];
    const float rearSliding = rearPrev.grounded ? mu::Smoothstep(0.4f, 0.9f, std::fabs(rearPrev.slipRatio)) : 0.0f;
    leanTarget *= 1.0f - P->slideUpright * rearSliding;
    // Freno de mano con handbrake_lean: el piloto sostiene la moto inclinada hacia la curva mientras colea (nunca
    // más derecha que sin la clave).
    if (P->handbrakeLean > 0.0f && (hbW > 0.0f || hbCatching > 0.0f)) leanTarget = HandbrakeLean(leanTarget);
    // La curva la da la inclinación del conjunto (moto + piloto), no la de la moto sola.
    const float kLean = kG * std::tan(mu::Clamp(roll + riderSide * sideLean, -1.2f, 1.2f)) / std::max(v2, 1.0f);
    const float kSteer = mu::Lerp(kCmd, kLean, mu::Smoothstep(P->leanSteerSpeedLow, P->leanSteerSpeedHigh, v));
    const float wheelbase = (wheels[FRONT].AxleLocal() - wheels[REAR].AxleLocal()).Length();
    const float maxSteer = mu::Lerp(mu::Rad(P->maxSteerDeg), mu::Rad(P->maxSteerHighSpeedDeg), mu::Smoothstep(3.0f, 20.0f, v));
    // En el aire el manubrio vuelve al centro: aterrizar con la rueda girada es un trompo seguro.
    float steerTarget = wasInAir ? 0.0f : mu::Clamp(std::atan(wheelbase * kSteer), -maxSteer, maxSteer);
    // Derrapando la trasera (trabada o patinando), la delantera se alinea sola con hacia dónde se
    // mueve (el trail de la horquilla la arrastra) y el piloto sólo la apunta un poco hacia la curva:
    // así empuja lo justo y la cola gira alrededor de ella en vez de terminar en un trompo.
    const float slideSteer = wasInAir ? 0.0f : slideIntent * (1.0f - mu::Smoothstep(P->slideSpeedMax - 4.0f, P->slideSpeedMax, v));
    if (slideSteer > 0.0f && v > 1.0f) {
        const Vec3 frontAxle = com + rot * wheels[FRONT].AxleLocal();
        const Vec3 vf = linVel + angVel.Cross(frontAxle - com);
        const float frontPath = std::atan2(vf.Dot(rightFlat), std::max(vf.Dot(fwdFlat), 0.5f));   // + = va a la derecha del rumbo
        // Respecto de su camino: un poco hacia la curva mientras la cola no llegó al ángulo buscado,
        // contravolante si se está abriendo rápido.
        const float rel = mu::Clamp(0.35f * (slideWant - slideAngle) - 0.3f * slideRate, -mu::Rad(P->maxSteerDeg), mu::Rad(P->slideSteerIn));
        const float aligned = mu::Clamp(frontPath + rel * slideTurn, -mu::Rad(P->maxSteerDeg), mu::Rad(P->maxSteerDeg));
        steerTarget = mu::Lerp(steerTarget, aligned, slideSteer);
    }
    if (hbW > 0.0f && !wasInAir)                 // freno de mano (más rápido que el derrape lento de arriba)
        steerTarget = HandbrakeSteer(steerTarget, linVel, angVel.GetY(), rot * wheels[FRONT].AxleLocal(), fwdFlat, mu::Rad(P->maxSteerDeg));
    // Frenando fuerte y derecho (brake_align): la rueda delantera es libre y el trail la hace seguir su
    // propio camino, así que no empuja de costado. Con el manubrio "fijo", en cuanto la moto giraba un poco la
    // delantera empujaba de costado y la hacía girar más (la trasera descargada no la sostiene): la moto se
    // cruzaba y se acostaba sola (inestable como un auto frenando con las de atrás trabadas).
    float brakeFree = (P->brakeAlign > 0.0f && !wasInAir && v > 5.0f)
                          ? mu::Smoothstep(P->brakeAlignFrom, P->brakeAlignFull, in.frontBrake) * (1.0f - mu::Smoothstep(0.1f, 0.3f, std::fabs(in.steer))) *
                                (1.0f - mu::Smoothstep(0.05f, 0.3f, slideIntent))
                          : 0.0f;
    if (P->brakeAlignFree != 1.0f) brakeFree *= P->brakeAlignFree;
    if (brakeFree > 0.0f) {
        const Vec3 frontAxle = com + rot * wheels[FRONT].AxleLocal();
        const Vec3 vf = linVel + angVel.Cross(frontAxle - com);
        const float frontPath = std::atan2(vf.Dot(rightFlat), std::max(vf.Dot(fwdFlat), 0.5f));   // + = va a la derecha del rumbo
        steerTarget = mu::Lerp(steerTarget, mu::Clamp(frontPath + steerTarget, -maxSteer, maxSteer), brakeFree);
    }
    float steerRate = P->steerRate + 6.0f * slideSteer + 6.0f * brakeFree;
    if (hbW > 0.0f) steerRate += 6.0f * hbW * (1.0f - slideSteer);
    steerBase = mu::MoveTowards(steerBase, steerTarget, steerRate * dt);

    // Autoalineación (trail): cuando la trasera se cruza, la rueda delantera tiende a apuntar hacia
    // donde va la moto, o sea contravolante automático. Es lo que hace que una moto derrape de
    // costado sin hacer un trompo como un auto. Usa la deriva de la rueda trasera del paso anterior.
    const Wheel& rw = wheels[REAR];
    float casterTarget = 0.0f;
    if (rw.grounded && !wasInAir) {
        // Sólo la parte de la deriva que excede la de una curva normal (zona muerta): así no le
        // quita manubrio a los giros comunes, pero atrapa la cola cuando de verdad se cruza.
        const float rearSlip = std::atan2(-rw.latVel, std::max(std::fabs(rw.longVel), 2.0f));   // + = cola hacia la izquierda
        const float excess = mu::Sign(rearSlip) * std::max(0.0f, std::fabs(rearSlip) - mu::Rad(P->casterDeadzoneDeg));
        casterTarget = mu::Clamp(excess * P->casterAlign, -mu::Rad(30.0f), mu::Rad(30.0f)) * mu::Smoothstep(1.5f, 4.0f, v);
        // Derrape con el freno trasero a poca velocidad: el piloto sostiene el manubrio hacia la curva
        // (la cola da la vuelta alrededor de la delantera) en vez de dejar que se enderece solo.
        casterTarget *= 1.0f - std::max(slideSteer, P->slideHoldBars * slideIntent * (1.0f - mu::Smoothstep(P->slideSpeedMax - 4.0f, P->slideSpeedMax, v)));
        casterTarget *= 1.0f - brakeFree;                    // frenando derecho ya sigue su camino (arriba)
        if (hbW > 0.0f) casterTarget *= 1.0f - hbW * (1.0f - hbLow);   // freno de mano: deja salir la cola
    }
    steerCaster = mu::MoveTowards(steerCaster, casterTarget, 10.0f * dt);
    const float steerStop = mu::Rad(std::max(35.0f, P->maxSteerDeg));   // topes de dirección
    steerAngle = mu::Clamp(steerBase + steerCaster, -steerStop, steerStop);

    // ------------------------------------------------------------------ motor
    float reflectedInertia = 0.0f;
    float driveTorque = engine.Update(wheels[REAR].omega, engineThrottle, dt, reflectedInertia);
    if (grauClutch < 1.0f) {                     // embrague patinando a propósito (grau, ver arriba)
        driveTorque *= grauClutch;
        reflectedInertia *= grauClutch;
    }
    if (clutchPop > 0.0f) {
        driveTorque *= 1.0f + P->grauClutchPop * (clutchPop / std::max(P->grauPopTime, 0.01f));
        clutchPop = std::max(0.0f, clutchPop - dt);
    }

    // ------------------------------------------------------------------ ruedas
    const JPH::IgnoreSingleBodyFilter ignoreSelf(b.GetID());
    const StaticOnlyFilter staticOnly;
    for (int i = 0; i < 2; ++i) {
        Wheel& w = wheels[i];
        const SuspensionParams& sp = i == FRONT ? P->front : P->rear;
        const TireParams& tp = i == FRONT ? P->frontTire : P->rearTire;
        const float I = tp.inertia + (i == REAR ? reflectedInertia : 0.0f);
        const float omegaBefore = w.omega;

        // Raycast a lo largo del eje de suspensión, desde un poco por encima del tope.
        const Vec3 mount = com + rot * w.mountLocal;
        const Vec3 axis = rot * w.axisLocal;
        const float above = 0.4f;
        // Con la moto muy parada (wheelie pasado) el eje queda casi acostado: si la moto venía tocando el
        // piso (una rueda o la cola), el rayo va más lejos para seguir encontrándolo debajo de la cubierta
        // (si no, la rueda "se pierde", la cubierta se mete en el suelo y al reencontrarlo la escupe).
        const float axisDown = std::max(-axis.GetY(), 0.08f);
        const bool upright = pitch > mu::Rad(40.0f) && (!wasInAir || world.AnyContact());   // parada sobre la trasera
        const float reach = upright ? std::max(1.0f, 0.7f / axisDown) : 1.0f;
        const float minAxisCos = upright && pitch > mu::Rad(60.0f) ? kMinAxisCosUpright : 0.25f;
        const float rayLength = above + w.travel + w.radius * 1.6f + (reach - 1.0f) * (w.travel + w.radius * 1.6f);
        const JPH::RRayCast ray(mount - axis * above, axis * rayLength);
        JPH::RayCastResult hit;

        bool groundFound = false;
        bool groundIsObject = false;                                // apoya en un objeto (caja, escalón): duro
        float groundExtension = 1e6f;                               // extensión a la que la rueda toca el suelo
        Vec3 n = Vec3::sAxisY();
        Vec3 contactPoint = Vec3::sZero();
        if (world.Query().CastRay(ray, hit, JPH::BroadPhaseLayerFilter(), staticOnly, ignoreSelf)) {
            Vec3 groundPoint = ray.GetPointOnRay(hit.mFraction);
            const bool onTerrain = hit.mBodyID == terrain.BodyID();
            if (onTerrain) {
                n = terrain.RideNormal(groundPoint.GetX(), groundPoint.GetZ());
            } else {
                JPH::BodyLockRead lock(world.System().GetBodyLockInterfaceNoLock(), hit.mBodyID);
                if (lock.Succeeded()) n = lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, groundPoint);
            }

            // Rueda = disco con perfil redondeado. Se busca la extensión e tal que la rueda quede
            // tangente al plano del suelo (groundPoint, n). Vale con rake e inclinación.
            auto solveExtension = [&](Vec3 gp, Vec3 gn) {
                const float na = gn.Dot(left);
                const float effR = (w.radius - tp.roundness) * std::sqrt(std::max(0.0f, 1.0f - na * na)) + tp.roundness;
                return (effR - (mount - gp).Dot(gn)) / axis.Dot(gn);
            };
            if (onTerrain && -axis.Dot(n) > minAxisCos) {
                float e = solveExtension(groundPoint, n);
                // Refinamiento: re-evaluar el suelo justo debajo del punto de contacto estimado.
                Vec3 below = mount + axis * e - n * w.radius;
                groundPoint = Vec3(below.GetX(), terrain.RideHeight(below.GetX(), below.GetZ()), below.GetZ());
                n = terrain.RideNormal(groundPoint.GetX(), groundPoint.GetZ());
                if (-axis.Dot(n) > minAxisCos) e = solveExtension(groundPoint, n);
                // Salvaguardas: si el tope de la rueda ya está bajo el suelo (moto volcada o
                // clavada) la suspensión no aplica fuerza, y el sobre-recorrido se acota: de ahí
                // en más se encarga la colisión del chasis. Sin esto, una moto de cabeza puede
                // generar una compresión de metros y una fuerza absurda.
                const bool mountAboveGround = (mount - groundPoint).Dot(n) > 0.0f;
                if (-axis.Dot(n) > minAxisCos && mountAboveGround) {
                    groundFound = true;
                    groundExtension = e;
                    const Vec3 axle = mount + axis * e;
                    const Vec3 inPlaneDown = (-n + left * n.Dot(left)).NormalizedOr(-up);
                    contactPoint = axle + inPlaneDown * (w.radius - tp.roundness) - n * tp.roundness;
                }
            }

            // Objetos (escalones, cordones, cajas, lajes): el rayo va por el eje de la suspensión, que está
            // inclinado (adelante la horquilla, atrás el basculante), así que ve otra cosa que lo que hay
            // debajo de la cubierta: bajando una escalera el de atrás pegaba en la contrahuella del escalón
            // que acababa de dejar (la cubierta "tangente" a una pared salía comprimida de más y disparaba la
            // moto) y subiendo el de adelante veía la pedada del escalón como si ya estuviera debajo de la
            // rueda. Sobre objetos la cubierta tantea su propio perfil: rayos verticales repartidos bajo el
            // círculo de la rueda. El piso bajo el eje es el apoyo plano; donde dos vecinos cambian de altura
            // hay un canto (se lo ubica por bisección) y la cubierta rueda alrededor de él. Vale lo primero
            // que toca al extenderse. Si ningún rayo toca un objeto (sólo terreno) no cambia nada.
            const Vec3 fwdW = left.Cross(Vec3::sAxisY());
            if (std::fabs(left.GetY()) < 0.7f && fwdW.LengthSq() > 0.25f) {
                const Vec3 fw = fwdW.Normalized();
                const Vec3 axleNow = mount + axis * mu::Clamp(w.extension, 0.0f, w.travel);
                const float reachDown = 0.15f + w.radius + w.travel + 0.3f;
                struct Probe {
                    bool hit = false, object = false;
                    Vec3 p = Vec3::sZero(), n = Vec3::sAxisY();
                };
                auto probeAt = [&](float d) {
                    Probe pr;
                    const JPH::RRayCast pray(axleNow + fw * d + Vec3(0.0f, 0.15f, 0.0f), Vec3(0.0f, -reachDown, 0.0f));
                    JPH::RayCastResult ph;
                    if (world.Query().CastRay(pray, ph, JPH::BroadPhaseLayerFilter(), staticOnly, ignoreSelf) && ph.mFraction > 0.0f) {
                        pr.hit = true;
                        pr.p = pray.GetPointOnRay(ph.mFraction);
                        pr.object = ph.mBodyID != terrain.BodyID();
                        if (pr.object) {
                            JPH::BodyLockRead lk(world.System().GetBodyLockInterfaceNoLock(), ph.mBodyID);
                            if (lk.Succeeded()) pr.n = lk.GetBody().GetWorldSpaceSurfaceNormal(ph.mSubShapeID2, pr.p);
                        }
                    }
                    return pr;
                };
                constexpr int kProbes = 7;
                Probe probes[kProbes];
                float offs[kProbes];
                bool anyObject = false;
                for (int k = 0; k < kProbes; ++k) {
                    offs[k] = (float)(k - kProbes / 2) * 0.28f * w.radius;
                    probes[k] = probeAt(offs[k]);
                    anyObject = anyObject || (probes[k].hit && probes[k].object);
                }
                if (anyObject || !onTerrain) {
                    // La normal del terreno de cada sonda se calcula sólo acá (con el surco suavizado, es cara).
                    for (Probe& pr : probes)
                        if (pr.hit && !pr.object) pr.n = terrain.RideNormal(pr.p.GetX(), pr.p.GetZ());
                    float bestE = groundFound ? groundExtension : 1e6f;
                    Vec3 bestN = n, bestP = contactPoint;
                    bool bestObject = false;
                    auto consider = [&](float e, Vec3 gn, Vec3 cp, Vec3 surf, bool object) {
                        if (e < bestE && -axis.Dot(gn) > minAxisCos && (mount - surf).Dot(gn) > 0.0f) {
                            bestE = e;
                            bestN = gn;
                            bestP = cp;
                            bestObject = object;
                        }
                    };
                    // Apoyo plano: el piso justo debajo del eje.
                    const Probe& mid = probes[kProbes / 2];
                    if (mid.hit && (mid.object || !groundFound) && mid.n.GetY() >= kSteepFaceY) {
                        const float e = solveExtension(mid.p, mid.n);
                        const Vec3 inPlaneDown = (-mid.n + left * mid.n.Dot(left)).NormalizedOr(-up);
                        consider(e, mid.n, mount + axis * e + inPlaneDown * (w.radius - tp.roundness) - mid.n * tp.roundness, mid.p, mid.object);
                    }
                    // Cantos: entre dos sondas que cambian de altura (un rayo que no pega cuenta como un pozo).
                    for (int k = 0; k + 1 < kProbes; ++k) {
                        const Probe& a = probes[k];
                        const Probe& c = probes[k + 1];
                        if (!a.hit && !c.hit) continue;
                        const float ya = a.hit ? a.p.GetY() : -1e6f, yc = c.hit ? c.p.GetY() : -1e6f;
                        if (std::fabs(ya - yc) < 0.03f) continue;
                        if (a.hit && c.hit) {                    // misma pendiente (una rampa): no es un canto
                            auto planeY = [](const Probe& from, Vec3 at) {
                                return from.p.GetY() - (from.n.GetX() * (at.GetX() - from.p.GetX()) + from.n.GetZ() * (at.GetZ() - from.p.GetZ())) /
                                                           std::max(from.n.GetY(), 0.2f);
                            };
                            if (std::fabs(planeY(a, c.p) - yc) < 0.03f && std::fabs(planeY(c, a.p) - ya) < 0.03f) continue;
                        }
                        const bool highA = ya > yc;
                        const Probe& hiP = highA ? a : c;
                        if (!(hiP.object) || hiP.n.GetY() < kSteepFaceY) continue;   // canto de un objeto
                        float dHi = highA ? offs[k] : offs[k + 1], dLo = highA ? offs[k + 1] : offs[k];
                        const float yHi = hiP.p.GetY(), yLo = highA ? yc : ya;
                        for (int it = 0; it < 5; ++it) {
                            const float dm = 0.5f * (dHi + dLo);
                            const Probe pm = probeAt(dm);
                            if (pm.hit && pm.p.GetY() > 0.5f * (yHi + std::max(yLo, yHi - 1.0f))) dHi = dm;
                            else dLo = dm;
                        }
                        const Vec3 edgeP = axleNow + fw * dHi + Vec3(0.0f, yHi - (axleNow + fw * dHi).GetY(), 0.0f);
                        // La extensión con la que el círculo de la rueda (en su plano) pasa por el canto: la
                        // primera vez que lo toca al extenderse.
                        const Vec3 dv = edgeP - mount, dP = dv - left * dv.Dot(left), ax = axis - left * axis.Dot(left);
                        const float qa = ax.LengthSq(), qb = ax.Dot(dP), qc = dP.LengthSq() - w.radius * w.radius;
                        const float disc = qb * qb - qa * qc;
                        if (disc < 0.0f || qa < 1e-4f) continue;
                        const float e = (qb - std::sqrt(disc)) / qa;
                        const Vec3 fromEdge = mount + axis * e - edgeP;
                        const Vec3 nE = (fromEdge - left * fromEdge.Dot(left)).NormalizedOr(Vec3::sAxisY());
                        if (nE.GetY() > 0.2f) consider(e, nE, edgeP, edgeP, true);    // canto por debajo del eje
                    }
                    // Sobre un objeto el piso no puede aparecer de golpe mucho más arriba (entrando de costado a
                    // un escalón, la rueda "subía" 24 cm en un paso y el neumático empujaba con 60 000 N): sube como
                    // mucho lo que permite rodar trepando. Viniendo del aire no hay límite (aterriza).
                    if (bestE < 1e5f && w.groundExtension < 1e5f) {
                        const float maxRise = (0.02f + 1.5f * v * dt) / std::max(-axis.Dot(bestN), 0.3f);
                        bestE = std::max(bestE, w.groundExtension - maxRise);
                    }
                    if (bestE < 1e5f && (!groundFound || bestE < groundExtension)) {
                        groundFound = true;
                        groundExtension = bestE;
                        n = bestN;
                        contactPoint = bestP;
                        groundIsObject = bestObject;
                    }
                }
            }
        }

        // -------------------------------------------------------------- rueda con masa (eje de suspensión)
        // La rueda tiene masa propia y el neumático es un resorte: la suspensión trabaja entre chasis
        // y rueda, el neumático entre rueda y suelo. Así un bache seco mueve primero la rueda (liviana)
        // y el chasis recibe un golpe filtrado, en vez de toda la velocidad del terreno de una vez.
        // Se integra en sub-pasos porque el neumático es mucho más rígido que la suspensión.
        const float cosAxis = groundFound ? -axis.Dot(n) : 1.0f;
        const float mountAxialVel = b.GetPointVelocity(mount).Dot(axis);
        // El eje gira con el chasis: la velocidad de la rueda proyectada sobre él cambia aunque no
        // actúe ninguna fuerza, v·(ω × eje). Sin este término, rotar rápido (flips) comprime la
        // suspensión de mentira y su reacción empuja el chasis: sube en un backflip, baja en un frontflip.
        const float axisTurn = b.GetPointVelocity(mount + axis * w.extension).Dot(angVel.Cross(axis));
        const float prevGround = w.groundExtension < 1e5f ? w.groundExtension : groundExtension;
        // Seguro: si el suelo aparece con la cubierta ya muy metida (más de lo que se mete en un paso aun
        // en el peor aterrizaje), la rueda sube por su eje hasta apoyar en vez de que el neumático, un
        // resorte muy duro, dispare la moto.
        if (upright && groundFound && w.groundExtension >= 1e5f && (w.extension - groundExtension) * cosAxis > 0.12f) {
            w.extension = std::max(groundExtension + 0.02f / cosAxis, -0.12f);
            w.axialVel = mountAxialVel;
            w.tireDelta = 0.0f;
        }
        constexpr int kSub = 8;
        const float h = dt / kSub;
        float springSum = 0.0f, tireSum = 0.0f;
        for (int k = 0; k < kSub; ++k) {
            const float suspF = UpdateSuspension(sp, w.susp, w.travel - w.extension, h, w.tireDelta > 0.0f);
            float tireF = 0.0f;
            float delta = -1.0f;
            if (groundFound) {
                const float g = mu::Lerp(prevGround, groundExtension, (float)(k + 1) / kSub);
                delta = (w.extension - g) * cosAxis;                // compresión del neumático
                if (delta > 0.0f) {
                    const float rate = (delta - w.tireDelta) / h;                 // + = comprimiéndose
                    tireF = std::max(0.0f, tp.stiffness * delta + (rate > 0.0f ? tp.damping : tp.reboundDamping) * rate);
                }
            }
            w.tireDelta = std::max(delta, 0.0f);                    // sin contacto no hay memoria
            const float accel = (suspF - tireF * cosAxis) / tp.unsprungMass - kG * axis.GetY() + axisTurn;
            w.axialVel += accel * h;
            w.extension += (w.axialVel - mountAxialVel) * h;
            if (w.extension > w.travel) {                           // tope de extensión
                w.extension = w.travel;
                w.axialVel = mountAxialVel;
            } else if (w.extension < -0.12f) {                      // más allá del tope: manda el chasis
                w.extension = -0.12f;
                w.axialVel = mountAxialVel;
            }
            springSum += suspF;
            tireSum += tireF;
        }
        const float loggedPrevGround = w.groundExtension;
        w.groundExtension = groundFound ? groundExtension : 1e6f;
        const float springForce = springSum / kSub;
        const float tireNormal = tireSum / kSub;
        if (wheelLog) {
            const Vec3 hp = groundFound ? contactPoint : Vec3::sZero();
            std::printf("rueda %c hit=%d %s n=(%.2f %.2f %.2f) p=(%.2f %.2f %.2f) e=%.3f prev=%.3f ext=%.3f comp=%.2f susp=%.0f cub=%.0f pitch=%.1f\n",
                        i == FRONT ? 'D' : 'T', groundFound ? 1 : 0, !groundFound ? "-" : (hit.mBodyID == terrain.BodyID() ? "terreno" : "cuerpo"),
                        n.GetX(), n.GetY(), n.GetZ(), hp.GetX(), hp.GetY(), hp.GetZ(), groundFound ? groundExtension : 0.0f,
                        loggedPrevGround < 1e5f ? loggedPrevGround : 0.0f, w.extension, 1.0f - w.extension / w.travel, springForce, tireNormal,
                        mu::Deg(pitch));
        }
        const bool contact = groundFound && tireNormal > 1.0f;

        // -------------------------------------------------------------- motor y frenos sobre la rueda
        // Orden: torque de motor -> frenos/resistencia -> neumático. Si el freno alcanza para
        // detener la rueda, queda "retenida": para el neumático se comporta como infinitamente
        // pesada mientras la fuerza del suelo no supere la capacidad de freno sobrante.
        float omega = w.omega + (i == REAR ? driveTorque / I * dt : 0.0f);
        float brake = i == FRONT ? in.frontBrake * P->frontBrakeTorque : in.rearBrake * P->rearBrakeTorque;
        if (i == FRONT && P->absSlip > 0.0f && contact && w.slipRatio < -P->absSlip)
            brake *= mu::Clamp(1.0f - (-w.slipRatio - P->absSlip) * 4.0f, 0.25f, 1.0f);   // ABS invisible
        if (i == FRONT && P->absSlip > 0.0f && contact) {
            // Anti-endo (como el "rear lift mitigation" de un ABS moderno): permite stoppies
            // chicos pero afloja el freno antes de pasar por encima del manubrio.
            const float predicted = pitch + std::min(0.0f, pitchRate) * 0.3f;
            brake *= 1.0f - 0.85f * mu::Smoothstep(mu::Rad(-12.0f), mu::Rad(-28.0f), predicted);
            // Si la trasera se queda sin carga, aflojar: con sólo la delantera apoyada, frenar es
            // direccionalmente inestable (la cola quiere pasar adelante). Con la moto derecha se
            // afloja poco (los stoppies siguen siendo posibles); inclinada, bastante más.
            const float rearLoad = wheels[REAR].grounded ? wheels[REAR].normalForce : 0.0f;
            const float turning = std::max(mu::Smoothstep(mu::Rad(2.0f), mu::Rad(8.0f), std::fabs(roll)),
                                           mu::Smoothstep(0.05f, 0.25f, std::fabs(in.steer)));
            const float mitigation = mu::Lerp(P->rearLiftMitigation, 0.8f, turning);
            brake *= 1.0f - mitigation * (1.0f - mu::Smoothstep(0.0f, P->rearLiftLoad, rearLoad));
            // Mientras la moto cambia mucho de inclinación (se endereza al soltar la curva, o se tira a una), el
            // piloto afloja: primero la levanta y después frena a fondo. La inclinación la cambia un torque en el
            // centro de masa y las cubiertas tienen que acompañar de costado: frenando fuerte, la trasera casi sin
            // peso no acompaña, la cola salía ~16° y la moto quedaba clavada inclinada, yéndose para el otro lado.
            if (P->brakeTransitionRelease > 0.0f)
                brake *= 1.0f - P->brakeTransitionRelease * mu::Smoothstep(mu::Rad(4.0f), mu::Rad(20.0f), std::fabs(leanTarget - roll));
        }
        const float resist = brake + (contact ? P->rollingResistance * w.normalForce * w.radius : 0.0f) + 0.4f;
        const bool held = std::fabs(omega) <= resist / I * dt;
        const float spareTorque = held ? resist - std::fabs(omega) * I / dt : 0.0f;
        omega = held ? 0.0f : omega - mu::Sign(omega) * resist / I * dt;

        if (contact) {
            // -------------------------------------------------------------- suspensión
            // Al chasis llega la fuerza de suspensión por el eje, más la parte de la fuerza del
            // neumático perpendicular al eje (la toman rígidas la horquilla / el basculante).
            const float suspensionForce = tireNormal;               // carga del neumático (para el grip)
            w.grounded = true;
            w.onTerrain = !groundIsObject;
            w.onObject = groundIsObject;
            w.contactPoint = contactPoint;
            w.contactNormal = n;
            w.normalForce = tireNormal;
            const Vec3 suspF = n * tireNormal + (-axis) * (springForce - tireNormal * cosAxis);

            // -------------------------------------------------------------- neumático
            Vec3 fwdG = (fwd - n * fwd.Dot(n)).NormalizedOr(fwdFlat);
            if (i == FRONT) fwdG = Quat::sRotation(n, -steerAngle) * fwdG;
            const Vec3 latG = n.Cross(fwdG);                        // hacia la izquierda
            Vec3 pointVel = b.GetPointVelocity(contactPoint);
            // El barrido del contacto por el rolido alrededor del centro de masa (lean_sweep_comp, ver Bike.h), sólo
            // mientras se endereza: tirándose a la curva ese empuje hacia adentro ayuda a doblar (frenando también).
            if (P->leanSweepComp > 0.0f && angVel.Dot(fwd) * roll < 0.0f)
                pointVel -= (fwd * (angVel.Dot(fwd) * P->leanSweepComp)).Cross(contactPoint - com);
            w.longVel = pointVel.Dot(fwdG);
            w.latVel = pointVel.Dot(latG);

            // Masa efectiva del cuerpo en el contacto a lo largo de una dirección.
            const Vec3 r = contactPoint - com;
            auto invMassAlong = [&](Vec3 d) {
                Vec3 rxd = r.Cross(d);
                return invMass + rxd.Dot(invInertia.Multiply3x3(rxd));
            };
            const float kLong = invMassAlong(fwdG);
            const float kLat = invMassAlong(latG);
            const float R = w.radius;
            const float wheelInvMass = held ? 0.0f : R * R / I;

            // Longitudinal: la fuerza que anularía el patinaje en este paso (tipo constraint:
            // estable a cualquier velocidad). Casi rígida hasta que el grip la limita.
            const float slipVel = omega * R - w.longVel;
            const float longNeeded = slipVel / ((kLong + wheelInvMass) * dt);

            // Lateral: rigidez de deriva realista (fuerza ~ ángulo de deriva * carga), con tope
            // en la fuerza que anularía la velocidad lateral (evita oscilar a baja velocidad).
            const float slipAngle = std::atan2(w.latVel, std::max(std::fabs(w.longVel), 2.0f));
            const float latNeeded = -w.latVel / (kLat * dt) * P->latStiffness;
            const float latLinear = -P->corneringStiffness * suspensionForce * slipAngle;
            const float latDesired = std::fabs(latLinear) < std::fabs(latNeeded) ? latLinear : latNeeded;

            TireSolveInput ti;
            ti.normalForce = suspensionForce;
            ti.desiredLong = longNeeded * P->longStiffness;
            ti.desiredLat = latDesired;
            ti.slipRatio = w.slipRatio;                             // patinaje del paso anterior
            ti.slipAngle = std::fabs(slipAngle);
            ti.surfaceGrip = terrain.SurfaceGrip(contactPoint.GetX(), contactPoint.GetZ());
            // La cubierta según la superficie (las lisas patinan en la tierra). Con los dos en 1 ni se toca.
            if (tp.looseGrip != 1.0f || tp.pavedGrip != 1.0f) {
                const float paved = groundIsObject ? 1.0f : terrain.PavedAmount(contactPoint.GetX(), contactPoint.GetZ());
                ti.surfaceGrip *= mu::Lerp(tp.looseGrip, tp.pavedGrip, paved);
            }
            w.gripFactor = ti.surfaceGrip * tp.latGrip;            // agarre lateral (en g): para lean_surface_grip, no cambia las fuerzas
            TireParams tpTurn = tp;                                 // con la pata afuera muerde más de costado
            tpTurn.latGrip *= 1.0f + P->legOutGrip * legAmount;
            TireSolveOutput to = SolveTireForces(tpTurn, ti);

            // Deslizando (trabada con el freno o patinando fuerte): el roce es cinético y va en contra de
            // la velocidad con que se desliza el contacto (círculo de fricción). Una trasera trabada no
            // sostiene la cola: se va de costado lo que la empujen, frenada sólo por el roce en esa
            // dirección; y la que patina con el gas empuja adelante pero agarra poco de costado.
            const float sliding = held ? 1.0f : mu::Smoothstep(0.5f, 0.9f, std::fabs(w.slipRatio));
            if (sliding > 0.0f) {
                const float slideLong = w.longVel - omega * R, vs = std::sqrt(slideLong * slideLong + w.latVel * w.latVel);
                if (vs > 0.2f) {
                    const float kinetic = suspensionForce * ti.surfaceGrip * tpTurn.longGrip * P->slideFriction;
                    float fl = -kinetic * slideLong / vs, ft = -kinetic * w.latVel / vs;
                    // Suelo irregular (tierra, piedritas, juntas del pavimento): el roce de costado de una
                    // cubierta que patina cambia de a poco; una trasera clavada no va derechita, baila.
                    wanderRng ^= wanderRng << 13;
                    wanderRng ^= wanderRng >> 17;
                    wanderRng ^= wanderRng << 5;
                    const float kick = (float)(wanderRng & 0xffff) / 32767.5f - 1.0f;
                    wander[i] += (kick - wander[i]) * std::min(1.0f, dt * 6.0f);
                    // Nunca más de lo que frenaría el deslizamiento en este paso (no se pasa para el otro lado).
                    const float stopLat = -w.latVel / (kLat * dt);
                    if (std::fabs(fl) > std::fabs(longNeeded)) fl = longNeeded;
                    if (std::fabs(ft) > std::fabs(stopLat)) ft = stopLat;
                    // El suelo empuja de costado aunque la rueda vaya derecha (una piedra, un surco).
                    ft += kinetic * P->slideWander * wander[i] * mu::Smoothstep(1.0f, 4.0f, vs);
                    to.longForce = mu::Lerp(to.longForce, fl, sliding);
                    to.latForce = mu::Lerp(to.latForce, ft, sliding);
                }
            }

            // La fuerza del suelo también acelera/frena la rueda.
            if (held) {
                const float excess = std::fabs(to.longForce) * R - spareTorque;
                if (excess > 0.0f) omega -= mu::Sign(to.longForce) * excess / I * dt;
            } else {
                omega -= to.longForce * R / I * dt;
            }

            // Patinaje real que queda tras resolver (el que ve el piloto, el HUD y el TC).
            const float slipAfter = omega * R - (w.longVel + to.longForce * kLong * dt);
            w.slipRatio = slipAfter / std::max(std::fabs(w.longVel), P->minSlipSpeed);
            w.slipAngle = std::fabs(slipAngle);
            w.longForce = to.longForce;
            w.latForce = to.latForce;
            w.gripUsage = to.usage;
            if (wheelLog)
                std::printf("  freno %c omegaR=%.2f v=%.2f slip=%.3f retenida=%d freno=%.0f uso=%.2f long=%.0f max=%.0f "
                            "latV=%.2f deriva=%.1f latPedida=%.0f lat=%.0f carga=%.0f\n",
                            i == FRONT ? 'D' : 'T', omega * R, w.longVel, w.slipRatio, held ? 1 : 0, brake, to.usage,
                            to.longForce, suspensionForce * ti.surfaceGrip * tp.longGrip * LongitudinalGripCurve(ti.slipRatio),
                            w.latVel, mu::Deg(slipAngle), ti.desiredLat, to.latForce, suspensionForce);

            const Vec3 tireF = fwdG * to.longForce + latG * to.latForce;
            // Frenando inclinada, el contacto queda hacia afuera del centro de masa y la fuerza de frenado hace
            // girar la moto hacia afuera de la curva (~800 Nm a 1 g y 36°). En una de verdad eso la endereza y el
            // piloto lo compensa con el manubrio; acá el balance sostiene la inclinación, así que la moto se
            // quedaba inclinada, cangrejeando para afuera sin doblar. brake_yaw_comp cancela esa parte del giro.
            // Vale para toda fuerza que frena (también el freno motor). Probado limitarlo al freno de adelante: el bot del
            // autódromo pasó de 0 a 2 caídas en 300 s. La motocross no lo usa (con su geometría empeoraba, ver FISICA.md).
            if (P->brakeYawComp > 0.0f && to.longForce * w.longVel < 0.0f)
                totalTorque -= Vec3::sAxisY() * (P->brakeYawComp * (contactPoint - com).Cross(fwdG * to.longForce).GetY());
            w.suspForce = suspF;
            w.tireForce = tireF;
            applyAt(suspF + tireF, contactPoint);
            // Wheelie con gas (wheelie_keep_throttle): la tracción de la trasera, debajo del centro de masa,
            // levanta la trompa. Dentro de la zona del limitador se anula esa parte (lo que el piloto hace con
            // el cuerpo y el freno trasero): la moto sigue acelerando y la rueda no sube más.
            if (i == REAR && wheelieCut > 0.0f && wheelieKeep > 0.0f && to.longForce > 0.0f) {
                const float lift = (contactPoint - forceCenter).Cross(fwdG * to.longForce).Dot(right);
                if (lift > 0.0f) totalTorque -= right * (lift * wheelieCut * wheelieKeep);
            }
        } else {
            w.grounded = false;
            w.onTerrain = false;
            w.onObject = false;
            // En el aire la suspensión sólo empuja la rueda (fuerza interna): el chasis recibe la
            // reacción por el eje hasta que la rueda llega al tope de extensión.
            if (springForce > 0.0f) totalForce += (-axis) * springForce;
            w.normalForce = w.longForce = w.latForce = w.gripUsage = 0.0f;
            w.slipRatio = w.slipAngle = w.longVel = w.latVel = 0.0f;
            w.suspForce = w.tireForce = Vec3::sZero();
        }
        w.omega = omega;
        w.locked = brake > 0.0f && omega == 0.0f;

        // Reacción de la rueda sobre el chasis (-I * alfa): acelerar la trasera en el aire levanta
        // la nariz, frenarla la baja. En el aire se exagera un poco.
        const bool coupled = i == REAR && !engine.clutchSlipping && engine.shiftTimer <= 0.0f;
        const float reactionInertia = tp.inertia + (coupled ? P->engine.engineInertia * engine.TotalRatio() : 0.0f);
        const float alpha = (w.omega - omegaBefore) / dt;
        totalTorque += right * (reactionInertia * alpha * (wasInAir ? P->airReactionGain : 1.0f));

        w.angle = std::fmod(w.angle + w.omega * dt, 2.0f * mu::kPi);
    }

    // ------------------------------------------------------------------ balance / aire
    const bool grounded = wheels[FRONT].grounded || wheels[REAR].grounded;
    airTime = grounded ? 0.0f : airTime + dt;

    // Cuerpo del piloto (whip): con el stick al costado en el aire corre el cuerpo para el otro lado,
    // como un resorte críticamente amortiguado (sus músculos). La moto recibe la reacción, -I·alfa:
    // gira para el lado pedido mientras el cuerpo se mueve y se frena cuando el cuerpo llega. Soltar
    // el stick trae el cuerpo al centro y la moto vuelve. En el piso sólo vuelve el cuerpo.
    {
        const bool flying = !grounded && !crashed && riderOnBike;
        const float maxTilt = mu::Rad(P->airBodyTiltDeg);
        const float target = flying ? -mu::Clamp(in.steer + in.side, -1.0f, 1.0f) * maxTilt : 0.0f;   // A/D o el cuerpo (flechas)
        const float w0 = P->airBodyRate;
        const float prevRate = bodyTiltRate;
        bodyTiltRate += ((target - bodyTilt) * w0 * w0 - bodyTiltRate * 2.0f * w0) * dt;
        bodyTilt += bodyTiltRate * dt;
        if (std::fabs(bodyTilt) > maxTilt) {
            bodyTilt = mu::Sign(bodyTilt) * maxTilt;
            bodyTiltRate = 0.0f;
        }
        if (flying) totalTorque -= fwd * (P->airBodyInertia * (bodyTiltRate - prevRate) / dt);
    }
    // Doblando en wheelie (cualquier moto sin grau_turn): la trasera empuja hacia adentro y curva la
    // trayectoria, pero nada orienta la moto, que se quedaba apuntando 20-25° para afuera de adonde iba.
    // Mientras dobla de verdad en una rueda (más de 0.3 s y con inclinación o deriva de unos grados) se hace
    // lo mismo que en el grau; derecha, o un instante en una rueda (un salto), no cambia nada.
    const bool oneWheel = wheels[REAR].grounded && !wheels[FRONT].grounded;
    oneWheelTime = oneWheel ? oneWheelTime + dt : 0.0f;
    const Vec3 flatVel(linVel.GetX(), 0.0f, linVel.GetZ());
    const float slipSide = flatVel.Dot(Vec3(right.GetX(), 0.0f, right.GetZ()).NormalizedOr(Vec3::sAxisX()));
    const float slipBeta = v > 1.0f ? std::atan2(slipSide, std::max(flatVel.Dot(fwdFlat), 0.2f)) : 0.0f;   // + = va a la derecha de adonde apunta
    const float wheelieTurning = (P->grauTurn <= 0.0f && P->wheelieTurn > 0.0f && oneWheel)
                                     ? mu::Smoothstep(0.25f, 0.4f, oneWheelTime) *
                                           mu::Smoothstep(mu::Rad(2.0f), mu::Rad(6.0f), std::max(std::fabs(roll), std::fabs(slipBeta)))
                                     : 0.0f;
    if (!crashed) {
        const float rollRate = angVel.Dot(fwd);
        if (grounded) {
            // Balance automático: mucho a baja velocidad, menos a alta. Es un torque, no se fija
            // la rotación: la moto puede desestabilizarse y caerse.
            const float s = mu::Smoothstep(0.0f, P->balanceSpeed, v);
            const float K = mu::Lerp(P->balanceKLow, P->balanceKHigh, s);
            const float D = mu::Lerp(P->balanceDLow, P->balanceDHigh, s);
            const float t = (leanTarget - roll) * K - rollRate * D;
            // En una rueda (grau) la moto se inclina alrededor de la línea del piso por donde va, no de su
            // eje levantado: si no, sostener la inclinación mete guiñada y la deja apuntando para afuera.
            const bool oneWheelGrau = P->grauTurn > 0.0f && wheels[REAR].grounded && !wheels[FRONT].grounded;
            const Vec3 rollAxis = oneWheelGrau ? fwdFlat : (wheelieTurning > 0.0f ? (fwd + (fwdFlat - fwd) * wheelieTurning).Normalized() : fwd);
            totalTorque += rollAxis * mu::Clamp(t, -P->balanceMaxTorque, P->balanceMaxTorque);

            // Lo que haría el piloto con el freno trasero cuando el wheelie se pasa: bajar la nariz.
            if (wheelieCut > 0.0f) totalTorque -= right * (wheelieCut * P->wheelieAssistTorque);

            // En pleno wheelie el piloto tirándose adelante baja la rueda (y atrás la sube). Cerca de la
            // vertical tirarse atrás ya no la levanta más (con la cola apoyada no la da vuelta); tirarse
            // adelante siempre la ayuda a bajar.
            if (wheels[REAR].grounded && !wheels[FRONT].grounded) {
                const float pull = riderLean < 0.0f ? 1.0f - mu::Smoothstep(mu::Rad(60.0f), mu::Rad(80.0f), pitch) : 1.0f;
                totalTorque -= right * (riderLean * P->wheelieLeanTorque * pull);
            }

            // En una rueda (grau) se dobla acostando la moto: la cubierta trasera inclinada empuja hacia
            // adentro y curva la trayectoria. Sin la delantera nada orienta la moto, y ese empuje (atrás del
            // centro de masa) la haría quedar apuntando para afuera, cruzada. La cubierta inclinada y
            // deslizando de costado la alinea (torque de camber y de autoalineación, más el piloto): la
            // moto gira a la par de su trayectoria y corrige lo que se le cruzó, así la trasera apunta
            // hacia donde va. A paso de hombre ayuda además lo que pide la inclinación.
            if (P->grauTurn > 0.0f && wheels[REAR].grounded && !wheels[FRONT].grounded) {
                const Vec3 flatVel(linVel.GetX(), 0.0f, linVel.GetZ());
                const float side = flatVel.Dot(Vec3(right.GetX(), 0.0f, right.GetZ()).NormalizedOr(Vec3::sAxisX()));
                const float beta = v > 1.0f ? std::atan2(side, std::max(flatVel.Dot(fwdFlat), 0.2f)) : 0.0f;   // + = va a la derecha de adonde apunta
                const float leanYaw = -kG * std::tan(mu::Clamp(roll, -1.0f, 1.0f)) / std::max(v, 2.0f);   // + roll (a la derecha) = girar a la derecha (-Y)
                const float follow = mu::Smoothstep(1.5f, 4.0f, v);
                const float wantYaw = mu::Lerp(leanYaw, pathRate - P->grauAlign * beta, follow);
                const float tq = mu::Clamp((wantYaw - angVel.GetY()) * P->grauTurn, -900.0f, 900.0f);
                totalTorque += Vec3::sAxisY() * tq;
            }
            // Derrape lento con el freno trasero doblando (slide_pivot): despacio (menos de ~13 km/h) la cola no
            // sale sola, la moto se frena antes; el piloto la empuja hacia afuera hasta el ángulo que busca.
            // Más rápido no hace nada (ahí la cola sale sola, ver slideIntent).
            if (P->slidePivot > 0.0f && slideIntent > 0.0f && wheels[REAR].grounded) {
                const float speed = Vec3(linVel.GetX(), 0.0f, linVel.GetZ()).Length();   // la real (derrapando, no sólo adelante)
                const float amount = slideIntent * mu::Smoothstep(0.8f, 1.5f, speed) * (1.0f - mu::Smoothstep(2.5f, 3.6f, speed));
                if (amount > 0.0f) {
                    const float missing = std::max(0.0f, slideWant - slideAngle);
                    const float tq = -slideTurn * P->slidePivot * missing - angVel.GetY() * 0.15f * P->slidePivot * (missing <= 0.0f ? 1.0f : 0.0f);
                    totalTorque += Vec3::sAxisY() * (tq * amount);
                }
            }
            // Freno de mano: el piloto saca la cola y la sostiene (UpdateHandbrake).
            if (handbrakeTorque != 0.0f) totalTorque += Vec3::sAxisY() * handbrakeTorque;
            // Frenando fuerte (brake_align): la moto vuelve a apuntar hacia donde va. Sin esto, con la trasera
            // descargada, un poco de inclinación hacía girar la moto, la delantera se alineaba sola con el camino
            // y la moto seguía cangrejeando ~9° (a 250 km/h, con la cola saltando, un wobble). Vale también
            // doblando: con la delantera cargada y la trasera casi sin peso, al tirarse a la curva la moto giraba
            // más rápido que su camino, la cola salía ~8° de golpe y la moto no doblaba (sólo iba cruzada). Sólo
            // frena el giro que sobra respecto del camino: el que acompaña la curva no lo toca.
            if (P->brakeAlign > 0.0f && wheels[FRONT].grounded && v > 5.0f) {
                const float amount = mu::Smoothstep(P->brakeAlignFrom, P->brakeAlignFull, in.frontBrake) * (1.0f - mu::Smoothstep(0.05f, 0.3f, slideIntent));
                if (amount > 0.0f) {
                    const float wantYaw = pathRate - P->brakeAlign * slipBeta;
                    const float tq = mu::Clamp((wantYaw - angVel.GetY()) * P->brakeAlignTorque, -2000.0f, 2000.0f);
                    totalTorque += Vec3::sAxisY() * (tq * amount);
                }
            }
            if (wheelieTurning > 0.0f) {
                const float leanYaw = -kG * std::tan(mu::Clamp(roll, -1.0f, 1.0f)) / std::max(v, 2.0f);
                const float follow = mu::Smoothstep(1.5f, 4.0f, v);
                const float wantYaw = mu::Lerp(leanYaw, pathRate - P->wheelieAlign * slipBeta, follow);
                const float tq = mu::Clamp((wantYaw - angVel.GetY()) * P->wheelieTurn, -900.0f, 900.0f);
                totalTorque += Vec3::sAxisY() * (tq * wheelieTurning);
            }

            // El piloto no es rígido: sus brazos y piernas absorben parte del cabeceo que meten los
            // baches. Sin esto la moto entra en resonancia en rollers y whoops.
            totalTorque -= right * (pitchRate * P->riderPitchDamping);

            // Apoyada sólo en la delantera (stoppie) nada frena la guiñada: el piloto la mantiene recta.
            if (wheels[FRONT].grounded && !wheels[REAR].grounded)
                totalTorque -= Vec3::sAxisY() * (angVel.GetY() * P->stoppieYawDamping);
        } else {
            // Efecto giroscópico de las ruedas: girando rápido, sus ejes resisten cambiar de dirección
            // y el chasis recibe L x w. Rolar la moto la hace guiñar y guiñar la hace rolar: acostada
            // de costado, la nariz dobla hacia ese lado. En el piso lo tapan las cubiertas.
            const Vec3 spin = left * (P->frontTire.inertia * wheels[FRONT].omega + P->rearTire.inertia * wheels[REAR].omega);
            totalTorque += spin.Cross(angVel) * P->airGyroGain;

            // El piloto endereza la moto (rolido hacia derecha y guiñada hacia donde vuela) salvo
            // mientras la tira de costado: ahí la deja seguir con lo que trae.
            const float hold = 1.0f - P->airWhipAssistFade * std::min(1.0f, std::fabs(in.steer));
            // El rolido sólo se corrige un poco, y nunca con la moto casi vertical o invertida (ahí
            // el ángulo de rolido deja de tener sentido).
            if (up.GetY() > 0.5f) {
                const float a = P->airRollAssist * hold;
                totalTorque += fwd * (-roll * P->balanceKHigh * a - rollRate * P->balanceDHigh * a);
            }

            // Tope de rolido (como el de cabeceo): una moto que sale girando de costado muy rápido, el
            // piloto la frena con el cuerpo. Un whip normal queda por debajo.
            const float rollExcess = std::fabs(rollRate) - P->airMaxRollRate;
            if (rollExcess > 0.0f) totalTorque -= fwd * (mu::Sign(rollRate) * rollExcess * P->airRollRateDamping);

            // Guiñada: hacia donde vuela, para aterrizar derecho.
            const Vec3 flatVel(linVel.GetX(), 0.0f, linVel.GetZ());
            if (flatVel.Length() > 3.0f && up.GetY() > 0.5f) {
                const Vec3 dir = flatVel.Normalized();
                const float yawError = std::atan2(fwdFlat.Cross(dir).GetY(), fwdFlat.Dot(dir));   // + = girar a la izquierda
                const float yawRate = angVel.GetY();
                totalTorque += Vec3::sAxisY() * ((yawError * P->airYawAlign - yawRate * P->airYawDamping) * hold);
            }

            // Stick adelante/atrás = velocidad de rotación deseada. El torque la persigue (con tope),
            // así frenar o invertir un giro responde enseguida; con el stick suelto la moto conserva
            // su impulso (sólo un leve amortiguamiento).
            if (std::fabs(in.lean) > 0.05f) {
                const float targetRate = -in.lean * P->airPitchRate;
                const float tq = mu::Clamp((targetRate - pitchRate) * P->airPitchGain, -P->airPitchTorque, P->airPitchTorque);
                totalTorque += right * tq;
            } else {
                totalTorque -= right * (pitchRate * P->airNeutralDamping);
            }
            const float excess = std::fabs(pitchRate) - P->airMaxPitchRate;
            if (excess > 0.0f) totalTorque -= right * (mu::Sign(pitchRate) * excess * P->airPitchRateDamping);
        }
    }

    // ------------------------------------------------------------------ aerodinámica
    totalForce += -linVel * (0.5f * 1.2f * P->dragArea * speed);

    b.AddForce(totalForce);
    b.AddTorque(totalTorque);
}

void Bike::PostPhysics()
{
    prevPos = currPos;
    prevRot = currRot;
    currPos = body->GetCenterOfMassPosition();
    currRot = body->GetRotation();
}
