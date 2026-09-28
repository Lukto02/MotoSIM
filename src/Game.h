#pragma once
#include <Jolt/Jolt.h>

// Primero todo lo que incluye Jolt, después raylib.
#include "Bike.h"
#include "Circuit.h"
#include "Favela.h"
#include "Maps.h"
#include "Props.h"
#include "Terrain.h"
#include "Track.h"
#include "Tuning.h"
#include "Rider.h"          // incluye Jolt y después raylib
#include "RiderModel.h"
#include "Multiplayer.h"

#include "Camera.h"
#include "EngineSound.h"
#include "Particles.h"
#include "Render.h"
#include "TerrainDeformation.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class PhysicsWorld;

struct GameOptions {
    bool headless = false;           // sin ventana: sólo física (tests automáticos)
    bool bot = false;                // piloto automático que sigue la línea central
    bool telemetry = false;          // imprime telemetría por consola
    float telemetryInterval = 0.25f; // s entre líneas (--telemetry-dt)
    std::string test;                // escenario scripted: idle, accel, wheelie, turn, brake
    float quitAfter = -1.0f;         // segundos de simulación
    float screenshotAt = -1.0f;
    std::string screenshotFile = "screenshot.png";
    float screenshotEvery = 0.0f;    // --shots-every DT: desde screenshotAt, una captura cada DT s (archivo_000.png...)
    bool hideHud = false, generatedRider = false;
    // --respawn-after S: segundos caído hasta reaparecer solo; S < 0, sólo con R. Sin la opción, el jugador
    // reaparece con R y el bot, las pruebas y sin ventana, solos a los 3 s (las vueltas y las regresiones lo usan).
    float respawnAfter = 3.0f;
    bool respawnAfterSet = false;
    float wheelLogFrom = -1.0f, wheelLogTo = -1.0f;   // --wheel-log T0 T1
    float spawnS = -1.0f;            // distancia sobre la pista donde aparecer
    int width = 1600, height = 900;
    bool debugVectors = false;       // --debug: arranca con los vectores físicos (F1)
    bool sideCamera = false;         // --side: arranca con la cámara lateral (C)
    bool ruts = false;               // --ruts: arranca con los surcos físicos (F8)
    bool flat = false;               // --flat: terreno plano sin obstáculos (pruebas)
    float dropHeight = 0.0f;         // --drop H: aparece H m más alto y a 12 m/s (prueba de aterrizaje)
    float botLateralAccel = 6.0f;    // --bot-lat A: m/s^2 con que el bot encara las curvas
    float viewZoom = 0.0f;           // --view yaw pitch zoom: cámara fija (grados; zoom 1 = normal)
    float viewYaw = 0.0f, viewPitch = 0.0f;
    bool hideRider = false;          // --norider: moto sola (para mirar las piezas)
    bool host = false;               // --host: crea una partida LAN al arrancar
    std::string join;                // --join CÓDIGO: se une a una partida al arrancar
    std::string name;                // --name: nombre del jugador (si no, el usuario de Windows)
    int port = 27015;                // --port: puerto UDP del anfitrión
    float netLag = 0.0f;             // --net-lag MS: demora lo que llega de la red (pruebas: como por internet)
    float netJitter = 0.0f;          // --net-jitter MS: más una demora al azar de hasta MS (llega en tandas)
    std::string menuScreen;          // --menu join|name|lobby|maps: abre esa pantalla (pruebas)
    std::string map;                 // --map motocross|favela|mod/mapa: mapa con que arranca (id, archivo o nombre)
    std::string bike;                // --bike mod/moto: corre con esa moto en cualquier mapa (si no, la del mapa o la elegida en el menú)
    int fullscreen = -1;             // --fullscreen / --windowed (-1: lo que se eligió la última vez)
    bool sizeGiven = false;          // --size: ventana de ese tamaño (pruebas: no se va a pantalla completa)
};

class Game {
public:
    explicit Game(const GameOptions& options);
    ~Game();
    int Run();

private:
    void Init();
    void Shutdown();
    void Frame(float frameDt);
    void Step(BikeInput input);

    void HandleKeys();
    BikeInput ReadPlayerInput(float dt);
    BikeInput BotInput();
    BikeInput TestInput();
    // Prueba netchoque (en red): ubica a cada uno, le da la velocidad y al final dice quién se cayó.
    void NetChoqueStep();
    BikeInput NetChoqueInput();

