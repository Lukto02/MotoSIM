#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>

#include "Multiplayer.h"

#include "MathUtil.h"
#include "PhysicsWorld.h"

#include <algorithm>
#include <chrono>
#include <cmath>

using JPH::Mat44;
using JPH::Quat;
using JPH::Vec3;

namespace {

// Paquete: 'M' 'X' versión tipo, y después lo de cada tipo (little endian).
constexpr uint8_t kMagic0 = 'M', kMagic1 = 'X', kProtocol = 7;   // v0.2: + ruedas (huellas y polvo); v0.3.1: + pata afuera y whip; v0.2.1 (tras la v0.3.1): + mapa (favela); 5 (v0.2.3): + cuerpo de costado; 6: mapa por nombre (mods); 7: la moto de cada uno
enum Type : uint8_t { HELLO = 1, WELCOME, REJECT, STATE, ROSTER, PING, PONG, BYE };
enum Reject : uint8_t { kFull = 1, kVersion = 2 };

constexpr float kSendRate = 60.0f;         // estados por segundo
constexpr double kTimeout = 5.0;           // s sin noticias: se fue
constexpr double kJoinTimeout = 12.0;
constexpr float kMaxExtrapolation = 0.25f; // más allá, la moto se queda donde estaba (se cortó la red)
constexpr float kSmoothTime = 0.1f;        // las correcciones se reparten en este tiempo
constexpr float kTeleport = 4.0f;          // m: una corrección más grande es un salto (reapareció)

void PutVec(net::Writer& w, Vec3 v)
{
    w.Put(v.GetX());
    w.Put(v.GetY());
    w.Put(v.GetZ());
}
Vec3 GetVec(net::Reader& r)
{
    const float x = r.Get<float>(), y = r.Get<float>(), z = r.Get<float>();
    return Vec3(x, y, z);
}
void PutQuat(net::Writer& w, Quat q)
{
    w.Put(q.GetX());
    w.Put(q.GetY());
    w.Put(q.GetZ());
    w.Put(q.GetW());
}
Quat GetQuat(net::Reader& r)
{
    const float x = r.Get<float>(), y = r.Get<float>(), z = r.Get<float>(), w = r.Get<float>();
    const Quat q(x, y, z, w);
    return q.LengthSq() > 1e-6f ? q.Normalized() : Quat::sIdentity();
}

void WriteState(net::Writer& w, const BikeState& s)
{
    PutVec(w, s.pos);
    PutQuat(w, s.rot);
    PutVec(w, s.vel);
    PutVec(w, s.angVel);
    for (int i = 0; i < 2; ++i) {
        w.Put(s.susp[i]);
        w.Put(s.wheelAngle[i]);
        w.Put(s.wheelOmega[i]);
        w.Put(s.contactX[i]);
        w.Put(s.contactZ[i]);
        w.Put(s.load[i]);
        w.Put(s.markSlip[i]);
        w.Put(s.spin[i]);
        w.Put(s.latVel[i]);
    }
    w.Put(s.steer);
    w.Put(s.lean);
    w.Put(s.gripThrottle);
    w.Put(s.legOut);
    w.Put(s.bodyTilt);
    w.Put(s.riderSide);
    w.Put(s.rpm);
    w.Put(s.throttle);
    w.Put(s.gear);
    w.Put(s.flags);
    w.Put(s.pops);
    w.Put(s.popStrength);
    w.Put(s.laps);
    w.Put(s.bestLap);
    if (!(s.flags & BikeState::kRiderOn))
        for (const Mat44& m : s.parts) {
            PutVec(w, m.GetTranslation());
            PutQuat(w, m.GetQuaternion());
        }
}

BikeState ReadState(net::Reader& r)
{
    BikeState s;
    s.pos = GetVec(r);
    s.rot = GetQuat(r);
    s.vel = GetVec(r);
    s.angVel = GetVec(r);
    for (int i = 0; i < 2; ++i) {
        s.susp[i] = r.Get<float>();
        s.wheelAngle[i] = r.Get<float>();
        s.wheelOmega[i] = r.Get<float>();
        s.contactX[i] = r.Get<float>();
        s.contactZ[i] = r.Get<float>();
        s.load[i] = r.Get<float>();
        s.markSlip[i] = r.Get<float>();
        s.spin[i] = r.Get<float>();
        s.latVel[i] = r.Get<float>();
    }
    s.steer = r.Get<float>();
    s.lean = r.Get<float>();
    s.gripThrottle = r.Get<float>();
    s.legOut = r.Get<float>();
    s.bodyTilt = r.Get<float>();
    s.riderSide = r.Get<float>();
    s.rpm = r.Get<float>();
    s.throttle = r.Get<float>();
    s.gear = r.Get<uint8_t>();
    s.flags = r.Get<uint8_t>();
    s.pops = r.Get<uint8_t>();
    s.popStrength = r.Get<float>();
    s.laps = r.Get<uint16_t>();
    s.bestLap = r.Get<float>();
    if (!(s.flags & BikeState::kRiderOn))
        for (Mat44& m : s.parts) {
            const Vec3 p = GetVec(r);
            m = Mat44::sRotationTranslation(GetQuat(r), p);
        }
    return s;
}

// Nombre prolijo: sin caracteres de control y con 16 bytes como mucho (sin cortar una letra UTF-8).
std::string CleanName(const std::string& s)
{
    std::string out;
    for (char c : s)
        if ((unsigned char)c >= 32) out += c;
    size_t n = std::min<size_t>(out.size(), 16);
    while (n > 0 && n < out.size() && ((unsigned char)out[n] & 0xC0) == 0x80) --n;
    out.resize(n);
    return out.empty() ? std::string("Piloto") : out;
}

} // namespace

