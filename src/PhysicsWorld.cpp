#include "PhysicsWorld.h"

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>

// Este archivo no incluye raylib, así que se puede usar el namespace de Jolt sin choques.
using namespace JPH;

namespace {

namespace BPLayers {
constexpr BroadPhaseLayer STATIC(0);
constexpr BroadPhaseLayer MOVING(1);
constexpr uint COUNT = 2;
} // namespace BPLayers

class BPLayerInterface final : public BroadPhaseLayerInterface {
public:
    uint GetNumBroadPhaseLayers() const override { return BPLayers::COUNT; }
    BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer layer) const override
    {
        return layer == Layers::STATIC ? BPLayers::STATIC : BPLayers::MOVING;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(BroadPhaseLayer layer) const override
    {
        return layer == BPLayers::STATIC ? "STATIC" : "MOVING";
    }
#endif
};

class ObjectVsBPFilter final : public ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(ObjectLayer layer, BroadPhaseLayer bp) const override
    {
        return layer == Layers::STATIC ? bp == BPLayers::MOVING : true;
    }
};

class ObjectPairFilter final : public ObjectLayerPairFilter {
public:
    bool ShouldCollide(ObjectLayer a, ObjectLayer b) const override
    {
        // Lo estático no choca entre sí; el resto sí (piloto suelto vs. moto lo decide su filtro de
        // grupo). Las motos remotas son cinemáticas: sólo chocan con la moto y el piloto propios (y
        // empujan los objetos sueltos de esta PC).
        if (a == Layers::REMOTE || b == Layers::REMOTE) {
            const ObjectLayer other = a == Layers::REMOTE ? b : a;
            return other == Layers::MOVING || other == Layers::RAGDOLL || other == Layers::PROP;
        }
        return a != Layers::STATIC || b != Layers::STATIC;
    }
};

// Anota si la moto tocó algo en el paso y, si fue la moto de otro jugador, con qué fuerza (Jolt
// puede llamarlo desde otros hilos: atómicos).
class ContactFlag final : public ContactListener {
public:
    void OnContactAdded(const Body& a, const Body& b, const ContactManifold& m, ContactSettings& s) override { Note(a, b, m, s, true); }
    void OnContactPersisted(const Body& a, const Body& b, const ContactManifold& m, ContactSettings& s) override { Note(a, b, m, s, false); }
    std::atomic<bool> touched{false};
    std::atomic<float> closing{0.0f}, fromOther{0.0f};
    std::atomic<int> player{-1};
    std::atomic<float> hitNX{0.0f}, hitNY{0.0f}, hitNZ{0.0f}, hitPX{0.0f}, hitPY{0.0f}, hitPZ{0.0f}, hitMine{0.0f};
    // Cola raspando (el Step corre en un solo hilo: alcanza con que cada campo sea atómico).
    std::atomic<bool> scraping{false}, scrapeTerrain{false};
    std::atomic<float> scrapeX{0.0f}, scrapeY{0.0f}, scrapeZ{0.0f}, scrapeDX{0.0f}, scrapeDZ{0.0f}, scrapeSpeed{0.0f};
    std::atomic<float> impactSpeed{0.0f}, impactNormalY{0.0f};   // golpe contra algo fijo (ver LastImpact)

private:
    void Note(const Body& a, const Body& b, const ContactManifold& m, ContactSettings& s, bool added)
    {
        const bool aMine = a.GetObjectLayer() == Layers::MOVING, bMine = b.GetObjectLayer() == Layers::MOVING;
        if (!aMine && !bMine) return;
        const Body& other = aMine ? b : a;
        const Body& me = aMine ? a : b;
        // La cola contra el piso o un objeto: plástico y metal que raspan (poco roce) y sin rebote.
        if (other.GetObjectLayer() != Layers::REMOTE && me.GetShape()->GetSubShapeUserData(aMine ? m.mSubShapeID1 : m.mSubShapeID2) == kTailSkidTag) {
            s.mCombinedFriction = 0.35f;
            s.mCombinedRestitution = 0.0f;
            const RVec3 p = aMine ? m.GetWorldSpaceContactPointOn1(0) : m.GetWorldSpaceContactPointOn2(0);
            const Vec3 n = m.mWorldSpaceNormal;
            Vec3 rel = me.GetPointVelocity(p) - other.GetPointVelocity(p);
            rel -= n * rel.Dot(n);
            const float speed = rel.Length();
            if (!scraping.load() || speed > scrapeSpeed.load()) {
                scraping = true;
                scrapeX = (float)p.GetX();
                scrapeY = (float)p.GetY();
                scrapeZ = (float)p.GetZ();
                const Vec3 d = rel.NormalizedOr(Vec3::sZero());
                scrapeDX = d.GetX();
                scrapeDZ = d.GetZ();
                scrapeSpeed = speed;
                scrapeTerrain = other.GetShape()->GetSubType() == EShapeSubType::HeightField;
            }
        }
        if (other.GetObjectLayer() != Layers::REMOTE) {
            touched = true;
            // Golpe contra algo fijo (no la cola raspando): la velocidad con que se acercaban al tocarse.
            if (added && other.GetObjectLayer() == Layers::STATIC &&
                me.GetShape()->GetSubShapeUserData(aMine ? m.mSubShapeID1 : m.mSubShapeID2) != kTailSkidTag) {
                const Vec3 n = aMine ? m.mWorldSpaceNormal : -m.mWorldSpaceNormal;   // de la moto hacia lo otro
                const RVec3 p = aMine ? m.GetWorldSpaceContactPointOn1(0) : m.GetWorldSpaceContactPointOn2(0);
                const float c = (me.GetPointVelocity(p) - other.GetPointVelocity(p)).Dot(n);
                if (c > impactSpeed.load()) {
                    impactSpeed = c;
                    impactNormalY = n.GetY();
                }
            }
            return;
        }
        // Moto contra moto: sin rebote y con poco roce (que se empujen, no que se enganchen).
        s.mCombinedRestitution = 0.0f;
        s.mCombinedFriction = 0.25f;
        const Body& mine = aMine ? a : b;
        const Vec3 n = aMine ? m.mWorldSpaceNormal : -m.mWorldSpaceNormal;   // de mi moto hacia la otra
        const RVec3 p = m.GetWorldSpaceContactPointOn1(0);
        const float vMine = mine.GetPointVelocity(p).Dot(n), vOther = other.GetPointVelocity(p).Dot(n);
        const float c = vMine - vOther;
        if (c > closing.load()) {
            closing = c;
            fromOther = std::max(0.0f, -vOther);
            player = (int)other.GetUserData();
            hitNX = n.GetX();
            hitNY = n.GetY();
            hitNZ = n.GetZ();
            hitPX = (float)p.GetX();
            hitPY = (float)p.GetY();
            hitPZ = (float)p.GetZ();
            hitMine = vMine;
        }
    }
};

