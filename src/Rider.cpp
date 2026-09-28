// Dibujo del piloto y ragdoll para las caídas.
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>

#include "Rider.h"

#include "MathUtil.h"
#include "PhysicsWorld.h"
#include "Terrain.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using JPH::Mat44;
using JPH::Quat;
using JPH::Vec3;

namespace {
// Visera del color de los plásticos de la moto (Bike.cpp); el resto del equipo va en las mallas.
const Color kPlastic = {226, 74, 32, 255};

// Unitario perpendicular a `axis`, lo más parecido posible a `hint`.
Vec3 Perpendicular(Vec3 axis, Vec3 hint)
{
    const Vec3 p = hint - axis * hint.Dot(axis);
    return p.LengthSq() > 1e-6f ? p.Normalized() : axis.GetNormalizedPerpendicular();
}

class OnlyBodyFilter final : public JPH::BodyFilter {
public:
    explicit OnlyBodyFilter(JPH::BodyID id) : id(id) {}
    bool ShouldCollide(const JPH::BodyID& other) const override { return other == id; }

private:
    JPH::BodyID id;
};
} // namespace

bool RiderCollisionFilter::CanCollide(const JPH::CollisionGroup& a, const JPH::CollisionGroup& b) const
{
    // `a` es siempre una parte del piloto (el grupo que tiene este filtro).
    if (b.GetGroupID() == kRiderCollisionGroup) return ((pairs[a.GetSubGroupID()] >> b.GetSubGroupID()) & 1u) == 0;
    if (b.GetGroupID() == kBikeCollisionGroup) return withBike[a.GetSubGroupID()];
    return true;
}

void RiderCollisionFilter::DisablePair(int partA, int partB)
{
    pairs[partA] |= 1u << partB;
    pairs[partB] |= 1u << partA;
}

void RiderPartLocals(const RiderPose& p, Mat44* out)
{
    // Las mismas cuentas que Spawn: centro de cada parte y su eje Y a lo largo del hueso.
    auto bone = [&](int part, Vec3 a, Vec3 b) { out[part] = Mat44::sRotationTranslation(Quat::sFromTo(Vec3::sAxisY(), (b - a).Normalized()), (a + b) * 0.5f); };
    out[RiderPart::Pelvis] = Mat44::sTranslation(p.hips + Vec3(0, 0.02f, 0));
    bone(RiderPart::Torso, p.hips, p.shoulders);
    out[RiderPart::Head] = Mat44::sTranslation(p.head);
    for (int i = 0; i < 2; ++i) {
        bone(RiderPart::Thigh + i, p.hip[i], p.knee[i]);
        bone(RiderPart::Shin + i, p.knee[i], p.ankle[i]);
        bone(RiderPart::UpperArm + i, p.shoulder[i], p.elbow[i]);
        bone(RiderPart::Forearm + i, p.elbow[i], p.grip[i]);
    }
}