// ------------------------------------------------------------------------------------ sesión
Multiplayer::~Multiplayer() = default;

double Multiplayer::Now() const
{
    using namespace std::chrono;
    static const steady_clock::time_point start = steady_clock::now();
    return duration<double>(steady_clock::now() - start).count();
}

void Multiplayer::SetRiderModel(const std::vector<std::string>& paths, bool withGraphics)
{
    modelPaths = paths;
    graphics = withGraphics;
}

net::Writer Multiplayer::Packet(uint8_t type) const
{
    net::Writer w;
    w.Put(kMagic0);
    w.Put(kMagic1);
    w.Put(kProtocol);
    w.Put(type);
    return w;
}

void Multiplayer::SendTo(const net::Address& to, const net::Writer& w) { socket.Send(to, w.data.data(), (int)w.data.size()); }

void Multiplayer::Broadcast(const net::Writer& w, int exceptId)
{
    for (int id = 1; id < kMaxPlayers; ++id)
        if (peers[id].active && id != exceptId) SendTo(peers[id].addr, w);
}

bool Multiplayer::Host(const std::string& name, uint16_t port)
{
    (void)name;
    if (!socket.Open(port)) {
        status = "No se pudo abrir el puerto " + std::to_string(port) + " (¿ya hay una partida abierta en esta PC?)";
        return false;
    }
    mode = Mode::Host;
    myId = 0;
    for (Peer& p : peers) p = Peer{};
    for (std::string& b : knownBike) b.clear();
    const std::vector<uint32_t> ips = net::LocalIPv4s();
    code = net::EncodeInvite(ips.empty() ? 0x7F000001u : ips[0], port);
    otherCodes.clear();
    for (size_t i = 1; i < ips.size(); ++i) otherCodes.push_back(net::EncodeInvite(ips[i], port) + "  (" + net::IpToString(ips[i]) + ")");
    status = ips.empty() ? "Sin red local: sólo se puede jugar en esta PC" : "Esperando jugadores en " + net::IpToString(ips[0]);
    sendTimer = pingTimer = rosterTimer = 0.0f;
    return true;
}

bool Multiplayer::Join(const std::string& inviteCode, const std::string& name)
{
    uint32_t ip = 0;
    uint16_t port = 0;
    if (!net::DecodeInvite(inviteCode, ip, port)) {
        status = "Código inválido: revisá las letras";
        return false;
    }
    if (!socket.Open(0)) {
        status = "No se pudo abrir la red";
        return false;
    }
    mode = Mode::Client;
    myId = -1;
    hostAddr = {ip, port};
    hostPort = port;
    code = net::EncodeInvite(ip, port);
    otherCodes.clear();
    joinName = name;
    joinStarted = lastFromHost = Now();
    lastHelloSent = -1.0;
    helloCount = 0;
    rtt = 0.0f;
    sendTimer = pingTimer = rosterTimer = 0.0f;
    status = "Conectando a " + net::IpToString(ip) + "...";
    return true;
}

