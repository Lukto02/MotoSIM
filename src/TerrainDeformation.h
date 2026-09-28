#pragma once
#include "raylib.h"

#include <vector>

class Terrain;
class PhysicsWorld;
struct Renderer;

// Deformación simplificada del suelo.
//
// HUELLAS (visual): una textura que cubre todo el terreno (gris = sin tocar). Cada rueda apoyada
// dibuja un surco oscuro entre su posición anterior y la actual, con bordes claros de tierra
// empujada. El shader del terreno la usa como color y como relieve (oscuro = hundido, claro =
// levantado). No toca la física. En el asfalto no hay surcos (ver GOMA).
//
// GOMA (visual, asfalto): donde la rueda resbala sobre el pavimento (trabada, derrapando o patinando
// con el gas) quedan marcas negras de goma. No van en la textura (en los mapas de asfalto un texel mide
// 34-50 cm y la marca de una cubierta, 10-18): son tiras de cuadriláteros apoyadas en el terreno que
// oscurecen lo que hay debajo (ver Renderer::DrawSkidMarks y docs/RENDER.md).
//
// SURCOS (físico, opcional, F8): donde la trasera patina mucho se hunde el heightfield (máx.
// ~10 cm). No se reconstruye el collider: los cambios se aplican a Jolt en lotes cada 0.3 s.
class TerrainDeformation {
public:
    static constexpr Color kUntouched = {200, 200, 200, 255};   // el shader divide por esto (0.784)

    void Init(const Terrain& terrain, int resolution = 4096);
    void Unload();
    void Clear();

    // Una vez por paso de física y por rueda. wheel: 0..kWheelSlots-1 (las propias y las de otros jugadores;
    // las pares son delanteras). slide: a cuántos m/s resbala la goma sobre el suelo (hipot(ω·R − vLong,
    // vLat)); heading: hacia dónde mira la moto (x, z); onTerrain: la rueda apoya en el terreno (no en una
    // rampa, un escalón o una caja: la goma se dibuja a la altura del terreno). dt: el paso de física. edge (0..1):
    // frena cerca del límite sin trabar (sólo la moto propia, ver BrakingEdge en Game.cpp): marca tenue.
    static constexpr int kWheelSlots = 10;
    void WheelContact(int wheel, float x, float z, bool grounded, float load, float slip, float slide = 0.0f,
                      Vector2 heading = {0.0f, 1.0f}, bool onTerrain = true, float dt = 1.0f / 120.0f, float edge = 0.0f);
    // Dibuja en la textura las huellas acumuladas y sube las marcas de goma nuevas (una vez por frame,
    // fuera de BeginDrawing).
    void Flush();
    // Marcas de goma: dentro de BeginMode3D, después de todo lo opaco del suelo (terreno, calcos del piso).
    void DrawSkids(Renderer& r) const;

    Texture2D MarksTexture() const { return marks.texture; }

    // Surcos físicos (funcionan también sin ventana).
    bool physicalRuts = false;
    float digRate = 0.00012f;        // m por paso y por m/s de patinaje
    float maxDepth = 0.10f;          // m
    void DigRut(Terrain& terrain, float x, float z, float spin, float slide);
    void CommitRuts(Terrain& terrain, PhysicsWorld& world, float dt);

private:
    struct Stroke {
        Vector2 a, b;
        float width;
        Color color;
        bool ridge;                  // borde de tierra levantada (se dibuja debajo del surco)
    };
    Vector2 ToTexel(float x, float z) const;

    RenderTexture2D marks{};
    int resolution = 4096;
    bool mipsStale = false;
    double lastMips = 0.0;
    float origin = 0.0f, size = 1.0f;
    std::vector<Stroke> pending;
    Vector2 last[kWheelSlots]{};
    bool hasLast[kWheelSlots]{};
    float commitTimer = 0.0f;

    // Goma: un anillo de cuadriláteros (4 vértices cada uno: 16384 entran justo en índices de 16 bits).
    // Cuando se llena, las marcas nuevas pisan las más viejas.
    static constexpr int kSkidQuads = 16384;
    struct SkidEnd {                 // el último borde de la tira de cada rueda
        bool active = false;         // está marcando
        bool hasEdge = false;        // ya tiene bordes (al empezar sólo el centro: falta saber hacia dónde va)
        float x = 0.0f, z = 0.0f;    // centro
        Vector2 side{};              // de izquierda a derecha
        Vector3 left{}, right{};     // bordes, a la altura del terreno
        float halfWidth = 0.0f;
        float along = 0.0f;          // m desde el comienzo de la tira
        unsigned char dark = 0;      // cuánto oscurece (0..255)
        float dwell = 0.0f;          // s patinando sin avanzar (burnout): cada tanto una mancha
    };
    struct SkidPoint {
        float x, z;
        float halfWidth;
        unsigned char dark;
    };
    const Terrain* terrain = nullptr;
    Mesh skid{};
    int skidNext = 0, skidUsed = 0;
    int dirtyLo = kSkidQuads, dirtyHi = -1;
    SkidEnd skidEnd[kWheelSlots]{};
    void Skid(int wheel, float x, float z, bool on, float load, float slide, Vector2 heading, float paved, float dt, float edge);
    SkidEnd MakeEnd(const SkidPoint& p, Vector2 side, float along) const;
    void AddSkidQuad(const SkidEnd& a, const SkidEnd& b);
};
