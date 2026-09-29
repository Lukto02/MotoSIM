#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include "Props.h"

#include "MathUtil.h"
#include "MeshBuilder.h"
#include "PhysicsWorld.h"
#include "Render.h"
#include "Terrain.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using JPH::Mat44;
using JPH::Quat;
using JPH::Vec3;

namespace {

// Medidas de un objeto (ancho, alto, largo) sin ceros; la esfera usa el ancho como diámetro.
Vec3 Dims(const MapObject& o)
{
    const float w = std::max(o.size[0], 0.05f), h = std::max(o.size[1], 0.05f), l = std::max(o.size[2], 0.05f);
    if (o.shape == MapObject::Shape::Sphere) return Vec3(w, w, w);
    if (o.shape == MapObject::Shape::Cylinder) return Vec3(w, h, w);
    return Vec3(w, h, l);
}

// Cuánto baja el objeto rotado desde su centro (media altura de su caja alineada con el mundo).
float HalfHeight(const MapObject& o, Quat rot)
{
    const Vec3 half = Dims(o) * 0.5f;
    const Mat44 r = Mat44::sRotation(rot);
    const float ax = std::fabs(r.GetAxisX().GetY()), ay = std::fabs(r.GetAxisY().GetY()), az = std::fabs(r.GetAxisZ().GetY());
    switch (o.shape) {
    case MapObject::Shape::Sphere: return half.GetX();
    case MapObject::Shape::Cylinder: return ay * half.GetY() + std::sqrt(std::max(0.0f, 1.0f - ay * ay)) * half.GetX();
    default: return ax * half.GetX() + ay * half.GetY() + az * half.GetZ();
    }
}

JPH::ShapeRefC MakeShape(const MapObject& o)
{
    const Vec3 d = Dims(o), half = d * 0.5f;
    const float radius = std::min(0.05f, 0.5f * std::min({half.GetX(), half.GetY(), half.GetZ()}));
    switch (o.shape) {
    case MapObject::Shape::Box: return new JPH::BoxShape(half, radius);
    case MapObject::Shape::Cylinder: return new JPH::CylinderShape(half.GetY(), half.GetX(), radius);
    case MapObject::Shape::Sphere: return new JPH::SphereShape(half.GetX());
    case MapObject::Shape::Ramp: {
        // Cuña: base entera abajo, sube desde el borde -Z (a ras del piso) hasta el +Z (alto completo).
        const float x = half.GetX(), y = half.GetY(), z = half.GetZ();
        JPH::Array<Vec3> pts = {Vec3(-x, -y, -z), Vec3(x, -y, -z), Vec3(-x, -y, z), Vec3(x, -y, z), Vec3(-x, y, z), Vec3(x, y, z)};
        JPH::ConvexHullShapeSettings settings(pts, std::min(radius, 0.02f));
        JPH::ShapeSettings::ShapeResult r = settings.Create();
        if (r.HasError()) {
            std::printf("props: rampa inválida (%s), queda una caja\n", r.GetError().c_str());
            return new JPH::BoxShape(half, radius);
        }
        return r.Get();
    }
    }
    return new JPH::BoxShape(half, radius);
}

} // namespace

