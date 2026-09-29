#pragma once
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/MutableCompoundShape.h>

#include "Maps.h"

#include <cstdint>
#include <memory>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

class Track;
class PhysicsWorld;
struct Renderer;
struct Mesh;
struct Texture;
struct Matrix;

// Heightmap N x N. Colisión con HeightFieldShape de Jolt, render como mallas por chunks
// (raylib usa índices de 16 bits, así que cada chunk debe tener < 65536 vértices).
class Terrain {
public:
    int N = 512;                           // muestras por lado (múltiplo del block size de Jolt; lo fija el mapa)
    float Cell = 0.5f;                     // metros entre muestras

    // El terreno del mapa (lomas, plano, imagen de alturas o el morro) con la pista estampada encima.
    void Build(const Track& track, const MapDef& def, bool flat = false);   // flat: plano, para pruebas
    void CreateCollision(PhysicsWorld& world);
    void CreateMeshes();                   // requiere contexto GL
    // radius > 0 (pasada de sombras): sólo los chunks que caen en el prisma de la luz de la cascada (ver
    // Renderer::InShadowPrism) y las baldosas finas a menos de radius de (centerX, centerZ). radius <= 0 (pasada
    // principal): sólo los chunks que tocan el frustum de la cámara (Renderer::BoxVisible); las baldosas finas
    // se dibujan todas.
    void Draw(Renderer& r, const Texture& marks, float centerX = 0.0f, float centerZ = 0.0f, float radius = -1.0f) const;
    void Unload();

    float Height(float x, float z) const;  // misma triangulación que el heightfield de Jolt
    JPH::Vec3 Normal(float x, float z) const;
    float SurfaceGrip(float x, float z) const;
    // 0..1 cuánto es pavimento (asfalto o cemento) en (x, z): en los mapas de calles, la calzada; en los
    // de pista, nada (tierra y pasto).
    float PavedAmount(float x, float z) const { return streets ? TrackMask(x, z) : 0.0f; }
    float TrackMask(float x, float z) const;   // 1 = tierra de pista (calles: pavimento), 0 = pasto (calles: tierra)
    float Dustiness(float x, float z) const;   // cuánto polvo y tierra levanta una rueda ahí (0..1)
    // Distancia a la línea central más cercana (para ubicar casas) y altura del terreno natural sin
    // la pista (hasta dónde bajan los cimientos).
    float RoadDistance(float x, float z) const;
    float NaturalHeight(float x, float z) const { return Ground(x, z); }
    bool Streets() const { return streets; }   // calles (pavimento y tierra) en vez de pista y pasto

    // Matas de pasto 3D a menos de radius de (cx, cz): posiciones fijas en el mundo (grilla con
    // jitter), sólo fuera de la pista y en manchones, y no en las pendientes fuertes; se achican hacia el borde
    // del radio. El hash de raleo de cada mata viaja en m15 de su matriz (kGrassVS funde con la distancia de
    // cada cuadro); density (0..1, grassDensity) sólo deja pasar las que van a entrar a esa densidad.
    void GrassTufts(float cx, float cz, float radius, std::vector<Matrix>& out, float density = 1.0f) const;
    // Zona sin matas: el cuadrilátero q (x, z de sus 4 vértices en orden) más margin m alrededor. Es para el asfalto que
    // una malla pone sobre el pasto (calle de boxes del circuito): el terreno de abajo es pasto y sus matas lo atraviesan.
    // Sólo afecta lo que se dibuja (la máscara de pista, el agarre y el polvo no cambian). Se limpia en Build y Unload.
    void ExcludeGrass(const float q[8], float margin) const;