void Multiplayer::Leave(PhysicsWorld& physics)
{
    if (mode != Mode::Off) {
        net::Writer w = Packet(BYE);
        w.Put<uint8_t>((uint8_t)LocalId());
        for (int k = 0; k < 2; ++k) {                // por las dudas se pierda uno
            if (mode == Mode::Host) Broadcast(w, -1);
            else if (myId >= 0) SendTo(hostAddr, w);
        }
    }
    socket.Close();
    for (int id = 0; id < kMaxPlayers; ++id) RemoveRemote(id, physics);
    for (Peer& p : peers) p = Peer{};
    for (std::string& b : knownBike) b.clear();
    mode = Mode::Off;
    myId = -1;
    code.clear();
    otherCodes.clear();
    pendingPops.clear();
}

// ------------------------------------------------------------------------------------ jugadores
void Multiplayer::ResetRemotes(PhysicsWorld& physics)
{
    for (int id = 0; id < kMaxPlayers; ++id) {
        const std::string name = remotes[id].name;
        const bool was = remotes[id].active;
        RemoveRemote(id, physics);
        remotes[id].name = was ? name : std::string();
    }
}

void Multiplayer::EnsureRemote(int id, PhysicsWorld& physics, BikeParams& params)
{
    Remote& r = remotes[id];
    if (r.active) return;
    r.active = true;
    r.id = id;
    r.hasState = false;
    r.teleport = true;
    // Con su moto (si este juego la tiene; si no, con la propia).
    BikeParams* own = bikeParams && !knownBike[id].empty() ? bikeParams(knownBike[id]) : nullptr;
    r.bike.InitVisual(own ? *own : params);
    r.bikeId = knownBike[id];
    // Cuerpo cinemático con la forma de la moto: la propia choca contra él (capa REMOTE).
    JPH::BodyCreationSettings bcs(r.bike.CollisionShape(true), Vec3(0.0f, -200.0f, 0.0f), Quat::sIdentity(), JPH::EMotionType::Kinematic,
                                  Layers::REMOTE);
    bcs.mUserData = (JPH::uint64)id;
    bcs.mCollisionGroup = JPH::CollisionGroup(nullptr, kRemoteCollisionGroup, (JPH::CollisionGroup::SubGroupID)id);
    bcs.mFriction = 0.3f;
    bcs.mRestitution = 0.0f;
    bcs.mAllowSleeping = false;
    r.proxy = physics.Bodies().CreateBody(bcs);
    if (r.proxy) physics.Bodies().AddBody(r.proxy->GetID(), JPH::EActivation::Activate);
    r.proxyWithRider = true;
    if (graphics && !modelPaths.empty()) {
        r.rider = std::make_unique<RiderModel>();
        if (!r.rider->Load(modelPaths)) r.rider.reset();
    }
}

void Multiplayer::RemoveRemote(int id, PhysicsWorld& physics)
{
    Remote& r = remotes[id];
    if (!r.active) return;
    if (r.proxy) {
        physics.Bodies().RemoveBody(r.proxy->GetID());
        physics.Bodies().DestroyBody(r.proxy->GetID());
    }
    if (r.rider) r.rider->Unload();
    r = Remote{};
}

Vec3 Multiplayer::Remote::DisplayPos() const
{
    const float a = std::min(age, kMaxExtrapolation);
    return state.pos + state.vel * a + posError;
}

Quat Multiplayer::Remote::DisplayRot() const
{
    const float a = std::min(age, kMaxExtrapolation), w = state.angVel.Length();
    Quat q = state.rot;
    if (w > 1e-4f) q = (Quat::sRotation(state.angVel / w, w * a) * state.rot).Normalized();
    return (rotError * q).Normalized();
}

