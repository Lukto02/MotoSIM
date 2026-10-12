#pragma once
// Jolt antes que raylib.
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include "raylib.h"

#include "Highway.h"
#include "Maps.h"

#include <cstdint>
#include <string>
#include <vector>

class Track;
class Terrain;
class PhysicsWorld;
struct Renderer;

// Tráfico de la autopista (sección "traffic" del mapa; ver docs/MAPAS.md, "Autopista"). Dos capas:
//
// - **Base**: todos los autos de la vuelta (las dos manos, todos los carriles), cada uno con su velocidad deseada, que
//   siguen a su carril con el modelo IDM (acelerar hasta su velocidad, guardar distancia al de adelante) y cada tanto
//   cambian de carril (para pasar a uno lento, para volver a la derecha o porque sí) con el guiño puesto antes, si hay
//   lugar. Paso fijo de 0.1 s, sólo sumas, productos, divisiones y raíces y un azar entero con semilla: sale igual en
//   todas las PCs para el mismo instante del reloj del tráfico (en red, el del anfitrión). No sabe nada de las motos.
// - **Local**: los autos cerca de una moto la miran (frenan si la tienen adelante en su carril, no se le tiran encima
//   al cambiar de carril) y se atrasan respecto de la base; después la alcanzan de a poco. Cada PC lo hace con las motos
//   que ve, así que en red los autos lejos de todos coinciden y cerca de una moto pueden diferir un rato.
//
// Los autos cerca de la moto propia son cuerpos cinemáticos de Jolt (capa PROP: los empujan contra la moto, el piloto y
// los objetos sueltos; las ruedas no los pisan): chocarlos o rozarlos con diferencia de velocidad tira al piloto
// (BikeHit). Se dibujan todos juntos en una malla que se rearma cada cuadro.
class Traffic {
public:
    void Build(const Track& track, const Terrain& terrain, const HighwayLayout& layout, const MapDef& def);
    void CreateBodies(PhysicsWorld& world);
    void CreateMeshes();                   // requiere contexto GL
    void Unload();
    bool Active() const { return built && !cars.empty(); }
    int Count() const { return (int)cars.size(); }

    // Una vez por paso de física, antes de PhysicsWorld::Step: avanza el reloj dt, la capa local con las motos
    // (posiciones y velocidades: [0] la moto propia, [1] su piloto (el ragdoll, o la moto si va arriba), después las de
    // los demás) y lleva los cuerpos (alrededor de [0] y [1]) a donde van a estar al final del paso.
    void Step(float dt, PhysicsWorld& world, const JPH::Vec3* bikes, const JPH::Vec3* bikeVel, int count);
    // Después de PhysicsWorld::Step: si la moto propia (o su piloto sentado) tocó un auto con más de kHitSpeed m/s de
    // diferencia en el punto de contacto (de frente, de atrás o rozándolo de costado), devuelve true y lo cuenta.
    bool BikeHit(PhysicsWorld& world, bool log, float simTime);
    // Reloj del tráfico (s). En red el invitado sigue al del anfitrión (SyncClock con lo que mandó, ya corrido por la
    // latencia): si se fue más de 2 s se pone al día de una (rehace la base si tiene que volver atrás); si no, corre un
    // poco más rápido o más lento hasta alcanzarlo.
    double Clock() const { return clock; }
    void SyncClock(double hostClock);

    // Bot: hacia qué lateral ir (m, + derecha; un carril libre o entre dos autos) y hasta qué velocidad (el de adelante)
    // estando en s del trazado, a v m/s y en el lateral lat.
    void BotAdvice(float s, float v, float lat, float& outLat, float& outSpeed);

    void Draw(Renderer& r, float alpha, bool shadowPass);