std::vector<RiderPiece> RiderPieces(const RiderPose& p)
{
    std::vector<RiderPiece> out;
    auto piece = [&](int part, RiderMesh::Id mesh, Vec3 center, Vec3 size, Quat q = Quat::sIdentity(), Color c = WHITE) {
        out.push_back({part, Mat44::sRotationTranslation(q, center) * Mat44::sScale(size), c, mesh});
    };
    // Pieza estirada entre dos articulaciones (piernas, brazos, torso, cuello).
    auto limb = [&](int part, RiderMesh::Id mesh, Vec3 a, Vec3 b, float thickX, float thickZ) {
        const Vec3 d = b - a;
        const float len = d.Length();
        if (len > 1e-4f) piece(part, mesh, (a + b) * 0.5f, Vec3(thickX, len, thickZ), Quat::sFromTo(Vec3::sAxisY(), d / len));
    };
    for (int i = 0; i < 2; ++i) {
        limb(RiderPart::Thigh + i, RiderMesh::Thigh, p.hip[i], p.knee[i], 0.15f, 0.15f);
        limb(RiderPart::Shin + i, RiderMesh::Shin, p.knee[i], p.ankle[i], 0.13f, 0.13f);
        piece(RiderPart::Shin + i, RiderMesh::Foot, p.foot[i], Vec3(0.11f, 0.11f, 0.27f));
        limb(RiderPart::UpperArm + i, RiderMesh::UpperArm, p.shoulder[i], p.elbow[i], 0.10f, 0.10f);
        limb(RiderPart::Forearm + i, RiderMesh::Forearm, p.elbow[i], p.grip[i], 0.085f, 0.085f);
        piece(RiderPart::Forearm + i, RiderMesh::Fist, p.grip[i], Vec3(0.075f, 0.075f, 0.09f));
    }
    limb(RiderPart::Torso, RiderMesh::Torso, p.hips, p.shoulders, 0.38f, 0.22f);
    limb(RiderPart::Torso, RiderMesh::Neck, p.shoulders + Vec3(0.0f, -0.03f, 0.01f), p.head + Vec3(0.0f, -0.08f, -0.03f), 0.19f, 0.17f);
    piece(RiderPart::Pelvis, RiderMesh::Pelvis, p.hips + Vec3(0.0f, 0.02f, 0.0f), Vec3(0.34f, 0.14f, 0.24f));
    // Casco y visera, con la mirada un poco hacia abajo.
    const Quat headTilt = Quat::sRotation(Vec3::sAxisX(), mu::Rad(8.0f));
    piece(RiderPart::Head, RiderMesh::Helmet, p.head, Vec3::sReplicate(1.0f), headTilt);
    piece(RiderPart::Head, RiderMesh::Peak, p.head, Vec3::sReplicate(1.0f), headTilt, kPlastic);
    return out;
}

void DrawRiderPiece(Renderer& r, const Mat44& world, RiderMesh::Id mesh, Color color)
{
    // Brillo por material, en el orden de RiderMesh: ropa mate, botas de plástico, casco pintado.
    static const float kGloss[RiderMesh::Count] = {0.12f, 0.12f, 0.3f, 0.12f, 0.4f, 0.4f, 0.12f, 0.12f, 0.1f, 0.65f, 0.55f};
    r.SetGloss(kGloss[mesh]);
    r.DrawMeshColored(r.rider[mesh], ToRl(world), color);
}

