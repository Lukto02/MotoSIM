#pragma once
// Jolt antes que raylib.
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

#include "raylib.h"

#include <cstdint>
#include <vector>

class Track;
class Terrain;
class PhysicsWorld;
struct Renderer;

// Morro do Grau: las casitas del morro (ladrillo a la vista o revoque pintado, lajes, caixas d'água),
// la escadaria, postes con cables enredados, banderines de colores, carteles, pipas en el cielo, las
// motos estacionadas y el paisaje de fondo con el Cristo. Todo sale de la pista y el terreno ya
// armados con una semilla fija: es igual en todas las PCs de una partida.
class Favela {
public:
    struct Parked {                       // motos estacionadas (las dibuja el juego con las piezas de la moto)
        JPH::Vec3 pos;
        float yaw;
        int livery;
        bool deliveryBox;                 // baú de motoboy en la parrilla
    };

    void Build(const Track& track, const Terrain& terrain);   // sin GPU: sirve también en las pruebas
    void CreateCollision(PhysicsWorld& world);
    void CreateMeshes(const Font& font);                     // requiere contexto GL
    void Draw(Renderer& r, float time) const;
    void DrawShadows(Renderer& r) const;      // los bloques que caen en el prisma de la luz de la pasada
    void Unload();
    bool Active() const { return built; }

    const std::vector<Parked>& ParkedBikes() const { return parked; }
    int HouseCount() const { return (int)houses.size(); }

private:
    struct House {
        float x, z, yaw;                  // centro de la planta; rumbo: +Z local = hacia el fondo (lejos de la calle)
        float w, d;                       // ancho (a lo largo de la calle) y fondo
        float base, entry;                // y de los cimientos y del piso de la entrada
        int floors;
        uint32_t seed;
        bool front;                       // da a la calle: puerta, carteles
    };
    struct Solid {                        // colisión: caja orientada
        Vector3 c, half;
        float yaw;
    };
    struct Wire {
        Vector3 a, b;
        float sag, radius;
        Color color;
    };
    struct Bunting {                      // banderines cruzando la calle
        Vector3 a, b;
        float sag;
    };
    struct Sign {                         // cartel o grafiti: rectángulo del atlas sobre una pared
        Vector3 c, right, up;             // centro, medio ancho y medio alto (vectores)
        int atlasIndex;
    };
    struct Kite {
        Vector3 anchor;                   // de dónde sale el hilo (una terraza)
        Vector3 center;                   // dónde vuela
        Color a, b;
        float phase;
    };
    struct Step {                         // escalón de la escadaria
        Vector3 c, half;
        float yaw;
    };
    struct Chunk {
        Mesh solid{}, decal{}, twoSided{};
        bool hasSolid = false, hasDecal = false, hasTwoSided = false;
        Vector3 center{};
        Vector3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};   // caja de todo lo del bloque (Renderer::BoxVisible)
    };

    void PlaceHouses(const Track& track, const Terrain& terrain);
    void PlaceStreetStuff(const Track& track, const Terrain& terrain);
    bool FootprintFree(const Terrain& terrain, float x, float z, float yaw, float w, float d, float minRoad) const;
    void MarkFootprint(float x, float z, float yaw, float w, float d);
    void BuildAtlas(const Font& font);
    int ChunkOf(float x, float z) const;

    bool built = false, meshes = false;
    float origin = -128.0f, size = 256.0f;
    static constexpr int kChunks = 8;     // 8 x 8 bloques de 32 m
    std::vector<House> houses;
    std::vector<Solid> solids;
    std::vector<Wire> wires;
    std::vector<Bunting> buntings;
    std::vector<Sign> signs;
    std::vector<Kite> kites;
    std::vector<Step> steps;
    std::vector<Parked> parked;
    std::vector<Vector3> poles;            // base de cada poste
    std::vector<Vector4> stripes;          // pintura en el piso: (x, z, rumbo, tipo) ver CreateMeshes
    std::vector<Vector3> stripeSize;       // (largo, ancho, y) de cada una
    std::vector<uint8_t> occupied;         // grilla de 0.5 m: dónde ya hay una casa
    std::vector<JPH::BodyID> bodies;
    Chunk chunks[kChunks * kChunks];
    Mesh backdrop{}, kiteMesh{}, kiteTail{};
    Texture2D atlas{};
    mutable int atlasAniso = -1;           // revisión del filtro anisotrópico (Renderer::AnisoRevision) aplicada al atlas
    std::vector<Rectangle> atlasRects;     // UV de cada cartel (0..1)
    Vector3 startA{}, startB{};           // postes del pórtico de largada
};