void Multiplayer::ReceiveState(int id, net::Reader& r, float latency, PhysicsWorld& physics, BikeParams& params)
{
    const uint16_t s = r.Get<uint16_t>();
    const BikeState st = ReadState(r);
    if (!r.ok || id < 0 || id >= kMaxPlayers || id == myId) return;
    EnsureRemote(id, physics, params);
    Remote& rm = remotes[id];
    rm.lastHeard = Now();
    if (rm.hasState && (int16_t)(uint16_t)(s - rm.lastSeq) <= 0) return;   // vieja o repetida
    // Petardeos nuevos desde la foto anterior.
    if (rm.hasState) {
        const int fresh = std::min(4, (int)(uint8_t)(st.pops - rm.state.pops));
        for (int k = 0; k < fresh; ++k) pendingPops.push_back({id, st.popStrength});
    }
    // La pose que se estaba mostrando pasa a ser una corrección que se desvanece: sin saltos.
    const bool had = rm.hasState;
    const Vec3 oldPos = rm.DisplayPos();
    const Quat oldRot = rm.DisplayRot();
    ++statesReceived;
    rm.state = st;
    rm.lastSeq = s;
    rm.hasState = true;
    rm.age = std::min(latency, 0.2f);
    rm.posError = Vec3::sZero();
    rm.rotError = Quat::sIdentity();
    if (had) {
        rm.posError = oldPos - rm.DisplayPos();
        correctionAvg = correctionAvg * 0.95f + rm.posError.Length() * 0.05f;
        rm.rotError = (oldRot * rm.DisplayRot().Conjugated()).Normalized();
        if (rm.posError.Length() > kTeleport) {
            rm.posError = Vec3::sZero();
            rm.rotError = Quat::sIdentity();
            rm.teleport = true;
        }
    }
}