    // Telemetría: autos frenando por una moto, cuerpos en uso, cambios de carril y golpes.
    int reacting = 0, lagging = 0, bodiesUsed = 0, laneChanges = 0, hits = 0;
    bool log = false;                      // --telemetry: los guiños cerca de la moto (líneas GUINO)

private:
    // Un auto de la base. pos: m recorridos en su sentido (0..L); en el trazado, s = pos (mano +1) o L - pos (mano -1).
    struct Car {
        int8_t dir = 1;                    // +1 / -1
        uint8_t kind = 0, color = 0;
        int8_t lane = 0, target = -1;      // carril (0 = el rápido) y al que va (-1: ninguno)
        float pos = 0.0f, v = 0.0f, a = 0.0f, v0 = 30.0f;
        float headway = 1.3f, accel = 1.4f;
        float lc = -1.0f, lcDur = 4.0f;    // avance del cambio de carril (0..1; < 0: no está cambiando)
        float signal = 0.0f;               // s de guiño antes de empezar a cambiar
        double nextThink = 0.0;
        uint32_t rng = 1;
        // Capa local (no es parte de la base): atraso, velocidad mostrada, lateral mostrado.
        float lag = 0.0f, dispV = 0.0f, dispLat = 0.0f;
        bool local = false;                // tiene capa local (cerca de una moto o todavía atrasado)
        bool braking = false;              // luces de freno (base o local)
        int body = -1;                     // cuerpo cinemático asignado (o -1)
    };
    struct Kind {
        float length, width, height;
        float accel, vMin, vMax;           // m/s² y velocidades deseadas (m/s), 0 = las del mapa
        JPH::RefConst<JPH::Shape> shape;
    };
    struct Pose {
        JPH::Vec3 pos = JPH::Vec3::sZero();
        JPH::Quat rot = JPH::Quat::sIdentity();
    };

    void BaseReset();
    void BaseStep();
    void Advance(double to);
    float LaneLat(const Car& c, float lcProgress) const;
    float Idm(const Car& c, float v, float gap, float vLead) const;
    Pose CarPose(const Car& c, float pos, float lat, float yawOffset) const;
    void BuildMeshKinds();

    bool built = false;
    const Track* track = nullptr;
    const Terrain* terrain = nullptr;
    HighwayLayout layout;
    float L = 1.0f;
    // Claves del mapa
    float density = 10.0f;                 // autos por km y por carril
    float speedMin = 85.0f / 3.6f, speedMax = 135.0f / 3.6f;
    float laneChangeEvery = 14.0f;         // s entre ideas de cambiar de carril (de cada auto, en promedio)
    float truckShare = 0.12f;
    uint32_t seed = 1;
    bool react = true;

    std::vector<Kind> kinds;
    std::vector<Car> cars, initial;        // initial: como los arma Build (la base vuelve ahí en BaseReset)
    std::vector<std::vector<int>> laneLists;   // por mano y carril: índices ordenados por pos (base)
    double clock = 0.0, baseTime = 0.0;    // reloj del tráfico; tiempo del último paso de la base
    long long baseSteps = 0;
    float clockRate = 1.0f;

    // Pose mostrada (antes y después del último paso de física) de los autos con pose al día.
    struct Shown {
        int car;
        Pose prev, cur;
        bool fresh;
        float s, lat, v;                   // dónde está sobre el trazado (s, lateral) y a qué velocidad (para el bot)
    };
    std::vector<Shown> shown;
    std::vector<int> shownIndex;           // por auto: lugar en shown o -1

    // Cuerpos cinemáticos alrededor de la moto propia.
    std::vector<JPH::BodyID> bodies;
    std::vector<int> bodyCar;              // por cuerpo: el auto que lo usa (o -1)
    JPH::Vec3 lastFocus = JPH::Vec3::sZero();
    bool focusSet = false;

    // Bot: carril elegido (para no cambiar a cada rato).
    float botLat = 0.0f, botBikeLat = 0.0f;
    bool botLatSet = false;

    // Mallas: una pieza por tipo (pintura, que se tiñe; el resto con sus colores; las luces aparte) y la malla que se
    // rearma cada cuadro.
    struct PartMesh {
        std::vector<float> pos, nrm, uv;
        std::vector<unsigned char> col;
    };
    struct KindMesh {
        // paint: la pintura (se arma en blanco y se tiñe con el color del auto); always: vidrios, caja del camión; near:
        // ruedas, paragolpes, espejos (sólo cerca); las luces aparte (según frene o ponga el guiño).
        PartMesh paint, always, nearOnly, tail, brake, head, blinkL, blinkR;
    };
    std::vector<KindMesh> kindMeshes;
    Mesh batches[3]{};
    int batchIdx = 0;
    int batchCap = 0, batchUsed = 0, batchShadow = 0;   // vértices: todos y los de los autos cerca (sombras)
    long long stepCount = 0, batchStep = -1;
    float batchAlpha = -1.0f;
    bool meshes = false;
    // Para la capa local: dónde está cada moto sobre el trazado (s, lateral, velocidad a lo largo) y la pista del bot.
    struct BikeOnRoad {
        float s = 0.0f, lat = 0.0f, vAlong = 0.0f;
    };
    std::vector<int> bikeHint;
    std::vector<char> signalShown;         // log: el guiño de cada auto ya se anotó
    std::vector<BikeOnRoad> bikeRoad;
    double adviceClock = -1.0;
};
