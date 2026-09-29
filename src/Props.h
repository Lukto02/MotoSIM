#pragma once
// Jolt antes que raylib.
#include <Jolt/Jolt.h>
#include <Jolt/Math/Quat.h>
#include <Jolt/Physics/Body/BodyID.h>

#include "raylib.h"

#include "Maps.h"

#include <vector>

class Terrain;
class PhysicsWorld;
struct Renderer;

// Objetos de los mapas ("objects" del archivo): cajas, rampas (cuñas), cilindros y esferas. Los fijos
// son cuerpos estáticos (las ruedas andan encima, como sobre el terreno); los sueltos son cuerpos
// dinámicos que la moto empuja y tira (cada PC simula los suyos: en red no viajan).
class Props {
public:
    void Build(const std::vector<MapObject>& objects, const Terrain& terrain, PhysicsWorld& world);   // también sin GPU
    void CreateMeshes();                  // requiere contexto GL
    // casters: pasada de sombras, sólo los que caen en el prisma de la luz de la cascada. staticOnly: sin los
    // sueltos (la cascada lejana está en caché y se movería su sombra).
    void Draw(Renderer& r, PhysicsWorld& world, bool casters = false, bool staticOnly = false) const;
    // Los sueltos vuelven a donde estaban al cargar el mapa.
    void Reset(PhysicsWorld& world);
    void Unload();
    int Count() const { return (int)items.size(); }
    int DynamicCount() const;
    // Pruebas: cuántos sueltos se movieron más de 0.3 m de donde estaban y lo más lejos que fue uno.
    int Moved(PhysicsWorld& world, float& farthest) const;

private:
    struct Item {
        MapObject def;
        JPH::Vec3 pos;                    // donde se armó
        JPH::Quat rot;
        JPH::BodyID body;
    };
    std::vector<Item> items;
    Mesh wedge{};                          // rampa unitaria: 1 x 1 x 1 centrada, sube hacia +Z
    bool meshes = false;
};
