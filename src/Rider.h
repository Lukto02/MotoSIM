#pragma once
// Piloto: su dibujo (piezas en espacio local de la moto) y el ragdoll en que se convierte al caerse.
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/CollisionGroup.h>
#include <Jolt/Physics/Collision/GroupFilter.h>
#include <Jolt/Physics/Constraints/Constraint.h>

#include "Bike.h"

#include "Render.h"

#include <vector>

class PhysicsWorld;
class Terrain;
namespace JPH { class Body; }

// Partes del cuerpo (= cuerpos del ragdoll). Las de a pares: + 0 izquierda, + 1 derecha.
namespace RiderPart {
enum { Pelvis, Torso, Head, Thigh, Shin = Thigh + 2, UpperArm = Shin + 2, Forearm = UpperArm + 2, Count = Forearm + 2 };
}

// Grupos de colisión de Jolt: el del ragdoll decide con quién choca cada parte (RiderCollisionFilter).
constexpr JPH::CollisionGroup::GroupID kRiderCollisionGroup = 0;
constexpr JPH::CollisionGroup::GroupID kBikeCollisionGroup = 1;
constexpr JPH::CollisionGroup::GroupID kRemoteCollisionGroup = 2;   // motos de otros jugadores (el ragdoll choca con ellas)

// Una malla del piloto (Renderer::rider) transformada, pegada a una parte del cuerpo.
struct RiderPiece {
    int part;
    JPH::Mat44 local;                // espacio local de la moto
    Color color;                     // tinte (las mallas traen sus colores; blanco = tal cual)
    RiderMesh::Id mesh;
};

// El dibujo del piloto en una pose. Bike::Draw lo usa sentado y el ragdoll al salir despedido.
std::vector<RiderPiece> RiderPieces(const RiderPose& pose);
// Transformación de cada parte del cuerpo (RiderPart::Count) en espacio local de la moto para una
// pose: donde aparece el ragdoll. También sirve para el ragdoll de otro jugador (llega por red).
void RiderPartLocals(const RiderPose& pose, JPH::Mat44* out);
void DrawRiderPiece(Renderer& r, const JPH::Mat44& world, RiderMesh::Id mesh, Color color);

// Con quién choca cada parte del ragdoll: entre ellas, no las unidas por una articulación; con la moto,
// sólo las que ya dejaron de estar superpuestas con ella (el piloto aparece encimado a la moto y
// separarlos de golpe los haría salir disparados). Con el terreno, siempre.
class RiderCollisionFilter final : public JPH::GroupFilter {
public:
    bool CanCollide(const JPH::CollisionGroup& a, const JPH::CollisionGroup& b) const override;
    void DisablePair(int partA, int partB);

    bool withBike[RiderPart::Count] = {};

private:
    JPH::uint32 pairs[RiderPart::Count] = {};    // bit j de pairs[i]: la parte i NO choca con la j
};

// Piloto suelto tras una caída: un cuerpo de Jolt por parte, unidos por articulaciones swing-twist
// con límites de cuerpo humano. Choca con el terreno y (cuando se separa) con la moto.
class RiderRagdoll {
public:
    // Aparece en la pose del piloto sobre la moto, con la velocidad que tenía ese punto de la moto.
    void Spawn(PhysicsWorld& world, const Terrain& terrain, const RiderPose& pose, JPH::Vec3 bikeCom, JPH::Quat bikeRot,
               JPH::Vec3 linVel, JPH::Vec3 angVel);
    void Remove();                   // llamar antes de destruir el PhysicsWorld
    // Guarda las transformaciones para interpolar el dibujo y habilita el choque con la moto de las
    // partes que ya se separaron de ella.
    void PostPhysics(JPH::BodyID bike);
    void Draw(Renderer& r, float alpha) const;

    bool Active() const { return !parts.empty(); }
    JPH::Vec3 Position(float alpha) const;   // pelvis (lo sigue la cámara)
    JPH::Vec3 Velocity() const;
    int PartsCollidingWithBike() const;      // telemetría
    JPH::Mat44 PartTransform(int part, float alpha) const;              // mundo, interpolada
    JPH::Mat44 PartSpawnLocal(int part) const { return spawnLocal[part]; }   // espacio de la moto al aparecer
    bool report = false;             // pruebas: al sacarlo imprime cuánto se dobló cada articulación

private:
    struct Part {
        JPH::Body* body = nullptr;
        JPH::Vec3 prevPos, currPos;
        JPH::Quat prevRot, currRot;
    };
    struct Piece {
        int part;
        JPH::Mat44 local;            // espacio local de la parte
        Color color;
        RiderMesh::Id mesh;
    };
    PhysicsWorld* world = nullptr;
    std::vector<Part> parts;
    std::vector<Piece> pieces;
    std::vector<JPH::Ref<JPH::Constraint>> joints;
    // Pruebas: cada articulación medida en el espacio de la parte de arriba (referencia, hacia dónde
    // flexiona y el eje de flexión), con lo máximo que llegó: flexión, de costado, giro sobre el hueso y
    // cuánto se separaron las dos partes en el punto de unión.
    struct JointWatch {
        const char* name;
        JPH::Constraint* c;          // swing-twist o bisagra (hinge)
        bool hinge;
        int parent, child;
        JPH::Vec3 anchorP, anchorC;  // el punto de unión en cada parte
        JPH::Vec3 n0, out, axis;     // espacio del padre
        JPH::Vec3 dirC, refC;        // el hueso hijo y una perpendicular, en su espacio
        JPH::Vec3 d0, refP;          // los mismos al aparecer, en el espacio del padre
        float lo, hi, side, twist;   // límites
        float flexMin = 1e9f, flexMax = -1e9f, sideMax = 0.0f, twistMax = 0.0f, sepMax = 0.0f;
        // Lo mismo según Jolt (su descomposición swing-twist o el ángulo de la bisagra).
        float jTwist = 0.0f, jSwingY = 0.0f, jSwingZmin = 1e9f, jSwingZmax = -1e9f, jPlane = 0.0f, jNormal = 0.0f;
        int over = 0, steps = 0;     // pasos fuera del rango (más de 5°)
    };
    std::vector<JointWatch> watch;
    JPH::Ref<RiderCollisionFilter> filter;
    JPH::Mat44 spawnLocal[RiderPart::Count];
};
