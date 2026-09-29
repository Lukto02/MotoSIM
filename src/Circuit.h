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

// Generador "circuit" de los mapas (circuito de velocidad asfaltado): lo que se arma alrededor de la
// pista. Sale de la pista, el terreno y el archivo del mapa (puede leer su propia sección de def.file):
// igual en todas las PCs.
//
// Todo se ubica a partir del trazado: los pianos donde la curvatura lo pide (adentro en el vértice,
// afuera a la salida), leca afuera de las frenadas fuertes, barreras (muro con alambrado, gomas o
// air fence) a una distancia que respeta las escapatorias y no se mete en otro tramo, y en la recta
// de la largada los boxes, el muro de boxes, la tribuna principal, la grilla y el pórtico con el
// semáforo. Después tribunas en las curvas más frenadas, carteles, torres de luz, puestos de
// banderilleros, un puente de publicidad y árboles.
class Circuit {
public:
    void Build(const Track& track, const Terrain& terrain, const MapDef& def);   // sin GPU: sirve también en las pruebas
    void CreateCollision(PhysicsWorld& world);
    void CreateMeshes();                                                       // requiere contexto GL
    void Draw(Renderer& r) const;
    void DrawShadows(Renderer& r) const;      // los bloques que caen en el prisma de la luz de la pasada
    void Unload();
    bool Active() const { return built; }

private:
    struct Solid {                        // colisión: caja orientada (rumbo yaw)
        Vector3 c, half;
        float yaw;
    };
    struct Chunk {                        // bloque del mapa: sus mallas (varias si no entran en 16 bits)
        std::vector<Mesh> solid, decal, textured, detail;   // detail: la gente de las tribunas (sólo cerca)
        Vector3 lo{}, hi{};               // caja de todo lo del bloque (recorte y sombras)
        bool hasSolid = false;
    };
    struct Lamp {                         // luces del semáforo de largada
        Vector3 pos;
        int column;                       // 0..4: se prenden de a una columna
    };

    struct Geometry;                      // mallas en armado, por bloque (Circuit.cpp)
    // Ubica todo y junta la colisión; con geo arma también las mallas (en CPU). Build y CreateMeshes
    // corren lo mismo, así lo que se ve y lo que choca salen iguales.
    void Generate(Geometry* geo);
    void BuildAtlas();

    bool built = false, meshes = false;
    const Track* track = nullptr;         // los del juego (viven mientras el mapa está cargado)
    const Terrain* terrain = nullptr;
    std::string name;                     // en el pórtico y los boxes
    std::string pits = "auto";            // lado de los boxes: auto | left | right
    std::vector<std::string> billboards;  // textos de los carteles

    std::vector<Solid> solids;
    std::vector<JPH::BodyID> bodies;
    std::vector<Chunk> chunks;
    std::vector<Lamp> lamps;
    int chunkCount = 1;                   // por lado
    float origin = 0.0f, size = 1.0f, chunkSize = 160.0f;
    Mesh backdrop{};
    Texture2D atlas{};
    mutable int atlasAniso = -1;          // revisión del filtro anisotrópico (Renderer::AnisoRevision) aplicada al atlas
    // Cuántas cosas armó (se imprime en Build).
    int kerbs = 0, stands = 0, signs = 0, trees = 0, posts = 0;
    float barrierLength = 0.0f, gravelArea = 0.0f;
    float edgeMargin = 0.0f;              // m del borde de la pista a la caja de colisión más cercana
};
