#pragma once
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>

#include "Maps.h"

#include <vector>

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
    // radius > 0: sólo los chunks a menos de radius de (centerX, centerZ) (pasada de sombras).
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
    // jitter), sólo fuera de la pista y en manchones; se achican hacia el borde del radio.
    void GrassTufts(float cx, float cz, float radius, std::vector<Matrix>& out) const;

    // Deformación física (surcos): baja el terreno alrededor de (x, z) sin pasar maxDepth por
    // debajo del terreno original. Los cambios se acumulan hasta CommitDeformation().
    void Dig(float x, float z, float depth, float radius, float maxDepth);
    bool HasPendingDeformation() const { return dirty; }
    // Aplica los cambios acumulados al HeightFieldShape de Jolt y a las mallas de render.
    void CommitDeformation(PhysicsWorld& world);
    void DeformationStats(int& samples, float& deepest) const;

    float OriginX() const { return origin; }
    float OriginZ() const { return origin; }
    float Size() const { return (N - 1) * Cell; }
    JPH::BodyID BodyID() const { return body; }

private:
    struct Chunk {
        Mesh* mesh;
        int x0, z0, w, h;                  // rango de muestras que cubre
    };

    float Ground(float x, float z) const;
    float Sample(const std::vector<float>& a, float x, float z) const;   // bilineal sobre la grilla
    void StampTrack(const Track& track);
    void StampStreets(const Track& track);
    void ApplyTerraces(const Track& track, std::vector<float>& base) const;
    float ImageHeight(float x, float z) const;
    float H(int x, int z) const { return heights[z * N + x]; }
    void FillChunkGeometry(const Chunk& c) const;   // posiciones y normales desde heights

    std::vector<float> heights;
    std::vector<float> original;           // alturas iniciales (límite de profundidad de surcos)
    std::vector<float> trackMask;
    std::vector<float> streetMask;         // calles: 1 sobre la calle (pavimentada o de tierra)
    std::vector<float> streetTone;         // calles: 1 asfalto (oscuro), 0 cemento (claro)
    std::vector<float> roadDist;           // m a la línea central más cercana
    std::vector<Chunk> chunks;
    bool streets = false;
    bool grassOffRoad = false;             // calles de asfalto entre pasto (circuito), no tierra roja
    MapTerrain def;                        // qué terreno natural es
    MapTerraces terraces;
    std::vector<float> image;              // heightmap: 0..1
    int imageW = 0, imageH = 0;
    float origin = 0.0f;
    JPH::BodyID body;
    JPH::Ref<JPH::HeightFieldShape> shape;
    bool dirty = false;
    int dirtyX0 = 0, dirtyZ0 = 0, dirtyX1 = 0, dirtyZ1 = 0;   // inclusive
};