    // Deformación física (surcos): baja el terreno alrededor de (x, z) sin pasar maxDepth por
    // debajo del terreno original. Los cambios se acumulan hasta CommitDeformation().
    void Dig(float x, float z, float depth, float radius, float maxDepth);
    // Huella orientada: presión, cizallamiento y tierra desplazada a los costados.
    void PressSoil(float x, float z, float headingX, float headingZ, float load, float slipSpeed,
                   float softness, float maxDepth, float dt, float rollingSpeed = 0.0f);
    float SoilAmount(float x, float z) const;
    // Suelo que sienten las ruedas: lo mismo que Height/Normal pero con el surco suavizado (ver TerrainSoil.cpp).
    // Sin surcos, o con rideSoil = 0 (rut_ride), dan exactamente Height y Normal.
    float RideHeight(float x, float z) const;
    JPH::Vec3 RideNormal(float x, float z) const;
    JPH::Vec3 BankNormal(float x, float z) const;   // el peralte del terreno sin surcos (para el limite de la curva)
    float rideSoil = 1.0f;                 // 0: las ruedas ven el surco tal cual (como antes); otro valor: suavizado (rut_ride)
    float rideDepth = 0.02f;               // tope blando de cuanto se hunde una rueda en el surco, m (0: sin tope; rut_ride_depth)
    float rideBlur = 0.25f;                // radio (sigma) del promedio del surco bajo la rueda, m (rut_ride_blur)
    float rideSink = 0.02f;                // m/s a los que el suelo de las ruedas sigue al surco; 0: al instante (rut_ride_sink)
    void StepRide(float dt);               // una vez por paso de fisica: el suelo que sienten las ruedas se va hundiendo de a poco
    void ResetDeformation(PhysicsWorld& world);
    unsigned DeformationRevision() const { return deformationRevision; }
    void SoilStats(float& excavated, float& deposited, float& highestBank) const;
    bool HasPendingDeformation() const { return dirty || !soilDirty.empty(); }
    float SoilCell() const { return Cell / soilSubdivision; }
    void UpdateSoilVisibility(float x,float z);
    // Aplica los cambios acumulados al HeightFieldShape de Jolt y a las mallas de render.
    void CommitDeformation(PhysicsWorld& world);
    void DeformationStats(int& samples, float& deepest) const;

    float OriginX() const { return origin; }
    float OriginZ() const { return origin; }
    float Size() const { return (N - 1) * Cell; }
    JPH::BodyID BodyID() const { return body; }

private:
    using SoilKey = std::pair<int,int>;
    struct SoilHash { size_t operator()(const SoilKey& p) const {
        return (size_t)((uint64_t)(uint32_t)p.first*0x9e3779b185ebca87ULL ^ (uint64_t)(uint32_t)p.second*0xc2b2ae3d27d4eb4fULL);
    } };
    struct SoilTile { Mesh* mesh = nullptr; };
    int soilSubdivision = 10, soilTileCells = 8;
    std::unordered_map<SoilKey,float,SoilHash> soilPending, soilHeights;
    std::set<SoilKey> soilDirty;
    std::set<SoilKey> soilChanged;
    std::map<SoilKey,SoilTile> soilTiles;
    JPH::Ref<JPH::MutableCompoundShape> soilRoot;
    std::map<SoilKey,JPH::Ref<JPH::MutableCompoundShape>> soilBranches;
    float BaseHeight(float x,float z) const;
    float SoilDelta(float x,float z) const;
    float RideDelta(float x,float z) const;   // el hundido suavizado bajo (x, z), <= 0 (como SoilDelta)
    std::unordered_map<SoilKey,float,SoilHash> soilRide;   // copia de soilHeights que sigue a la real con demora
    std::unordered_set<SoilKey,SoilHash> rideActive;       // nodos de soilRide que todavia no llegaron
    float SoilNode(int x,int z,bool pending) const;
    void MarkSoilNode(int x,int z);
    JPH::Ref<JPH::HeightFieldShape> SoilShape(int x,int z,int width,int height,int subdiv,bool pending) const;
    void CommitFineSoil(PhysicsWorld& world);
    void UpdateSoilMesh(SoilKey key);
    void FilterCoarseMeshes();
    void ClearFineSoil();
    struct GrassCache;                     // matas ya armadas por bloques (Terrain.cpp)
    void GrassBlock(int bi, int bj, std::vector<Matrix>& out) const;
    mutable std::shared_ptr<GrassCache> grassCache;
    mutable std::vector<uint8_t> noGrass;  // grilla de 1 m con 1 donde no van matas (ExcludeGrass); vacía si no hay
    bool GrassExcluded(float x, float z) const;
    struct Chunk {
        Mesh* mesh;
        int x0, z0, w, h;                  // rango de muestras que cubre
        float low = 1e9f, high = -1e9f;    // altura mínima y máxima de sus muestras al armar la malla (culling)
    };