void RiderRagdoll::Spawn(PhysicsWorld& w, const Terrain& terrain, const RiderPose& p, Vec3 bikeCom, Quat bikeRot,
                         Vec3 linVel, Vec3 angVel)
{
    Remove();
    world = &w;
    const Mat44 bikeM = Mat44::sRotationTranslation(bikeRot, bikeCom);

    // Cada parte en espacio local de la moto: centro, orientación (Y a lo largo del hueso), forma y
    // masa (suman ~75 kg, Bike::kRiderMass).
    struct Def {
        Vec3 center;
        Quat rot;
        JPH::ShapeRefC shape;
        float mass;
    };
    Def defs[RiderPart::Count];
    auto bone = [&](int part, Vec3 a, Vec3 b, const JPH::Shape* shape, float mass) {
        defs[part] = {(a + b) * 0.5f, Quat::sFromTo(Vec3::sAxisY(), (b - a).Normalized()), shape, mass};
    };
    auto limb = [&](int part, Vec3 a, Vec3 b, float radius, float mass) {
        bone(part, a, b, new JPH::CapsuleShape(std::max(0.01f, 0.5f * (b - a).Length() - radius), radius), mass);
    };
    defs[RiderPart::Pelvis] = {p.hips + Vec3(0, 0.02f, 0), Quat::sIdentity(), new JPH::BoxShape(Vec3(0.16f, 0.07f, 0.12f)), 12.0f};
    bone(RiderPart::Torso, p.hips, p.shoulders, new JPH::BoxShape(Vec3(0.17f, 0.5f * (p.shoulders - p.hips).Length(), 0.10f)), 26.0f);
    defs[RiderPart::Head] = {p.head, Quat::sIdentity(), new JPH::SphereShape(0.15f), 5.0f};
    for (int i = 0; i < 2; ++i) {
        limb(RiderPart::Thigh + i, p.hip[i], p.knee[i], 0.07f, 8.0f);
        limb(RiderPart::Shin + i, p.knee[i], p.ankle[i], 0.06f, 4.5f);
        limb(RiderPart::UpperArm + i, p.shoulder[i], p.elbow[i], 0.05f, 2.2f);
        limb(RiderPart::Forearm + i, p.elbow[i], p.grip[i], 0.045f, 1.6f);
    }

    // Con la moto acostada parte del piloto puede quedar bajo tierra: se sube entero lo necesario.
    float lift = 0.0f;
    for (const Def& d : defs) {
        const JPH::AABox b = d.shape->GetWorldSpaceBounds(Mat44::sRotationTranslation(bikeRot * d.rot, bikeM * d.center), Vec3::sReplicate(1.0f));
        const Vec3 c = b.GetCenter();
        lift = std::max(lift, terrain.Height(c.GetX(), c.GetZ()) + 0.02f - b.mMin.GetY());
    }
    const Vec3 up(0.0f, lift, 0.0f);
    RiderPartLocals(p, spawnLocal);

    filter = new RiderCollisionFilter();
    parts.resize(RiderPart::Count);
    for (int i = 0; i < RiderPart::Count; ++i) {
        const Def& d = defs[i];
        const Vec3 pos = bikeM * d.center + up;
        const Quat rot = (bikeRot * d.rot).Normalized();
        JPH::BodyCreationSettings bcs(d.shape, pos, rot, JPH::EMotionType::Dynamic, Layers::RAGDOLL);
        bcs.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        bcs.mMassPropertiesOverride.mMass = d.mass;
        bcs.mMotionQuality = JPH::EMotionQuality::LinearCast;   // partes chicas y rápidas: que no atraviesen el suelo
        bcs.mFriction = 0.8f;
        bcs.mRestitution = 0.1f;
        bcs.mLinearDamping = 0.05f;
        bcs.mAngularDamping = 0.5f;
        bcs.mCollisionGroup = JPH::CollisionGroup(filter, kRiderCollisionGroup, (JPH::CollisionGroup::SubGroupID)i);
        bcs.mLinearVelocity = linVel + angVel.Cross(bikeRot * d.center);   // velocidad de ese punto de la moto
        bcs.mAngularVelocity = angVel;
        Part& part = parts[i];
        part.body = w.Bodies().CreateBody(bcs);
        w.Bodies().AddBody(part.body->GetID(), JPH::EActivation::Activate);
        part.prevPos = part.currPos = pos;
        part.prevRot = part.currRot = rot;
    }

    // Articulaciones con los rangos del cuerpo humano (espacio de mundo, en la pose actual). Cada una
    // dice la dirección del hueso hijo en la postura de referencia (de pie, vista desde el padre), el
    // eje de flexión (girarla alrededor de él con ángulo positivo = flexionar) y cuánto se puede
    // flexionar para cada lado: una rodilla va de recta a doblada atrás, nunca al revés.
    // En el swing-twist de Jolt el giro alrededor del eje "plane" (Z) lo limita mNormalHalfConeAngle y
    // el giro alrededor del "normal" (Y = plane × twist, de costado) mPlaneHalfConeAngle, y el cono es
    // simétrico: se centra en el medio del rango (el eje twist del padre apunta ahí) para que el rango
    // quede asimétrico respecto de la postura de referencia. Si la pose de la moto cae fuera del rango
    // (piloto muy agachado o parado), el rango se agranda lo justo para que no salte al aparecer.
    // Más pasadas del solver que el resto del mundo: con golpes fuertes (la moto encima, el suelo a
    // 60 km/h) las 10 + 2 de siempre dejaban que brazos y piernas se pasaran de sus límites.
    constexpr JPH::uint kVelocitySteps = 24, kPositionSteps = 8;
    // Para las pruebas (--telemetry): cuánto se dobló cada articulación, medido en el espacio del padre.
    auto watchJoint = [&](const char* name, JPH::Constraint* c, bool isHinge, int parent, int child, Vec3 point, Vec3 t2, Vec3 n0, Vec3 a,
                          Vec3 out, Vec3 ref2, float lo, float hi, float side, float twist) {
        const JPH::Body& bp = *parts[parent].body;
        const JPH::Body& bc = *parts[child].body;
        const Quat toP = bp.GetRotation().Conjugated(), toC = bc.GetRotation().Conjugated();
        JointWatch jw;
        jw.name = name;
        jw.c = c;
        jw.hinge = isHinge;
        jw.parent = parent;
        jw.child = child;
        jw.anchorP = toP * (point - bp.GetPosition());
        jw.anchorC = toC * (point - bc.GetPosition());
        jw.n0 = toP * n0;
        jw.out = toP * out;
        jw.axis = toP * a;
        jw.dirC = toC * t2;
        jw.refC = toC * ref2;
        jw.d0 = toP * t2;
        jw.refP = toP * ref2;
        jw.lo = lo;
        jw.hi = hi;
        jw.side = side;
        jw.twist = twist;
        watch.push_back(jw);
    };
    // Rótula (columna, cuello, hombros, caderas): swing-twist de Jolt.
    auto joint = [&](const char* name, int parent, int child, Vec3 point, Vec3 childDir, Vec3 neutral, Vec3 flexAxis, float flexMin,
                     float flexMax, float sideways, float twist, float friction) {
        const Vec3 t2 = (bikeRot * childDir).Normalized();
        const Vec3 n0 = (bikeRot * neutral).Normalized();
        const Vec3 a = Perpendicular(n0, bikeRot * flexAxis);          // eje de flexión, perpendicular a la referencia
        const Vec3 out = a.Cross(n0);                                   // hacia donde lleva la flexión positiva
        const Vec3 inPlane = t2 - a * t2.Dot(a);
        const float flexNow = std::atan2(inPlane.Dot(out), inPlane.Dot(n0));
        const float sideNow = std::asin(mu::Clamp(t2.Dot(a), -1.0f, 1.0f));
        const float margin = mu::Rad(6.0f);
        const float lo = std::min(flexMin, flexNow - margin), hi = std::max(flexMax, flexNow + margin);
        const float mid = 0.5f * (lo + hi), half = std::min(0.5f * (hi - lo), mu::Rad(170.0f));
        const Vec3 t1 = (n0 * std::cos(mid) + out * std::sin(mid)).Normalized();

        JPH::SwingTwistConstraintSettings s;
        s.mSpace = JPH::EConstraintSpace::WorldSpace;
        s.mPosition1 = s.mPosition2 = bikeM * point + up;
        s.mTwistAxis1 = t1;
        s.mTwistAxis2 = t2;
        s.mPlaneAxis1 = a;
        s.mPlaneAxis2 = Perpendicular(t2, a);
        s.mNormalHalfConeAngle = half;                                  // flexión (alrededor del eje de flexión)
        s.mPlaneHalfConeAngle = std::max(sideways, std::fabs(sideNow) + margin);   // de costado
        s.mTwistMinAngle = -twist;
        s.mTwistMaxAngle = twist;
        s.mMaxFrictionTorque = friction;                                // sin esto brazos y piernas quedan de trapo
        s.mNumVelocityStepsOverride = kVelocitySteps;
        s.mNumPositionStepsOverride = kPositionSteps;
        joints.push_back(s.Create(*parts[parent].body, *parts[child].body));
        w.System().AddConstraint(joints.back());
        filter->DisablePair(parent, child);
        watchJoint(name, joints.back().GetPtr(), false, parent, child, s.mPosition1, t2, n0, a, out, s.mPlaneAxis2, lo, hi,
                   s.mPlaneHalfConeAngle, twist);
    };
    // Bisagras (rodillas y codos): HingeConstraint de Jolt, que no deja torcer ni doblar de costado y
    // acepta un rango asimétrico. El ángulo 0 es el hueso derecho (siguiendo al de arriba) y crece
    // flexionando, alrededor del eje que ya tiene doblado (o el anatómico si en la pose está casi recto).
    auto hinge = [&](const char* name, int parent, int child, Vec3 a0, Vec3 j, Vec3 c, Vec3 fallbackAxis, float flexMax) {
        const Vec3 d1 = (j - a0).Normalized(), d2 = (c - j).Normalized();
        const Vec3 cr = d1.Cross(d2);
        const Vec3 axisLocal = cr.Length() > 0.15f ? cr.Normalized() : fallbackAxis;
        const Vec3 n0 = (bikeRot * d1).Normalized();
        const Vec3 a = Perpendicular(n0, bikeRot * axisLocal);
        const Vec3 out = a.Cross(n0);
        const Vec3 t2 = (bikeRot * d2).Normalized();
        const Vec3 inPlane = (t2 - a * t2.Dot(a)).NormalizedOr(n0);
        const float flexNow = std::atan2(inPlane.Dot(out), inPlane.Dot(n0));
        const float lo = std::min(mu::Rad(-4.0f), flexNow - mu::Rad(4.0f)), hi = std::max(flexMax, flexNow + mu::Rad(4.0f));

        JPH::HingeConstraintSettings h;
        h.mSpace = JPH::EConstraintSpace::WorldSpace;
        h.mPoint1 = h.mPoint2 = bikeM * j + up;
        h.mHingeAxis1 = h.mHingeAxis2 = a;
        h.mNormalAxis1 = n0;                                            // ángulo 0: derecho
        h.mNormalAxis2 = inPlane;                                       // ahora: flexNow
        h.mLimitsMin = std::max(lo, -JPH::JPH_PI);
        h.mLimitsMax = std::min(hi, JPH::JPH_PI);
        h.mMaxFrictionTorque = 1.5f;
        h.mNumVelocityStepsOverride = kVelocitySteps;
        h.mNumPositionStepsOverride = kPositionSteps;
        joints.push_back(h.Create(*parts[parent].body, *parts[child].body));
        w.System().AddConstraint(joints.back());
        filter->DisablePair(parent, child);
        watchJoint(name, joints.back().GetPtr(), true, parent, child, h.mPoint1, t2, n0, a, out, a, lo, hi, 0.0f, 0.0f);
    };
    auto dir = [](Vec3 a, Vec3 b) { return (b - a).Normalized(); };
    // Espacio de la moto: +X izquierda, +Y arriba, +Z adelante. Girar +Y alrededor de +X lleva hacia +Z
    // (el torso y la cabeza se flexionan hacia adelante); girar -Y alrededor de -X también (la pierna y el
    // brazo colgando van hacia adelante).
    const Vec3 upAxis = Vec3::sAxisY(), fwdAxis = Vec3::sAxisZ(), lateral = Vec3::sAxisX();
    const Vec3 neck = p.shoulders + Vec3(0.0f, 0.08f, 0.04f);
    // Columna: de -25° (arqueada atrás) a 70° (agachado), 25° de costado, 20° de giro.
    joint("columna", RiderPart::Pelvis, RiderPart::Torso, p.hips, dir(p.hips, p.shoulders), upAxis, lateral, mu::Rad(-25.0f), mu::Rad(70.0f),
          mu::Rad(25.0f), mu::Rad(20.0f), 6.0f);
    // Cuello: de -40° a 50°, 30° de costado, 50° de giro (mirar a los costados).
    joint("cuello", RiderPart::Torso, RiderPart::Head, neck, dir(neck, p.head), upAxis, lateral, mu::Rad(-40.0f), mu::Rad(50.0f), mu::Rad(30.0f),
          mu::Rad(50.0f), 3.0f);
    for (int i = 0; i < 2; ++i) {
        // Hombro: brazo colgando como referencia; de -45° (atrás) a 165° (arriba), 80° de costado, 40° de giro.
        joint(i ? "hombro D" : "hombro I", RiderPart::Torso, RiderPart::UpperArm + i, p.shoulder[i], dir(p.shoulder[i], p.elbow[i]), -upAxis,
              -lateral, mu::Rad(-45.0f), mu::Rad(165.0f), mu::Rad(80.0f), mu::Rad(40.0f), 1.5f);
        // Codo: de recto a 150° hacia adelante / arriba del brazo.
        const Vec3 upper = dir(p.shoulder[i], p.elbow[i]);
        const Vec3 elbowAxis = Perpendicular(upper, upper.Cross(upAxis).LengthSq() > 0.05f ? upper.Cross(upAxis) : upper.Cross(fwdAxis));
        hinge(i ? "codo D" : "codo I", RiderPart::UpperArm + i, RiderPart::Forearm + i, p.shoulder[i], p.elbow[i], p.grip[i], elbowAxis, mu::Rad(150.0f));
        // Cadera: pierna colgando como referencia; de -25° (atrás) a 125° (la rodilla al pecho), 35° de costado, 20° de giro.
        joint(i ? "cadera D" : "cadera I", RiderPart::Pelvis, RiderPart::Thigh + i, p.hip[i], dir(p.hip[i], p.knee[i]), -upAxis, -lateral,
              mu::Rad(-25.0f), mu::Rad(125.0f), mu::Rad(35.0f), mu::Rad(20.0f), 3.0f);
        // Rodilla: de recta a 150° hacia atrás del muslo.
        const Vec3 thigh = dir(p.hip[i], p.knee[i]);
        hinge(i ? "rodilla D" : "rodilla I", RiderPart::Thigh + i, RiderPart::Shin + i, p.hip[i], p.knee[i], p.ankle[i], Perpendicular(thigh, thigh.Cross(-fwdAxis)),
              mu::Rad(150.0f));
        filter->DisablePair(RiderPart::Torso, RiderPart::Thigh + i);   // se tocan en la cadera
    }

    // Piezas del dibujo, pasadas al espacio local de su parte.
    for (const RiderPiece& pc : RiderPieces(p)) {
        const Def& d = defs[pc.part];
        pieces.push_back({pc.part, Mat44::sRotationTranslation(d.rot, d.center).InversedRotationTranslation() * pc.local, pc.color, pc.mesh});
    }
}

