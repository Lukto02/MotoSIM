#pragma once
// Multijugador por LAN. Un jugador crea la partida (anfitrión) y los demás se unen con el código de
// invitación, que lleva la IP y el puerto del anfitrión. Todo va por UDP: cada uno simula su propia
// moto y manda su estado 60 veces por segundo; el anfitrión reenvía el de cada uno a los demás.
//
// Las motos de los otros se dibujan donde se predice que están ahora (la última foto + su velocidad
// por el tiempo que pasó, con la latencia medida por ping) y las correcciones se reparten en ~0,1 s
// para que no salten. Cada una tiene un cuerpo cinemático en la física local: la moto propia choca
// contra ellas (y cada jugador resuelve su lado del choque en su PC).
#include <Jolt/Jolt.h>

#include "Bike.h"
#include "Net.h"
#include "Rider.h"
#include "RiderModel.h"

#include <deque>
#include <memory>
#include <functional>
#include <string>
#include <utility>
#include <vector>

class PhysicsWorld;
namespace JPH { class Body; }

// Lo que viaja por red de cada moto.
struct BikeState {
    enum Flags : uint8_t { kRiderOn = 1, kCrashed = 2, kLimiter = 4, kShifting = 8, kFrontGround = 16, kRearGround = 32 };
    JPH::Vec3 pos = JPH::Vec3::sZero(), vel = JPH::Vec3::sZero(), angVel = JPH::Vec3::sZero();
    JPH::Quat rot = JPH::Quat::sIdentity();
    float susp[2] = {0.2f, 0.2f}, wheelAngle[2] = {}, wheelOmega[2] = {};
    // Cada rueda en el suelo: dónde apoya, con cuánta carga y cuánto patina (huellas, tierra y polvo).
    float contactX[2] = {}, contactZ[2] = {}, load[2] = {}, markSlip[2] = {}, spin[2] = {}, latVel[2] = {};
    float steer = 0.0f, lean = 0.0f, gripThrottle = 0.0f, rpm = 1800.0f, throttle = 0.0f;
    float legOut = 0.0f;             // pata afuera en curva (+ derecha, - izquierda)
    float bodyTilt = 0.0f;           // cuerpo corrido de costado en el aire (whip)
    float riderSide = 0.0f;          // cuerpo de costado en el piso (flechas)
    uint8_t gear = 1, flags = kRiderOn;
    uint8_t pops = 0;                // contador de petardeos (cuántos hubo desde el comienzo, mod 256)
    float popStrength = 0.0f;
    uint16_t laps = 0;
    float bestLap = 0.0f;
    JPH::Mat44 parts[RiderPart::Count];   // el ragdoll, si el piloto se cayó
};

class Multiplayer {
public:
    enum class Mode { Off, Host, Client };
    static constexpr int kMaxPlayers = 4;

    ~Multiplayer();
    // Para cargar el piloto de los demás (sin gráficos, en las pruebas sin ventana, no se carga).
    void SetRiderModel(const std::vector<std::string>& paths, bool graphics);

    bool Host(const std::string& name, uint16_t port = net::kDefaultPort);
    bool Join(const std::string& code, const std::string& name);
    void Leave(PhysicsWorld& physics);

    Mode GetMode() const { return mode; }
    bool Active() const { return mode != Mode::Off; }
    bool Connected() const { return mode == Mode::Host || (mode == Mode::Client && myId >= 0); }
    int LocalId() const { return myId < 0 ? 0 : myId; }
    const std::string& InviteCode() const { return code; }
    const std::vector<std::string>& OtherCodes() const { return otherCodes; }
    const std::string& Status() const { return status; }   // "Conectando...", errores
    float PingMs() const { return rtt * 1000.0f; }          // cliente: con el anfitrión
    // Mapa de la partida ("mod/archivo"): el anfitrión lo fija (SetLocalMap) y lo manda; el cliente lo
    // lee (HostMap; vacío hasta conectarse).
    void SetLocalMap(const std::string& map) { localMap = map; rosterTimer = 1.0f; }
    const std::string& HostMap() const { return hostMap; }
    // Moto de cada uno ("mod/archivo"): la propia se anuncia al entrar y cada vez que cambia; la de los
    // demás llega en la lista de jugadores. bikeParams devuelve los números de una moto (cargados por el
    // juego; nullptr si no la tiene: se dibuja con los propios).
    void SetLocalBike(const std::string& bike);
    std::function<BikeParams*(const std::string& bike)> bikeParams;
    // Saca las motos de los demás de la física (al rehacerla): vuelven con su próximo estado.
    void ResetRemotes(PhysicsWorld& physics);
    // Telemetría: estados recibidos y tamaño medio de las correcciones de posición (m) al llegar cada uno.
    int statesReceived = 0;
    float correctionAvg = 0.0f;
    // Pruebas (--net-lag, --net-jitter): lo que llega se procesa recién a los lag s, más hasta jitter s al
    // azar (como una partida por internet).
    float lag = 0.0f, jitter = 0.0f;