void TraceImpl(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    std::vprintf(fmt, args);
    va_end(args);
    std::printf("\n");
}

#ifdef JPH_ENABLE_ASSERTS
bool AssertFailedImpl(const char* expr, const char* msg, const char* file, uint line)
{
    std::printf("Jolt assert %s:%u: (%s) %s\n", file, line, expr, msg ? msg : "");
    return false;
}
#endif

} // namespace

struct PhysicsWorld::Filters {
    BPLayerInterface broadPhase;
    ObjectVsBPFilter objectVsBroadPhase;
    ObjectPairFilter objectPair;
    ContactFlag contacts;
};

PhysicsWorld::PhysicsWorld()
{
    RegisterDefaultAllocator();
    Trace = TraceImpl;
    JPH_IF_ENABLE_ASSERTS(AssertFailed = AssertFailedImpl;)
    Factory::sInstance = new Factory();
    RegisterTypes();

    filters = std::make_unique<Filters>();
    temp = std::make_unique<TempAllocatorImpl>(16 * 1024 * 1024);
    // Pocos cuerpos dinámicos (la moto, el piloto suelto y los objetos sueltos de los mapas, casi
    // siempre dormidos): un job system de un hilo es más barato que despertar un pool.
    jobs = std::make_unique<JobSystemSingleThreaded>(cMaxPhysicsJobs);

    system = std::make_unique<PhysicsSystem>();
    system->Init(8192, 0, 8192, 4096, filters->broadPhase, filters->objectVsBroadPhase, filters->objectPair);
    system->SetGravity(Vec3(0.0f, -9.81f, 0.0f));
    system->SetContactListener(&filters->contacts);
}

PhysicsWorld::~PhysicsWorld()
{
    system.reset();
    jobs.reset();
    temp.reset();
    UnregisterTypes();
    delete Factory::sInstance;
    Factory::sInstance = nullptr;
}

TempAllocator& PhysicsWorld::Temp() { return *temp; }

void PhysicsWorld::Step(float dt)
{
    filters->contacts.touched = false;
    filters->contacts.closing = 0.0f;
    filters->contacts.fromOther = 0.0f;
    filters->contacts.player = -1;
    filters->contacts.scraping = false;
    filters->contacts.scrapeSpeed = 0.0f;
    filters->contacts.impactSpeed = 0.0f;
    filters->contacts.impactNormalY = 0.0f;
    system->Update(dt, 1, temp.get(), jobs.get());
}

bool PhysicsWorld::AnyContact() const { return filters->contacts.touched; }

PhysicsWorld::Impact PhysicsWorld::LastImpact() const
{
    Impact i;
    i.speed = filters->contacts.impactSpeed.load();
    i.normalY = filters->contacts.impactNormalY.load();
    return i;
}

PhysicsWorld::BikeHit PhysicsWorld::LastBikeHit() const
{
    const ContactFlag& c = filters->contacts;
    BikeHit h;
    h.closing = c.closing.load();
    h.fromOther = c.fromOther.load();
    h.player = c.player.load();
    h.normal = Vec3(c.hitNX.load(), c.hitNY.load(), c.hitNZ.load());
    h.point = Vec3(c.hitPX.load(), c.hitPY.load(), c.hitPZ.load());
    h.mineAlongNormal = c.hitMine.load();
    return h;
}

PhysicsWorld::Scrape PhysicsWorld::LastScrape() const
{
    const ContactFlag& c = filters->contacts;
    Scrape r;
    r.active = c.scraping.load();
    r.point = Vec3(c.scrapeX.load(), c.scrapeY.load(), c.scrapeZ.load());
    r.slideDir = Vec3(c.scrapeDX.load(), 0.0f, c.scrapeDZ.load());
    r.speed = c.scrapeSpeed.load();
    r.onTerrain = c.scrapeTerrain.load();
    return r;
}