void RiderRagdoll::Remove()
{
    if (!world) return;
    if (report && !watch.empty()) {
        std::printf("ragdoll (flexión medida [rango], de costado [límite], giro [límite], separación, pasos fuera):\n");
        for (const JointWatch& j : watch) {
            std::printf("  %-10s %5.0f..%4.0f [%4.0f..%4.0f]  costado %3.0f [%3.0f]  giro %3.0f [%3.0f]  sep %4.1f cm  fuera %d/%d", j.name,
                        mu::Deg(j.flexMin), mu::Deg(j.flexMax), mu::Deg(j.lo), mu::Deg(j.hi), mu::Deg(j.sideMax), mu::Deg(j.side),
                        mu::Deg(j.twistMax), mu::Deg(j.twist), j.sepMax * 100.0f, j.over, j.steps);
            if (j.hinge)
                std::printf("   | jolt: ángulo %.0f..%.0f\n", mu::Deg(j.jSwingZmin), mu::Deg(j.jSwingZmax));
            else
                std::printf("   | jolt: giro %.0f [%.0f], Y %.0f [%.0f], Z %.0f..%.0f [±%.0f]\n", mu::Deg(j.jTwist), mu::Deg(j.twist), mu::Deg(j.jSwingY),
                            mu::Deg(j.jPlane), mu::Deg(j.jSwingZmin), mu::Deg(j.jSwingZmax), mu::Deg(j.jNormal));
        }
    }
    watch.clear();
    for (const JPH::Ref<JPH::Constraint>& c : joints) world->System().RemoveConstraint(c);
    for (const Part& p : parts) {
        world->Bodies().RemoveBody(p.body->GetID());
        world->Bodies().DestroyBody(p.body->GetID());
    }
    joints.clear();
    parts.clear();
    pieces.clear();
    filter = nullptr;
    world = nullptr;
}

