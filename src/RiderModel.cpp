#include <Jolt/Jolt.h>

#include "RiderModel.h"

#include "BikeStyleDef.h"
#include "MathUtil.h"
#include "raymath.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <tuple>

namespace {

// Ajustes de la pose del modelo sobre la moto (sus proporciones no son las del piloto generado).
constexpr float kHipsDrop = 0.05f;       // m que baja la cadera: rodillas más dobladas (posición de ataque)
constexpr float kExtraLean = 0.1f;       // rad extra de torso hacia adelante respecto de la pose
constexpr float kForwardLean = 0.35f;    // rad más tirado adelante: pecho sobre el manubrio...
constexpr float kForwardHipsBack = 0.08f;// ...con la cola en el asiento y no sobre el tanque
constexpr float kWristBack = 0.07f;      // m de la muñeca al centro del puño del manubrio
constexpr float kMaxReach = 0.97f;       // brazo como mucho al 97%: nunca del todo estirado
constexpr float kMaxExtraPitch = 0.45f;  // rad que puede sumar el torso para llegar al manubrio...
constexpr float kMaxHipShift = 0.3f;     // ...y después m que se acerca la cadera
constexpr float kBackHipsForward = 0.12f;// tirado atrás, si los brazos no llegan: m que la cadera va menos atrás...
constexpr float kBackLean = 0.35f;       // ...y rad más de torso adelante (antes que kMaxExtraPitch y kMaxHipShift)
constexpr float kClavicleReach = 0.35f;  // rad que la clavícula lleva el hombro hacia el manubrio si queda lejos (antes que el torso)
constexpr float kThrottleRoll = 0.45f;   // rad que gira la mano derecha sobre el puño con el gas a fondo
constexpr float kShoulderTurn = 0.6f;    // cuánto del giro del manubrio toman los hombros...
constexpr float kMaxShoulderTurn = 0.5f; // ...hasta estos rad
// Para no meterse en la moto (PoseOnBike; ver docs/PILOTO.md, "El piloto se metía en la moto"):
constexpr float kSeatSink = 0.015f;      // m que se hunden cola y muslos en el asiento (la espuma)
constexpr float kMaxLift = 0.25f;        // m que puede subir la cadera (la de carreras tiene el asiento a 21 cm y la cadera de la pose a 26)
constexpr float kSideTolerance = 0.005f; // m que se pueden meter de costado rodillas, canillas y botas
constexpr float kMaxAnkleOut = 0.06f;    // m que se puede correr el tobillo hacia afuera en la estribera...
constexpr float kMaxLegOutWiden = 0.15f; // ...y más, con la pata afuera
constexpr float kMaxKneeOpen = 0.8f;     // rad que se puede abrir la rodilla
constexpr float kBarRadius = 0.035f;     // m: el manubrio con la almohadilla, para el pecho, el casco y los brazos
constexpr float kBarTolerance = 0.005f;
constexpr float kMaxBackPitch = 0.6f;    // rad que se puede enderezar el torso para no atravesar el manubrio...
constexpr float kReachLimit = 0.99f;     // ...sin que los brazos dejen de llegar
constexpr float kLiftReach = 1.02f;      // la cadera baja si el brazo más exigido no llega ni al 102% (la mano, a ~1 cm del puño)
// La de carreras (RacePose, SolveBody): colgado, cuánto más hacia adentro van la cadera y los hombros y cuánto bajan los
// hombros, además de lo de la pose.
constexpr float kHangHips = 0.06f, kHangShoulders = 0.12f, kHangDrop = 0.03f;
constexpr float kTuckElbowOut = 0.45f;   // polo de los codos agachado (de costado, contra 0.85 abajo)
constexpr float kArmMargin = 0.015f;   // m que el brazo queda afuera de la moto (ArmDepth)
constexpr float kMaxElbowOpen = 1.2f;  // rad que se puede abrir un codo para no meterse en la moto
constexpr float kHangKeep = 0.85f;     // colgado, cuánto del corrimiento de la cadera no vuelve al medio (Fit::center)
constexpr float kHangKneeOut = 1.0f;    // polo de la rodilla de adentro (afuera, con kHangKneeOut de costado contra 0.55 adelante)
constexpr float kShapeCell = 0.01f;      // m: celdas de la forma de la moto
constexpr float kNothing = -10.0f;       // techo de una celda sin moto

// Cuerpo con masa (RiderMotion). w en rad/s (2π·frecuencia propia); zeta 1 = crítico, menos rebota apenas.
// Las "fuerzas" son aceleraciones de la moto (m/s², rad/s²) que el cuerpo siente por inercia.
constexpr float kBaseW = 13.0f;                  // la pose pedida, suavizada (riderLean y legOut van en rampa)
constexpr float kHipsW[3] = {9.0f, 12.0f, 8.0f}; // cadera: costado, arriba (piernas), adelante
constexpr float kHipsZeta[3] = {0.8f, 0.7f, 0.75f};
constexpr float kHipsGain[3] = {0.35f, 1.0f, 0.5f};      // fracción de la aceleración que la mueve
constexpr float kHipsLimit[3] = {0.04f, 0.055f, 0.05f};  // m (saturación suave); arriba, kHipsUp
constexpr float kHipsUp = 0.03f;
constexpr float kVertAdapt = 0.6f;               // s: los músculos se acomodan a una carga vertical sostenida
constexpr float kVertLight = 0.5f;               // de lo que alivia (despegar, caer) siente la mitad: estira menos de lo que se hunde
constexpr float kTorsoW = 7.5f, kTorsoZeta = 0.65f;
constexpr float kPitchPerAcc = 1.5f;             // rad/s² de torso por m/s² adelante (un péndulo de ~0.65 m)
constexpr float kRollPerAcc = 1.0f;
constexpr float kTorsoInertia = 0.6f;            // fracción del giro de la moto que el torso no acompaña al instante
constexpr float kPitchLimit = 0.32f, kRollLimit = 0.28f;   // rad (saturación suave)
constexpr float kFold = 3.0f;                    // rad de torso adelante por m que se hunde la cadera (absorbe con todo el cuerpo)
constexpr float kInward = 0.12f;                 // rad de torso hacia adentro por g de curva
constexpr float kAirUpright = 0.25f;             // en el aire, fracción del cabeceo / tumbado de la moto que el torso compensa
constexpr float kHeadW = 10.0f, kHeadZeta = 0.85f;
constexpr float kHeadLookAhead = 0.45f;          // s: mira hacia donde va a estar apuntando la curva
constexpr float kHeadLevel = 0.45f;              // fracción del tumbado que la cabeza endereza (horizonte)
constexpr float kSitUpDecel[2] = {2.0f, 7.0f};  // m/s² de frenada (agachado) en que se va sentando...
constexpr float kSitUpW = 6.0f;                  // ...con un resorte crítico (~0.5 s)...
constexpr float kSitUpHips = 0.03f;              // ...la cola adelante contra el tanque...
const JPH::Vec3 kSitUpShoulders(0.0f, 0.13f, -0.05f);   // ...y el pecho arriba (la pose agachada lo baja 0.20)
constexpr float kOutW = 7.0f;                    // pata afuera (crítico)
constexpr float kFitW = 15.0f;                   // lo que dibuja el ajuste (Fit) sigue a lo que busca: crítico, ~0.1 s
constexpr float kRagdollBlend = 0.35f;           // s: del cuerpo como se veía al ragdoll

float SoftLimit(float x, float lim) { return lim * std::tanh(x / lim); }

Vector3 V(JPH::Vec3 v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
Quaternion Q(JPH::Quat q) { return {q.GetX(), q.GetY(), q.GetZ(), q.GetW()}; }

Quaternion FromTo(Vector3 a, Vector3 b)
{
    return QuaternionFromVector3ToVector3(Vector3Normalize(a), Vector3Normalize(b));
}

// Dos huesos A-B-C de largos l1, l2: dónde queda la articulación del medio para que la punta llegue a
// `target` (o lo más cerca que pueda), doblando hacia el lado de `pole`.
void TwoBoneIK(Vector3 a, Vector3 target, float l1, float l2, Vector3 pole, Vector3& mid, Vector3& tip)
{
    const Vector3 d = Vector3Subtract(target, a);
    const float dist = mu::Clamp(Vector3Length(d), std::fabs(l1 - l2) + 1e-3f, (l1 + l2) * 0.999f);
    const Vector3 dir = Vector3Normalize(d);
    const float along = (l1 * l1 - l2 * l2 + dist * dist) / (2.0f * dist);
    const float h = std::sqrt(std::max(0.0f, l1 * l1 - along * along));
    Vector3 side = Vector3Subtract(pole, a);
    side = Vector3Subtract(side, Vector3Scale(dir, Vector3DotProduct(side, dir)));
    side = Vector3Length(side) > 1e-4f ? Vector3Normalize(side) : Vector3Perpendicular(dir);
    mid = Vector3Add(a, Vector3Add(Vector3Scale(dir, along), Vector3Scale(side, h)));
    tip = Vector3Add(a, Vector3Scale(dir, dist));
}

// Dónde va la rodilla de la IK de dos huesos de `a` a `target` (como TwoBoneIK, del lado de `pole`) girada
// `open` rad hacia afuera (`side`: +1 = +X) alrededor de la recta a-target. Sirve de polo para TwoBoneIK.
Vector3 OpenJoint(Vector3 a, Vector3 target, float l1, float l2, Vector3 pole, float side, float open)
{
    const Vector3 d = Vector3Subtract(target, a);
    const float dist = mu::Clamp(Vector3Length(d), std::fabs(l1 - l2) + 1e-3f, (l1 + l2) * 0.999f);
    const Vector3 n = Vector3Normalize(d);
    const float along = (l1 * l1 - l2 * l2 + dist * dist) / (2.0f * dist);
    const float h = std::sqrt(std::max(0.0f, l1 * l1 - along * along));
    const Vector3 c = Vector3Add(a, Vector3Scale(n, along));
    Vector3 e1 = Vector3Subtract(pole, a);
    e1 = Vector3Subtract(e1, Vector3Scale(n, Vector3DotProduct(e1, n)));
    e1 = Vector3Length(e1) > 1e-4f ? Vector3Normalize(e1) : Vector3Perpendicular(n);
    Vector3 e2 = Vector3CrossProduct(n, e1);
    if (e2.x * side < 0.0f) e2 = Vector3Negate(e2);
    return Vector3Add(c, Vector3Add(Vector3Scale(e1, h * std::cos(open)), Vector3Scale(e2, h * std::sin(open))));
}

// De la dirección a hacia la b, girando (t: 0..1). Mezclar los vectores pasa por cero si son casi opuestos.
Vector3 DirSlerp(Vector3 a, Vector3 b, float t)
{
    a = Vector3Normalize(a);
    b = Vector3Normalize(b);
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    Vector3 axis = Vector3CrossProduct(a, b);
    const float s = Vector3Length(axis), c = Vector3DotProduct(a, b);
    if (s < 1e-5f) {
        if (c > 0.0f) return a;
        axis = Vector3Perpendicular(a);             // opuestos: por cualquier lado, siempre el mismo
    }
    return Vector3RotateByAxisAngle(a, Vector3Normalize(axis), std::atan2(s, c) * t);
}

bool PoseLog()
{
    static const bool on = std::getenv("MOTOSIM_PILOTO_LOG") != nullptr;
    return on;
}

// Detector de saltos (MOTOSIM_SALTOS=1; pruebas, no cambia nada): cada cuadro sobre la moto compara cada hueso con el del
// cuadro anterior (espacio de la moto: lo que la moto se movió no cuenta) y avisa si se movió más que kJump* llevado a un
// cuadro de 60 fps. Ver docs/PILOTO.md, "Saltos de la pose".
bool JumpLog()
{
    static const bool on = std::getenv("MOTOSIM_SALTOS") != nullptr;
    return on;
}
constexpr float kJumpDist = 0.025f;      // m por cuadro de 60 fps
constexpr float kJumpAngle = 0.10f;      // rad por cuadro de 60 fps (~6°)

} // namespace

// ---------------------------------------------------------------------------------------------- cuerpo con masa

void RiderMotion::Spring::Step(float target, float force, float w, float zeta, float dt)
{
    // Semi-implícito en pasos de a lo sumo 1/240 s: estable con w hasta ~100 y cualquier cuadro.
    const int n = std::max(1, (int)std::ceil(dt * 240.0f));
    const float h = dt / (float)n;
    for (int i = 0; i < n; ++i) {
        v += (w * w * (target - x) - 2.0f * zeta * w * v + force) * h;
        x += v * h;
    }
}

RiderPose RiderMotion::Apply(const RiderPose& base, const Bike& bike, const Input& in)
{
    using JPH::Vec3;
    using JPH::Quat;
    // El dt de verdad (un cuadro largo, p. ej. al guardar una captura, también movió la moto todo eso: con un dt
    // recortado la velocidad salía al doble y parecía un golpe). Los resortes van en pasos chicos (Spring::Step).
    const float dt = mu::Clamp(in.dt, 0.0f, 0.25f);
    const float tv = 0.035f * in.smooth, ta = 0.04f * in.smooth;   // filtros de velocidad y aceleración
    // Un punto fijo de la moto, más o menos en la cadera: lo que siente el cuerpo (con el giro de la moto).
    const Vec3 anchor = in.pos + in.rot * Vec3(0.0f, 0.30f, -0.25f);
    // Aparece, reaparece o se teletransporta: arranca quieto en la pose pedida.
    // (Respawn, reaparecer cerca o girada: más de 1 m o 0.6 rad en un cuadro, imposible andando.)
    const float jump = (anchor - prevAnchor).Length();
    const float turn = 2.0f * std::acos(mu::Clamp(std::fabs(prevRot.Dot(in.rot)), 0.0f, 1.0f));
    // Y reaparecer cerca (el bot trabado contra una pared reaparece a menos de 1 m): lo que se corrió en este cuadro no sale
    // de la velocidad que traía (más de 25 cm de diferencia: un cambio de velocidad de 15 m/s en un cuadro). Antes el cuerpo
    // lo sentía como un golpe y el torso saltaba 27 cm (detector de saltos, 2T en la favela).
    const float offPath = valid ? ((anchor - prevAnchor) - velF * dt).Length() : 0.0f;
    if (!valid || jump > std::max(1.0f, 120.0f * dt) || turn > 0.6f || offPath > 0.25f + 60.0f * dt * dt) {
        valid = true;
        prevAnchor = anchor;
        prevRot = in.rot;
        velF = accF = angVelF = angAccF = Vec3::sZero();
        groundS = in.ground;
        vertSlow = 0.0f;
        warm = 2;
        for (Spring& s : dof) s.Set(0.0f);
        dof[Lean].Set(base.lean);
        dof[OutL].Set(base.legOut[0]);
        dof[OutR].Set(base.legOut[1]);
        for (int k = 0; k < 3; ++k) {
            hipsS[k].Set(base.hips[k] - base.trickHips[k]);
            shouldersS[k].Set(base.shoulders[k] - base.trickShoulders[k]);
        }
    }
    const BikeStyleDef& style = GetBikeStyle(bike.Params() ? bike.Params()->visualStyle : 0);

    // --- Lo que mide el cuerpo: aceleración y giro de la moto (diferencias de su transformación, filtradas).
    if (dt > 1e-5f && warm > 0) {
        // Recién aparecido: la velocidad que trae, sin aceleración (si no, de 0 a la velocidad de golpe parecía
        // un tirón de 50 m/s²).
        Quat dq = (prevRot.Conjugated() * in.rot).Normalized();
        if (dq.GetW() < 0.0f) dq = -dq;
        Vec3 axis;
        float angle;
        dq.GetAxisAngle(axis, angle);
        velF = (anchor - prevAnchor) / dt;
        angVelF = angle > 1e-6f ? axis * (angle / dt) : Vec3::sZero();
        accF = angAccF = Vec3::sZero();
        --warm;
    } else if (dt > 1e-5f) {
        const Vec3 vel = (anchor - prevAnchor) / dt;
        const Vec3 velPrev = velF;
        velF += (vel - velF) * (1.0f - std::exp(-dt / tv));
        accF += ((velF - velPrev) / dt - accF) * (1.0f - std::exp(-dt / ta));
        Quat dq = (prevRot.Conjugated() * in.rot).Normalized();
        if (dq.GetW() < 0.0f) dq = -dq;
        Vec3 axis;
        float angle;
        dq.GetAxisAngle(axis, angle);
        const Vec3 w = angle > 1e-6f ? axis * (angle / dt) : Vec3::sZero();   // local (de la moto)
        const Vec3 wPrev = angVelF;
        angVelF += (w - angVelF) * (1.0f - std::exp(-dt / tv));
        angAccF += ((angVelF - wPrev) / dt - angAccF) * (1.0f - std::exp(-dt / ta));
    }
    prevAnchor = anchor;
    prevRot = in.rot;
    groundS += (in.ground - groundS) * (1.0f - std::exp(-dt / 0.08f));
    const Quat inv = in.rot.Conjugated();
    // Lo que siente el cuerpo respecto de la pose de siempre (parado en el piso, 1 g hacia abajo de la moto):
    // - de costado, la aceleración "propia" (con la gravedad): en una curva bien inclinada da ~0, el piloto no
    //   se va para afuera; sí cuando la moto se tumba o se endereza más rápido que la curva;
    // - adelante, la aceleración de la moto a lo largo (sin la gravedad: en una subida no se tira atrás);
    // - arriba, la propia menos 1 g (aterrizajes, pozos; en el aire, -1 g, que los músculos absorben enseguida).
    // En el aire nada empuja al cuerpo de costado ni adelante (cae junto con la moto).
    const Vec3 properL = inv * (accF + Vec3(0.0f, 9.81f, 0.0f));
    Vec3 aL(properL.GetX() * groundS, properL.GetY() - 9.81f, (inv * accF).GetZ() * groundS);
    if (aL.Length() > 60.0f) aL = aL.Normalized() * 60.0f;     // golpes: el cuerpo no se entera de más
    Vec3 alphaL = angAccF;
    if (alphaL.Length() > 80.0f) alphaL = alphaL.Normalized() * 80.0f;
    accLocal = aL;
    angAccLocal = alphaL;
    const Vec3 up = Vec3::sAxisY();
    const Vec3 fwdW = in.rot * Vec3::sAxisZ(), leftW = in.rot * Vec3::sAxisX();
    const float bikePitch = std::asin(mu::Clamp(fwdW.GetY(), -1.0f, 1.0f));      // + trompa arriba
    const float bikeRoll = std::asin(mu::Clamp(leftW.GetY(), -1.0f, 1.0f));      // + tumbada a la derecha
    const float air = 1.0f - groundS;
    const Vec3 flatV(velF.GetX(), 0.0f, velF.GetZ());
    const float speed = flatV.Length();
    const float yawRate = (in.rot * angVelF).GetY();                               // + doblando a la izquierda

    // --- La pose pedida, suavizada: lo que cambia en rampa (cuerpo adelante/atrás, pata afuera) o de golpe
    // (de pie sobre la cola) arranca y frena como un movimiento del cuerpo. Lo del truco (Bike::ApplyTrickPose) ya viene
    // suave (el cuerpo de la física, un resorte) y va aparte: suavizado de nuevo quedaba atrás de las piernas.
    for (int k = 0; k < 3; ++k) {
        hipsS[k].Step(base.hips[k] - base.trickHips[k], 0.0f, kBaseW, 1.0f, dt);
        shouldersS[k].Step(base.shoulders[k] - base.trickShoulders[k], 0.0f, kBaseW, 1.0f, dt);
    }
    dof[Lean].Step(base.lean, 0.0f, kBaseW, 1.0f, dt);

    // --- Cadera: se queda atrás al acelerar, se va adelante al frenar y se hunde al aterrizar. Lo vertical,
    // sólo lo que cambia: una carga sostenida (o la caída libre en el aire) los músculos la sostienen.
    vertSlow += (aL.GetY() - vertSlow) * (1.0f - std::exp(-dt / kVertAdapt));
    const float vert = aL.GetY() - vertSlow;
    const float hipForce[3] = {-kHipsGain[0] * aL.GetX(), -kHipsGain[1] * vert * (vert < 0.0f ? kVertLight : 1.0f), -kHipsGain[2] * aL.GetZ()};
    for (int k = 0; k < 3; ++k) {
        Spring& s = dof[HipsX + k];
        s.Step(0.0f, hipForce[k], kHipsW[k], kHipsZeta[k], dt);
        s.x = mu::Clamp(s.x, -2.0f * kHipsLimit[k], 2.0f * kHipsLimit[k]);
    }
    const float hipsDrop = std::max(0.0f, -dof[HipsY].x);

    // --- Torso (péndulo sobre la cadera): la aceleración lo tira atrás o adelante; el giro de la moto no lo
    // acompaña al instante (inercia); se dobla al absorber; hacia adentro en la curva; en el aire busca la
    // vertical (con el whip la moto se acuesta y el cuerpo no).
    const float centripetal = mu::Clamp(yawRate * speed / 9.81f, -1.2f, 1.2f);   // g de curva (+ izquierda)
    const float pitchTarget = kFold * hipsDrop + kAirUpright * air * bikePitch;
    const float rollTarget = kInward * centripetal * groundS + kAirUpright * air * bikeRoll;
    // Agachado sobre el tanque (carreras, rápido) se sostiene con las rodillas: acelerando no se levanta.
    const float tucked = style.tuck * mu::Smoothstep(8.0f, 25.0f, speed);
    const float pitchAcc = -kPitchPerAcc * aL.GetZ() * (aL.GetZ() > 0.0f ? 1.0f - 0.7f * tucked : 1.0f);
    // Frenando fuerte agachado (carreras): sale de atrás de la cúpula, el pecho sube y la cola se va contra el tanque; los
    // brazos sostienen. Antes seguía acostado sobre el tanque y la inercia lo tiraba más adelante: el casco contra la cúpula.
    dof[SitUp].Step(tucked * mu::Smoothstep(kSitUpDecel[0], kSitUpDecel[1], -aL.GetZ()) * groundS, 0.0f, kSitUpW, 1.0f, dt);
    dof[TorsoPitch].Step(pitchTarget, pitchAcc - kTorsoInertia * alphaL.GetX(), kTorsoW, kTorsoZeta, dt);
    dof[TorsoRoll].Step(rollTarget, -kRollPerAcc * aL.GetX() + kTorsoInertia * alphaL.GetZ(), kTorsoW, kTorsoZeta, dt);
    for (int d : {TorsoPitch, TorsoRoll}) dof[d].x = mu::Clamp(dof[d].x, -1.0f, 1.0f);

    // --- Cola afuera: el ángulo entre la moto y la velocidad (sobre el piso). Con la cola de costado (freno
    // de mano, derrape) saca la pierna de adentro, la de la punta de la moto.
    const Vec3 vL = inv * velF;
    drift = 0.0f;
    float driftOut[2] = {0.0f, 0.0f};
    if (speed > 1.0f && vL.GetZ() > 0.0f) {
        drift = std::atan2(-vL.GetX(), vL.GetZ());                                  // + la trompa a la izquierda de la velocidad
        const float amount = mu::Smoothstep(mu::Rad(12.0f), mu::Rad(24.0f), std::fabs(drift)) * mu::Smoothstep(2.5f, 4.5f, speed) *
                             (1.0f - mu::Smoothstep(18.0f, 24.0f, speed)) * mu::Smoothstep(0.6f, 0.9f, groundS) * (style.hangOff > 0.0f ? 0.0f : 1.0f);
        driftOut[drift > 0.0f ? 0 : 1] = amount;
    }
    float outT[2];
    for (int i = 0; i < 2; ++i) {
        outT[i] = std::max(base.legOut[i], driftOut[i]);
        Spring& s = dof[OutL + i];
        s.Step(outT[i], 0.0f, kOutW, 1.0f, dt);
        s.x = mu::Clamp(s.x, 0.0f, 1.0f);
    }

    // --- Cabeza: mira hacia donde va (la velocidad, o adelante si va despacio o para atrás), adelantada a lo que
    // gira la curva; abajo hacia la pista en el piso y hacia el aterrizaje en el aire; casi nivelada.
    {
        Vec3 dir = Vec3(fwdW.GetX(), 0.0f, fwdW.GetZ()).NormalizedOr(Vec3::sAxisZ());
        if (speed > 1.0f && flatV.Dot(dir) > 0.3f * speed) {
            // Con la cola afuera la velocidad apunta hacia afuera de la curva: ahí mira la trompa (la salida).
            const float w = mu::Smoothstep(2.0f, 6.0f, speed) * (1.0f - mu::Smoothstep(mu::Rad(5.0f), mu::Rad(20.0f), std::fabs(drift)));
            dir = (dir * (1.0f - w) + flatV.Normalized() * w).NormalizedOr(dir);
        }
        const float ahead = mu::Clamp(yawRate * kHeadLookAhead, -0.6f, 0.6f) * groundS;
        dir = Quat::sRotation(up, ahead) * dir;
        const float airPitch = mu::Clamp(std::atan2(velF.GetY(), std::max(speed, 3.0f)) * 0.6f, -0.7f, 0.15f);
        const float lookPitch = -0.2f * groundS + (airPitch - 0.15f) * air;
        const Vec3 lookW = dir * std::cos(lookPitch) + up * std::sin(lookPitch);
        const Vec3 lookL = inv * lookW;
        // Lo que pide la pose (el nac-nac mira atrás, por arriba del hombro): reemplaza a la pista lo que gira.
        const float extra = mu::Clamp(std::fabs(base.lookYaw) / 1.2f, 0.0f, 1.0f);
        const float yawT = mu::Clamp(std::atan2(lookL.GetX(), lookL.GetZ()), -1.0f, 1.0f) * (1.0f - extra) + base.lookYaw;
        const float pitchT = mu::Clamp(std::asin(mu::Clamp(lookL.GetY(), -1.0f, 1.0f)) * (1.0f - extra) - base.lookDown, -0.9f, 0.35f);
        const float rollT = mu::Clamp(-kHeadLevel * bikeRoll, -0.4f, 0.4f);   // + cabeza hacia la derecha (rotación sobre +Z)
        dof[HeadYaw].Step(yawT, 0.0f, kHeadW, kHeadZeta, dt);
        dof[HeadPitch].Step(pitchT, 0.0f, kHeadW, kHeadZeta, dt);
        dof[HeadRoll].Step(rollT, 0.0f, kHeadW, kHeadZeta, dt);
        const float yaw = dof[HeadYaw].x, pitch = dof[HeadPitch].x;
        look = Vec3(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
        lookUp = Quat::sRotation(look, dof[HeadRoll].x) * Vec3::sAxisY();
    }

    // --- La pose resultante.
    RiderPose p = base;
    p.lean = dof[Lean].x;
    const float sitUp = mu::Clamp(dof[SitUp].x, 0.0f, 1.0f);
    const Vec3 hipsBase = Vec3(hipsS[0].x, hipsS[1].x, hipsS[2].x) + base.trickHips + Vec3(0.0f, 0.0f, kSitUpHips) * sitUp;
    const Vec3 shouldersBase = Vec3(shouldersS[0].x, shouldersS[1].x, shouldersS[2].x) + base.trickShoulders + kSitUpShoulders * sitUp;
    const float hy = dof[HipsY].x;
    const Vec3 hipsOff(SoftLimit(dof[HipsX].x, kHipsLimit[0]), hy < 0.0f ? SoftLimit(hy, kHipsLimit[1]) : SoftLimit(hy, kHipsUp),
                       SoftLimit(dof[HipsZ].x, kHipsLimit[2]));
    // La pata afuera por el derrape (lo que agrega a la de la pose): sentado adelante, como en la pose.
    const float extraOut = std::max(0.0f, std::max(dof[OutL].x, dof[OutR].x) - std::max(base.legOut[0], base.legOut[1]));
    const float outSide = dof[OutL].x >= dof[OutR].x ? 1.0f : -1.0f;              // +X = izquierda
    p.hips = hipsBase + hipsOff + Vec3(-0.03f * outSide, -0.01f, 0.19f) * extraOut;
    Vec3 torso = shouldersBase - hipsBase + Vec3(-0.04f * outSide, -0.01f, -0.11f) * extraOut;   // los hombros, como en la pose (-0.07, -0.02, 0.08)
    // En un truco (estirado, una pierna cruzada) las reacciones del torso valen menos: el cuerpo está en otra pose y lo
    // sostienen los brazos.
    const float react = 1.0f - 0.6f * base.trick;
    const float tp = SoftLimit(dof[TorsoPitch].x, kPitchLimit) * react, tr = SoftLimit(dof[TorsoRoll].x, kRollLimit) * react;
    torso = Quat::sRotation(Vec3::sAxisX(), tp) * torso;                          // + adelante
    torso = Quat::sRotation(Vec3::sAxisZ(), -tr) * torso;                         // + los hombros a la izquierda
    p.shoulders = p.hips + torso;
    p.head = p.shoulders + (base.head - base.shoulders);
    for (int i = 0; i < 2; ++i) {
        const float side = i == 0 ? 1.0f : -1.0f;
        p.hip[i] = p.hips + (base.hip[i] - base.hips);
        p.shoulder[i] = p.shoulders + (base.shoulder[i] - base.shoulders);
        p.elbow[i] = (p.shoulder[i] + p.grip[i]) * 0.5f + Vec3(0.12f * side, -0.05f, -0.06f);
        // Pierna afuera (la de la pose o la del derrape), con la misma forma que RiderPoseLocal: sale por afuera
        // de los plásticos en arco hasta el costado de la rueda delantera.
        const float a = dof[OutL + i].x;
        p.legOut[i] = a;
        if ((a > 1e-3f || base.legOut[i] > 0.0f) && base.legFree[i] < 1.0f) {
            const Vec3 peg(style.peg.GetX() * side, style.peg.GetY(), style.peg.GetZ());
            const Vec3 ankleOut(0.34f * side, -0.28f, 0.64f);
            p.ankle[i] = peg + Vec3(0, 0.06f, 0);
            p.ankle[i] = p.ankle[i] + (ankleOut - p.ankle[i]) * a + Vec3(0.07f * side, 0.04f, 0.0f) * std::sin(mu::kPi * a);
            p.foot[i] = p.ankle[i] + Vec3(0.0f, -0.04f + 0.07f * a, 0.04f + 0.03f * a);
            const Vec3 bent = (p.hip[i] + p.ankle[i]) * 0.5f + Vec3(0.05f * side, 0.07f, 0.02f);
            p.knee[i] = base.knee[i] + (bent - base.knee[i]) * a;
            // En un truco el pie va por su curva (la pata afuera que quedaba del piso se va con él).
            const float f = base.legFree[i];
            p.ankle[i] = p.ankle[i] + (base.ankle[i] - p.ankle[i]) * f;
            p.foot[i] = p.foot[i] + (base.foot[i] - p.foot[i]) * f;
            p.knee[i] = p.knee[i] + (base.knee[i] - p.knee[i]) * f;
        }
    }
    return p;
}

// ---------------------------------------------------------------------------------------------- modelo

int RiderModel::Bone(const char* name) const
{
    for (int i = 0; i < model.boneCount; ++i)
        if (std::strcmp(model.bones[i].name, name) == 0) return i;
    return -1;
}

bool RiderModel::Load(const std::vector<std::string>& paths)
{
    for (const std::string& path : paths) {
        if (!FileExists(path.c_str())) continue;
        model = LoadModel(path.c_str());
        modelPath = path;
        break;
    }
    if (model.meshCount < 1 || model.boneCount < 1 || !model.meshes[0].boneIds || !model.meshes[0].animVertices) {
        if (model.meshCount > 0) UnloadModel(model);
        model = Model{};
        return false;
    }

    hips = Bone("Hips");
    spine = Bone("Spine02");
    spineChain[0] = spine;                   // columna de abajo hacia arriba (las que falten, -1)
    spineChain[1] = Bone("Spine01");
    spineChain[2] = Bone("Spine");
    neck = Bone("neck");
    head = Bone("Head");
    const char* sides[2] = {"Left", "Right"};                  // [0] = izquierda (+X), como RiderPose
    for (int i = 0; i < 2; ++i) {
        auto bone = [&](const char* suffix) { return Bone((std::string(sides[i]) + suffix).c_str()); };
        clavicle[i] = bone("Shoulder");                        // opcional
        arm[i] = bone("Arm");
        foreArm[i] = bone("ForeArm");
        hand[i] = bone("Hand");
        upLeg[i] = bone("UpLeg");
        leg[i] = bone("Leg");
        foot[i] = bone("Foot");
        toe[i] = bone("ToeBase");
    }
    for (int b : {hips, spine, neck, head, arm[0], arm[1], foreArm[0], foreArm[1], hand[0], hand[1], upLeg[0], upLeg[1],
                  leg[0], leg[1], foot[0], foot[1], toe[0], toe[1]}) {
        if (b < 0) {
            TraceLog(LOG_WARNING, "piloto: el modelo no tiene los huesos esperados, se usa el generado");
            UnloadModel(model);
            model = Model{};
            return false;
        }
    }

    const int n = model.boneCount;
    bindPos.resize(n);
    parent.resize(n);
    for (int i = 0; i < n; ++i) {
        bindPos[i] = model.bindPose[i].translation;
        parent[i] = model.bones[i].parent;
    }
    // Padres antes que hijos.
    std::vector<int> depth(n, 0);
    for (int i = 0; i < n; ++i)
        for (int p = parent[i]; p >= 0; p = parent[p]) ++depth[i];
    order.resize(n);
    for (int i = 0; i < n; ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return depth[a] < depth[b]; });

    // raylib aplica a los vértices la escala del nodo del esqueleto (0.01 en exportaciones de
    // Blender en centímetros) pero no a la pose de los huesos: se deshace para que coincidan.
    Mesh& m = model.meshes[0];
    float maxY = 0.0f;
    for (int v = 0; v < m.vertexCount; ++v) maxY = std::max(maxY, m.vertices[v * 3 + 1]);
    const float scaleFix = (maxY < 0.2f * bindPos[hips].y && model.bindPose[hips].scale.y > 0.0f) ? 1.0f / model.bindPose[hips].scale.y : 1.0f;
    bindVerts.resize(m.vertexCount);
    bindNormals.resize(m.vertexCount);
    for (int v = 0; v < m.vertexCount; ++v) {
        bindVerts[v] = Vector3Scale({m.vertices[v * 3], m.vertices[v * 3 + 1], m.vertices[v * 3 + 2]}, scaleFix);
        bindNormals[v] = m.normals ? Vector3Normalize({m.normals[v * 3], m.normals[v * 3 + 1], m.normals[v * 3 + 2]}) : Vector3{0.0f, 1.0f, 0.0f};
    }

    // Parte del ragdoll de cada hueso (los que no están en la lista siguen a su padre).
    partOf.assign(n, -1);
    auto assign = [&](int b, int part) { if (b >= 0) partOf[b] = part; };
    assign(hips, RiderPart::Pelvis);
    assign(spine, RiderPart::Torso);
    assign(head, RiderPart::Head);
    for (int i = 0; i < 2; ++i) {
        assign(arm[i], RiderPart::UpperArm + i);
        assign(foreArm[i], RiderPart::Forearm + i);
        assign(upLeg[i], RiderPart::Thigh + i);
        assign(leg[i], RiderPart::Shin + i);
    }
    for (int b : order)
        if (partOf[b] < 0) partOf[b] = parent[b] >= 0 ? partOf[parent[b]] : RiderPart::Pelvis;

    texture = model.materials[model.meshMaterial[0]].maps[MATERIAL_MAP_DIFFUSE].texture;
    BuildSuitMask();                         // antes de los mipmaps: lee el nivel 0 de la textura
    GenTextureMipmaps(&texture);
    SetTextureFilter(texture, TEXTURE_FILTER_TRILINEAR);

    // Pesos propios (se les suman los dedos virtuales).
    realBones = n;
    skinBone.assign(m.boneIds, m.boneIds + m.vertexCount * 4);
    skinWeight.assign(m.boneWeights, m.boneWeights + m.vertexCount * 4);
    curlAngle.assign(n, 0.0f);
    curlAxis.assign(n, Vector3{0.0f, 0.0f, 0.0f});
    curlSide.assign(n, 0);
    for (int i = 0; i < 2; ++i) BuildGrip(i);
    BuildProbes();

    pos = bindPos;
    delta.assign(bindPos.size(), QuaternionIdentity());
    shape = BikeShape{};
    loaded = true;
    TraceLog(LOG_INFO, "piloto: modelo con %d huesos (+%d de dedos) y %d vértices", n, (int)bindPos.size() - n, m.vertexCount);
    return true;
}

// Arma los dedos de una mano. Mide la mano en reposo en sus propios ejes (s a lo largo desde la
// muñeca, t a lo ancho hacia el pulgar, u hacia la palma): nudillos al 65% del largo, el pulgar es
// lo que sobresale a un costado de la palma, y la flexión de cada falange se calcula para que el
// dedo envuelva un puño del radio del manubrio apoyado en la palma, 2 cm antes de los nudillos
// (como en un agarre de fuerza: así el pulgar llega a cerrarlo por el otro lado).
void RiderModel::BuildGrip(int side)
{
    constexpr float kBarRadius = 0.0175f;    // puño de goma del manubrio (BikeMeshes: Cockpit)
    const Mesh& m = model.meshes[0];
    const int hb = hand[side];
    const Vector3 W = bindPos[hb];
    auto handWeight = [&](int v) {
        float w = 0.0f;
        for (int k = 0; k < 4; ++k)
            if (skinBone[v * 4 + k] == hb) w += skinWeight[v * 4 + k];
        return w;
    };

    // Eje a lo largo: de la muñeca al centro de las puntas de los dedos (el 10% más lejano).
    const Vector3 fore = Vector3Normalize(Vector3Subtract(W, bindPos[foreArm[side]]));
    std::vector<std::pair<float, int>> byS;
    for (int v = 0; v < m.vertexCount; ++v)
        if (handWeight(v) >= 0.5f) byS.push_back({Vector3DotProduct(Vector3Subtract(bindVerts[v], W), fore), v});
    if (byS.size() < 100) return;
    std::sort(byS.begin(), byS.end());
    Vector3 tip = {0.0f, 0.0f, 0.0f};
    const int tipCount = (int)byS.size() / 10;
    for (int i = (int)byS.size() - tipCount; i < (int)byS.size(); ++i) tip = Vector3Add(tip, bindVerts[byS[i].second]);
    tip = Vector3Scale(tip, 1.0f / (float)tipCount);
    const Vector3 A = Vector3Normalize(Vector3Subtract(tip, W));

    // A lo ancho: la dirección de más extensión perpendicular a A (análisis de componentes principales).
    const Vector3 e1 = Vector3Normalize(Vector3Perpendicular(A)), e2 = Vector3CrossProduct(A, e1);
    float mx = 0.0f, my = 0.0f, cxx = 0.0f, cxy = 0.0f, cyy = 0.0f;
    for (const auto& sv : byS) {
        const Vector3 d = Vector3Subtract(bindVerts[sv.second], W);
        mx += Vector3DotProduct(d, e1);
        my += Vector3DotProduct(d, e2);
    }
    mx /= (float)byS.size();
    my /= (float)byS.size();
    for (const auto& sv : byS) {
        const Vector3 d = Vector3Subtract(bindVerts[sv.second], W);
        const float a = Vector3DotProduct(d, e1) - mx, b = Vector3DotProduct(d, e2) - my;
        cxx += a * a;
        cxy += a * b;
        cyy += b * b;
    }
    const float ang = 0.5f * std::atan2(2.0f * cxy, cxx - cyy);
    Vector3 K = Vector3Add(Vector3Scale(e1, std::cos(ang)), Vector3Scale(e2, std::sin(ang)));
    Vector3 P = Vector3CrossProduct(A, K);
    if (P.x * W.x > 0.0f) P = Vector3Negate(P);                     // la palma mira al centro del cuerpo
    K = Vector3CrossProduct(P, A);

    struct HV { int v; float s, t, u; };
    std::vector<HV> hv;
    float sMax = 0.0f;
    for (const auto& sv : byS) {
        const Vector3 d = Vector3Subtract(bindVerts[sv.second], W);
        hv.push_back({sv.second, Vector3DotProduct(d, A), Vector3DotProduct(d, K), Vector3DotProduct(d, P)});
        sMax = std::max(sMax, hv.back().s);
    }
    const float sK = 0.65f * sMax;                                   // nudillos

    // Pulgar: el costado donde la mano, entre la muñeca y los nudillos, sobresale más que la palma
    // cerca de la muñeca. Se da vuelta K para que quede en +t.
    auto range = [&](float s0, float s1, float& lo, float& hi) {
        lo = 1e9f;
        hi = -1e9f;
        for (const HV& h : hv)
            if (h.s >= s0 && h.s <= s1) {
                lo = std::min(lo, h.t);
                hi = std::max(hi, h.t);
            }
    };
    float wLo, wHi, mLo, mHi;
    range(0.08f * sMax, 0.3f * sMax, wLo, wHi);
    range(0.35f * sMax, 0.62f * sMax, mLo, mHi);
    if (wLo - mLo > mHi - wHi) {
        K = Vector3Negate(K);
        for (HV& h : hv) h.t = -h.t;
        std::swap(wLo, wHi);
        wLo = -wLo;
        wHi = -wHi;
    }
    const float tEdge = wHi;                                         // borde de la palma del lado del pulgar

    auto smooth = [](float e0, float e1, float x) {
        const float k = mu::Clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
        return k * k * (3.0f - 2.0f * k);
    };
    auto thumbness = [&](const HV& h) {
        return smooth(tEdge, tEdge + 0.008f, h.t) * (1.0f - smooth(sK - 0.005f, sK + 0.008f, h.s)) * smooth(0.25f * sMax, 0.35f * sMax, h.s);
    };
    // Promedio de u (el centro del dedo o del pulgar) cerca de una altura s.
    auto centerU = [&](float s, bool thumb) {
        float sum = 0.0f;
        int count = 0;
        for (const HV& h : hv)
            if (std::fabs(h.s - s) < 0.004f && ((thumbness(h) > 0.5f) == thumb) && (thumb || h.t < tEdge)) {
                sum += h.u;
                ++count;
            }
        return count > 0 ? sum / (float)count : 0.0f;
    };

    // Dedos: articulaciones al 0, 42 y 72% del largo del dedo; el ancho que ocupan los cuatro.
    const float fs[4] = {sK, sK + 0.42f * (sMax - sK), sK + 0.72f * (sMax - sK), sMax - 0.004f};
    float tLo = 1e9f, tHi = -1e9f, uPalm = -1e9f, uLo = 1e9f, uHi = -1e9f;
    int thumbCount = 0;
    float thumbS0 = 1e9f, thumbS1 = -1e9f;
    for (const HV& h : hv) {
        if (thumbness(h) > 0.5f) {
            ++thumbCount;
            thumbS0 = std::min(thumbS0, h.s);
            thumbS1 = std::max(thumbS1, h.s);
            continue;
        }
        if (h.s > sK + 0.01f) {
            tLo = std::min(tLo, h.t);
            tHi = std::max(tHi, h.t);
        }
        if (h.s > sK - 0.02f && h.s < sK && h.t < tEdge) uPalm = std::max(uPalm, h.u);
        if (std::fabs(h.s - fs[1]) < 0.003f) {
            uLo = std::min(uLo, h.u);
            uHi = std::max(uHi, h.u);
        }
    }
    if (thumbCount < 10 || tHi <= tLo) return;
    const float halfFinger = std::min(0.012f, 0.5f * (uHi - uLo));

    // Flexión: cada falange gira (hacia la palma) hasta que la articulación siguiente queda a la
    // distancia del puño; la última, hasta que la punta llega.
    const Vector2 channel = {sK - 0.02f, uPalm + kBarRadius - 0.002f};
    const float wrap = kBarRadius + halfFinger;
    Vector2 J[4];
    for (int j = 0; j < 4; ++j) J[j] = {fs[j], centerU(fs[j], false)};
    auto rotate2 = [](Vector2 p, Vector2 c, float a) {             // +a lleva +s hacia +u
        const Vector2 d = Vector2Subtract(p, c);
        return Vector2Add(c, {d.x * std::cos(a) - d.y * std::sin(a), d.x * std::sin(a) + d.y * std::cos(a)});
    };
    float fingerCurl[3];
    for (int j = 0; j < 3; ++j) {
        float best = 0.0f, bestDist = 1e9f;
        for (int deg = 0; deg <= 110; ++deg) {
            const float a = mu::Rad((float)deg);
            const float dist = Vector2Distance(rotate2(J[j + 1], J[j], a), channel);
            if (dist < bestDist) {
                bestDist = dist;
                best = a;
            }
            if (dist <= wrap) break;
        }
        fingerCurl[j] = best + (j > 0 ? mu::Rad(8.0f) : 0.0f);         // un poco más: aprieta la goma
        for (int k = j + 1; k < 4; ++k) J[k] = rotate2(J[k], J[j], best);
    }

    // Huesos virtuales: pivotes sobre la línea media de los dedos y del pulgar.
    const float tMid = 0.5f * (tLo + tHi);
    auto point = [&](float s, float t, float u) {
        return Vector3Add(W, Vector3Add(Vector3Scale(A, s), Vector3Add(Vector3Scale(K, t), Vector3Scale(P, u))));
    };
    const Vector3 axis = Vector3Normalize(Vector3CrossProduct(A, P));   // girar +: de "a lo largo" hacia la palma
    auto addBone = [&](int par, Vector3 at, float curl) {
        const int b = (int)bindPos.size();
        bindPos.push_back(at);
        parent.push_back(par);
        partOf.push_back(partOf[par]);
        order.push_back(b);
        curlAngle.push_back(curl);
        curlAxis.push_back(axis);
        curlSide.push_back(side);
        return b;
    };
    int finger[3];
    int par = hb;
    for (int j = 0; j < 3; ++j) par = finger[j] = addBone(par, point(fs[j], tMid, centerU(fs[j], false)), fingerCurl[j]);
    // Pulgar: sale hacia afuera de la palma rodeando el puño del lado de la muñeca y se dobla de
    // vuelta sobre él, hasta tocar los dedos.
    const float sT[2] = {0.3f * sMax, 0.5f * (thumbS0 + thumbS1)};
    const int thumb0 = addBone(hb, point(sT[0], tEdge, centerU(sT[0] + 0.01f, true)), mu::Rad(35.0f));
    const int thumb1 = addBone(thumb0, point(sT[1], tEdge + 0.02f, centerU(sT[1], true)), mu::Rad(-20.0f));

    // Pesos: palma -> falanges con transiciones suaves en cada articulación.
    for (const HV& h : hv) {
        if (handWeight(h.v) < 0.98f) continue;                        // la muñeca queda como estaba
        const float tf = thumbness(h);
        const float ff = (1.0f - tf) * smooth(sK - 0.006f, sK + 0.006f, h.s);
        const float a2 = smooth(fs[1] - 0.004f, fs[1] + 0.004f, h.s), a3 = smooth(fs[2] - 0.004f, fs[2] + 0.004f, h.s);
        const float aT = smooth(sT[1] - 0.005f, sT[1] + 0.005f, h.s);
        std::pair<float, int> w[6] = {{1.0f - ff - tf, hb}, {ff * (1.0f - a2), finger[0]}, {ff * (a2 - a3), finger[1]},
                                      {ff * a3, finger[2]}, {tf * (1.0f - aT), thumb0}, {tf * aT, thumb1}};
        std::sort(w, w + 6, [](const auto& a, const auto& b) { return a.first > b.first; });
        float total = 0.0f;
        for (int k = 0; k < 4; ++k) total += std::max(w[k].first, 0.0f);
        for (int k = 0; k < 4; ++k) {
            skinBone[h.v * 4 + k] = w[k].second;
            skinWeight[h.v * 4 + k] = std::max(w[k].first, 0.0f) / std::max(total, 1e-6f);
        }
    }

    Grip& g = grip[side];
    g.across = K;
    g.along = A;
    g.palm = P;
    g.channel = point(channel.x, tMid, channel.y);
    g.valid = true;
    TraceLog(LOG_INFO, "piloto: mano %s con dedos (flexión %.0f/%.0f/%.0f°)", side == 0 ? "izquierda" : "derecha",
             mu::Deg(fingerCurl[0]), mu::Deg(fingerCurl[1]), mu::Deg(fingerCurl[2]));
}

// Giro de la mano (respecto del reposo) para agarrar el manubrio: el ancho de la mano a lo largo
// del manubrio con el pulgar hacia adentro, y la mano en la dirección del antebrazo (muñeca derecha).
Quaternion RiderModel::GripRotation(int side, Vector3 barInward, Vector3 forearmDir) const
{
    const Grip& g = grip[side];
    const Vector3 D = Vector3Normalize(barInward);
    Vector3 A = Vector3Subtract(forearmDir, Vector3Scale(D, Vector3DotProduct(forearmDir, D)));
    A = Vector3Length(A) > 1e-4f ? Vector3Normalize(A) : Vector3Perpendicular(D);
    Vector3 P = Vector3CrossProduct(D, A);
    if (Vector3DotProduct(Vector3CrossProduct(g.across, g.along), g.palm) < 0.0f) P = Vector3Negate(P);   // misma mano (sin espejar)
    auto frame = [](Vector3 x, Vector3 y, Vector3 z) {
        Matrix mm = MatrixIdentity();
        mm.m0 = x.x; mm.m1 = x.y; mm.m2 = x.z;
        mm.m4 = y.x; mm.m5 = y.y; mm.m6 = y.z;
        mm.m8 = z.x; mm.m9 = z.y; mm.m10 = z.z;
        return mm;
    };
    // R = mundo * reposo^T: lleva los ejes de la mano en reposo a los pedidos.
    const Matrix R = MatrixMultiply(MatrixTranspose(frame(g.across, g.along, g.palm)), frame(D, A, P));
    return QuaternionNormalize(QuaternionFromMatrix(R));
}

void RiderModel::CurlFingers()
{
    for (int b = realBones; b < (int)bindPos.size(); ++b) {
        const int pa = parent[b];
        pos[b] = Vector3Add(pos[pa], Vector3RotateByQuaternion(Vector3Subtract(bindPos[b], bindPos[pa]), delta[pa]));
        delta[b] = QuaternionMultiply(QuaternionFromAxisAngle(Vector3RotateByQuaternion(curlAxis[b], delta[pa]), curlAngle[b] * FingerGrip(b)), delta[pa]);
    }
}

void RiderModel::Unload()
{
    if (loaded) UnloadModel(model);          // también libera la textura del material
    if (suitMask.id) {                       // la máscara es compartida: se suelta con el último que la usa
        auto it = SuitMasks().find(modelPath);
        if (it != SuitMasks().end() && --it->second.users <= 0) {
            UnloadTexture(it->second.mask);
            SuitMasks().erase(it);
        }
        suitMask = Texture2D{};
    }
    model = Model{};
    loaded = false;
}

// ------------------------------------------------------------------------------------ colores del traje
// La textura del traje (un atlas pintado) tiene dos colores de equipo: el rojo (principal) y el azul marino con sus
// tonos más claros (secundario); el resto (blanco, gris, piel, negro y el visor violeta) no se toca. Por texel, cuánto
// es de cada color sale del tono y la saturación con bordes suaves (w), y cuán claro es respecto del color medio de la
// zona (k = luma / luma media). La máscara guarda w·k / 2 en R (principal) y G (secundario), con los mismos mipmaps y
// el mismo filtro que la textura: en el shader, color + máscara·(elegido − medio) cambia exactamente la parte de ese
// color que trae el texel ya filtrado (un borde rojo y blanco cambia sólo su mitad roja: sin halos), conservando las
// sombras pintadas (k) y los detalles (lo que el texel se aparta del color medio). El archivo no se toca.
std::map<std::string, RiderModel::SharedMask>& RiderModel::SuitMasks()
{
    static std::map<std::string, SharedMask> masks;
    return masks;
}

void RiderModel::BuildSuitMask()
{
    auto it = SuitMasks().find(modelPath);
    if (it != SuitMasks().end()) {
        ++it->second.users;
        suitMask = it->second.mask;
        suitRef[0] = it->second.ref[0];
        suitRef[1] = it->second.ref[1];
        return;
    }
    Image img = LoadImageFromTexture(texture);
    if (!img.data || img.width <= 0) return;
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    const int count = img.width * img.height;
    const unsigned char* px = (const unsigned char*)img.data;
    std::vector<float> wa(count), wb(count);
    auto smooth = [](float e0, float e1, float x) {
        const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };
    double sum[2][4] = {};                   // color medio de cada zona (r, g, b y el peso)
    for (int i = 0; i < count; ++i) {
        const float r = px[i * 4] / 255.0f, g = px[i * 4 + 1] / 255.0f, b = px[i * 4 + 2] / 255.0f;
        const float mx = std::max({r, g, b}), mn = std::min({r, g, b}), c = mx - mn;
        const float sat = mx > 1e-4f ? c / mx : 0.0f;
        float hue = 0.0f;                    // grados
        if (c > 1e-4f) {
            if (mx == r) hue = 60.0f * std::fmod((g - b) / c + 6.0f, 6.0f);
            else if (mx == g) hue = 60.0f * ((b - r) / c + 2.0f);
            else hue = 60.0f * ((r - g) / c + 4.0f);
        }
        auto hueNear = [&](float center, float in, float out) {
            const float d = std::fabs(std::fmod(hue - center + 540.0f, 360.0f) - 180.0f);
            return 1.0f - smooth(in, out, d);
        };
        // Rojo: tono 355-15 (también el bordó de las sombras); marino: 205-245 (sin el violeta del visor, 265). Para el color
        // medio, sólo eso; para la máscara, también las sombras pintadas de cada uno: el rojo que se va a magenta (318-355) y el
        // marino que se va a violeta (hasta 258). Sin ellas quedaban líneas magenta entre los paneles de un traje blanco.
        const float core[2] = {hueNear(4.0f, 16.0f, 28.0f) * smooth(0.32f, 0.48f, sat) * smooth(0.16f, 0.26f, mx),
                               hueNear(224.0f, 20.0f, 32.0f) * smooth(0.25f, 0.4f, sat) * smooth(0.07f, 0.14f, mx)};
        wa[i] = std::max(core[0], hueNear(347.0f, 29.0f, 39.0f) * smooth(0.36f, 0.5f, sat) * smooth(0.12f, 0.2f, mx));
        wb[i] = std::max(core[1], hueNear(231.5f, 26.5f, 34.0f) * smooth(0.25f, 0.4f, sat) * smooth(0.07f, 0.14f, mx));
        const float* w = core;
        for (int z = 0; z < 2; ++z) {
            sum[z][0] += w[z] * r;
            sum[z][1] += w[z] * g;
            sum[z][2] += w[z] * b;
            sum[z][3] += w[z];
        }
    }
    float ref[2][3], luma[2];
    for (int z = 0; z < 2; ++z) {
        for (int k = 0; k < 3; ++k) ref[z][k] = sum[z][3] > 0.0 ? (float)(sum[z][k] / sum[z][3]) : 0.5f;
        luma[z] = std::max(1e-3f, 0.2126f * ref[z][0] + 0.7152f * ref[z][1] + 0.0722f * ref[z][2]);
        suitRef[z] = {(unsigned char)std::lround(ref[z][0] * 255.0f), (unsigned char)std::lround(ref[z][1] * 255.0f),
                      (unsigned char)std::lround(ref[z][2] * 255.0f), 255};
    }
    Image mask = GenImageColor(img.width, img.height, BLACK);
    unsigned char* out = (unsigned char*)mask.data;
    // Los texels de borde de la propia textura son mezclas de dos colores (rojo con marino, marino con blanco, rojo con el
    // negro de una costura...): por tono no son ni uno ni otro y quedaban de su color viejo (una línea violeta entre el
    // marino y el rojo en un traje blanco). Si el texel cae cerca del segmento entre dos de los colores de la textura
    // (los dos de equipo, el blanco, el negro y el violeta del visor), se toma esa mezcla: cuánto tiene de cada uno.
    const float pal[5][3] = {{ref[0][0], ref[0][1], ref[0][2]}, {ref[1][0], ref[1][1], ref[1][2]},
                             {0.77f, 0.77f, 0.77f}, {0.09f, 0.09f, 0.11f}, {0.21f, 0.15f, 0.29f}};
    for (int i = 0; i < count; ++i) {
        const float p[3] = {px[i * 4] / 255.0f, px[i * 4 + 1] / 255.0f, px[i * 4 + 2] / 255.0f};
        const float l = 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
        float a = wa[i] * l / luma[0], b = wb[i] * l / luma[1];   // por tono, con el brillo
        float best = 0.05f;                                        // más lejos que esto de toda mezcla: queda lo de arriba
        for (int u = 0; u < 5; ++u)
            for (int v = u + 1; v < 5; ++v) {
                float d[3], e[3], dd = 0.0f, de = 0.0f;
                for (int c = 0; c < 3; ++c) {
                    d[c] = pal[v][c] - pal[u][c];
                    e[c] = p[c] - pal[u][c];
                    dd += d[c] * d[c];
                    de += d[c] * e[c];
                }
                const float t = std::clamp(de / std::max(dd, 1e-6f), 0.0f, 1.0f);
                float dist = 0.0f;
                for (int c = 0; c < 3; ++c) dist += (e[c] - t * d[c]) * (e[c] - t * d[c]);
                dist = std::sqrt(dist);
                if (dist >= best) continue;
                best = dist;
                a = (u == 0 ? 1.0f - t : 0.0f);                    // u < v: el rojo sólo puede ser u
                b = (u == 1 ? 1.0f - t : 0.0f) + (v == 1 ? t : 0.0f);
            }
        out[i * 4] = (unsigned char)std::lround(std::clamp(a * 0.5f, 0.0f, 1.0f) * 255.0f);
        out[i * 4 + 1] = (unsigned char)std::lround(std::clamp(b * 0.5f, 0.0f, 1.0f) * 255.0f);
    }
    UnloadImage(img);
    suitMask = LoadTextureFromImage(mask);
    UnloadImage(mask);
    GenTextureMipmaps(&suitMask);
    SetTextureFilter(suitMask, TEXTURE_FILTER_TRILINEAR);
    SuitMasks()[modelPath] = SharedMask{suitMask, {suitRef[0], suitRef[1]}, 1};
    TraceLog(LOG_INFO, "piloto: máscara de colores del traje (principal %d,%d,%d; secundario %d,%d,%d)", suitRef[0].r, suitRef[0].g, suitRef[0].b,
             suitRef[1].r, suitRef[1].g, suitRef[1].b);
}

void RiderModel::PoseOnBike(const RiderPose& p)
{
    gripAmount = 1.0f;
    for (int i = 0; i < 2; ++i) handOpen[i] = p.handFree[i];   // truco: la mano suelta se abre
    throttleS += (p.throttle - throttleS) * (1.0f - std::exp(-14.0f * GetFrameTime()));
    Fit fit;
    barA = V(p.grip[0]);
    barB = V(p.grip[1]);
    auto solve = [&]() { return lastReach = SolveBody(p, fit); };
    // Las manos no se sueltan del manubrio. Si con la pose no llegan:
    //   1. con el cuerpo corrido de costado (whip, cuerpo de costado) el cuerpo vuelve hacia el medio lo justo: con la
    //      moto acostada en el aire se queda con las manos en los puños, no se va 30 cm al costado (ver docs/PILOTO.md,
    //      "Whip con las manos en el manubrio");
    //   2. el torso se inclina más hacia adelante (hasta kMaxExtraPitch);
    //   3. la cadera se acerca al manubrio: tirado atrás queda la cola atrás y los brazos estirados, no acostado sobre el
    //      tanque.
    // (Antes que todo eso, en SolveBody, la clavícula adelanta el hombro.)
    auto search = [&](float hiValue, float& value) {        // el valor mínimo con el que las manos llegan
        float lo = 0.0f, hi = hiValue;
        for (int k = 0; k < 10; ++k) {
            value = 0.5f * (lo + hi);
            if (solve() > kMaxReach) lo = value;
            else hi = value;
        }
        value = hi;
        solve();
    };
    const bool sideways = std::fabs(p.hips.GetX()) > 0.02f || std::fabs(p.shoulders.GetX()) > 0.02f;
    const bool back = p.lean < -0.02f && p.trick < 0.99f;
    auto reach = [&]() {
        fit.pitch = fit.shift = fit.sideIn = fit.backIn = 0.0f;
        if (solve() <= kMaxReach) return;
        if (sideways && hang < 0.3f) {
            fit.sideIn = 1.0f;
            if (solve() <= kMaxReach) {
                search(1.0f, fit.sideIn);
                return;
            }
        }
        if (back) {
            fit.backIn = 1.0f;
            if (solve() <= kMaxReach) {
                search(1.0f, fit.backIn);
                return;
            }
        }
        fit.pitch = kMaxExtraPitch;
        if (solve() <= kMaxReach) search(kMaxExtraPitch, fit.pitch);
        else search(kMaxHipShift, fit.shift);
    };
    reach();
    lastFit = fit;
    if (shape.style < 0) return;             // todavía no se dibujó la moto: no se conoce su forma

    // Sentado: la cola y los muslos apoyan en el asiento (se hunden hasta kSeatSink, la espuma), no adentro.
    auto seat = [&](int group) {
        const float before = fit.lift;
        for (int k = 0; k < 4; ++k) {
            const float d = SeatDepth(group);
            if (d <= kSeatSink + 0.002f || fit.lift >= kMaxLift) break;
            fit.lift = std::min(kMaxLift, fit.lift + d - kSeatSink);
            solve();
        }
        return fit.lift > before;
    };
    // Cada pierna: el tobillo se corre sobre la estribera hasta que la bota no se mete en el motor, y la rodilla se abre
    // hasta que muslo y canilla quedan afuera del tanque, los plásticos y el escape (la canilla de abajo gira con la
    // rodilla: después se vuelve a mirar la bota). Con la pata afuera, si abriendo la rodilla no alcanza, el pie sale
    // más (la de carreras tiene el carenado ancho). La pierna suelta de un truco va por su curva: no se toca.
    auto legs = [&]() {
        for (int i = 0; i < 2; ++i) {
            if (p.legFree[i] > 0.6f) {
                fit.open[i] = fit.ankleOut[i] = 0.0f;
                continue;
            }
            const float most = kMaxAnkleOut + kMaxLegOutWiden * p.legOut[i];
            auto foot = [&]() {
                for (int k = 0; k < 3; ++k) {
                    const float d = SideDepth(ProbeFoot + i);
                    if (d <= kSideTolerance + 0.002f || fit.ankleOut[i] >= most) break;
                    fit.ankleOut[i] = std::min(most, fit.ankleOut[i] + d - kSideTolerance);
                    solve();
                }
            };
            // La primera apertura (de a 0.1 rad desde la que tiene) que alcanza, afinada después; si ninguna alcanza, la
            // que menos se mete. Devuelve cuánto se sigue metiendo.
            auto knee = [&]() {
                const float d0 = SideDepth(ProbeLeg + i);
                if (d0 <= kSideTolerance) return d0;
                const float start = fit.open[i];
                float best = start, bestDepth = d0, found = -1.0f;
                for (float o = start + 0.1f; o <= kMaxKneeOpen + 1e-4f; o += 0.1f) {
                    fit.open[i] = o;
                    solve();
                    const float d = SideDepth(ProbeLeg + i);
                    if (d < bestDepth) {
                        bestDepth = d;
                        best = o;
                    }
                    if (d <= kSideTolerance) {
                        found = o;
                        break;
                    }
                }
                if (found > 0.0f) {
                    float lo = std::max(start, found - 0.1f), hi = found;
                    for (int k = 0; k < 5; ++k) {
                        fit.open[i] = 0.5f * (lo + hi);
                        solve();
                        if (SideDepth(ProbeLeg + i) > kSideTolerance) lo = fit.open[i];
                        else hi = fit.open[i];
                    }
                    best = hi;
                    bestDepth = kSideTolerance;
                }
                fit.open[i] = best;
                solve();
                return bestDepth;
            };
            foot();
            float left = knee();
            for (int k = 0; k < 5 && left > kSideTolerance && p.legOut[i] > 0.05f && fit.ankleOut[i] < most; ++k) {
                fit.ankleOut[i] = std::min(most, fit.ankleOut[i] + 0.03f);
                solve();
                left = knee();
            }
            foot();
        }
    };
    if (seat(ProbeSeat)) reach();
    // Los brazos no se meten en el tanque ni el carenado: cada codo se abre (gira alrededor de la recta hombro-muñeca, hacia
    // afuera) lo justo. Con los codos cerrados de la de carreras (agachado, colgado) el brazo de afuera cruzaba el tanque y
    // el pecho se enderezaba para sacarlo (ver docs/PILOTO.md, "La Carrera 1000").
    auto arms = [&]() {
        for (int i = 0; i < 2; ++i) {
            if (p.handFree[i] > 0.5f) continue;
            const float start = fit.elbowOpen[i];
            float d = ArmDepth(i);
            if (d <= kSideTolerance) continue;
            float best = start, bestDepth = d, found = -1.0f;
            for (float o = start + 0.1f; o <= kMaxElbowOpen + 1e-4f; o += 0.1f) {
                fit.elbowOpen[i] = o;
                solve();
                d = ArmDepth(i);
                if (d < bestDepth) {
                    bestDepth = d;
                    best = o;
                }
                if (d <= kSideTolerance) {
                    found = o;
                    break;
                }
            }
            if (found > 0.0f) {
                float lo = std::max(start, found - 0.1f), hi = found;
                for (int k = 0; k < 5; ++k) {
                    fit.elbowOpen[i] = 0.5f * (lo + hi);
                    solve();
                    if (ArmDepth(i) > kSideTolerance) lo = fit.elbowOpen[i];
                    else hi = fit.elbowOpen[i];
                }
                best = hi;
            }
            fit.elbowOpen[i] = best;
            solve();
        }
    };
    arms();
    // El pecho, el casco y los brazos no atraviesan el manubrio ni el tanque (tirado adelante con el cuerpo corrido, en el
    // aire; agachado en la de carreras): el torso se endereza lo justo, sin soltar el manubrio (como mucho hasta donde los
    // brazos llegan, kReachLimit). En un truco no: su pose ya está hecha para no tocar.
    if (p.trick < 0.3f && ChestDepth(p) > kBarTolerance) {
        const float base = fit.pitch;
        auto reaches = [&](float d) {
            fit.pitch = base - d;
            return solve() <= kReachLimit;
        };
        float most = kMaxBackPitch;
        if (!reaches(most)) {
            float lo = 0.0f, hi = most;
            for (int k = 0; k < 6; ++k) {
                const float mid = 0.5f * (lo + hi);
                if (reaches(mid)) lo = mid;
                else hi = mid;
            }
            most = lo;
        }
        fit.pitch = base - most;
        solve();
        float lo = 0.0f, hi = most;
        if (ChestDepth(p) <= kBarTolerance) {
            for (int k = 0; k < 7; ++k) {
                const float mid = 0.5f * (lo + hi);
                fit.pitch = base - mid;
                solve();
                if (ChestDepth(p) > kBarTolerance) lo = mid;
                else hi = mid;
            }
        }
        fit.pitch = base - hi;
        solve();
    }
    legs();
    // Con el cuerpo corrido de costado (o el whip), el muslo de afuera cruza el asiento. La cadera del modelo es angosta:
    // corrida entera, ese muslo queda adentro del asiento (por arriba o por el costado, cerca de la rodilla, aunque se abra
    // del todo). Vuelve hacia el medio lo justo y después sube lo que falte. Mientras se busca, las rodillas abiertas del
    // todo (lo que después se afina).
    auto residual = [&]() {
        return std::max({SeatDepth(ProbeDrape) - kSeatSink, SideDepth(ProbeLeg) - kSideTolerance, SideDepth(ProbeLeg + 1) - kSideTolerance});
    };
    if (std::fabs(p.hips.GetX()) * (1.0f - fit.sideIn) > 0.01f && residual() > kSideTolerance) {
        const Fit before = fit;
        fit.open[0] = fit.open[1] = kMaxKneeOpen;
        // Colgado (carreras) la cola sale del asiento a propósito: vuelve como mucho un poco y lo demás lo resuelve subir
        // (el muslo de afuera queda apoyado sobre el asiento).
        float lo = 0.0f, hi = 1.0f - kHangKeep * hang;
        fit.center = hi;
        solve();
        if (residual() <= kSideTolerance) {
            for (int k = 0; k < 6; ++k) {
                fit.center = 0.5f * (lo + hi);
                solve();
                if (residual() > kSideTolerance) lo = fit.center;
                else hi = fit.center;
            }
        }
        fit.center = hi;
        fit.open[0] = before.open[0];
        fit.open[1] = before.open[1];
        solve();
        legs();
    }
    if (seat(ProbeDrape)) legs();
    // Lo último: las manos en el manubrio (si al subir o centrar la cadera dejaron de llegar, se vuelve a buscar, aunque el
    // pecho roce el manubrio). Y si ni así llegan, la cadera baja lo justo: las manos mandan (con la pata afuera en la de
    // carreras subía hasta el tope y las manos quedaban a 15-20 cm de los puños).
    if (lastReach > kReachLimit) reach();
    arms();                                  // la cadera subió o se centró: el brazo puede haber vuelto al tanque
    if (lastReach > kLiftReach && fit.lift > 0.0f) {
        float lo = 0.0f, hi = fit.lift;
        for (int k = 0; k < 7; ++k) {
            fit.lift = 0.5f * (lo + hi);
            reach();
            if (lastReach > kLiftReach) hi = fit.lift;
            else lo = fit.lift;
        }
        fit.lift = lo;
        reach();
        legs();
        arms();
    }
    lastFit = fit;
}

Vector3 RiderModel::SkinVertex(int v) const
{
    Vector3 p = {0.0f, 0.0f, 0.0f};
    for (int k = 0; k < 4; ++k) {
        const float w = skinWeight[v * 4 + k];
        if (w <= 0.0f) continue;
        const int b = skinBone[v * 4 + k];
        p = Vector3Add(p, Vector3Scale(Vector3Add(pos[b], Vector3RotateByQuaternion(Vector3Subtract(bindVerts[v], bindPos[b]), delta[b])), w));
    }
    return p;
}

bool RiderModel::BikeShape::Inside(Vector3 p, float& dl, float& dv) const
{
    // Bilineal (continua: la pose no salta de a una celda).
    auto sample = [&](const std::vector<float>& f, int n0, int n1, float a, float b, float nothing) {
        const float fa = a / kShapeCell - 0.5f, fb = b / kShapeCell - 0.5f;
        const int ia = (int)std::floor(fa), ib = (int)std::floor(fb);
        const float ta = fa - (float)ia, tb = fb - (float)ib;
        auto at = [&](int x, int y) { return x < 0 || y < 0 || x >= n0 || y >= n1 ? nothing : f[(size_t)y * n0 + x]; };
        return (at(ia, ib) * (1.0f - ta) + at(ia + 1, ib) * ta) * (1.0f - tb) + (at(ia, ib + 1) * (1.0f - ta) + at(ia + 1, ib + 1) * ta) * tb;
    };
    if (style < 0) return false;
    const float w = sample(side[p.x >= 0.0f ? 0 : 1], ny, nz, p.y - y0, p.z - z0, 0.0f);
    const float ax = std::fabs(p.x);
    if (ax >= w) return false;
    const float t = sample(top, nx, nz, p.x - x0, p.z - z0, kNothing);
    if (p.y >= t) return false;
    dl = w - ax;
    dv = t - p.y;
    return true;
}

// Cuánto se hunden en el asiento (hacia abajo) la cola y los muslos. Cada vértice adentro cuenta para abajo o para el
// costado según por dónde sale más cerca, con una transición suave (si no, la pose saltaría cuando un vértice cambia de
// lado).
float RiderModel::SeatDepth(int group) const
{
    float worst = 0.0f;
    for (int v : probe[group]) {
        float dl, dv;
        if (!shape.Inside(SkinVertex(v), dl, dv)) continue;
        worst = std::max(worst, dv * mu::Smoothstep(-0.01f, 0.01f, dl - dv));
    }
    return worst;
}

float RiderModel::SideDepth(int group) const
{
    // Las rodillas, además, contra el manubrio: con la pata afuera y el manubrio a fondo para ese lado, el puño vuelve
    // hasta la rodilla (se abre la rodilla hasta que pasa por afuera o por abajo del puño).
    const bool legs = group == ProbeLeg || group == ProbeLeg + 1;
    const Vector3 ab = Vector3Subtract(barB, barA);
    const float len2 = std::max(Vector3DotProduct(ab, ab), 1e-6f);
    float worst = 0.0f;
    for (int v : probe[group]) {
        const Vector3 q = SkinVertex(v);
        if (legs) {
            const float u = mu::Clamp(Vector3DotProduct(Vector3Subtract(q, barA), ab) / len2, 0.0f, 1.0f);
            worst = std::max(worst, kBarRadius - Vector3Distance(q, Vector3Add(barA, Vector3Scale(ab, u))));
        }
        float dl, dv;
        if (!shape.Inside(q, dl, dv)) continue;
        worst = std::max(worst, dl * (1.0f - mu::Smoothstep(-0.01f, 0.01f, dl - dv)));
    }
    return worst;
}

// Un brazo (y el antebrazo del lado del codo) adentro de la moto: lo que le falta para salir (por el costado o arriba).
float RiderModel::ArmDepth(int side) const
{
    // Cada vértice se prueba corrido kArmMargin hacia adentro del brazo (por su normal): la malla es de pocos triángulos y
    // el borde del tanque se metía entre vértices (2 cm, medido con tools/holgura) sin que ningún vértice quedara adentro.
    float worst = 0.0f;
    for (int v : probe[ProbeArm + side]) {
        Vector3 n = {0.0f, 0.0f, 0.0f};
        for (int k = 0; k < 4; ++k) {
            const float w = skinWeight[v * 4 + k];
            if (w > 0.0f) n = Vector3Add(n, Vector3Scale(Vector3RotateByQuaternion(bindNormals[v], delta[skinBone[v * 4 + k]]), w));
        }
        const Vector3 q = Vector3Subtract(SkinVertex(v), Vector3Scale(Vector3Normalize(n), kArmMargin));
        float dl, dv;
        if (shape.Inside(q, dl, dv)) worst = std::max(worst, std::min(dl, dv));
    }
    return worst;
}

// Pecho, casco y brazos contra el manubrio (un tubo de puño a puño, con la almohadilla del medio) y contra la moto (el
// tanque y el carenado, agachado); los antebrazos, sólo contra el manubrio.
float RiderModel::ChestDepth(const RiderPose& p) const
{
    const Vector3 a = V(p.grip[0]), b = V(p.grip[1]), ab = Vector3Subtract(b, a);
    const float len2 = std::max(Vector3DotProduct(ab, ab), 1e-6f);
    float worst = 0.0f;
    for (int g : {(int)ProbeChest, (int)ProbeForearm})
        for (int v : probe[g]) {
            const Vector3 q = SkinVertex(v);
            const float u = mu::Clamp(Vector3DotProduct(Vector3Subtract(q, a), ab) / len2, 0.0f, 1.0f);
            worst = std::max(worst, kBarRadius - Vector3Distance(q, Vector3Add(a, Vector3Scale(ab, u))));
            float dl, dv;
            if (g == ProbeChest && shape.Inside(q, dl, dv)) worst = std::max(worst, std::min(dl, dv));
        }
    return worst;
}

void RiderModel::BuildShape(const Renderer& r)
{
    const Mesh& m = r.BikePart(BikeMesh::Body, 0);
    if (!m.vertices || (shape.style == r.bikeStyle && shape.key == m.vertices)) return;
    const double t0 = GetTime();
    shape = BikeShape{};
    shape.style = r.bikeStyle;
    shape.key = m.vertices;
    shape.x0 = -0.6f;
    shape.y0 = -0.8f;
    shape.z0 = -1.2f;
    shape.nx = (int)(1.2f / kShapeCell);
    shape.ny = (int)(1.7f / kShapeCell);
    shape.nz = (int)(2.4f / kShapeCell);
    std::vector<float> side[2] = {std::vector<float>((size_t)shape.ny * shape.nz, 0.0f), std::vector<float>((size_t)shape.ny * shape.nz, 0.0f)};
    std::vector<float> top((size_t)shape.nx * shape.nz, kNothing);
    const JPH::Vec3 peg = GetBikeStyle(r.bikeStyle).peg;
    auto splat = [&](Vector3 q) {
        const int iy = (int)((q.y - shape.y0) / kShapeCell), iz = (int)((q.z - shape.z0) / kShapeCell), ix = (int)((q.x - shape.x0) / kShapeCell);
        if (iz < 0 || iz >= shape.nz) return;
        if (iy >= 0 && iy < shape.ny) {
            float& s = side[q.x >= 0.0f ? 0 : 1][(size_t)iz * shape.ny + iy];
            s = std::max(s, std::fabs(q.x));
        }
        if (ix >= 0 && ix < shape.nx) {
            float& t = top[(size_t)iz * shape.nx + ix];
            t = std::max(t, q.y);
        }
    };
    const int tris = m.indices ? m.triangleCount : m.vertexCount / 3;
    for (int t = 0; t < tris; ++t) {
        Vector3 v[3];
        for (int e = 0; e < 3; ++e) {
            const int i = m.indices ? m.indices[3 * t + e] : 3 * t + e;
            v[e] = {m.vertices[3 * i], m.vertices[3 * i + 1], m.vertices[3 * i + 2]};
        }
        // Estriberas, pedal de freno y palanca de cambios: ahí apoya la bota.
        const Vector3 c = Vector3Scale(Vector3Add(Vector3Add(v[0], v[1]), v[2]), 1.0f / 3.0f);
        if (std::fabs(c.x) > std::fabs(peg.GetX()) - 0.05f && std::fabs(c.y - peg.GetY()) < 0.07f && c.z > peg.GetZ() - 0.10f && c.z < peg.GetZ() + 0.25f)
            continue;
        const float L = std::max({Vector3Distance(v[0], v[1]), Vector3Distance(v[1], v[2]), Vector3Distance(v[2], v[0])});
        const int n = std::max(1, (int)std::ceil(L / (0.6f * kShapeCell)));
        for (int i = 0; i <= n; ++i)
            for (int j = 0; i + j <= n; ++j)
                splat(Vector3Add(v[0], Vector3Add(Vector3Scale(Vector3Subtract(v[1], v[0]), (float)i / n), Vector3Scale(Vector3Subtract(v[2], v[0]), (float)j / n))));
    }
    // Se agranda una celda para tapar los huecos entre muestras (conservador: la moto queda 1 cm más grande).
    auto dilate = [](const std::vector<float>& f, int n0, int n1, std::vector<float>& out) {
        out = f;
        for (int b = 0; b < n1; ++b)
            for (int a = 0; a < n0; ++a) {
                float mx = f[(size_t)b * n0 + a];
                for (int db = -1; db <= 1; ++db)
                    for (int da = -1; da <= 1; ++da) {
                        const int x = a + da, y = b + db;
                        if (x >= 0 && y >= 0 && x < n0 && y < n1) mx = std::max(mx, f[(size_t)y * n0 + x]);
                    }
                out[(size_t)b * n0 + a] = mx;
            }
    };
    dilate(side[0], shape.ny, shape.nz, shape.side[0]);
    dilate(side[1], shape.ny, shape.nz, shape.side[1]);
    dilate(top, shape.nx, shape.nz, shape.top);
    TraceLog(LOG_INFO, "piloto: forma de la moto (estilo %s) en %.0f ms", GetBikeStyle(r.bikeStyle).name, 1000.0 * (GetTime() - t0));
}

// Vértices de prueba para no meterse en la moto, por el hueso que más pesa en cada uno (uno por posición: las costuras
// repiten vértices). Las manos no: agarran el manubrio.
void RiderModel::BuildProbes()
{
    const Mesh& m = model.meshes[0];
    const int n = model.boneCount;
    for (std::vector<int>& g : probe) g.clear();
    std::map<std::tuple<long long, long long, long long>, int> seen;
    for (int v = 0; v < m.vertexCount; ++v) {
        const Vector3 q = bindVerts[v];
        if (!seen.emplace(std::make_tuple(std::llround(q.x * 1e5), std::llround(q.y * 1e5), std::llround(q.z * 1e5)), v).second) continue;
        int best = -1;
        float bw = 0.0f;
        for (int k = 0; k < 4; ++k)
            if (skinWeight[v * 4 + k] > bw) {
                bw = skinWeight[v * 4 + k];
                best = skinBone[v * 4 + k];
            }
        if (best >= n) continue;                                     // dedos virtuales: la mano
        for (int b = best; b >= 0 && b < n; b = parent[b]) {
            int found = -1;
            for (int i = 0; i < 2; ++i) {
                if (b == hand[i]) found = ProbeCount;                        // afuera
                else if (b == foot[i] || b == toe[i]) found = ProbeFoot + i;
                else if (b == leg[i]) {
                    // Canilla: en la del costado; cerca del tobillo también con la bota.
                    const Vector3 k0 = bindPos[leg[i]], k1 = bindPos[foot[i]], d = Vector3Subtract(k1, k0);
                    if (Vector3DotProduct(Vector3Subtract(q, k0), d) / Vector3DotProduct(d, d) > 0.65f) probe[ProbeFoot + i].push_back(v);
                    found = ProbeLeg + i;
                } else if (b == upLeg[i]) {
                    // Muslo: en la pierna; la mitad de arriba (la que apoya en el asiento), también en el asiento.
                    const Vector3 h0 = bindPos[upLeg[i]], h1 = bindPos[leg[i]], d = Vector3Subtract(h1, h0);
                    const float u = Vector3DotProduct(Vector3Subtract(q, h0), d) / Vector3DotProduct(d, d);
                    if (u < 0.5f) probe[ProbeSeat].push_back(v);
                    probe[ProbeDrape].push_back(v);
                    found = ProbeLeg + i;
                } else if (b == arm[i]) {
                    probe[ProbeArm + i].push_back(v);
                    found = ProbeChest;
                }
                else if (b == foreArm[i]) {
                    // Antebrazo: la mitad del lado del codo, también en el brazo (contra la moto).
                    const Vector3 e0 = bindPos[foreArm[i]], e1 = bindPos[hand[i]], d = Vector3Subtract(e1, e0);
                    if (Vector3DotProduct(Vector3Subtract(q, e0), d) / Vector3DotProduct(d, d) < 0.5f) probe[ProbeArm + i].push_back(v);
                    found = ProbeForearm;
                }
            }
            if (found < 0 && (b == neck || b == head || b == spineChain[0] || b == spineChain[1] || b == spineChain[2])) found = ProbeChest;
            if (found < 0 && b == hips) {
                probe[ProbeDrape].push_back(v);
                found = ProbeSeat;
            }
            if (found < 0) continue;
            if (found < ProbeCount) probe[found].push_back(v);
            break;
        }
    }
    TraceLog(LOG_INFO, "piloto: vértices de prueba: asiento %d, piernas %d/%d, botas %d/%d, pecho %d", (int)probe[ProbeSeat].size(),
             (int)probe[ProbeLeg].size(), (int)probe[ProbeLeg + 1].size(), (int)probe[ProbeFoot].size(), (int)probe[ProbeFoot + 1].size(),
             (int)probe[ProbeChest].size());
}

// Whip, sólo el modelo: la pose (la del piloto generado, que también arma el ragdoll y por eso no se toca) corre la cadera
// 0.40·sen(inclinación) y los hombros 0.62 al costado: ~23 y ~36 cm con el whip a fondo, la cola afuera del asiento y
// una mano lejos de su puño. Un piloto de verdad aprieta la moto con las piernas y deja que se acueste debajo de él: la
// cadera apenas se corre (0.14), el torso queda más derecho en el mundo (los hombros, 0.40) y las rodillas cerca del
// tanque. Lo que siga sin llegar lo resuelve la IK (PoseOnBike: el cuerpo vuelve hacia el medio). Ver docs/PILOTO.md,
// "Whip con las manos en el manubrio".
RiderPose RiderModel::WhipPose(const RiderPose& base, const Bike& bike)
{
    RiderPose whipped = base;
    const float tiltX = -std::sin(bike.bodyTilt);                  // +X = izquierda (como en Bike::RiderPoseLocal)
    const JPH::Vec3 dHips(-0.26f * tiltX, 0.0f, 0.0f), dShoulders(-0.22f * tiltX, 0.0f, 0.0f);
    whipped.hips += dHips;
    whipped.shoulders += dShoulders;
    whipped.head += dShoulders;
    for (int i = 0; i < 2; ++i) {
        whipped.hip[i] += dHips;
        whipped.shoulder[i] += dShoulders;
        whipped.elbow[i] += dShoulders;
        if (base.legFree[i] <= 0.0f && base.legOut[i] <= 0.0f) whipped.knee[i] += JPH::Vec3(-0.12f * tiltX, 0.0f, 0.0f);
    }
    return whipped;
}

// La de carreras, sólo el modelo (la pose compartida arma el ragdoll y no se toca). Colgado, la pose (la del piloto
// generado) corre la cadera 14 cm y los hombros 20 hacia adentro: con la moto a 55° el cuerpo quedaba casi en el eje de
// la moto, derecho, como sentado en una moto inclinada. Un piloto de MotoGP saca media cola del asiento, baja el pecho
// sobre el costado de adentro del tanque y lleva el casco hasta el espejo de adentro. Ver docs/PILOTO.md, "La Carrera 1000".
RiderPose RiderModel::RacePose(const RiderPose& base, const Bike& bike)
{
    const BikeStyleDef& style = GetBikeStyle(bike.Params() ? bike.Params()->visualStyle : 0);
    // Lo mismo que Bike::RiderPoseLocal (cuánto cuelga y para qué lado).
    hang = style.hangOff * mu::Smoothstep(mu::Rad(12.0f), mu::Rad(45.0f), std::fabs(bike.roll)) * mu::Smoothstep(6.0f, 12.0f, bike.forwardSpeed) *
           (1.0f - base.trick);
    if (std::fabs(bike.roll) > mu::Rad(1.0f)) hangSide = bike.roll > 0.0f ? -1.0f : 1.0f;
    tuckAmt = style.tuck * mu::Smoothstep(8.0f, 25.0f, bike.forwardSpeed) * (1.0f - base.trick);
    RiderPose p = base;
    if (hang <= 0.0f) return p;
    const JPH::Vec3 dHips(kHangHips * hangSide * hang, -0.02f * hang, 0.0f);
    const JPH::Vec3 dShoulders(kHangShoulders * hangSide * hang, -kHangDrop * hang, 0.02f * hang);
    p.hips += dHips;
    p.shoulders += dShoulders;
    p.head += dShoulders;
    for (int i = 0; i < 2; ++i) {
        p.hip[i] += dHips;
        p.shoulder[i] += dShoulders;
        p.elbow[i] += dShoulders;
    }
    return p;
}

void RiderModel::PoseOnBike(const RiderPose& base, const Bike& bike, JPH::Vec3 bikePos, JPH::Quat bikeRot, double time, float ground, float smooth)
{
    const auto t0 = std::chrono::steady_clock::now();
    // Reapareció (Bike::Reset, también cerca y con la misma velocidad): arranca de nuevo, no siente el salto como un golpe.
    static std::map<std::pair<const void*, const void*>, unsigned> seenReset;   // por modelo y moto
    const auto key = std::make_pair((const void*)this, (const void*)&bike);
    auto seen = seenReset.find(key);
    const bool reappeared = seen != seenReset.end() && seen->second != bike.resetCount;
    seenReset[key] = bike.resetCount;
    const bool restart = !motionShown || time < motionTime || time > motionTime + 0.5 || reappeared;
    if (restart) motion.Reset();             // volvió a la moto, o el tiempo saltó
    RiderMotion::Input in;
    in.pos = bikePos;
    in.rot = bikeRot;
    in.dt = motionTime < 0.0 ? 0.0f : (float)std::max(0.0, time - motionTime);
    motionTime = time;
    in.ground = ground;
    in.smooth = smooth;
    const BikeStyleDef& style = GetBikeStyle(bike.Params() ? bike.Params()->visualStyle : 0);
    elbowsIn = style.tuck > 0.0f || style.hangOff > 0.0f ? 1.0f : 0.0f;   // la de carreras: codos cerca del tanque
    const RiderPose p = motion.Apply(RacePose(WhipPose(base, bike), bike), bike, in);
    headFree = true;
    PoseOnBike(p);
    // Lo que busca el ajuste (Fit) se elige cada cuadro por separado y puede saltar: la rodilla que se abre "la primera
    // apertura que alcanza" pasaba de 0.76 a 0.31 rad de un cuadro al otro (la canilla 7 cm), el tirado atrás de 0.33 a 0
    // (el torso 3 cm). Lo que se dibuja lo sigue con un resorte crítico (kFitW): un instante puede rozar la moto, pero
    // nunca salta. Ver docs/PILOTO.md, "Saltos de la pose".
    {
        static_assert(sizeof(Fit) % sizeof(float) == 0, "Fit: sólo floats");
        constexpr int n = (int)(sizeof(Fit) / sizeof(float));
        float* shownF = reinterpret_cast<float*>(&fitShown);
        float* velF = reinterpret_cast<float*>(&fitVel);
        const float* want = reinterpret_cast<const float*>(&lastFit);
        if (restart || !fitValid) {
            fitShown = lastFit;
            fitVel = Fit{};
            for (int k = 0; k < n; ++k) velF[k] = 0.0f;
            fitValid = true;
        } else {
            const float dt = mu::Clamp(in.dt, 0.0f, 0.25f);
            for (int k = 0; k < n; ++k) {
                RiderMotion::Spring s{shownF[k], velF[k]};
                s.Step(want[k], 0.0f, kFitW, 1.0f, dt);
                shownF[k] = s.x;
                velF[k] = s.v;
            }
            lastReach = SolveBody(p, fitShown);
        }
    }
    headFree = false;
    motionShown = true;
    if (JumpLog()) CheckJumps(time, in.dt, base, p, bike);
    if (PoseLog()) {
        const RiderMotion& m = motion;
        const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
        std::printf("PILOTO %03d t=%.3f us=%.0f a=%.1f,%.1f,%.1f al=%.1f,%.1f,%.1f cadera=%.3f,%.3f,%.3f torso=%.1f,%.1f cabeza=%.0f,%.0f,%.0f afuera=%.2f,%.2f "
                    "deriva=%.0f alcance=%.3f +torso=%.2f +cadera=%.3f medio=%.2f atras=%.2f sube=%.3f mano=%.3f incl=%.0f cadx=%.3f cadz=%.3f hombx=%.3f codo=%.2f,%.2f brazo=%.3f,%.3f colgado=%.2f agachado=%.2f sentado=%.2f\n",
                    (int)(((uintptr_t)this >> 4) % 1000), time, us, m.accLocal.GetX(), m.accLocal.GetY(), m.accLocal.GetZ(), m.angAccLocal.GetX(), m.angAccLocal.GetY(),
                    m.angAccLocal.GetZ(), m.dof[RiderMotion::HipsX].x, m.dof[RiderMotion::HipsY].x, m.dof[RiderMotion::HipsZ].x,
                    mu::Deg(m.dof[RiderMotion::TorsoPitch].x), mu::Deg(m.dof[RiderMotion::TorsoRoll].x), mu::Deg(m.dof[RiderMotion::HeadYaw].x),
                    mu::Deg(m.dof[RiderMotion::HeadPitch].x), mu::Deg(m.dof[RiderMotion::HeadRoll].x), m.dof[RiderMotion::OutL].x,
                    m.dof[RiderMotion::OutR].x, mu::Deg(m.drift), lastReach, lastFit.pitch, lastFit.shift, lastFit.sideIn, lastFit.backIn, lastFit.lift, handMiss,
                    mu::Deg(std::atan2(pos[neck].z - pos[hips].z, pos[neck].y - pos[hips].y)), pos[hips].x, pos[hips].z, pos[neck].x,
                    lastFit.elbowOpen[0], lastFit.elbowOpen[1], ArmDepth(0), ArmDepth(1), hang, tuckAmt, m.dof[RiderMotion::SitUp].x);
    }
}

float RiderModel::SolveBody(const RiderPose& p, const Fit& fit)
{
    // Cada hueso: si no tiene un ajuste propio, sigue a su padre (cinemática directa).
    const float forward = std::max(0.0f, p.lean);
    // Los ajustes de la posición de ataque (cadera más baja, torso más adelante) no van en un truco: su pose ya es la del modelo.
    const float seated = 1.0f - p.trick;
    // El cuerpo corrido de costado vuelve hacia el medio lo que pida fit.sideIn (cadera y hombros juntos: el torso no
    // cambia de inclinación) y la cadera sola lo que pida fit.center (el muslo no cruza el asiento).
    Vector3 hipsPose = V(p.hips), shouldersPose = V(p.shoulders);
    hipsPose.x *= 1.0f - fit.sideIn;
    shouldersPose.x *= 1.0f - fit.sideIn;
    // Tirado atrás (si los brazos no llegan, ver PoseOnBike): la cadera menos atrás y el torso más adelante, con los brazos
    // estirados. La pose es la del piloto generado (brazos elásticos): con los brazos del modelo no llegaba.
    const float back = std::max(0.0f, -p.lean) * seated;
    const Vector3 hipsTarget = Vector3Add(hipsPose, {-fit.center * hipsPose.x, -kHipsDrop * seated + fit.lift + 0.3f * fit.shift,
                                                     fit.shift - kForwardHipsBack * forward * seated + kBackHipsForward * fit.backIn * back});
    const Vector3 torsoBind = Vector3Subtract(bindPos[neck], bindPos[hips]);
    Vector3 torsoTarget = Vector3Subtract(shouldersPose, hipsPose);
    const float pitch = (kExtraLean + kForwardLean * forward) * seated + fit.pitch + kBackLean * fit.backIn * back;
    torsoTarget = Vector3RotateByAxisAngle(torsoTarget, {1.0f, 0.0f, 0.0f}, pitch);   // + adelante
    float reach = 0.0f;                                     // distancia hombro-manubrio / largo del brazo
    handMiss = 0.0f;
    const Quaternion torsoTurn = FromTo(torsoBind, torsoTarget);
    // La inclinación la toma sobre todo la pelvis (la bisagra está en las articulaciones de la cadera,
    // como en la posición de ataque) y un poco cada vértebra: espalda casi recta, sin quiebre en la
    // cintura. Girando sólo la columna la panza se inflaba y la espalda baja se doblaba.
    const Quaternion pelvisTurn = QuaternionSlerp(QuaternionIdentity(), torsoTurn, 0.75f);
    const Quaternion spineStep = QuaternionSlerp(QuaternionIdentity(), torsoTurn, 0.25f / 3.0f);
    // Con el manubrio girado, los hombros giran con él (sobre el eje del torso, repartido en las vértebras): el hombro de
    // afuera va adelante con su puño. Sin esto, tirado atrás y con el manubrio a fondo la mano de afuera no llegaba al
    // puño (quedaba a 8-9 cm). Con las manos sueltas (no-hander), nada. Más el giro que pida la pose (`twist`: el
    // nac-nac mira atrás con los hombros).
    const Vector3 bar = V(p.grip[0] - p.grip[1]);
    const float held = 1.0f - std::max(p.handFree[0], p.handFree[1]);
    const float shoulderTurn = mu::Clamp(-kShoulderTurn * std::atan2(bar.z, bar.x), -kMaxShoulderTurn, kMaxShoulderTurn) * held + p.twist;
    const Quaternion twistStep = QuaternionFromAxisAngle(Vector3Normalize(torsoTarget), shoulderTurn / 3.0f);

    Vector3 elbow[2], wrist[2], knee[2], ankle[2];
    Vector3 kneeFront[2] = {{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}};
    Vector3 thighFront[2] = {{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}}, shinFront[2] = {{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}};   // hacia dónde mira la rodilla (fuera de la recta cadera-tobillo)
    Quaternion handRot[2] = {QuaternionIdentity(), QuaternionIdentity()};
    for (int b : order) {
        const int pa = parent[b];
        const Quaternion inherited = pa >= 0 ? delta[pa] : QuaternionIdentity();
        pos[b] = pa >= 0 ? Vector3Add(pos[pa], Vector3RotateByQuaternion(Vector3Subtract(bindPos[b], bindPos[pa]), inherited)) : bindPos[b];
        delta[b] = inherited;
        if (curlAngle[b] != 0.0f) {                                 // falange virtual: se dobla sobre su padre
            delta[b] = QuaternionMultiply(QuaternionFromAxisAngle(Vector3RotateByQuaternion(curlAxis[b], inherited), curlAngle[b] * FingerGrip(b)), inherited);
            continue;
        }

        // Gira el hueso para que su hijo (en reposo en childBind) quede apuntando a `target`.
        auto aim = [&](int child, Vector3 target) {
            const Vector3 now = Vector3RotateByQuaternion(Vector3Subtract(bindPos[child], bindPos[b]), delta[b]);
            delta[b] = QuaternionMultiply(FromTo(now, Vector3Subtract(target, pos[b])), delta[b]);
        };

        // Como aim, pero con el giro sobre el hueso fijado: el frente del hueso en reposo (+Z, la rodilla) queda hacia
        // `front`. aim (el arco más corto desde el giro heredado) no está definido cuando el hueso apunta casi al revés que en
        // reposo: con las piernas arriba (heel clicker) el muslo daba 70-150° de vuelta sobre sí mismo de un cuadro al otro.
        auto aimFront = [&](int child, Vector3 target, Vector3 front) {
            const Vector3 d0 = Vector3Normalize(Vector3Subtract(bindPos[child], bindPos[b]));
            const Vector3 d1 = Vector3Normalize(Vector3Subtract(target, pos[b]));
            Vector3 f0 = {0.0f, 0.0f, 1.0f};
            f0 = Vector3Subtract(f0, Vector3Scale(d0, Vector3DotProduct(f0, d0)));
            Vector3 f1 = Vector3Subtract(front, Vector3Scale(d1, Vector3DotProduct(front, d1)));
            if (Vector3Length(f0) < 1e-4f || Vector3Length(f1) < 1e-4f) {
                aim(child, target);
                return;
            }
            f0 = Vector3Normalize(f0);
            f1 = Vector3Normalize(f1);
            auto frame = [](Vector3 x, Vector3 y) {
                const Vector3 z = Vector3CrossProduct(x, y);
                Matrix mm = MatrixIdentity();
                mm.m0 = x.x; mm.m1 = x.y; mm.m2 = x.z;
                mm.m4 = y.x; mm.m5 = y.y; mm.m6 = y.z;
                mm.m8 = z.x; mm.m9 = z.y; mm.m10 = z.z;
                return mm;
            };
            delta[b] = QuaternionNormalize(QuaternionFromMatrix(MatrixMultiply(MatrixTranspose(frame(d0, f0)), frame(d1, f1))));
        };

        if (b == hips) {
            pos[b] = hipsTarget;
            delta[b] = pelvisTurn;
        } else if (b == spineChain[0] || b == spineChain[1] || b == spineChain[2]) {
            delta[b] = QuaternionMultiply(twistStep, QuaternionMultiply(spineStep, delta[b]));
        } else if (b == head || (b == neck && headFree)) {
            if (!headFree) {
                // Mirando la pista: la cara (+Z en reposo) hacia adelante y un poco abajo.
                const Vector3 face = Vector3RotateByQuaternion({0.0f, 0.0f, 1.0f}, delta[b]);
                delta[b] = QuaternionMultiply(FromTo(face, {0.0f, -0.2f, 1.0f}), delta[b]);
            } else {
                // Con el cuerpo con masa: la cara hacia motion.look y el casco derecho según motion.lookUp (giro
                // completo respecto del reposo, que mira a +Z con +Y arriba). El cuello toma la mitad: con el
                // torso inclinado, girar sólo la cabeza estiraba la garganta (ver "El cuello del modelo 3").
                const Vector3 lookV = Vector3Normalize(V(motion.look));
                const Quaternion aimQ = FromTo({0.0f, 0.0f, 1.0f}, lookV);
                Vector3 upNow = Vector3RotateByQuaternion({0.0f, 1.0f, 0.0f}, aimQ);
                Vector3 upWant = V(motion.lookUp);
                upNow = Vector3Normalize(Vector3Subtract(upNow, Vector3Scale(lookV, Vector3DotProduct(upNow, lookV))));
                upWant = Vector3Normalize(Vector3Subtract(upWant, Vector3Scale(lookV, Vector3DotProduct(upWant, lookV))));
                const float roll = std::atan2(Vector3DotProduct(Vector3CrossProduct(upNow, upWant), lookV), Vector3DotProduct(upNow, upWant));
                const Quaternion want = QuaternionMultiply(QuaternionFromAxisAngle(lookV, roll), aimQ);
                delta[b] = b == head ? want : QuaternionSlerp(delta[b], want, 0.5f);
            }
        }
        for (int i = 0; i < 2; ++i) {
            const float side = i == 0 ? 1.0f : -1.0f;               // +X = izquierda
            if (b == clavicle[i] && arm[i] >= 0 && parent[arm[i]] == b) {
                // El hombro va hacia el manubrio (la escápula se adelanta) si el brazo no llega cómodo: la clavícula gira
                // alrededor del eje del torso, nada con el brazo al 80% y kClavicleReach al 105%. Es lo primero que hace un
                // piloto para estirarse, antes de inclinarse o correr la cadera (ver docs/PILOTO.md, "Brazos más largos").
                const Vector3 c = Vector3RotateByQuaternion(Vector3Subtract(bindPos[arm[i]], bindPos[b]), delta[b]);
                const float armLen = Vector3Distance(bindPos[foreArm[i]], bindPos[arm[i]]) + Vector3Distance(bindPos[hand[i]], bindPos[foreArm[i]]);
                const float r0 = (Vector3Distance(Vector3Add(pos[b], c), V(p.grip[i])) - kWristBack) / armLen;
                const float t = mu::Smoothstep(0.8f, 1.05f, r0) * (1.0f - p.handFree[i]);
                const Vector3 up = Vector3RotateByQuaternion({0.0f, 1.0f, 0.0f}, delta[b]);
                auto flat = [&](Vector3 v) { return Vector3Subtract(v, Vector3Scale(up, Vector3DotProduct(v, up))); };
                const Vector3 cf = flat(c), gf = flat(Vector3Subtract(V(p.grip[i]), pos[b]));
                const Vector3 axis = Vector3CrossProduct(cf, gf);
                if (t > 0.0f && Vector3Length(axis) > 1e-6f) {
                    const float ang = std::atan2(Vector3Length(axis), Vector3DotProduct(cf, gf));
                    delta[b] = QuaternionMultiply(QuaternionFromAxisAngle(Vector3Normalize(axis), std::min(ang, kClavicleReach) * t), delta[b]);
                }
            } else if (b == arm[i]) {
                const Vector3 gripAt = V(p.grip[i]);
                const float l1 = Vector3Distance(bindPos[foreArm[i]], bindPos[b]), l2 = Vector3Distance(bindPos[hand[i]], bindPos[foreArm[i]]);
                // Codos hacia afuera y arriba (posición de ataque); en la de carreras, más abajo y cerca del tanque (con los
                // brazos largos el codo doblado se abría como un ala); con la mano suelta (truco), abajo y atrás.
                const float freeW = p.handFree[i];
                Vector3 attack = Vector3Lerp({side * 1.0f, 0.1f, -0.3f}, {side * 0.75f, -0.25f, -0.3f}, elbowsIn);
                // Agachado (carreras, rápido) los codos bajan y se cierran contra el tanque; colgado, el de adentro
                // baja hacia el piso y el de afuera apoya sobre el tanque (no por arriba de la cúpula).
                attack = Vector3Lerp(attack, {side * kTuckElbowOut, -0.85f, -0.25f}, tuckAmt);
                attack = Vector3Lerp(attack, side == hangSide ? Vector3{side * 0.45f, -1.0f, -0.1f} : Vector3{side * 0.5f, -0.7f, -0.3f}, hang);
                const Vector3 pole = Vector3Add(pos[b], Vector3Lerp(attack, {side * 0.3f, -1.0f, -0.6f}, freeW));
                Vector3 target = Vector3Add(gripAt, Vector3Scale(Vector3Normalize(Vector3Subtract(pos[b], gripAt)), kWristBack));
                if (grip[i].valid) {
                    // La mano agarra el puño: su orientación sale del manubrio y del antebrazo, y de ahí dónde
                    // tiene que quedar la muñeca. Dos pasadas: el antebrazo depende de dónde quede la muñeca.
                    const Vector3 inward = Vector3Scale(Vector3Normalize(V(p.grip[0] - p.grip[1])), -side);   // hacia el pulgar
                    Vector3 fore = Vector3Normalize(Vector3Subtract(gripAt, pos[b]));
                    // La derecha gira el acelerador: la mano rueda sobre el puño, el dorso hacia atrás.
                    const Quaternion twist = QuaternionFromAxisAngle(inward, i == 1 ? -kThrottleRoll * throttleS * (1.0f - freeW) : 0.0f);
                    for (int pass = 0; pass < 2; ++pass) {
                        handRot[i] = QuaternionMultiply(twist, GripRotation(i, inward, fore));
                        if (freeW > 0.0f) {
                            // Suelta (truco): la mano sigue al antebrazo, la palma abajo, y `grip` es la muñeca.
                            Vector3 palmDown = {0.0f, -1.0f, 0.0f};
                            palmDown = Vector3Normalize(Vector3Subtract(palmDown, Vector3Scale(fore, Vector3DotProduct(palmDown, fore))));
                            Vector3 across = Vector3CrossProduct(fore, palmDown);
                            Quaternion freeRot = GripRotation(i, across, fore);
                            if (Vector3DotProduct(Vector3RotateByQuaternion(grip[i].palm, freeRot), palmDown) < 0.0f)
                                freeRot = GripRotation(i, Vector3Negate(across), fore);
                            handRot[i] = QuaternionSlerp(handRot[i], freeRot, freeW);
                        }
                        target = Vector3Subtract(gripAt, Vector3Scale(Vector3RotateByQuaternion(Vector3Subtract(grip[i].channel, bindPos[hand[i]]), handRot[i]),
                                                                      1.0f - freeW));
                        TwoBoneIK(pos[b], target, l1, l2, fit.elbowOpen[i] > 0.0f ? OpenJoint(pos[b], target, l1, l2, pole, side, fit.elbowOpen[i]) : pole,
                                  elbow[i], wrist[i]);
                        fore = Vector3Normalize(Vector3Subtract(wrist[i], elbow[i]));
                    }
                } else {
                    TwoBoneIK(pos[b], target, l1, l2, pole, elbow[i], wrist[i]);
                }
                // La mano suelta no tiene que llegar a nada (no cuenta para inclinar el torso).
                reach = std::max(reach, Vector3Distance(pos[b], target) / (l1 + l2) * (1.0f - freeW));
                handMiss = std::max(handMiss, Vector3Distance(wrist[i], target) * (1.0f - freeW));
                aim(foreArm[i], elbow[i]);
            } else if (b == foreArm[i]) {
                aim(hand[i], wrist[i]);
            } else if (b == hand[i] && grip[i].valid) {
                delta[b] = handRot[i];
            } else if (b == upLeg[i]) {
                const float out = p.legOut[i];                              // pata afuera en la curva
                const float freeW = p.legFree[i];                           // truco: el pie dejó la estribera
                // El ajuste de la pierna (rodilla abierta, tobillo afuera) se va mientras el pie se suelta: antes se cortaba de
                // golpe al pasar la mitad (la rodilla saltaba 0.4-0.8 rad al empezar un nac-nac o un heel clicker).
                const float legFit = 1.0f - mu::Smoothstep(0.2f, 0.6f, freeW);
                // El pie apoya en la estribera; afuera (o suelto), el tobillo va donde lo pide la pose.
                // (Corrido hacia afuera lo que pida fit, para que la bota no se meta en el motor.)
                Vector3 target = Vector3Add(V(p.ankle[i]), Vector3Add(Vector3Lerp({0.0f, 0.04f, -0.03f}, {0.0f, 0.0f, 0.0f}, std::max(out, freeW)),
                                                                      {side * fit.ankleOut[i] * legFit, 0.0f, 0.0f}));
                const float l1 = Vector3Distance(bindPos[leg[i]], bindPos[b]), l2 = Vector3Distance(bindPos[foot[i]], bindPos[leg[i]]);
                if (freeW > 0.0f) {
                    // Suelta, la pierna de la pose (cadera -> tobillo) llevada al largo de la del modelo: la pose tiene las
                    // proporciones del piloto generado y con el tobillo donde ella lo pide la rodilla del modelo quedaba doblada.
                    const float poseLen = (p.knee[i] - p.hip[i]).Length() + (p.ankle[i] - p.knee[i]).Length();
                    if (poseLen > 1e-3f) {
                        const Vector3 scaled = Vector3Add(pos[b], Vector3Scale(V(p.ankle[i] - p.hip[i]), (l1 + l2) / poseLen));
                        target = Vector3Lerp(target, scaled, freeW);
                    }
                }
                // Rodillas adelante y apenas abiertas; con la pata afuera, adelante, arriba y afuera; suelta, hacia donde la
                // dobla la pose del truco (la rodilla de la pose respecto de la recta cadera-tobillo).
                Vector3 pole = Vector3Lerp({side * 0.25f, 0.0f, 1.0f}, {side * 0.5f, 0.6f, 0.8f}, out);
                // Colgado (carreras), la rodilla de adentro hacia el piso: afuera, adelante y apenas abajo (la de la pose,
                // "rodilla al piso", no llegaba al modelo: el polo era siempre adelante); la de afuera, arriba contra el tanque.
                if (hang > 0.0f) pole = Vector3Lerp(pole, side == hangSide ? Vector3{side * kHangKneeOut, -0.15f, 0.55f} : Vector3{side * 0.1f, 0.5f, 1.0f}, hang);
                if (freeW > 0.0f) {
                    const Vector3 bend = Vector3Subtract(V(p.knee[i]), Vector3Scale(Vector3Add(V(p.hip[i]), V(p.ankle[i])), 0.5f));
                    // Pesa según cuánto está doblada: con la pierna casi estirada (heel clicker) la dirección de la rodilla de la
                    // pose no está definida y daba vuelta de un cuadro al otro (la pierna giraba 80-150° sobre sí misma).
                    const float bendLen = Vector3Length(bend);
                    if (bendLen > 1e-4f) pole = Vector3Lerp(pole, Vector3Scale(bend, 1.0f / bendLen), freeW * mu::Smoothstep(0.01f, 0.05f, bendLen));
                }
                // Y abiertas lo que pida fit para no meterse en el tanque ni en los plásticos.
                const Vector3 kneeAt = OpenJoint(pos[b], target, l1, l2, Vector3Add(pos[b], pole), side, fit.open[i] * legFit);
                TwoBoneIK(pos[b], target, l1, l2, kneeAt, knee[i], ankle[i]);
                // El frente de la rodilla: doblada, el que da el plano de la pierna (del lado contrario a donde se pliega la
                // canilla); casi estirada, el del polo (afuera de la recta cadera-tobillo). Con uno solo, el muslo daba media
                // vuelta en un cuadro cuando la cadera pasaba los 90° (nac-nac) o la pierna se estiraba.
                {
                    const Vector3 n = Vector3Normalize(Vector3Subtract(ankle[i], pos[b]));
                    Vector3 f = Vector3Subtract(kneeAt, pos[b]);
                    f = Vector3Subtract(f, Vector3Scale(n, Vector3DotProduct(f, n)));
                    if (Vector3Length(f) > 1e-5f) kneeFront[i] = Vector3Normalize(f);
                    const Vector3 dT = Vector3Normalize(Vector3Subtract(knee[i], pos[b])), dS = Vector3Normalize(Vector3Subtract(ankle[i], knee[i]));
                    const Vector3 bendT = Vector3Negate(Vector3Subtract(dS, Vector3Scale(dT, Vector3DotProduct(dS, dT))));
                    const Vector3 bendS = Vector3Subtract(dT, Vector3Scale(dS, Vector3DotProduct(dT, dS)));
                    const float w = mu::Smoothstep(0.15f, 0.35f, Vector3Length(bendT));
                    auto mix = [&](Vector3 bend, Vector3 along) {
                        Vector3 pf = Vector3Subtract(kneeFront[i], Vector3Scale(along, Vector3DotProduct(kneeFront[i], along)));
                        pf = Vector3Length(pf) > 1e-4f ? Vector3Normalize(pf) : pf;
                        const Vector3 bf = Vector3Length(bend) > 1e-4f ? Vector3Normalize(bend) : pf;
                        return Vector3Lerp(pf, bf, w);
                    };
                    thighFront[i] = mix(bendT, dT);
                    shinFront[i] = mix(bendS, dS);
                }
                aimFront(leg[i], knee[i], thighFront[i]);
            } else if (b == leg[i]) {
                aimFront(foot[i], ankle[i], shinFront[i]);
            } else if (b == foot[i]) {
                // Pie hacia adelante, casi plano; estirado afuera, con la punta para arriba; suelto (truco), hacia `toe`.
                Vector3 dir = Vector3Lerp({0.0f, -0.05f, 0.15f}, {side * 0.03f, 0.08f, 0.15f}, p.legOut[i]);
                // (Girando, no mezclando vectores: con la punta del truco casi opuesta, la mezcla pasaba cerca de cero y el pie
                // daba media vuelta en un cuadro.)
                dir = Vector3Scale(DirSlerp(dir, V(p.toe[i]), p.legFree[i]), 0.15f);
                aim(toe[i], Vector3Add(pos[b], dir));
            }
        }
    }
    return reach;
}

void RiderModel::BindToRagdoll(const RiderPose& p, const RiderRagdoll& ragdoll)
{
    JPH::Mat44 locals[RiderPart::Count];
    for (int i = 0; i < RiderPart::Count; ++i) locals[i] = ragdoll.PartSpawnLocal(i);
    BindToParts(p, locals);
}

void RiderModel::BindToParts(const RiderPose& p, const JPH::Mat44* partLocal)
{
    const int n = (int)pos.size();
    auto bind = [&](std::vector<Vector3>& rp, std::vector<Quaternion>& rd) {
        rp.resize(n);
        rd.resize(n);
        for (int b = 0; b < n; ++b) {
            const JPH::Mat44& part = partLocal[partOf[b]];
            const Quaternion partRot = QuaternionInvert(Q(part.GetQuaternion()));
            rp[b] = Vector3RotateByQuaternion(Vector3Subtract(pos[b], V(part.GetTranslation())), partRot);
            rd[b] = QuaternionMultiply(partRot, delta[b]);
        }
    };
    // Lo que se veía (el cuerpo con masa, en espacio de la moto como las partes al aparecer): de ahí arranca.
    const bool fromShown = motionShown;
    if (fromShown) bind(relPosShown, relDeltaShown);
    // El ragdoll se arma con la pose sin reacciones (es física: no cambia); el modelo va pasando a ella.
    PoseOnBike(p);
    bind(relPos, relDelta);
    ragdollBlend = fromShown ? 0.0f : 1.0f;
    motionShown = false;
    motion.Reset();
}

void RiderModel::PoseFromRagdoll(const RiderRagdoll& ragdoll, float alpha)
{
    JPH::Mat44 parts[RiderPart::Count];
    for (int i = 0; i < RiderPart::Count; ++i) parts[i] = ragdoll.PartTransform(i, alpha);
    PoseFromParts(parts);
}

void RiderModel::PoseFromParts(const JPH::Mat44* partWorld)
{
    if (relPos.size() != pos.size()) return;
    // Recién caído: de la pose que se veía a la del ragdoll, suave (smoothstep) en kRagdollBlend s.
    const bool blend = ragdollBlend < 1.0f && relPosShown.size() == relPos.size();
    const float s = mu::Smoothstep(0.0f, 1.0f, ragdollBlend);
    ragdollBlend = std::min(1.0f, ragdollBlend + GetFrameTime() / kRagdollBlend);
    for (int b = 0; b < realBones; ++b) {
        const JPH::Mat44& part = partWorld[partOf[b]];
        const Quaternion rot = Q(part.GetQuaternion());
        const Vector3 rp = blend ? Vector3Lerp(relPosShown[b], relPos[b], s) : relPos[b];
        const Quaternion rd = blend ? QuaternionSlerp(relDeltaShown[b], relDelta[b], s) : relDelta[b];
        pos[b] = Vector3Add(V(part.GetTranslation()), Vector3RotateByQuaternion(rp, rot));
        delta[b] = QuaternionMultiply(rot, rd);
    }
    // Al salir despedido suelta el manubrio: los dedos se abren en un cuarto de segundo.
    gripAmount = std::max(0.0f, gripAmount - GetFrameTime() / 0.25f);
    CurlFingers();
}

void RiderModel::Skin()
{
    // Cada vértice: suma pesada de sus huesos, v' = pos + giro * (v - posReposo).
    const int n = (int)pos.size();
    std::vector<Matrix> xf(n), rot(n);
    for (int b = 0; b < n; ++b) {
        rot[b] = QuaternionToMatrix(delta[b]);
        xf[b] = MatrixMultiply(MatrixMultiply(MatrixTranslate(-bindPos[b].x, -bindPos[b].y, -bindPos[b].z), rot[b]),
                               MatrixTranslate(pos[b].x, pos[b].y, pos[b].z));
    }
    Mesh& m = model.meshes[0];
    for (int v = 0; v < m.vertexCount; ++v) {
        Vector3 p = {0.0f, 0.0f, 0.0f}, nrm = {0.0f, 0.0f, 0.0f};
        for (int k = 0; k < 4; ++k) {
            const float w = skinWeight[v * 4 + k];
            if (w <= 0.0f) continue;
            const int b = skinBone[v * 4 + k];
            p = Vector3Add(p, Vector3Scale(Vector3Transform(bindVerts[v], xf[b]), w));
            nrm = Vector3Add(nrm, Vector3Scale(Vector3Transform(bindNormals[v], rot[b]), w));
        }
        nrm = Vector3Normalize(nrm);
        std::memcpy(&m.animVertices[v * 3], &p, sizeof(Vector3));
        std::memcpy(&m.animNormals[v * 3], &nrm, sizeof(Vector3));
    }
    UpdateMeshBuffer(m, 0, m.animVertices, m.vertexCount * 3 * (int)sizeof(float), 0);
    UpdateMeshBuffer(m, 2, m.animNormals, m.vertexCount * 3 * (int)sizeof(float), 0);
}

void RiderModel::Draw(Renderer& r, const Matrix& world)
{
    BuildShape(r);                           // la forma de la moto que se acaba de dibujar (para las poses que siguen)
    r.SetGloss(0.2f);
    const bool painted = paint.on && suitMask.id != 0;
    if (painted) r.SetSuit({1, suitMask, {suitRef[0], suitRef[1]}, {paint.main, paint.second}});
    r.DrawMeshTextured(model.meshes[0], world, texture);
    if (painted) r.SetSuit({});
}

// Detector de saltos (MOTOSIM_SALTOS). Un salto es un cambio de velocidad de un hueso que no sale de un movimiento (la
// diferencia segunda: lo que se corrió en este cuadro respecto de lo que venía corriéndose), llevado a un cuadro de 60 fps:
// un movimiento rápido pero continuo (el pie de un truco a 6 m/s) no cuenta; una rodilla que pasa de una solución a otra, sí.
// Imprime "SALTO t= hueso=cm/grados" con lo que pudo dispararlo: los ajustes de la IK (Fit, lo buscado: antes>ahora) y la
// pose pedida. Al salir, un resumen por hueso.
void RiderModel::CheckJumps(double time, float dt, const RiderPose& base, const RiderPose& shown, const Bike& bike)
{
    struct Worst {
        float cm = 0.0f, deg = 0.0f;
        double t = 0.0;
        int count = 0;
    };
    static std::map<std::string, Worst> worst;
    static bool registered = false;
    if (!registered) {
        registered = true;
        std::atexit([] {
            std::printf("SALTOS resumen (por hueso: veces, lo peor en cm y grados por cuadro de 60 fps, cuándo)\n");
            for (const auto& [name, w] : worst) std::printf("  %-14s %4d  %5.1f cm  %5.1f grados  t=%.2f\n", name.c_str(), w.count, w.cm, w.deg, w.t);
            std::fflush(stdout);
        });
    }
    // Reaparecer (Bike::Reset) no es un salto: la moto entera vuelve a la pista (con el manubrio derecho, el piloto de nuevo).
    static std::map<std::pair<const void*, const void*>, unsigned> jumpReset;
    const auto keyJ = std::make_pair((const void*)this, (const void*)&bike);
    auto seenJ = jumpReset.find(keyJ);
    const bool reappeared = seenJ != jumpReset.end() && seenJ->second != bike.resetCount;
    jumpReset[keyJ] = bike.resetCount;
    const bool fresh = jumpTime < 0.0 || time <= jumpTime || time > jumpTime + 0.25 || jumpPos.size() != pos.size() || dt <= 1e-5f || reappeared;
    const int n = (int)pos.size();
    std::vector<Vector3> vel(n), angVel(n);
    if (!fresh) {
        const float h = std::max(dt, 1.0f / 240.0f);
        for (int b = 0; b < n; ++b) {
            vel[b] = Vector3Scale(Vector3Subtract(pos[b], jumpPos[b]), 1.0f / h);
            Quaternion dq = QuaternionMultiply(delta[b], QuaternionInvert(jumpRot[b]));
            if (dq.w < 0.0f) dq = {-dq.x, -dq.y, -dq.z, -dq.w};
            Vector3 axis;
            float angle;
            QuaternionToAxisAngle(dq, &axis, &angle);
            angVel[b] = Vector3Scale(axis, angle / h);
        }
    }
    if (!fresh && jumpHasVel) {
        std::string what;
        for (int b = 0; b < realBones; ++b) {
            const char* name = model.bones[b].name;
            const float d = Vector3Distance(vel[b], jumpVel[b]) / 60.0f;
            const float ang = Vector3Distance(angVel[b], jumpAngVel[b]) / 60.0f;
            if (d <= kJumpDist && ang <= kJumpAngle) continue;
            Worst& w = worst[name];
            ++w.count;
            if (d * 100.0f > w.cm) w.cm = d * 100.0f;
            if (mu::Deg(ang) > w.deg) {
                w.deg = mu::Deg(ang);
                w.t = time;
            }
            char buf[96];
            std::snprintf(buf, sizeof buf, " %s=%.1fcm/%.0fg", name, d * 100.0f, mu::Deg(ang));
            what += buf;
        }
        // Lo que pide la pose (compartida: Bike::RiderPoseLocal / ApplyTrickPose): el mismo cambio de velocidad de tobillos y
        // muñecas pedidos. Si es grande, el salto viene de la pose y no de la IK del modelo.
        {
            struct Prev { JPH::Vec3 p[4], v[4]; int n = 0; };
            static std::map<const void*, Prev> prevPose;
            Prev& pp = prevPose[this];
            const JPH::Vec3 now[4] = {shown.ankle[0], shown.ankle[1], shown.grip[0], shown.grip[1]};
            float poseKick = 0.0f;
            for (int k = 0; k < 4; ++k) {
                const JPH::Vec3 v = (now[k] - pp.p[k]) / std::max(dt, 1.0f / 240.0f);
                if (pp.n >= 2) poseKick = std::max(poseKick, (v - pp.v[k]).Length() / 60.0f);
                pp.v[k] = v;
                pp.p[k] = now[k];
            }
            pp.n = std::min(pp.n + 1, 2);
            if (!what.empty()) {
                char buf[48];
                std::snprintf(buf, sizeof buf, " [pose %.1fcm]", poseKick * 100.0f);
                what += buf;
            }
        }
        if (!what.empty()) {
            const Fit& a = jumpFit;
            const Fit& f = lastFit;
            std::printf("SALTO t=%.3f dt=%.3f%s | fit pitch %.2f>%.2f shift %.3f>%.3f medio %.2f>%.2f atras %.2f>%.2f sube %.3f>%.3f centro %.2f>%.2f "
                        "abre %.2f,%.2f>%.2f,%.2f tob %.3f,%.3f>%.3f,%.3f codo %.2f,%.2f>%.2f,%.2f alcance %.3f | pose lean %.2f afuera %.2f,%.2f truco %.2f "
                        "pieLibre %.2f,%.2f manoLibre %.2f,%.2f colgado %.2f agachado %.2f | moto v %.1f roll %.0f pitch %.0f tilt %.0f side %.2f steer %.1f>%.1f\n",
                        time, dt, what.c_str(), a.pitch, f.pitch, a.shift, f.shift, a.sideIn, f.sideIn, a.backIn, f.backIn, a.lift, f.lift, a.center,
                        f.center, a.open[0], a.open[1], f.open[0], f.open[1], a.ankleOut[0], a.ankleOut[1], f.ankleOut[0], f.ankleOut[1], a.elbowOpen[0],
                        a.elbowOpen[1], f.elbowOpen[0], f.elbowOpen[1], lastReach, shown.lean, shown.legOut[0], shown.legOut[1], base.trick,
                        base.legFree[0], base.legFree[1], base.handFree[0], base.handFree[1], hang, tuckAmt, bike.forwardSpeed * 3.6f,
                        mu::Deg(bike.roll), mu::Deg(bike.pitch), mu::Deg(bike.bodyTilt), bike.riderSide, mu::Deg(jumpSteer), mu::Deg(bike.steerAngle));
        }
    }
    jumpPos.assign(pos.begin(), pos.end());
    jumpRot.assign(delta.begin(), delta.end());
    jumpVel = vel;
    jumpAngVel = angVel;
    jumpHasVel = !fresh;
    jumpSteer = bike.steerAngle;
    jumpFit = lastFit;
    jumpTime = time;
}