    // Forma esculpida lista para evaluar: el perfil ya suavizado en una tabla cada kShapeStep m.
    struct Shape {
        MapShape def;
        float dirX = 0.0f, dirZ = 1.0f;    // hacia dónde crece u (line) / eje del estirado (round)
        float u0 = 0.0f;                   // u de la primera muestra de la tabla
        std::vector<float> table;
        float first = 0.0f, last = 0.0f;   // u del primer y último punto del perfil
        float base = 0.0f;                 // level: altura a la que nivela
        float arcWidth = 360.0f;           // round con "arc": grados que abarca el sector (360 = entera)
        float minX = 0.0f, maxX = 0.0f, minZ = 0.0f, maxZ = 0.0f;   // hasta dónde llega (con el borde)
    };

    float Ground(float x, float z) const;  // Natural + formas + la subida del borde
    float Natural(float x, float z) const; // el terreno del tipo (lomas, imagen, morro, médanos), sin formas
    float Dunes(float x, float z) const;
    // El suelo con las formas [0, count) aplicadas en orden sobre la altura natural h.
    float ApplyShapes(float x, float z, float h, size_t count) const;
    // Cuánto cubre la forma el punto (1 adentro, 0 fuera de su borde) y la u de su perfil ahí.
    float ShapeCover(const Shape& s, float x, float z, float& u) const;
    void PrepareShapes();
    float Sample(const std::vector<float>& a, float x, float z) const;   // bilineal sobre la grilla
    void StampTrack(const Track& track);
    void StampStreets(const Track& track);
    void ApplyTerraces(const Track& track, std::vector<float>& base) const;
    float ImageHeight(float x, float z) const;
    float H(int x, int z) const { return heights[z * N + x]; }
    void FillChunkGeometry(const Chunk& c) const;   // posiciones y normales desde heights

    std::vector<float> heights;
    std::vector<float> pendingHeights; // física y render ven heights hasta el mismo commit
    unsigned deformationRevision = 0;
    bool deformed = false;
    void DirtyRegion(int x0, int z0, int x1, int z1);
    std::vector<float> original;           // alturas iniciales (límite de profundidad de surcos)
    std::vector<float> trackMask;
    std::vector<float> streetMask;         // calles: 1 sobre la calle (pavimentada o de tierra)
    std::vector<float> streetTone;         // calles: 1 asfalto (oscuro), 0 cemento (claro)
    std::vector<float> roadDist;           // m a la línea central más cercana
    std::vector<Chunk> chunks;
    bool streets = false;
    bool grassOffRoad = false;             // calles de asfalto entre pasto (circuito), no tierra roja
    bool guide = false;                    // la pista es sólo una vuelta guía: no se estampa en el terreno
    bool sand = false;                     // arena (ground_textures "sand"): tono, polvo y pocas matas secas
    std::vector<Shape> shapes;             // terrain.shapes, listas para evaluar
    float duneDirX = 0.0f, duneDirZ = 1.0f, duneSeedX = 0.0f, duneSeedZ = 0.0f;
    MapTerrain def;                        // qué terreno natural es
    MapTerraces terraces;
    std::vector<float> image;              // heightmap: 0..1
    int imageW = 0, imageH = 0;
    float origin = 0.0f;
    JPH::BodyID body;
    JPH::Ref<JPH::HeightFieldShape> shape, baseShape;
    bool soilCollision = false;
    bool dirty = false;
    int dirtyX0 = 0, dirtyZ0 = 0, dirtyX1 = 0, dirtyZ1 = 0;   // inclusive
};