    // Arma el mapa (pista, terreno, objetos, favela, luz y moto). Rehace la física: las motos de los
    // demás se vuelven a crear solas con su próximo estado. keepPlace: la moto sigue donde estaba
    // (recarga del mismo mapa) en vez de ir a la largada.
    void LoadMap(const MapDef& def, bool keepPlace = false);
    // Mods: lee todas las carpetas mods/ (al arrancar y con Shift+F5). reloadMap: rearma el mapa actual
    // con lo que diga ahora su archivo. Si el archivo del mapa que se está corriendo cambia en disco
    // (se guardó desde un editor), el mapa se rearma solo (WatchMapFile).
    void ScanMods();
    void ReloadMods(bool reloadMap);
    void WatchMapFile(float dt);
    int CurrentMapIndex() const { return mods.FindMap(current.id); }
    void Respawn(float s);
    void RespawnNearest();
    void UpdateLap();
    void EmitEffects();              // partículas, huellas y sacudón según el estado de las ruedas
    // Llamarada y estampido en la salida del escape de una moto (owner 0 = la propia, 1 + id = la
    // del jugador id); gain: volumen del estampido (baja con la distancia).
    void EmitBackfire(float strength, int owner, const JPH::Mat44& bikeWorld, JPH::Vec3 bikeVel, float gain);
    void FeedFlames();
    BikeState LocalState() const;    // lo que se manda por red
    void StartHost();
    void StartJoin(const std::string& code);
    void HandleMenuKeys();
    void DrawMenu();
    void DrawSession();              // HUD de la partida LAN: código, jugadores, nombres sobre las motos
    void LoadTuning(bool reload);
    std::string bikeTuning;                         // .ini de la moto (pisa tuning.ini), si tiene
    std::string bikeName;
    // Motos: la elegida en el menú ("" = la de cada mapa; se guarda en preferencias.ini) y los números de
    // cada una (tuning.ini + su .ini), para las estadísticas del menú y las motos de los demás jugadores.
    std::string chosenBike, bikeId;                 // bikeId: con la que se corre ahora
    // La que se guarda en preferencias.ini: la elegida en el menú. --bike cambia chosenBike sólo por esa vez
    // (si se guardaba, la moto de una prueba quedaba para todos los mapas en las siguientes partidas).
    std::string savedBike;
    std::string baseTuningPath;                     // dónde se encontró tuning.ini
    struct BikeStats {
        float hp, kg, topKmh, zeroTo100, latG, travelMm, turnRadius;
    };
    struct BikeData {
        Tuning tuning;
        BikeParams params;
        BikeStats stats{};
    };
    std::map<std::string, std::unique_ptr<BikeData>> bikeData;
    BikeData* DataForBike(const std::string& id);   // nullptr si no existe
    void ReloadBikeData();                          // tras F5 o releer los mods (los objetos no cambian de lugar)
    static BikeStats ComputeStats(const BikeParams& p);
    void ChooseBike(const std::string& id);         // "" = la del mapa; rearma el mapa
    void OpenBikeMenu();
    static std::string StyleLabel(const std::string& style);
    static EngineSound::Character SoundCharacter(const BikeParams& p);
    // Cámara del menú (plano de cine) y la moto del selector girando en su plataforma.
    void UpdateMenuCamera(float dt);
    float menuCamBlend = 0.0f, menuCamTime = 0.0f, menuCamYaw = 0.0f;
    float showroomBlend = 0.0f;
    JPH::Vec3 showroomAnchor = JPH::Vec3::sZero();   // dónde queda la vitrina (fijo mientras se elige moto)
    float showroomGround = 0.0f;                     // altura de la tapa de la plataforma (menos kPlatformHeight)                     // 0 = plano del menú, 1 = el del selector de motos
    Bike previewBike;
    std::string previewBikeId;
    float previewLift = 0.0f;
    JPH::Vec3 previewPos = JPH::Vec3::sZero();
    static constexpr float kPlatformHeight = 0.08f;
    void PrintTelemetry() const;
    void ShowMessage(const std::string& text, float seconds = 2.0f);
    // Pantalla completa (sin bordes, del tamaño del monitor): F11, Alt+Enter o el menú. Se recuerda
    // en preferencias.ini, junto al ejecutable.
    void SetFullscreen(bool on, bool remember = true);   // remember: guardarlo (no cuando lo pide la línea de comandos)
    void LoadPrefs();
    void SavePrefs() const;
    bool fullscreen = false;

