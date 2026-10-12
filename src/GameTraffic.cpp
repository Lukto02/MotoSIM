// Game: la autopista y su tráfico (ver Traffic.h y docs/MAPAS.md, "Autopista"). Desde Game.cpp sólo hay ganchos:
// TrafficStep antes de la física, Traffic::BikeHit después, BotLane en el bot y los dibujos en DrawScene.
#include "Game.h"

#include "MathUtil.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

using JPH::Vec3;

void Game::TrafficStep()
{
    // Reloj: el anfitrión manda el suyo (al final de la lista de jugadores) y el invitado lo sigue: así en las dos PCs
    // los autos lejos de las motos están en el mismo lugar.
    if (mp.GetMode() == Multiplayer::Mode::Host) mp.SetSharedClock(traffic.Clock());
    double host = 0.0;
    if (mp.HostClock(host)) traffic.SyncClock(host);

    // Las motos que miran los autos: la propia, su piloto (caído, puede estar lejos de ella) y las de los demás.
    Vec3 pos[2 + Multiplayer::kMaxPlayers], vel[2 + Multiplayer::kMaxPlayers];
    int n = 0;
    pos[n] = bike.Position();
    vel[n++] = bike.Velocity();
    pos[n] = ragdoll.Active() ? ragdoll.Position(1.0f) : bike.Position();
    vel[n++] = ragdoll.Active() ? ragdoll.Velocity() : bike.Velocity();
    for (int id = 0; id < Multiplayer::kMaxPlayers; ++id) {
        const Multiplayer::Remote& r = mp.Remotes()[id];
        if (!r.active || !r.hasState) continue;
        pos[n] = r.DisplayPos();
        vel[n++] = r.state.vel;
    }
    traffic.log = opt.telemetry;
    const auto t0 = std::chrono::steady_clock::now();
    traffic.Step(kDt, *physics, pos, vel, n);
    static float worstMs = 0.0f;
    worstMs = std::max(worstMs, std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count());
    if (opt.telemetry && simTime - trafficLog >= 5.0f - 1e-4f) {
        trafficLog = simTime;
        std::printf("TRAFICO t=%.1f reloj=%.1f autos=%d frenan_por_moto=%d atrasados=%d cuerpos=%d cambios_de_carril=%d golpes=%d paso_max=%.2f ms\n",
                    simTime, traffic.Clock(), traffic.Count(), traffic.reacting, traffic.lagging, traffic.bodiesUsed, traffic.laneChanges, traffic.hits,
                    worstMs);
        worstMs = 0.0f;
    }
}

void Game::BotLane(float s, float v, float& lat, float& speed)
{
    lat = highway.MiddleLane();
    speed = 1e9f;
    // Dónde va ahora (lateral desde la línea central, + a la derecha): al aparecer, en la banquina.
    const Vec3 p = bike.Position();
    const TrackPoint t = track.At(s);
    const float now = (p.GetX() - t.x) * -t.tz + (p.GetZ() - t.z) * t.tx;
    if (traffic.Active()) traffic.BotAdvice(s, v, now, lat, speed);
    // Siguiendo un punto adelante (el bot de siempre), en una curva la moto se abre hasta que el ángulo al punto alcanza
    // para doblar: a 200 km/h en R 600, 3-7 m (sobre el separador o el guardarraíl). El punto se corre hacia adentro de la
    // curva que viene 0.8 m por m/s² de aceleración de costado: con 162 km/h el error medio bajó de 1.3 a 0.5 m (ver
    // docs/MAPAS.md, "Autopista"). Una integral del error lo empeoraba (la moto zigzagueaba más).
    const float kAhead = track.At(s + 4.0f + v * 0.55f).curvature;
    lat += mu::Clamp(kAhead * v * v * 0.8f, -6.0f, 6.0f);
}