// ------------------------------------------------------------------------------------ paquetes
void Multiplayer::HandlePacket(const net::Address& from, const uint8_t* data, int size, PhysicsWorld& physics, BikeParams& params)
{
    if (size < 4 || data[0] != kMagic0 || data[1] != kMagic1) return;
    const uint8_t type = data[3];
    const double now = Now();
    if (data[2] != kProtocol) {
        if (mode == Mode::Host && type == HELLO) {
            net::Writer w = Packet(REJECT);
            w.Put<uint8_t>(kVersion);
            SendTo(from, w);
        }
        return;
    }
    net::Reader r(data + 4, size - 4);

    if (mode == Mode::Host) {
        int id = -1;
        for (int k = 1; k < kMaxPlayers; ++k)
            if (peers[k].active && peers[k].addr == from) id = k;
        switch (type) {
        case HELLO: {
            r.GetString();                               // el código que escribió (informativo)
            const std::string name = CleanName(r.GetString());
            const std::string bike = r.GetString();
            if (id >= 0 && (peers[id].bike != bike || peers[id].name != name)) {   // cambió de moto (o de nombre)
                peers[id].bike = bike;
                peers[id].name = name;
                knownBike[id] = bike;
                rosterTimer = 1.0f;
            }
            if (id < 0) {
                for (int k = 1; k < kMaxPlayers && id < 0; ++k)
                    if (!peers[k].active) id = k;
                if (id < 0) {
                    net::Writer w = Packet(REJECT);
                    w.Put<uint8_t>(kFull);
                    SendTo(from, w);
                    return;
                }
                peers[id] = Peer{true, from, name, bike, now, 0.0f};
                knownBike[id] = bike;
                status = name + " se unió a la partida";
                rosterTimer = 1.0f;                      // la lista nueva sale ya
            }
            net::Writer w = Packet(WELCOME);             // (se repite si el primero se perdió)
            w.Put<uint8_t>((uint8_t)id);
            w.PutString(localMap, 96);
            SendTo(from, w);
            break;
        }
        case STATE: {
            if (id < 0) return;
            peers[id].lastHeard = now;
            for (int k = 1; k < kMaxPlayers; ++k)        // se reenvía tal cual a los demás
                if (peers[k].active && k != id) socket.Send(peers[k].addr, data, size);
            r.Get<uint8_t>();
            ReceiveState(id, r, peers[id].rtt * 0.5f, physics, params);
            break;
        }
        case PING: {
            net::Writer w = Packet(PONG);
            w.Put(r.Get<double>());
            SendTo(from, w);
            if (id >= 0) peers[id].lastHeard = now;
            break;
        }
        case PONG: {
            const double t = r.Get<double>();
            if (id >= 0 && r.ok) {
                const float sample = (float)(now - t);
                peers[id].rtt = peers[id].rtt <= 0.0f ? sample : peers[id].rtt * 0.8f + sample * 0.2f;
                peers[id].lastHeard = now;
                if (remotes[id].active) remotes[id].ping = (int)(peers[id].rtt * 1000.0f + 0.5f);
            }
            break;
        }
        case BYE:
            if (id >= 0) {
                status = peers[id].name + " salió de la partida";
                peers[id] = Peer{};
                RemoveRemote(id, physics);
                rosterTimer = 1.0f;
            }
            break;
        default: break;
        }
        return;
    }

    // Cliente
    if (type == WELCOME) {
        if (myId < 0) {
            const int id = r.Get<uint8_t>();
            const std::string map = r.GetString();
            if (!r.ok || id <= 0 || id >= kMaxPlayers) return;
            myId = id;
            hostMap = map;
            hostAddr = from;                             // puede haber contestado desde otra IP (broadcast)
            lastFromHost = now;
            status = "Conectado";
        }
        return;
    }
    if (type == REJECT) {
        const int reason = r.Get<uint8_t>();
        Leave(physics);
        status = reason == kFull ? "La partida está llena (4 jugadores)" : "El anfitrión tiene otra versión del juego";
        return;
    }
    if (myId < 0 || from != hostAddr) return;
    lastFromHost = now;
    switch (type) {
    case STATE: {
        const int id = r.Get<uint8_t>();
        // Del anfitrión: media ida y vuelta; de otro cliente (reenviado): una entera, más o menos.
        ReceiveState(id, r, id == 0 ? rtt * 0.5f : rtt, physics, params);
        break;
    }
    case ROSTER: {
        const std::string map = r.GetString();
        if (r.ok) hostMap = map;
        const int count = r.Get<uint8_t>();
        bool listed[kMaxPlayers] = {};
        for (int k = 0; k < count && r.ok; ++k) {
            const int id = r.Get<uint8_t>();
            const int ping = r.Get<uint16_t>();
            const std::string name = r.GetString();
            const std::string bike = r.GetString();
            if (!r.ok || id < 0 || id >= kMaxPlayers) break;
            listed[id] = true;
            if (id == myId) continue;
            knownBike[id] = bike;
            EnsureRemote(id, physics, params);
            remotes[id].name = name;
            remotes[id].ping = id == 0 ? (int)(rtt * 1000.0f + 0.5f) : ping;
        }
        if (r.ok)
            for (int id = 0; id < kMaxPlayers; ++id)
                if (!listed[id] && id != myId) RemoveRemote(id, physics);
        break;
    }
    case PING: {
        net::Writer w = Packet(PONG);
        w.Put(r.Get<double>());
        SendTo(from, w);
        break;
    }
    case PONG: {
        const double t = r.Get<double>();
        if (r.ok) {
            const float sample = (float)(now - t);
            rtt = rtt <= 0.0f ? sample : rtt * 0.8f + sample * 0.2f;
        }
        break;
    }
    case BYE:
        Leave(physics);
        status = "El anfitrión cerró la partida";
        break;
    default: break;
    }
}

