#pragma once
#include "raylib.h"

#include <vector>

class Terrain;
class PhysicsWorld;

// Deformación simplificada del suelo.
//
// HUELLAS (visual): una textura que cubre todo el terreno (gris = sin tocar). Cada rueda apoyada
// dibuja un surco oscuro entre su posición anterior y la actual, con bordes claros de tierra
// empujada. El shader del terreno la usa como color y como relieve (oscuro = hundido, claro =
// levantado). No toca la física.
//
// SURCOS (físico, opcional, F8): donde la trasera patina mucho se hunde el heightfield (máx.
// ~10 cm). No se reconstruye el collider: los cambios se aplican a Jolt en lotes cada 0.3 s.
class TerrainDeformation {
public:
    static constexpr Color kUntouched = {200, 200, 200, 255};   // el shader divide por esto (0.784)

    void Init(const Terrain& terrain, int resolution = 4096);
    void Unload();
    void Clear();

    // Una vez por paso de física y por rueda. wheel: 0..kWheelSlots-1 (las propias y las de otros jugadores).
    static constexpr int kWheelSlots = 10;
    void WheelContact(int wheel, float x, float z, bool grounded, float load, float slip);
    // Dibuja en la textura las huellas acumuladas (una vez por frame, fuera de BeginDrawing).
    void Flush();

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
};