    void Draw();
    void DrawScene(bool shadowCasters);
    void BuildTrackDressing();
    void DrawHUD();

    GameOptions opt;
    Tuning tuning;
    BikeParams bikeParams;
    std::unique_ptr<PhysicsWorld> physics;
    Track track;
    Terrain terrain;
    Bike bike;
    RiderRagdoll ragdoll;            // el piloto suelto tras una caída
    RiderModel riderModel;           // piloto con modelo glTF (si se encuentra el archivo)
    bool useRiderModel = true;       // F9: modelo / piloto generado
    bool drawRiderModel = false;     // este frame
    Matrix riderWorld{};
    ChaseCamera camera;
    Renderer renderer;
    Particles particles;
    EngineSound sound;
    TerrainDeformation deformation;
    Font font{}, mono{};
    bool fontsLoaded = false;

    struct Prop { JPH::Mat44 transform; Color color; };
    std::vector<Prop> dressing;
    ModRegistry mods;
    MapDef current;                                 // el mapa cargado (copia: los mods se pueden releer)
    long long mapFileTime = 0;                      // cuándo se modificó su archivo (para rearmarlo al guardar)
    float watchTimer = 0.0f;
    std::string missingMap;                         // el anfitrión corre un mapa que acá no está
    Props props;
    Favela favela;
    Circuit circuit;
    Bike parkedBike;                                // para dibujar las motos estacionadas de la favela
    bool mapLoaded = false;
    int mapLoadIn = -1;                             // frames hasta cargar el mapa pedido (se muestra "Cargando")
    int mapRequested = -1;                          // índice en mods.Maps()
    int mapScroll = 0;                              // primer mapa visible en la lista
    // Grau (wheelie): cuánto lleva el actual, el último que terminó y el mejor.
    float grauTime = 0.0f, lastGrau = 0.0f, bestGrau = 0.0f, grauShow = 0.0f;
    JPH::Vec3 ExhaustTip() const;
    JPH::Vec3 ExhaustDir() const;
    std::vector<Matrix> grassTufts;                // matas de pasto 3D alrededor de la cámara
    Vector3 grassCenter{1e9f, 0.0f, 1e9f};

    static constexpr float kDt = 1.0f / 120.0f;    // física a 120 Hz, 1 collision step
    float accumulator = 0.0f, alpha = 0.0f, simTime = 0.0f;
    bool pendingShiftUp = false, pendingShiftDown = false;
    bool debugVectors = false, showHud = true, slowMotion = false, paused = false;
    bool postEffects = true;                       // motion blur, aberración, viñeta y grano (F7)
    // Menú (se guardan en preferencias.ini): el motion blur con la velocidad y el sacudón de cámara
    // (camera.shakeEnabled). Los dos, prendidos por defecto.
    bool motionBlur = true;

    // Input de teclado suavizado + gatillos del gamepad
    float kbThrottle = 0.0f, kbFront = 0.0f, kbRear = 0.0f, kbSteer = 0.0f, kbLean = 0.0f, kbSide = 0.0f;
    bool rtArmed = false, ltArmed = false;

    // Vueltas
    int trackIndex = -1;
    float trackS = 0.0f;
    float lapStart = -1.0f, lastLap = 0.0f, bestLap = 0.0f;
    int lapCount = 0;
    bool halfway = false;

