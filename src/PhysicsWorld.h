#pragma once
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <memory>

namespace JPH {
class TempAllocatorImpl;
class JobSystem;
} // namespace JPH

namespace Layers {
constexpr JPH::ObjectLayer STATIC = 0;
constexpr JPH::ObjectLayer MOVING = 1;    // la moto
constexpr JPH::ObjectLayer RAGDOLL = 2;   // el piloto suelto tras una caída
constexpr JPH::ObjectLayer REMOTE = 3;    // motos de los otros jugadores (cinemáticas, siguen a la red)
constexpr JPH::ObjectLayer PROP = 4;      // objetos sueltos de los mapas (tambores, cajas...): los simula cada PC
} // namespace Layers

// User data de la forma de la cola de la moto (guardabarros / parrilla / patente): raspa el piso.
constexpr JPH::uint64 kTailSkidTag = 0x7A11;

// Boilerplate de Jolt: allocators, job system, filtros de capas y el PhysicsSystem.
// Todo el acceso a cuerpos se hace desde el hilo principal entre Step()s, por eso se usan
// las interfaces "NoLock".
class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    void Step(float dt);
    // ¿La moto (capa MOVING) tocó algo en el último Step? Es su chasis o el piloto sentado tocando
    // el suelo (las ruedas son virtuales y no cuentan).
    bool AnyContact() const;
    // Choque de la moto con la de otro jugador en el último Step: velocidad con que se acercaban
    // (m/s, a lo largo de la normal), qué parte de ella traía el otro, y su número de jugador
    // (el user data del cuerpo remoto). 0 si no hubo. closing y fromOther salen de la velocidad del
    // cuerpo cinemático, que incluye las correcciones de la predicción; para decidir una caída, Game
    // los rehace con la velocidad que mandó el otro: para eso, la normal (de la moto propia hacia la
    // otra), el punto de contacto y la velocidad propia en ese punto a lo largo de la normal.
    struct BikeHit {
        float closing = 0.0f, fromOther = 0.0f;
        int player = -1;
        JPH::Vec3 normal = JPH::Vec3::sZero(), point = JPH::Vec3::sZero();
        float mineAlongNormal = 0.0f;
    };
    BikeHit LastBikeHit() const;
    // El golpe más fuerte de la moto (con su piloto) contra algo fijo en el último Step: la velocidad con
    // que se acercaban por la normal del contacto al tocarse y cuánto apunta esa normal hacia arriba.
    struct Impact {
        float speed = 0.0f, normalY = 0.0f;
    };
    Impact LastImpact() const;
    // La cola de la moto raspando el piso en el último Step (wheelie pasado): dónde y a qué velocidad
    // desliza contra el suelo, y hacia dónde.
    struct Scrape {
        bool active = false;
        JPH::Vec3 point = JPH::Vec3::sZero();
        JPH::Vec3 slideDir = JPH::Vec3::sZero();
        float speed = 0.0f;
        bool onTerrain = false;             // contra el terreno (si no, un objeto o una casa)
    };
    Scrape LastScrape() const;
    // Golpes secos de la moto contra el piso que la harían girar más (hard_landing_spin): qué parte del giro le dan
    // (la inercia de giro se divide por esto en ese contacto). 1 = cuerpo rígido, como siempre (no se toca nada).
    void SetHardLandingSpin(float s);

    JPH::PhysicsSystem& System() { return *system; }
    JPH::BodyInterface& Bodies() { return system->GetBodyInterfaceNoLock(); }
    const JPH::NarrowPhaseQuery& Query() const { return system->GetNarrowPhaseQueryNoLock(); }
    JPH::TempAllocator& Temp();

private:
    struct Filters;
    std::unique_ptr<Filters> filters;
    std::unique_ptr<JPH::TempAllocatorImpl> temp;
    std::unique_ptr<JPH::JobSystem> jobs;
    std::unique_ptr<JPH::PhysicsSystem> system;
};
