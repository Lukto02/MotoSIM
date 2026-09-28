#pragma once
#include "raylib.h"

class Terrain;

// Cámara en tercera persona con resortes: sigue a la moto con retraso, mira un poco hacia
// donde va y abre el FOV con la velocidad. Nunca está rígidamente unida a la moto.
class ChaseCamera {
public:
    Camera3D cam{};

    float distance = 4.0f;       // m detrás
    float height = 1.55f;        // m arriba
    float lookHeight = 0.7f;
    float stiffness = 7.0f;      // frecuencia del resorte horizontal (rad/s)
    float verticalStiffness = 3.5f;
    float fovMin = 65.0f, fovMax = 75.0f;
    bool shakeEnabled = true;    // menú "Sacudón de cámara": sin él, ni golpes ni la vibración a alta velocidad

    enum class Mode { Chase, Side };
    Mode mode = Mode::Chase;

    void Reset(Vector3 bikePos, Vector3 bikeForward);
    void ToggleMode() { mode = mode == Mode::Chase ? Mode::Side : Mode::Chase; }
    // Golpe (aterrizaje, choque): sacudón con giro, zoom hacia adentro y la cámara que "cae" y vuelve.
    void AddShake(float amount);
    // Órbita con el mouse: gira alrededor de la moto (radianes); sin tocarla un rato vuelve atrás.
    void Orbit(float dYaw, float dPitch);
    void Zoom(float steps);          // rueda del mouse: + acerca
    // Corrimiento lateral de la vista en metros (el menú la corre para que la moto quede a la derecha).
    void SetShift(float meters) { shift = meters; }
    // Pruebas: órbita (radianes) y distancia fijas, sin volver atrás.
    void FixView(float yaw, float pitch, float zoomFactor)
    {
        fixedView = true;
        fixedYaw = yaw;
        fixedPitch = pitch;
        fixedZoom = zoomFactor;
    }
    void Update(float dt, Vector3 bikePos, Vector3 bikeForward, Vector3 bikeVelocity, const Terrain& terrain);

private:
    Vector3 pos{}, vel{};
    Vector3 look{}, lookVel{};
    Vector3 heading{0.0f, 0.0f, 1.0f};
    float fov = 65.0f;
    float trauma = 0.0f, shakeTime = 0.0f, fovKick = 0.0f;
    void UpdateOrbit(float dt);
    float orbitYaw = 0.0f, orbitPitch = 0.0f;           // pedido por el mouse (yaw sin envolver mientras se arrastra)
    float orbitYawS = 0.0f, orbitPitchS = 0.0f;         // lo que se ve (suavizado)
    float orbitYawVel = 0.0f, orbitPitchVel = 0.0f;     // resorte de vuelta atrás
    float orbitIdle = 10.0f, zoom = 1.0f, zoomS = 1.0f;
    float shift = 0.0f, shiftS = 0.0f;
    bool fixedView = false;
    float fixedYaw = 0.0f, fixedPitch = 0.0f, fixedZoom = 1.0f;
    float smoothVy = 0.0f;
};
