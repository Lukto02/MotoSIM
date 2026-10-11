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
constexpr float kClavicleReach = 0.35f;  // rad que la clavícula lleva el hombro hacia el manubrio si queda lejos (antes que el torso)
constexpr float kThrottleRoll = 0.45f;   // rad que gira la mano derecha sobre el puño con el gas a fondo

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
constexpr float kOutW = 7.0f;                    // pata afuera (crítico)
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

bool PoseLog()
{
    static const bool on = std::getenv("MOTOSIM_PILOTO_LOG") != nullptr;
    return on;
}

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
    if (!valid || jump > std::max(1.0f, 120.0f * dt) || turn > 0.6f) {
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
        const float yawT = mu::Clamp(std::atan2(lookL.GetX(), lookL.GetZ()), -1.0f, 1.0f);
        const float pitchT = mu::Clamp(std::asin(mu::Clamp(lookL.GetY(), -1.0f, 1.0f)), -0.9f, 0.35f);
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
    const Vec3 hipsBase = Vec3(hipsS[0].x, hipsS[1].x, hipsS[2].x) + base.trickHips;
    const Vec3 shouldersBase = Vec3(shouldersS[0].x, shouldersS[1].x, shouldersS[2].x) + base.trickShoulders;
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

    pos = bindPos;
    delta.assign(bindPos.size(), QuaternionIdentity());
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
    model = Model{};
    loaded = false;
}

void RiderModel::PoseOnBike(const RiderPose& p)
{
    // Las manos no se sueltan del manubrio: si con la inclinación de la pose no llegan (sobre todo con
    // el piloto tirado atrás), el torso se inclina más hacia adelante, lo justo para que lleguen.
    // Primero se inclina el torso (hasta un límite razonable); si todavía no llega, la cadera se acerca
    // al manubrio: tirado atrás queda la cola atrás y los brazos estirados, no acostado sobre el tanque.
    gripAmount = 1.0f;
    for (int i = 0; i < 2; ++i) handOpen[i] = p.handFree[i];   // truco: la mano suelta se abre
    throttleS += (p.throttle - throttleS) * (1.0f - std::exp(-14.0f * GetFrameTime()));
    lastExtraPitch = lastHipShift = 0.0f;
    if ((lastReach = SolveBody(p, 0.0f, 0.0f)) <= kMaxReach) return;
    auto search = [&](float hiValue, auto solve) {          // el valor mínimo con el que las manos llegan
        float lo = 0.0f, hi = hiValue;
        for (int k = 0; k < 10; ++k) {
            const float mid = 0.5f * (lo + hi);
            if (solve(mid) > kMaxReach) lo = mid;
            else hi = mid;
        }
        lastReach = solve(hi);
        return hi;
    };
    if (SolveBody(p, kMaxExtraPitch, 0.0f) <= kMaxReach) {
        lastExtraPitch = search(kMaxExtraPitch, [&](float pitch) { return SolveBody(p, pitch, 0.0f); });
    } else {
        lastExtraPitch = kMaxExtraPitch;
        lastHipShift = search(kMaxHipShift, [&](float shift) { return SolveBody(p, kMaxExtraPitch, shift); });
    }
}

void RiderModel::PoseOnBike(const RiderPose& base, const Bike& bike, JPH::Vec3 bikePos, JPH::Quat bikeRot, double time, float ground, float smooth)
{
    const auto t0 = std::chrono::steady_clock::now();
    if (!motionShown || time < motionTime || time > motionTime + 0.5) motion.Reset();   // volvió a la moto, o el tiempo saltó
    RiderMotion::Input in;
    in.pos = bikePos;
    in.rot = bikeRot;
    in.dt = motionTime < 0.0 ? 0.0f : (float)std::max(0.0, time - motionTime);
    motionTime = time;
    in.ground = ground;
    in.smooth = smooth;
    const BikeStyleDef& style = GetBikeStyle(bike.Params() ? bike.Params()->visualStyle : 0);
    elbowsIn = style.tuck > 0.0f || style.hangOff > 0.0f ? 1.0f : 0.0f;   // la de carreras: codos cerca del tanque
    const RiderPose p = motion.Apply(base, bike, in);
    headFree = true;
    PoseOnBike(p);
    headFree = false;
    motionShown = true;
    if (PoseLog()) {
        const RiderMotion& m = motion;
        const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
        std::printf("PILOTO %03d t=%.3f us=%.0f a=%.1f,%.1f,%.1f al=%.1f,%.1f,%.1f cadera=%.3f,%.3f,%.3f torso=%.1f,%.1f cabeza=%.0f,%.0f,%.0f afuera=%.2f,%.2f "
                    "deriva=%.0f alcance=%.3f +torso=%.2f +cadera=%.3f\n",
                    (int)(((uintptr_t)this >> 4) % 1000), time, us, m.accLocal.GetX(), m.accLocal.GetY(), m.accLocal.GetZ(), m.angAccLocal.GetX(), m.angAccLocal.GetY(),
                    m.angAccLocal.GetZ(), m.dof[RiderMotion::HipsX].x, m.dof[RiderMotion::HipsY].x, m.dof[RiderMotion::HipsZ].x,
                    mu::Deg(m.dof[RiderMotion::TorsoPitch].x), mu::Deg(m.dof[RiderMotion::TorsoRoll].x), mu::Deg(m.dof[RiderMotion::HeadYaw].x),
                    mu::Deg(m.dof[RiderMotion::HeadPitch].x), mu::Deg(m.dof[RiderMotion::HeadRoll].x), m.dof[RiderMotion::OutL].x,
                    m.dof[RiderMotion::OutR].x, mu::Deg(m.drift), lastReach, lastExtraPitch, lastHipShift);
    }
}