// ------------------------------------------------------------------------------------ por paso
void Multiplayer::Step(float dt, const BikeState& local, const std::string& localName, PhysicsWorld& physics, BikeParams& params)
{
    if (mode == Mode::Off) return;
    const double now = Now();
    uint8_t buf[1500];
    net::Address from;
    for (int n; mode != Mode::Off && (n = socket.Receive(from, buf, (int)sizeof(buf))) >= 0;) HandlePacket(from, buf, n, physics, params);
    if (mode == Mode::Off) return;

    if (mode == Mode::Client) {
        if (myId < 0) {
            // Pidiendo entrar: a la IP del código y, por si esa no es la que se ve desde acá, a toda la red.
            if (now - lastHelloSent > 0.4) {
                net::Writer w = Packet(HELLO);
                w.PutString(code, 16);
                w.PutString(joinName, 16);
                w.PutString(localBike, 96);
                SendTo(hostAddr, w);
                if (helloCount % 2 == 1) SendTo({net::kBroadcast, hostPort}, w);
                lastHelloSent = now;
                ++helloCount;
            }
            if (now - joinStarted > kJoinTimeout) {
                Leave(physics);
                status = "No se encontró la partida: ¿misma red? ¿el firewall la deja pasar?";
            }
            return;
        }
        if (now - lastFromHost > kTimeout) {
            Leave(physics);
            status = "Se perdió la conexión con el anfitrión";
            return;
        }
    } else {
        for (int id = 1; id < kMaxPlayers; ++id)
            if (peers[id].active && now - peers[id].lastHeard > kTimeout) {
                status = peers[id].name + " se desconectó";
                peers[id] = Peer{};
                RemoveRemote(id, physics);
                rosterTimer = 1.0f;
            }
    }

    // Estado propio, 60 veces por segundo.
    sendTimer += dt;
    if (sendTimer >= 1.0f / kSendRate) {
        sendTimer = std::min(sendTimer - 1.0f / kSendRate, 1.0f / kSendRate);
        net::Writer w = Packet(STATE);
        w.Put<uint8_t>((uint8_t)myId);
        w.Put<uint16_t>(++seq);
        WriteState(w, local);
        if (mode == Mode::Host) Broadcast(w, -1);
        else SendTo(hostAddr, w);
    }
    // Ping (latencia) y, el anfitrión, la lista de jugadores.
    pingTimer += dt;
    if (pingTimer >= 0.5f) {
        pingTimer = 0.0f;
        net::Writer w = Packet(PING);
        w.Put<double>(now);
        if (mode == Mode::Host) Broadcast(w, -1);
        else SendTo(hostAddr, w);
    }
    if (mode == Mode::Host) {
        rosterTimer += dt;
        if (rosterTimer >= 1.0f) {
            rosterTimer = 0.0f;
            net::Writer w = Packet(ROSTER);
            w.PutString(localMap, 96);
            int count = 1;
            for (int id = 1; id < kMaxPlayers; ++id) count += peers[id].active ? 1 : 0;
            w.Put<uint8_t>((uint8_t)count);
            w.Put<uint8_t>(0);
            w.Put<uint16_t>(0);
            w.PutString(localName, 16);
            w.PutString(localBike, 96);
            for (int id = 1; id < kMaxPlayers; ++id)
                if (peers[id].active) {
                    w.Put<uint8_t>((uint8_t)id);
                    w.Put<uint16_t>((uint16_t)std::min(65535.0f, peers[id].rtt * 1000.0f + 0.5f));
                    w.PutString(peers[id].name, 16);
                    w.PutString(peers[id].bike, 96);
                    if (remotes[id].active) remotes[id].name = peers[id].name;
                }
            Broadcast(w, -1);
        }
    }

    // El que cambió de moto se vuelve a armar con la nueva (la próxima foto lo ubica).
    for (int id = 0; id < kMaxPlayers; ++id)
        if (remotes[id].active && remotes[id].bikeId != knownBike[id]) {
            const std::string name = remotes[id].name;
            const int ping = remotes[id].ping;
            RemoveRemote(id, physics);
            EnsureRemote(id, physics, params);
            remotes[id].name = name;
            remotes[id].ping = ping;
        }

    // Motos remotas: la foto envejece, la corrección se desvanece y el cuerpo cinemático va a donde
    // estará la moto al terminar este paso (así empuja con la velocidad justa).
    const float fade = std::exp(-dt / kSmoothTime);
    for (Remote& r : remotes) {
        if (!r.active || !r.hasState) continue;
        r.age += dt;
        r.posError = r.posError * fade;
        r.rotError = Quat::sIdentity().SLERP(r.rotError, fade).Normalized();
        if (!r.proxy) continue;
        const JPH::BodyID body = r.proxy->GetID();
        const bool withRider = (r.state.flags & BikeState::kRiderOn) != 0;
        if (withRider != r.proxyWithRider) {
            physics.Bodies().SetShape(body, r.bike.CollisionShape(withRider), false, JPH::EActivation::Activate);
            r.proxyWithRider = withRider;
        }
        if (r.teleport) {
            physics.Bodies().SetPositionAndRotation(body, r.DisplayPos(), r.DisplayRot(), JPH::EActivation::Activate);
            physics.Bodies().SetLinearAndAngularVelocity(body, Vec3::sZero(), Vec3::sZero());
            r.teleport = false;
        } else {
            physics.Bodies().MoveKinematic(body, r.DisplayPos(), r.DisplayRot(), dt);
        }
    }
}

