// Recuperación auditada en historico-0.3.1/recovery-audit.json.
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

// Las ruedas sólo apoyan en lo estático (terreno), no en el piloto suelto tras una caída.
class StaticOnlyFilter final : public JPH::ObjectLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer) const override { return layer == Layers::STATIC; }
};
} // namespace

void Bike::PrePhysics031(const BikeInput& rawInput, float dt, PhysicsWorld& world, const Terrain& terrain)
{
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

    // Caída: demasiado inclinada o volcada un instante MIENTRAS toca el suelo (ruedas, chasis o
    // piloto, del paso anterior). En el aire el contador no cambia: un flip no suma nada, y una moto
    // que rebota dando tumbos tampoco se "recupera" entre golpes. Sólo baja derecha sobre las ruedas.
    const bool tipped = up.GetY() < std::cos(mu::Rad(P->crashAngleDeg));
    if (tipped && (!wasInAir || world.AnyContact())) crashTimer += dt;
    else if (!tipped && !wasInAir) crashTimer = std::max(0.0f, crashTimer - dt);
    // De cabeza contra el suelo es caída en el acto.
    if (crashTimer > 0.4f || (up.GetY() < 0.0f && world.AnyContact())) crashed = true;
    if (crashed) crashedTime += dt;

    BikeInput in = rawInput;
    if (crashed) {
        in.throttle = 0.0f;
        in.steer = 0.0f;
        in.lean = 0.0f;
    }

    const float riderThrottle = in.throttle;                     // lo que gira el piloto (antes de los recortes)
    if (P->handbrakeYaw > 0.0f) UpdateHandbrake(in, linVel, fwdFlat, 0.0f, dt);   // freno de mano (BikePhysics.cpp)

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
        if (engine.gear < 5 && !engine.clutchSlipping && engine.rpm > 9400.0f && in.throttle > 0.3f) {
            engine.ShiftUp();
            autoShiftCooldown = 0.5f;
        } else if (engine.gear > 1 && engine.rpm < 4800.0f) {
            float newRPM = wheels[REAR].omega * engine.Ratio(engine.gear - 1) * kRadPerSecToRPM;
            if (newRPM < 8500.0f) {
                engine.ShiftDown(wheels[REAR].omega);
                autoShiftCooldown = 0.5f;
            }
        }
    }

    // ------------------------------------------------------------------ piloto (weight shift)
    // El piloto no es un cuerpo físico: sólo desplaza el centro de masa efectivo.
    riderLean = mu::MoveTowards(riderLean, in.lean, P->riderSpeed * dt);

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
    comShiftWorld = rot * Vec3(0.0f, P->comHeight, P->comForward + riderLean * P->riderShift + legAmount * P->legOutComForward);
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
    const Vec3 groundN = terrain.Normal(com.GetX(), com.GetZ());
    const Vec3 rightFlat = Vec3(right.GetX(), 0.0f, right.GetZ()).NormalizedOr(Vec3::sAxisX());
    const float sinBank = mu::Clamp(groundN.Dot(rightFlat) * (in.steer >= 0.0f ? 1.0f : -1.0f) * P->bankTurnGain, -0.5f, 0.5f);
    const float cosBank = std::sqrt(1.0f - sinBank * sinBank);
    const float muLat = P->maxLateralAccel / kG * (1.0f + P->legOutGrip * legAmount);
    const float maxAccel = kG * (sinBank + muLat * cosBank) / std::max(cosBank - muLat * sinBank, 0.3f);
    const float kMax = std::min(1.0f / P->minTurnRadius, maxAccel / std::max(v2, 0.01f));
    const float kCmd = in.steer * kMax;
    const float maxLean = mu::Rad(P->maxLeanDeg);
    leanTarget = mu::Clamp(std::atan(v2 * kCmd / kG), -maxLean, maxLean);
    // Frenar fuerte inclinado endereza la moto (geometría de dirección): se abre de la curva en
    // vez de pivotar sobre la delantera por el contacto desplazado del centro de masa.
    leanTarget *= 1.0f - P->brakeStandUp * in.frontBrake * mu::Smoothstep(3.0f, 8.0f, v);
    // Freno de mano: con la trasera derrapando el piloto lleva la moto más derecha (como slide_upright en BikePhysics.cpp).
    // Con handbrake_lean, en cambio, la sostiene inclinada hacia la curva mientras colea (BikePhysics.cpp).
    if (handbrake > 0.0f) leanTarget *= 1.0f - P->slideUpright * handbrake;
    if (P->handbrakeLean > 0.0f && (hbW > 0.0f || hbCatching > 0.0f)) leanTarget = HandbrakeLean(leanTarget);
    const float kLean = kG * std::tan(mu::Clamp(roll, -1.2f, 1.2f)) / std::max(v2, 1.0f);
    const float kSteer = mu::Lerp(kCmd, kLean, mu::Smoothstep(P->leanSteerSpeedLow, P->leanSteerSpeedHigh, v));
    const float wheelbase = (wheels[FRONT].AxleLocal() - wheels[REAR].AxleLocal()).Length();
    const float maxSteer = mu::Lerp(mu::Rad(P->maxSteerDeg), mu::Rad(P->maxSteerHighSpeedDeg), mu::Smoothstep(3.0f, 20.0f, v));
    // En el aire el manubrio vuelve al centro: aterrizar con la rueda girada es un trompo seguro.
    float steerTarget = wasInAir ? 0.0f : mu::Clamp(std::atan(wheelbase * kSteer), -maxSteer, maxSteer);
    float steerRate = P->steerRate;
    if (hbW > 0.0f && !wasInAir) {               // freno de mano: la delantera sigue su camino (BikePhysics.cpp)
        steerTarget = HandbrakeSteer(steerTarget, linVel, angVel.GetY(), rot * wheels[FRONT].AxleLocal(), fwdFlat, mu::Rad(P->maxSteerDeg));
        steerRate += 6.0f * hbW;
    }
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
        // Freno de mano: el piloto deja salir la cola en vez de contravolantear (ver UpdateHandbrake).
        if (hbW > 0.0f) casterTarget *= 1.0f - hbW;
    }
    steerCaster = mu::MoveTowards(steerCaster, casterTarget, 10.0f * dt);
    const float steerStop = mu::Rad(std::max(35.0f, P->maxSteerDeg));   // topes de dirección
    steerAngle = mu::Clamp(steerBase + steerCaster, -steerStop, steerStop);

    // ------------------------------------------------------------------ motor
    float reflectedInertia = 0.0f;
    const float driveTorque = engine.Update(wheels[REAR].omega, engineThrottle, dt, reflectedInertia);

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
        const float rayLength = above + w.travel + w.radius * 1.6f;
        const JPH::RRayCast ray(mount - axis * above, axis * rayLength);
        JPH::RayCastResult hit;

        bool groundFound = false;
        w.onTerrain = false; // metadato visual de 0.3.2: polvo y huellas, sin cambiar fuerzas
        float groundExtension = 1e6f;                               // extensión a la que la rueda toca el suelo
        Vec3 n = Vec3::sAxisY();
        Vec3 contactPoint = Vec3::sZero();
        if (world.Query().CastRay(ray, hit, JPH::BroadPhaseLayerFilter(), staticOnly, ignoreSelf)) {
            Vec3 groundPoint = ray.GetPointOnRay(hit.mFraction);
            const bool onTerrain = hit.mBodyID == terrain.BodyID();
            if (onTerrain) {
                n = terrain.Normal(groundPoint.GetX(), groundPoint.GetZ());
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
            if (-axis.Dot(n) > 0.25f) {
                float e = solveExtension(groundPoint, n);
                if (onTerrain) {
                    // Refinamiento: re-evaluar el suelo justo debajo del punto de contacto estimado.
                    Vec3 below = mount + axis * e - n * w.radius;
                    groundPoint = Vec3(below.GetX(), terrain.Height(below.GetX(), below.GetZ()), below.GetZ());
                    n = terrain.Normal(groundPoint.GetX(), groundPoint.GetZ());
                    if (-axis.Dot(n) > 0.25f) e = solveExtension(groundPoint, n);
                }
                // Salvaguardas: si el tope de la rueda ya está bajo el suelo (moto volcada o
                // clavada) la suspensión no aplica fuerza, y el sobre-recorrido se acota: de ahí
                // en más se encarga la colisión del chasis. Sin esto, una moto de cabeza puede
                // generar una compresión de metros y una fuerza absurda.
                const bool mountAboveGround = (mount - groundPoint).Dot(n) > 0.0f;
                if (-axis.Dot(n) > 0.25f && mountAboveGround) {
                    groundFound = true;
                    groundExtension = e;
                    const Vec3 axle = mount + axis * e;
                    const Vec3 inPlaneDown = (-n + left * n.Dot(left)).NormalizedOr(-up);
                    contactPoint = axle + inPlaneDown * (w.radius - tp.roundness) - n * tp.roundness;
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
        constexpr int kSub = 8;
        const float h = dt / kSub;
        float springSum = 0.0f, tireSum = 0.0f;
        for (int k = 0; k < kSub; ++k) {
            const float suspF = UpdateSuspension(sp, w.susp, w.travel - w.extension, h);
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
        w.groundExtension = groundFound ? groundExtension : 1e6f;
        const float springForce = springSum / kSub;
        const float tireNormal = tireSum / kSub;
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
            const float mitigation = mu::Lerp(0.25f, 0.8f, turning);
            brake *= 1.0f - mitigation * (1.0f - mu::Smoothstep(0.0f, 300.0f, rearLoad));
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
            w.onTerrain = hit.mBodyID == terrain.BodyID();
            w.contactPoint = contactPoint;
            w.contactNormal = n;
            w.normalForce = tireNormal;
            const Vec3 suspF = n * tireNormal + (-axis) * (springForce - tireNormal * cosAxis);

            // -------------------------------------------------------------- neumático
            Vec3 fwdG = (fwd - n * fwd.Dot(n)).NormalizedOr(fwdFlat);
            if (i == FRONT) fwdG = Quat::sRotation(n, -steerAngle) * fwdG;
            const Vec3 latG = n.Cross(fwdG);                        // hacia la izquierda
            const Vec3 pointVel = b.GetPointVelocity(contactPoint);
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
            TireParams tpTurn = tp;                                 // con la pata afuera muerde más de costado
            tpTurn.latGrip *= 1.0f + P->legOutGrip * legAmount;
            const TireSolveOutput to = SolveTireForces(tpTurn, ti);

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

            const Vec3 tireF = fwdG * to.longForce + latG * to.latForce;
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
        const float target = flying ? -in.steer * maxTilt : 0.0f;
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
    if (!crashed) {
        const float rollRate = angVel.Dot(fwd);
        if (grounded) {
            // Balance automático: mucho a baja velocidad, menos a alta. Es un torque, no se fija
            // la rotación: la moto puede desestabilizarse y caerse.
            const float s = mu::Smoothstep(0.0f, P->balanceSpeed, v);
            const float K = mu::Lerp(P->balanceKLow, P->balanceKHigh, s);
            const float D = mu::Lerp(P->balanceDLow, P->balanceDHigh, s);
            const float t = (leanTarget - roll) * K - rollRate * D;
            totalTorque += fwd * mu::Clamp(t, -P->balanceMaxTorque, P->balanceMaxTorque);

            // Lo que haría el piloto con el freno trasero cuando el wheelie se pasa: bajar la nariz.
            if (wheelieCut > 0.0f) totalTorque -= right * (wheelieCut * P->wheelieAssistTorque);

            // En pleno wheelie el piloto tirándose adelante baja la rueda (y atrás la sube).
            if (wheels[REAR].grounded && !wheels[FRONT].grounded) totalTorque -= right * (riderLean * P->wheelieLeanTorque);

            // El piloto no es rígido: sus brazos y piernas absorben parte del cabeceo que meten los
            // baches. Sin esto la moto entra en resonancia en rollers y whoops.
            totalTorque -= right * (pitchRate * P->riderPitchDamping);

            // Apoyada sólo en la delantera (stoppie) nada frena la guiñada: el piloto la mantiene recta.
            if (wheels[FRONT].grounded && !wheels[REAR].grounded)
                totalTorque -= Vec3::sAxisY() * (angVel.GetY() * P->stoppieYawDamping);

            // Freno de mano: el piloto saca la cola y la sostiene (UpdateHandbrake).
            if (handbrakeTorque != 0.0f) totalTorque += Vec3::sAxisY() * handbrakeTorque;
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