float RiderModel::SolveBody(const RiderPose& p, float extraPitch, float hipShift)
{
    // Cada hueso: si no tiene un ajuste propio, sigue a su padre (cinemática directa).
    const float forward = std::max(0.0f, p.lean);
    // Los ajustes de la posición de ataque (cadera más baja, torso más adelante) no van en un truco: su pose ya es la del modelo.
    const float seated = 1.0f - p.trick;
    const Vector3 hipsTarget = Vector3Add(V(p.hips), {0.0f, -kHipsDrop * seated + 0.3f * hipShift, hipShift - kForwardHipsBack * forward * seated});
    const Vector3 torsoBind = Vector3Subtract(bindPos[neck], bindPos[hips]);
    Vector3 torsoTarget = Vector3Subtract(V(p.shoulders), V(p.hips));
    const float pitch = (kExtraLean + kForwardLean * forward) * seated + extraPitch;
    torsoTarget = Vector3RotateByAxisAngle(torsoTarget, {1.0f, 0.0f, 0.0f}, pitch);   // + adelante
    float reach = 0.0f;                                     // distancia hombro-manubrio / largo del brazo
    const Quaternion torsoTurn = FromTo(torsoBind, torsoTarget);
    // La inclinación la toma sobre todo la pelvis (la bisagra está en las articulaciones de la cadera,
    // como en la posición de ataque) y un poco cada vértebra: espalda casi recta, sin quiebre en la
    // cintura. Girando sólo la columna la panza se inflaba y la espalda baja se doblaba.
    const Quaternion pelvisTurn = QuaternionSlerp(QuaternionIdentity(), torsoTurn, 0.75f);
    const Quaternion spineStep = QuaternionSlerp(QuaternionIdentity(), torsoTurn, 0.25f / 3.0f);

    Vector3 elbow[2], wrist[2], knee[2], ankle[2];
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

        if (b == hips) {
            pos[b] = hipsTarget;
            delta[b] = pelvisTurn;
        } else if (b == spineChain[0] || b == spineChain[1] || b == spineChain[2]) {
            delta[b] = QuaternionMultiply(spineStep, delta[b]);
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
                const Vector3 attack = Vector3Lerp({side * 1.0f, 0.1f, -0.3f}, {side * 0.75f, -0.25f, -0.3f}, elbowsIn);
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
                        TwoBoneIK(pos[b], target, l1, l2, pole, elbow[i], wrist[i]);
                        fore = Vector3Normalize(Vector3Subtract(wrist[i], elbow[i]));
                    }
                } else {
                    TwoBoneIK(pos[b], target, l1, l2, pole, elbow[i], wrist[i]);
                }
                // La mano suelta no tiene que llegar a nada (no cuenta para inclinar el torso).
                reach = std::max(reach, Vector3Distance(pos[b], target) / (l1 + l2) * (1.0f - freeW));
                aim(foreArm[i], elbow[i]);
            } else if (b == foreArm[i]) {
                aim(hand[i], wrist[i]);
            } else if (b == hand[i] && grip[i].valid) {
                delta[b] = handRot[i];
            } else if (b == upLeg[i]) {
                const float out = p.legOut[i];                              // pata afuera en la curva
                const float freeW = p.legFree[i];                           // truco: el pie dejó la estribera
                // El pie apoya en la estribera; afuera (o suelto), el tobillo va donde lo pide la pose.
                Vector3 target = Vector3Add(V(p.ankle[i]), Vector3Lerp({0.0f, 0.04f, -0.03f}, {0.0f, 0.0f, 0.0f}, std::max(out, freeW)));
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
                if (freeW > 0.0f) {
                    const Vector3 bend = Vector3Subtract(V(p.knee[i]), Vector3Scale(Vector3Add(V(p.hip[i]), V(p.ankle[i])), 0.5f));
                    if (Vector3Length(bend) > 1e-4f) pole = Vector3Lerp(pole, Vector3Normalize(bend), freeW);
                }
                TwoBoneIK(pos[b], target, l1, l2, Vector3Add(pos[b], pole), knee[i], ankle[i]);
                aim(leg[i], knee[i]);
            } else if (b == leg[i]) {
                aim(foot[i], ankle[i]);
            } else if (b == foot[i]) {
                // Pie hacia adelante, casi plano; estirado afuera, con la punta para arriba; suelto (truco), hacia `toe`.
                Vector3 dir = Vector3Lerp({0.0f, -0.05f, 0.15f}, {side * 0.03f, 0.08f, 0.15f}, p.legOut[i]);
                dir = Vector3Lerp(dir, Vector3Scale(V(p.toe[i]), 0.15f), p.legFree[i]);
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
    r.SetGloss(0.2f);
    r.DrawMeshTextured(model.meshes[0], world, texture);
}
