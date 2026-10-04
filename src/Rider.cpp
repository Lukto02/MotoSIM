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
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>

#include "Rider.h"

#include "MathUtil.h"
#include "PhysicsWorld.h"
#include "Terrain.h"

#include <algorithm>
#include <cmath>

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

    // Articulaciones (espacio de mundo, en la pose actual): el eje twist va a lo largo del hueso hijo;
    // `bend` limita el giro alrededor de bendAxis (hacia donde se dobla) y `sideways` el de costado.
    auto joint = [&](int parent, int child, Vec3 point, Vec3 childDir, Vec3 bendAxis, float bend, float sideways,
                     float twist, float friction) {
        JPH::SwingTwistConstraintSettings s;
        s.mSpace = JPH::EConstraintSpace::WorldSpace;
        s.mPosition1 = s.mPosition2 = bikeM * point + up;
        const Vec3 t = (bikeRot * childDir).Normalized();
        s.mTwistAxis1 = s.mTwistAxis2 = t;
        s.mPlaneAxis1 = s.mPlaneAxis2 = Perpendicular(t, bikeRot * bendAxis);
        s.mPlaneHalfConeAngle = bend;
        s.mNormalHalfConeAngle = sideways;
        s.mTwistMinAngle = -twist;
        s.mTwistMaxAngle = twist;
        s.mMaxFrictionTorque = friction;                        // sin esto brazos y piernas quedan de trapo
        joints.push_back(s.Create(*parts[parent].body, *parts[child].body));
        w.System().AddConstraint(joints.back());
        filter->DisablePair(parent, child);
    };
    // Rodillas y codos: se doblan en el plano del miembro, con margen simétrico alrededor del ángulo
    // actual sin pasar de recto ni de ~140°. Casi nada de costado.
    auto hinge = [&](int parent, int child, Vec3 a, Vec3 j, Vec3 c) {
        const Vec3 d1 = (j - a).Normalized(), d2 = (c - j).Normalized();
        const float bent = std::acos(mu::Clamp(d1.Dot(d2), -1.0f, 1.0f));
        const Vec3 axis = d1.Cross(d2);
        const float range = mu::Clamp(std::min(bent - 0.05f, 2.4f - bent), 0.15f, 1.2f);
        joint(parent, child, j, d2, axis.LengthSq() > 1e-6f ? axis.Normalized() : Vec3::sAxisX(), range, mu::Rad(6.0f),
              mu::Rad(8.0f), 1.5f);
    };
    auto dir = [](Vec3 a, Vec3 b) { return (b - a).Normalized(); };
    const Vec3 lateral = Vec3::sAxisX();
    const Vec3 neck = p.shoulders + Vec3(0.0f, 0.08f, 0.04f);
    joint(RiderPart::Pelvis, RiderPart::Torso, p.hips, dir(p.hips, p.shoulders), lateral, mu::Rad(35.0f), mu::Rad(20.0f), mu::Rad(20.0f), 6.0f);
    joint(RiderPart::Torso, RiderPart::Head, neck, dir(neck, p.head), lateral, mu::Rad(40.0f), mu::Rad(25.0f), mu::Rad(45.0f), 3.0f);
    for (int i = 0; i < 2; ++i) {
        joint(RiderPart::Torso, RiderPart::UpperArm + i, p.shoulder[i], dir(p.shoulder[i], p.elbow[i]), lateral, mu::Rad(80.0f),
              mu::Rad(80.0f), mu::Rad(60.0f), 1.5f);
        hinge(RiderPart::UpperArm + i, RiderPart::Forearm + i, p.shoulder[i], p.elbow[i], p.grip[i]);
        joint(RiderPart::Pelvis, RiderPart::Thigh + i, p.hip[i], dir(p.hip[i], p.knee[i]), lateral, mu::Rad(70.0f),
              mu::Rad(35.0f), mu::Rad(25.0f), 3.0f);
        hinge(RiderPart::Thigh + i, RiderPart::Shin + i, p.hip[i], p.knee[i], p.ankle[i]);
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