// ------------------------------------------------------------------------------------ dibujo
void Multiplayer::UpdateVisuals(float frameDt)
{
    const float follow = 1.0f - std::exp(-frameDt / 0.05f);
    for (Remote& r : remotes) {
        if (!r.active || !r.hasState) continue;
        const bool riderOn = (r.state.flags & BikeState::kRiderOn) != 0;
        const float a = std::min(r.age, kMaxExtrapolation);
        r.bike.SetVisualState(r.DisplayPos(), r.DisplayRot(), riderOn);
        for (int i = 0; i < 2; ++i) {
            r.bike.wheels[i].extension = r.state.susp[i];
            r.bike.wheels[i].angle = r.state.wheelAngle[i] + r.state.wheelOmega[i] * a;
        }
        r.bike.steerAngle = r.state.steer;
        r.bike.riderLean = r.state.lean;
        r.bike.gripThrottle = r.state.gripThrottle;
        r.bike.legOut = r.state.legOut;
        r.bike.bodyTilt = r.state.bodyTilt;
        r.bike.riderSide = r.state.riderSide;
        const bool onGround = (r.state.flags & (BikeState::kFrontGround | BikeState::kRearGround)) != 0;
        r.bike.supported += ((onGround ? 1.0f : 0.0f) - r.bike.supported) * std::min(1.0f, frameDt * 6.0f);
        if (riderOn) {
            r.lastPose = r.bike.RiderPoseLocal();
            r.riderBound = false;
        } else {
            for (int i = 0; i < RiderPart::Count; ++i) {
                if (!r.riderBound) {
                    r.parts[i] = r.state.parts[i];
                    continue;
                }
                const Vec3 p = r.parts[i].GetTranslation() + (r.state.parts[i].GetTranslation() - r.parts[i].GetTranslation()) * follow;
                const Quat q = r.parts[i].GetQuaternion().SLERP(r.state.parts[i].GetQuaternion(), follow).Normalized();
                r.parts[i] = Mat44::sRotationTranslation(q, p);
            }
        }
        if (r.rider) {
            if (riderOn) {
                r.rider->PoseOnBike(r.lastPose);
            } else {
                if (!r.riderBound) {
                    Mat44 locals[RiderPart::Count];
                    RiderPartLocals(r.rider->JointPose(r.lastPose), locals);   // como lo armó su PC (Game)
                    r.rider->BindToParts(r.lastPose, locals);
                }
                r.rider->PoseFromParts(r.parts);
            }
            r.rider->Skin();
        }
        if (!riderOn) r.riderBound = true;
    }
}

std::vector<std::pair<int, float>> Multiplayer::TakePops()
{
    std::vector<std::pair<int, float>> out;
    out.swap(pendingPops);
    return out;
}

std::vector<Multiplayer::Entry> Multiplayer::Roster(const std::string& localName) const
{
    std::vector<Entry> out;
    out.push_back({LocalId(), localName, -1});
    for (const Remote& r : remotes)
        if (r.active) out.push_back({r.id, r.name.empty() ? std::string("...") : r.name, r.ping});
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.id < b.id; });
    return out;
}

void Multiplayer::SetLocalBike(const std::string& bike)
{
    if (bike == localBike) return;
    localBike = bike;
    rosterTimer = 1.0f;                                  // anfitrión: la lista con la moto nueva sale ya
    if (mode == Mode::Client && myId >= 0) {             // cliente ya adentro: se lo avisa al anfitrión
        net::Writer w = Packet(HELLO);
        w.PutString(code, 16);
        w.PutString(joinName, 16);
        w.PutString(localBike, 96);
        SendTo(hostAddr, w);
    }
}