    // Una vez por paso de física: recibe, manda el estado propio (60 Hz), crea/borra jugadores,
    // envejece las fotos y mueve los cuerpos cinemáticos a donde estarán al terminar el paso.
    void Step(float dt, const BikeState& local, const std::string& localName, PhysicsWorld& physics, BikeParams& params);

    struct Remote {
        bool active = false;
        int id = -1;
        std::string name;
        int ping = 0;                            // ms (según el anfitrión)
        BikeState state;
        bool hasState = false;
        float age = 0.0f;                        // s desde que se tomó la foto (con la latencia)
        uint16_t lastSeq = 0;
        double lastHeard = 0.0;
        JPH::Vec3 posError = JPH::Vec3::sZero();   // corrección pendiente (se desvanece)
        JPH::Quat rotError = JPH::Quat::sIdentity();
        Bike bike;                               // sólo para dibujarla
        std::unique_ptr<RiderModel> rider;
        RiderPose lastPose;                      // última pose sentado (de ahí sale el ragdoll al caerse)
        bool riderBound = false;
        JPH::Mat44 parts[RiderPart::Count];      // ragdoll suavizado
        JPH::Body* proxy = nullptr;
        bool proxyWithRider = true;
        uint8_t lastPops = 0;
        bool teleport = true;                    // la próxima pose es un salto (aparece o reaparece): sin empujar
        std::string bikeId;                      // con qué moto se armó
        JPH::Vec3 DisplayPos() const;
        JPH::Quat DisplayRot() const;
        // La velocidad que mandó en un punto de la moto (sin las correcciones de la predicción, que el
        // cuerpo cinemático sí lleva); cero si la foto es tan vieja que ya no se extrapola (quedó quieta).
        JPH::Vec3 PointVelocity(JPH::Vec3 point) const;
    };
    const Remote* Remotes() const { return remotes; }         // [kMaxPlayers], por número de jugador
    // Frame a frame (para dibujar): aplica el estado a la moto visual y posa al piloto.
    void UpdateVisuals(float frameDt);
    // Petardeos de los demás desde el último llamado: (número de jugador, fuerza).
    std::vector<std::pair<int, float>> TakePops();

    // Lista de jugadores para el HUD: número, nombre, ping (ms, -1 = uno mismo).
    struct Entry {
        int id;
        std::string name;
        int ping;
    };
    std::vector<Entry> Roster(const std::string& localName) const;

private:
    struct Peer {                                // anfitrión: cada cliente
        bool active = false;
        net::Address addr;
        std::string name, bike;
        double lastHeard = 0.0;
        float rtt = 0.0f;
    };
    void HandlePacket(const net::Address& from, const uint8_t* data, int size, PhysicsWorld& physics, BikeParams& params);
    void ReceiveState(int id, net::Reader& r, float latency, PhysicsWorld& physics, BikeParams& params);
    void SendTo(const net::Address& to, const net::Writer& w);
    void Broadcast(const net::Writer& w, int exceptId);
    void EnsureRemote(int id, PhysicsWorld& physics, BikeParams& params);
    void RemoveRemote(int id, PhysicsWorld& physics);
    net::Writer Packet(uint8_t type) const;
    double Now() const;

    Mode mode = Mode::Off;
    net::UdpSocket socket;
    std::string code, status;
    std::vector<std::string> otherCodes;
    int myId = -1;
    // Cliente
    net::Address hostAddr;
    uint16_t hostPort = net::kDefaultPort;
    double joinStarted = 0.0, lastHelloSent = -1.0, lastFromHost = 0.0;
    int helloCount = 0;
    std::string joinName;
    // Anfitrión
    Peer peers[kMaxPlayers];
    // Ambos
    Remote remotes[kMaxPlayers];
    uint16_t seq = 0;
    float sendTimer = 0.0f, pingTimer = 0.0f, rosterTimer = 0.0f;
    float rtt = 0.0f;
    std::string localMap, hostMap, localBike;
    std::string knownBike[kMaxPlayers];          // la moto de cada jugador según la última noticia
    std::vector<std::pair<int, float>> pendingPops;
    struct Delayed {                             // --net-lag: paquete esperando su hora
        double at;
        net::Address from;
        std::vector<uint8_t> data;
    };
    std::deque<Delayed> inbox;
    uint32_t jitterSeed = 12345u;
    std::vector<std::string> modelPaths;
    bool graphics = false;
};