    // Efectos
    // Por moto (0 = la propia, 1 + id = la del jugador id) y por rueda (2 * moto + rueda).
    static constexpr int kBikeSlots = 1 + Multiplayer::kMaxPlayers;
    float roostAccum[kBikeSlots] = {}, dustAccum[2 * kBikeSlots] = {};
    float sparkAccum = 0.0f;                        // chispas de la cola raspando (la moto propia)
    bool wasGrounded[2 * kBikeSlots] = {};
    static_assert(2 * kBikeSlots <= TerrainDeformation::kWheelSlots, "huellas: faltan lugares para las ruedas de todos");
    struct WheelFx {                 // lo que hace falta para las huellas, la tierra y el polvo de una rueda
        bool grounded;
        JPH::Vec3 contact, normal;
        float spin, latVel, load, markSlip;
    };
    void EmitWheelEffects(int bikeSlot, int wheel, const WheelFx& w, JPH::Vec3 fwd, JPH::Vec3 bikeVel);
    // Petardeos: se detectan en los cambios y al soltar el gas; cada ráfaga agenda varias explosiones.
    struct Pop { float at, strength; };
    std::vector<Pop> pops;
    int lastGear = 1;
    float lastRpm = 0.0f, lastGripThrottle = 0.0f;
    struct Flame { float time = 0.0f, strength = 0.0f; };   // llama viva en la boca del escape (se alimenta cada paso)
    Flame flames[1 + Multiplayer::kMaxPlayers];
    uint8_t localPops = 0;           // petardeos propios (viajan por red: los demás los ven y escuchan)
    float lastPopStrength = 0.0f;

    // Multijugador
    Multiplayer mp;
    std::string playerName;
    std::vector<std::string> riderPaths;
    // Menú (Esc): principal, código para unirse, sala de la partida y nombre. La carrera sigue de fondo
    // con la cámara girando alrededor de la moto.
    enum class Menu { None, Main, Join, Lobby, Name, Maps, Bikes };
    struct MenuItem {
        std::string label, hint;
        std::function<void()> action;
    };
    std::vector<MenuItem> MenuItems();
    void OpenMenu(Menu m);
    void LeaveSession();
    Menu menu = Menu::None;
    int menuSel = 0;
    float menuTime = 0.0f, menuSelY = -1.0f;
    bool raced = false;              // ya se cerró el menú alguna vez ('Seguir corriendo' en vez de 'Jugar solo')
    std::vector<Rectangle> menuRects;   // dónde quedó cada opción (para el mouse)
    Vector2 lastMouse{};
    std::string codeInput, nameInput, lastStatus;
    bool wasConnected = false, quitRequested = false;
    int duelOthers = 0;              // tests netduel / netcrash / netchoque
    float duelStart = -1.0f;
    // netchoque: ya ubicado, cuándo se cayó (s desde la largada de la prueba), el choque más fuerte
    // (cierre y lo que traía el otro) y si ya imprimió el resumen.
    bool choquePlaced = false, choqueReported = false, choqueOtherFell = false;
    float choqueCrashAt = -1.0f, choqueClosing = 0.0f, choqueFromOther = 0.0f, choqueHitSpeed = -1.0f, choqueMeetAt = 0.0f;
    JPH::Vec3 choqueFrom = JPH::Vec3::sZero(), choqueDir = JPH::Vec3::sAxisZ();
    // Último golpe fuerte con cada jugador (simTime): los contactos con él justo después son el eco del
    // mismo choque (la moto que empujó su PC vuelve por la red) y no tiran a nadie.
    float bikeHitAt[Multiplayer::kMaxPlayers] = {-100.0f, -100.0f, -100.0f, -100.0f};
    // Último contacto trompa contra trompa con cada uno y si ya estaba caído: si se cae justo después, de
    // frente caen los dos aunque esta PC lo haya visto retroceder (su PC vio el golpe entero).
    float headOnAt[Multiplayer::kMaxPlayers] = {-100.0f, -100.0f, -100.0f, -100.0f};
    bool remoteDown[Multiplayer::kMaxPlayers] = {};
    float copiedTime = 0.0f;         // "¡Copiado!" en la sala
    unsigned rngState = 0x12345u;
    float Rand(float a, float b);

    int botIndex = -1;
    float stuckTime = 0.0f;
    JPH::Vec3 flipRef = JPH::Vec3::sAxisZ();       // test flip: rumbo inicial y giro acumulado
    float flipAngle = 0.0f, flipPrev = 0.0f;
    bool flipReported = false;
    float lastTelemetry = -1.0f;
    bool screenshotTaken = false;
    int screenshotCount = 0;
    bool testBraking = false;        // pruebas frenadaN y frenacurvaN
    float testBrake = 0.0f;
    float testSteer = 0.0f;
    float testT0 = 0.0f;             // cuando llegó a la velocidad de la prueba
    std::string message;
    float messageTime = 0.0f;
};