void Props::Build(const std::vector<MapObject>& objects, const Terrain& terrain, PhysicsWorld& world)
{
    items.clear();
    for (const MapObject& o : objects) {
        Item it;
        it.def = o;
        it.rot = (Quat::sRotation(Vec3::sAxisY(), mu::Rad(o.yaw)) * Quat::sRotation(Vec3::sAxisX(), mu::Rad(o.pitch)) *
                  Quat::sRotation(Vec3::sAxisZ(), mu::Rad(o.roll)))
                     .Normalized();
        // "at" dice dónde va el punto más bajo del objeto (ya rotado): sobre el terreno (+ y; con y
        // negativo queda enterrado) o a una altura absoluta.
        const float bottom = o.onGround ? terrain.Height(o.pos[0], o.pos[2]) + o.pos[1] : o.pos[1];
        it.pos = Vec3(o.pos[0], bottom + HalfHeight(o, it.rot), o.pos[2]);
        JPH::BodyCreationSettings bcs(MakeShape(o), JPH::RVec3(it.pos), it.rot, o.dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static,
                                      o.dynamic ? Layers::PROP : Layers::STATIC);
        bcs.mFriction = o.friction;
        if (o.dynamic) {
            bcs.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            bcs.mMassPropertiesOverride.mMass = std::max(o.mass, 0.1f);
            bcs.mRestitution = 0.15f;
            bcs.mAngularDamping = 0.2f;
        }
        it.body = world.Bodies().CreateAndAddBody(bcs, o.dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
        items.push_back(it);
    }
    if (!items.empty()) {
        world.System().OptimizeBroadPhase();
        std::printf("props: %zu objetos (%d sueltos)\n", items.size(), DynamicCount());
    }
}

int Props::DynamicCount() const
{
    int n = 0;
    for (const Item& it : items) n += it.def.dynamic ? 1 : 0;
    return n;
}

int Props::Moved(PhysicsWorld& world, float& farthest) const
{
    int n = 0;
    farthest = 0.0f;
    for (const Item& it : items) {
        if (!it.def.dynamic) continue;
        const float d = (Vec3(world.Bodies().GetPosition(it.body)) - it.pos).Length();
        farthest = std::max(farthest, d);
        n += d > 0.3f ? 1 : 0;
    }
    return n;
}

void Props::Reset(PhysicsWorld& world)
{
    for (const Item& it : items) {
        if (!it.def.dynamic || it.body.IsInvalid()) continue;
        world.Bodies().SetPositionAndRotation(it.body, JPH::RVec3(it.pos), it.rot, JPH::EActivation::Activate);
        world.Bodies().SetLinearAndAngularVelocity(it.body, Vec3::sZero(), Vec3::sZero());
    }
}

void Props::CreateMeshes()
{
    if (meshes) return;
    // Cuña con caras planas (una normal por cara): piso, frente alto (+Z), rampa y los dos costados.
    MeshBuilder b;
    auto quad = [&](Vector3 a, Vector3 c, Vector3 d, Vector3 e, Vector3 n) {
        const unsigned short i0 = b.Vertex(a, n), i1 = b.Vertex(c, n), i2 = b.Vertex(d, n), i3 = b.Vertex(e, n);
        b.Tri(i0, i1, i2);
        b.Tri(i0, i2, i3);
    };
    auto tri = [&](Vector3 a, Vector3 c, Vector3 d, Vector3 n) { b.Tri(b.Vertex(a, n), b.Vertex(c, n), b.Vertex(d, n)); };
    const float h = 0.5f;
    const Vector3 slope = Vector3Normalize({0.0f, 1.0f, -1.0f});
    quad({-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h}, {0, -1, 0});
    quad({-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}, {0, 0, 1});
    quad({-h, -h, -h}, {h, -h, -h}, {h, h, h}, {-h, h, h}, slope);
    tri({-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-1, 0, 0});
    tri({h, -h, -h}, {h, -h, h}, {h, h, h}, {1, 0, 0});
    b.FixWinding();
    wedge = b.Build();
    meshes = true;
}

void Props::Draw(Renderer& r, PhysicsWorld& world, bool casters, bool staticOnly) const
{
    // Pasada principal: nada más allá de graphics.propsRadius de la cámara (0 = sin límite). La lejanía ya es niebla.
    const bool limited = !casters && r.graphics.propsRadius > 0;
    const Vector3 cam = limited ? r.CameraPosition() : Vector3{};
    const float radius = (float)r.graphics.propsRadius;
    for (const Item& it : items) {
        const MapObject& o = it.def;
        if (staticOnly && o.dynamic) continue;
        const Mat44 t = o.dynamic ? world.Bodies().GetWorldTransform(it.body) : Mat44::sRotationTranslation(it.rot, it.pos);
        const Vec3 d = Dims(o);
        if (limited) {
            const Vec3 p = t.GetTranslation();
            const float reach = radius + 0.5f * d.Length();                 // el borde más cercano del objeto, sin importar el giro
            const float dx = p.GetX() - cam.x, dz = p.GetZ() - cam.z;
            if (dx * dx + dz * dz > reach * reach) continue;
        }
        if (casters) {
            const Vec3 p = t.GetTranslation();
            const float reach = 0.5f * d.Length();          // la mitad de la diagonal: cualquier giro entra
            if (!r.InShadowPrism({p.GetX() - reach, p.GetY() - reach, p.GetZ() - reach}, {p.GetX() + reach, p.GetY() + reach, p.GetZ() + reach})) continue;
        }
        const Color c = {o.color[0], o.color[1], o.color[2], 255};
        switch (o.shape) {
        case MapObject::Shape::Box: r.Box(t * Mat44::sScale(d), c); break;
        case MapObject::Shape::Cylinder: r.Cylinder(t * Mat44::sScale(Vec3(d.GetX() * 0.5f, d.GetY(), d.GetZ() * 0.5f)), c); break;
        case MapObject::Shape::Sphere: r.Sphere(t * Mat44::sScale(d * 0.5f), c); break;
        case MapObject::Shape::Ramp:
            if (meshes) r.DrawMeshColored(wedge, ToRl(t * Mat44::sScale(d)), c);
            break;
        }
    }
}

void Props::Unload()
{
    if (meshes) UnloadMesh(wedge);
    wedge = Mesh{};
    meshes = false;
}