void RiderRagdoll::PostPhysics(JPH::BodyID bike)
{
    JPH::CollideShapeSettings settings;
    settings.mMaxSeparationDistance = 0.01f;             // tiene que quedar al menos 1 cm de aire
    const OnlyBodyFilter onlyBike(bike);
    for (int i = 0; i < (int)parts.size(); ++i) {
        Part& p = parts[i];
        p.prevPos = p.currPos;
        p.prevRot = p.currRot;
        p.currPos = p.body->GetPosition();
        p.currRot = p.body->GetRotation();

        if (!filter->withBike[i]) {
            JPH::AnyHitCollisionCollector<JPH::CollideShapeCollector> hit;
            world->Query().CollideShape(p.body->GetShape(), Vec3::sReplicate(1.0f), p.body->GetCenterOfMassTransform(), settings,
                                        JPH::RVec3::sZero(), hit, {}, {}, onlyBike);
            filter->withBike[i] = !hit.HadHit();
        }
    }
    for (JointWatch& j : watch) {
        const JPH::Body& bp = *parts[j.parent].body;
        const JPH::Body& bc = *parts[j.child].body;
        const Quat rel = bp.GetRotation().Conjugated() * bc.GetRotation();
        const Vec3 d = (rel * j.dirC).Normalized();
        const Vec3 inPlane = d - j.axis * d.Dot(j.axis);
        const float flex = std::atan2(inPlane.Dot(j.out), inPlane.Dot(j.n0));
        const float side = std::fabs(std::asin(mu::Clamp(d.Dot(j.axis), -1.0f, 1.0f)));
        const float sep = ((bp.GetPosition() + bp.GetRotation() * j.anchorP) - (bc.GetPosition() + bc.GetRotation() * j.anchorC)).Length();
        float twist;                                     // giro sobre el hueso
        if (j.hinge) {
            // La perpendicular de referencia llevada por la flexión (el camino más corto de la dirección
            // inicial a la actual, que en una bisagra es el giro alrededor de su eje), contra la real.
            const Vec3 expect = Quat::sFromTo(j.d0, d) * j.refP, actual = rel * j.refC;
            twist = std::fabs(std::atan2(expect.Cross(actual).Dot(d), expect.Dot(actual)));
            const float angle = static_cast<const JPH::HingeConstraint*>(j.c)->GetCurrentAngle();
            j.jSwingZmin = std::min(j.jSwingZmin, angle);
            j.jSwingZmax = std::max(j.jSwingZmax, angle);
        } else {
            // En una rótula el giro depende de por dónde se llegó: vale el de la descomposición de Jolt.
            const JPH::SwingTwistConstraint* st = static_cast<const JPH::SwingTwistConstraint*>(j.c);
            Quat swing, tw;
            st->GetRotationInConstraintSpace().GetSwingTwist(swing, tw);
            auto angleOf = [](float c, float w) { return 2.0f * std::atan2(c, w); };
            twist = std::fabs(angleOf(tw.GetX(), tw.GetW()));
            j.jTwist = std::max(j.jTwist, twist);
            j.jSwingY = std::max(j.jSwingY, std::fabs(angleOf(swing.GetY(), swing.GetW())));
            j.jSwingZmin = std::min(j.jSwingZmin, angleOf(swing.GetZ(), swing.GetW()));
            j.jSwingZmax = std::max(j.jSwingZmax, angleOf(swing.GetZ(), swing.GetW()));
            j.jPlane = st->GetPlaneHalfConeAngle();
            j.jNormal = st->GetNormalHalfConeAngle();
        }
        j.flexMin = std::min(j.flexMin, flex);
        j.flexMax = std::max(j.flexMax, flex);
        j.sideMax = std::max(j.sideMax, side);
        j.twistMax = std::max(j.twistMax, twist);
        j.sepMax = std::max(j.sepMax, sep);
        const float margin = mu::Rad(5.0f);
        j.over += (flex < j.lo - margin || flex > j.hi + margin || side > j.side + margin || twist > j.twist + margin) ? 1 : 0;
        ++j.steps;
    }
}

void RiderRagdoll::Draw(Renderer& r, float alpha) const
{
    for (const Piece& pc : pieces) {
        const Part& p = parts[pc.part];
        const Mat44 m = Mat44::sRotationTranslation(p.prevRot.SLERP(p.currRot, alpha).Normalized(), p.prevPos + (p.currPos - p.prevPos) * alpha);
        DrawRiderPiece(r, m * pc.local, pc.mesh, pc.color);
    }
}

Vec3 RiderRagdoll::Position(float alpha) const
{
    const Part& p = parts[RiderPart::Pelvis];
    return p.prevPos + (p.currPos - p.prevPos) * alpha;
}

Vec3 RiderRagdoll::Velocity() const { return parts[RiderPart::Pelvis].body->GetLinearVelocity(); }

int RiderRagdoll::PartsCollidingWithBike() const { return filter ? (int)std::count(filter->withBike, filter->withBike + RiderPart::Count, true) : 0; }

Mat44 RiderRagdoll::PartTransform(int part, float alpha) const
{
    const Part& p = parts[part];
    return Mat44::sRotationTranslation(p.prevRot.SLERP(p.currRot, alpha).Normalized(), p.prevPos + (p.currPos - p.prevPos) * alpha);
}
