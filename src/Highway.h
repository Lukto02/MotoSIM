#pragma once
// Jolt antes que raylib.
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

#include "raylib.h"

#include "Maps.h"

#include <string>
#include <vector>

class Track;
class Terrain;
class PhysicsWorld;
struct Renderer;

// Corte transversal de la autopista (sección "highway" del mapa): la línea central del trazado es el medio del
// separador; cada mano tiene su banquina de adentro, sus carriles y la de afuera. Lateral con el signo de Track
// (+ = a la derecha de la línea central, (-tz, tx)). dir +1: la mano que va en el sentido del trazado (a la derecha,
// tránsito por la derecha); -1: la otra. El carril 0 es el rápido, junto al separador.
struct HighwayLayout {
    int lanes = 3;
    float laneWidth = 3.6f, median = 3.0f, innerShoulder = 1.0f, outerShoulder = 3.0f;
    float LanesFrom() const { return median * 0.5f + innerShoulder; }   // |lat| donde empieza el carril rápido
    float LaneLat(int dir, float lane) const { return (float)dir * (LanesFrom() + (lane + 0.5f) * laneWidth); }
    float PavedHalf() const { return LanesFrom() + (float)lanes * laneWidth + outerShoulder; }
};

// Generador "highway": todo lo que va alrededor del trazado de una autopista, igual en todas las PCs (sale del
// trazado, el terreno y el archivo del mapa): líneas de carril (calcos), separador New Jersey en el medio,
// guardarraíles, columnas de luz, pórticos con carteles verdes, carteles de velocidad y de kilómetro, puentes de los
// cruces (el terraplén es una forma del terreno que escribe tools/autopista.py; acá van el tablero, la pila y los
// estribos) y el campo: montes de eucaliptos, hileras de álamos, casas, galpones y silos.
class Highway {
public:
    void Build(const Track& track, const Terrain& terrain, const MapDef& def);   // sin GPU: sirve también en las pruebas
    void CreateCollision(PhysicsWorld& world);
    void CreateMeshes();                                                       // requiere contexto GL
    void Draw(Renderer& r) const;
    void DrawShadows(Renderer& r) const;
    void Unload();
    bool Active() const { return built; }
    const HighwayLayout& Layout() const { return layout; }
    // Dónde aparece y reaparece la moto: en la banquina de afuera de la mano del trazado (m a la derecha), como quien se
    // detuvo, para entrar al tránsito desde ahí (en un carril, los autos que vienen a 130 la tendrían encima).
    float RideOffset() const { return built ? layout.LanesFrom() + (float)layout.lanes * layout.laneWidth + 0.5f * layout.outerShoulder : 0.0f; }
    // El carril del medio de esa mano: por donde anda el bot sin tráfico.
    float MiddleLane() const { return built ? layout.LaneLat(1, (float)(layout.lanes - 1) * 0.5f) : 0.0f; }

private:
    struct Solid {                        // colisión: caja orientada (rumbo yaw)
        Vector3 c, half;
        float yaw;
    };
    struct Chunk {
        std::vector<Mesh> solid, decal, textured, detail;   // detail: postes y cosas chicas (sólo cerca)
        Vector3 lo{}, hi{};
        bool hasSolid = false;
    };
    struct Geometry;
    void Generate(Geometry* geo);
    void BuildAtlas();

    bool built = false, meshes = false;
    const Track* track = nullptr;
    const Terrain* terrain = nullptr;
    HighwayLayout layout;
    std::string name = "AUTOPISTA";
    std::vector<std::string> destinations;      // carteles de los pórticos
    std::vector<float> bridgeS;                 // s de cada puente (de "bridges": [[x, z], ...])
    float lightSpacing = 50.0f, gantrySpacing = 2400.0f, trees = 1.0f;
    float shoulder = 2.5f;                      // banquina de la calzada hasta el terreno natural (track.shoulder)
    float crossoverSpacing = 2000.0f, crossoverLength = 40.0f;   // cruces del separador ("crossovers", 0 = ninguno)

    std::vector<Solid> solids;
    std::vector<JPH::BodyID> bodies;
    std::vector<Chunk> chunks;
    int chunkCount = 1;
    float origin = 0.0f, size = 1.0f, chunkSize = 256.0f;
    Texture2D atlas{};
    mutable int atlasAniso = -1;
    int lights = 0, gantries = 0, treeCount = 0, buildings = 0, crossovers = 0;
    float railLength = 0.0f;
};
