// Tráfico de la autopista (ver Traffic.h): la base determinista (todos los autos de la vuelta, paso fijo de 0.1 s), la
// capa local que reacciona a las motos, los cuerpos cinemáticos cerca de la moto propia y la malla de los autos.
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>

#include "Traffic.h"

#include "Json.h"
#include "MathUtil.h"
#include "PhysicsWorld.h"
#include "Render.h"
#include "Terrain.h"
#include "Track.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using JPH::Vec3;
using JPH::Quat;

namespace {

constexpr double kBaseDt = 0.1;            // paso de la base
constexpr int kWarmupSteps = 600;          // 60 s antes del reloj 0: los autos ya andan acomodados
constexpr int kBodies = 36;                // cuerpos cinemáticos (los autos más cerca de la moto propia)
constexpr float kBodyRadius = 110.0f;      // m: hasta dónde tienen cuerpo
constexpr float kShowRadius = 800.0f;      // m: hasta dónde se dibujan (la niebla los tapa antes)
constexpr float kNearRadius = 110.0f;      // m: con ruedas, paragolpes y espejos
constexpr float kShadowRadius = 70.0f;     // m: los que van en la pasada de sombras
constexpr float kLocalRadius = 450.0f;     // m a lo largo: los autos que miran a una moto
constexpr float kHitSpeed = 4.5f;          // m/s de diferencia en el punto de contacto: tira al piloto
constexpr float kHitNormal = 3.0f;         // m/s en la normal (un empujón de frente o de atrás)
constexpr float kS0 = 2.5f, kComfort = 2.2f, kMaxBrake = 8.0f;   // IDM: distancia parado, frenada cómoda y tope
constexpr float kBotMargin = 1.0f;         // bot: medio ancho de su franja más el error con que sigue el lateral (m)
constexpr float kBotFollow = 25.0f;        // bot: a cuántos m se queda detrás del de adelante
constexpr float kGlow = 7.0f;              // v de la UV: el shader lo dibuja sin sombrear (luces encendidas, ver kLitFS)

uint32_t Next(uint32_t& s)
{
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}
float Rand01(uint32_t& s) { return (float)(Next(s) & 0xffffff) / (float)0xffffff; }

float WrapPos(float p, float L)
{
    while (p >= L) p -= L;
    while (p < 0.0f) p += L;
    return p;
}
// Distancia de a hacia adelante hasta b (0..L).
float Ahead(float a, float b, float L) { return WrapPos(b - a, L); }

// Colores de los autos (pintura): blancos, grises y negros son los más.
const Color kPaints[] = {{236, 236, 232, 255}, {232, 232, 228, 255}, {172, 176, 180, 255}, {150, 154, 158, 255}, {96, 100, 106, 255},
                         {30, 30, 34, 255},    {24, 26, 30, 255},    {168, 26, 28, 255},   {32, 52, 112, 255},   {64, 112, 168, 255},
                         {198, 184, 152, 255}, {44, 82, 62, 255},    {112, 26, 40, 255},   {222, 112, 32, 255},  {228, 192, 44, 255},
                         {236, 236, 232, 255}, {172, 176, 180, 255}, {96, 100, 106, 255}};
constexpr int kPaintCount = sizeof(kPaints) / sizeof(kPaints[0]);

// ------------------------------------------------------------------ armado de las piezas (triángulos sueltos)
void Tri(std::vector<float>& P, std::vector<float>& N, std::vector<float>& U, std::vector<unsigned char>& C, Vector3 a, Vector3 b, Vector3 c,
         Vector3 n, Color col, float gloss, float mode)
{
    const Vector3 f = Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a));
    if (Vector3DotProduct(f, n) < 0.0f) std::swap(b, c);
    for (const Vector3& p : {a, b, c}) {
        P.insert(P.end(), {p.x, p.y, p.z});
        N.insert(N.end(), {n.x, n.y, n.z});
        U.insert(U.end(), {gloss, mode});
        C.insert(C.end(), {col.r, col.g, col.b, col.a});
    }
}

// Un constructor de piezas: cuadriláteros y cajas en el espacio del auto (+Z adelante, +X a la izquierda, +Y arriba,
// el origen en el piso entre los ejes).
struct CarParts {
    std::vector<float> *P, *N, *U;
    std::vector<unsigned char>* C;
    void Use(std::vector<float>& p, std::vector<float>& n, std::vector<float>& u, std::vector<unsigned char>& c)
    {
        P = &p;
        N = &n;
        U = &u;
        C = &c;
    }
    void Quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Vector3 out, Color col, float gloss, float mode = 2.0f)
    {
        Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(c, a), Vector3Subtract(d, b)));
        if (Vector3DotProduct(n, out) < 0.0f) n = Vector3Negate(n);
        Tri(*P, *N, *U, *C, a, b, c, n, col, gloss, mode);
        Tri(*P, *N, *U, *C, a, c, d, n, col, gloss, mode);
    }
    // Hexaedro: abajo lo[0..3] y arriba hi[0..3] en el mismo orden. faces: bit 0..3 los costados (k a k+1), 4 arriba, 5 abajo.
    void Hexa(const Vector3 lo[4], const Vector3 hi[4], Color col, float gloss, int faces = 0x1F, const Color* sideCol = nullptr)
    {
        Vector3 c = {0, 0, 0};
        for (int k = 0; k < 4; ++k) c = Vector3Add(c, Vector3Add(lo[k], hi[k]));
        c = Vector3Scale(c, 0.125f);
        for (int k = 0; k < 4; ++k) {
            if (!(faces & (1 << k))) continue;
            const int n = (k + 1) % 4;
            const Vector3 out = Vector3Subtract(Vector3Scale(Vector3Add(Vector3Add(lo[k], lo[n]), Vector3Add(hi[k], hi[n])), 0.25f), c);
            Quad(lo[k], lo[n], hi[n], hi[k], out, sideCol ? *sideCol : col, gloss);
        }
        if (faces & 16) Quad(hi[0], hi[1], hi[2], hi[3], {0, 1, 0}, col, gloss);
        if (faces & 32) Quad(lo[0], lo[1], lo[2], lo[3], {0, -1, 0}, col, gloss);
    }
    void Box(Vector3 mn, Vector3 mx, Color col, float gloss, int faces = 0x1F)
    {
        const Vector3 lo[4] = {{mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mn.y, mx.z}, {mn.x, mn.y, mx.z}};
        const Vector3 hi[4] = {{mn.x, mx.y, mn.z}, {mx.x, mx.y, mn.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z}};
        Hexa(lo, hi, col, gloss, faces);
    }
    // Rueda: prisma de 8 caras a lo largo de X, centrado en c.
    void Wheel(Vector3 c, float r, float w)
    {
        const Color tire = {22, 22, 24, 255}, hub = {150, 152, 156, 255};
        const int n = 6;                                   // la tapa de adentro no se ve: sólo la de afuera
        const float out = c.x >= 0.0f ? 1.0f : -1.0f;
        for (int i = 0; i < n; ++i) {
            const float a0 = 6.2831853f * (float)i / n, a1 = 6.2831853f * (float)(i + 1) / n;
            const Vector3 p0 = {0, std::sin(a0) * r, std::cos(a0) * r}, p1 = {0, std::sin(a1) * r, std::cos(a1) * r};
            const Vector3 l0 = Vector3Add(c, {w * 0.5f, p0.y, p0.z}), l1 = Vector3Add(c, {w * 0.5f, p1.y, p1.z});
            const Vector3 r0 = Vector3Add(c, {-w * 0.5f, p0.y, p0.z}), r1 = Vector3Add(c, {-w * 0.5f, p1.y, p1.z});
            Quad(l0, l1, r1, r0, {0, 0.5f * (p0.y + p1.y), 0.5f * (p0.z + p1.z)}, tire, 0.05f);
            for (float sx : {out}) {
                const Vector3 cc = Vector3Add(c, {sx * w * 0.5f, 0, 0});
                const Vector3 a = sx > 0 ? l0 : r0, b = sx > 0 ? l1 : r1;
                Tri(*P, *N, *U, *C, cc, a, b, {sx, 0, 0}, i % 2 ? hub : Color{120, 122, 126, 255}, 0.5f, 2.0f);
            }
        }
    }
};

} // namespace

// ======================================================================================= armado
void Traffic::Build(const Track& t, const Terrain& g, const HighwayLayout& lay, const MapDef& def)
{
    track = &t;
    terrain = &g;
    layout = lay;
    L = t.Length();
    built = false;
    cars.clear();
    density = 10.0f;
    speedMin = 85.0f / 3.6f;
    speedMax = 135.0f / 3.6f;
    laneChangeEvery = 14.0f;
    truckShare = 0.12f;
    seed = 1;
    react = true;
    Json j;
    std::string err;
    if (Json::Load(def.file, j, err)) {
        const Json& tr = j["traffic"];
        density = std::clamp(tr.Num("density", density), 0.0f, 40.0f);
        const Json& sp = tr["speed"];
        if (sp.IsArray() && sp.Size() >= 2) {
            speedMin = std::clamp(sp[0].AsNum(85.0f), 20.0f, 250.0f) / 3.6f;
            speedMax = std::clamp(sp[1].AsNum(135.0f), 20.0f, 250.0f) / 3.6f;
            if (speedMax < speedMin) std::swap(speedMin, speedMax);
        }
        laneChangeEvery = std::clamp(tr.Num("lane_change", laneChangeEvery), 2.0f, 600.0f);
        truckShare = std::clamp(tr.Num("trucks", truckShare), 0.0f, 1.0f);
        seed = (uint32_t)std::max(0.0f, tr.Num("seed", (float)seed));
        react = tr.Bool("react", react);
    }

    // Tipos: auto mediano, chico, camioneta, pickup, utilitario y camión. Largo, ancho, alto, aceleración, velocidades.
    kinds = {{4.6f, 1.80f, 1.45f, 1.5f, 0, 0, nullptr}, {4.0f, 1.76f, 1.50f, 1.4f, 0, 0, nullptr},
             {4.7f, 1.88f, 1.74f, 1.4f, 0, 0, nullptr}, {5.3f, 1.88f, 1.82f, 1.3f, 0, 0, nullptr},
             {5.2f, 1.95f, 2.38f, 1.1f, 80.0f / 3.6f, 115.0f / 3.6f, nullptr}, {12.0f, 2.50f, 3.90f, 0.7f, 75.0f / 3.6f, 92.0f / 3.6f, nullptr}};
    for (Kind& k : kinds) {
        // Colisión: la caja de abajo (carrocería) y la de arriba (cabina), un poco más angosta.
        JPH::StaticCompoundShapeSettings sc;
        const bool truck = &k == &kinds[5];
        const float belt = truck ? 1.1f : 0.25f + 0.45f * k.height;
        sc.AddShape(Vec3(0.0f, 0.5f * (0.28f + belt), 0.0f), Quat::sIdentity(),
                    new JPH::BoxShapeSettings(Vec3(0.5f * k.width, 0.5f * (belt - 0.28f), 0.5f * k.length), 0.03f));
        sc.AddShape(Vec3(0.0f, 0.5f * (belt + k.height), truck ? -0.3f : -0.1f * k.length), Quat::sIdentity(),
                    new JPH::BoxShapeSettings(Vec3(0.5f * k.width - 0.08f, 0.5f * (k.height - belt), truck ? 0.5f * k.length - 0.3f : 0.3f * k.length),
                                              0.03f));
        JPH::ShapeSettings::ShapeResult r = sc.Create();
        k.shape = r.HasError() ? nullptr : r.Get();
    }

    const int lanes = layout.lanes;
    laneLists.assign((size_t)2 * lanes, {});
    if (density > 0.0f) {
        uint32_t rng = 0x9E3779B9u ^ (seed * 2654435761u);
        if (rng == 0) rng = 1;
        for (int d = 0; d < 2; ++d)
            for (int lane = 0; lane < lanes; ++lane) {
                const int n = std::max(1, (int)std::lround(density * L / 1000.0f));
                const float spacing = L / (float)n;
                // Velocidad deseada según el carril: el rápido arriba del rango, el de la derecha abajo.
                const float lo = lanes == 1 ? 0.0f : (float)(lanes - 1 - lane) / (float)lanes;
                const float hi = std::min(1.0f, lo + 2.0f / (float)lanes);
                for (int i = 0; i < n; ++i) {
                    Car c;
                    c.dir = d == 0 ? 1 : -1;
                    c.lane = (int8_t)lane;
                    c.rng = Next(rng) | 1u;
                    const float rk = Rand01(c.rng);
                    const bool truckLane = lane >= lanes - 2;
                    if (truckLane && rk < truckShare * (lanes >= 3 ? 1.6f : 1.0f)) c.kind = Rand01(c.rng) < 0.7f ? 5 : 4;
                    else {
                        const float q = Rand01(c.rng);
                        c.kind = q < 0.38f ? 0 : (q < 0.66f ? 1 : (q < 0.84f ? 2 : (q < 0.95f ? 3 : 4)));
                    }
                    c.color = (uint8_t)(Next(c.rng) % kPaintCount);
                    const Kind& k = kinds[c.kind];
                    const float u = lo + (hi - lo) * Rand01(c.rng);
                    c.v0 = k.vMax > 0.0f ? mu::Lerp(k.vMin, std::min(k.vMax, speedMax), Rand01(c.rng)) : mu::Lerp(speedMin, speedMax, u);
                    c.v = c.v0 * 0.9f;
                    c.accel = k.accel * (0.85f + 0.3f * Rand01(c.rng));
                    c.headway = 1.0f + 0.7f * Rand01(c.rng);
                    c.pos = WrapPos(((float)i + 0.3f * (Rand01(c.rng) - 0.5f)) * spacing + (float)lane * 17.0f, L);
                    c.nextThink = laneChangeEvery * Rand01(c.rng);
                    cars.push_back(c);
                }
            }
    }
    initial = cars;
    BaseReset();
    bikeHint.clear();
    bikeRoad.clear();
    botLatSet = false;
    adviceClock = -1.0;
    focusSet = false;
    stepCount = 0;
    built = true;
    std::printf("tráfico: %zu autos (%.1f por km y carril, %.0f-%.0f km/h, cambio de carril cada ~%.0f s, semilla %u)\n", cars.size(), density,
                speedMin * 3.6f, speedMax * 3.6f, laneChangeEvery, seed);
}

// La base vuelve al reloj 0: los autos donde se armaron y 60 s de rodaje (igual en todas las PCs).
void Traffic::BaseReset()
{
    cars = initial;                                // como los armó Build (también para volver atrás, SyncClock)
    baseSteps = -kWarmupSteps;
    baseTime = (double)baseSteps * kBaseDt;
    for (int k = 0; k < kWarmupSteps; ++k) BaseStep();
    clock = 0.0;
    clockRate = 1.0f;
    shown.clear();
    shownIndex.assign(cars.size(), -1);
}

float Traffic::LaneLat(const Car& c, float lcProgress) const
{
    const float a = layout.LaneLat(c.dir, (float)c.lane);
    if (c.target < 0 || lcProgress <= 0.0f) return a;
    const float b = layout.LaneLat(c.dir, (float)c.target);
    const float t = std::min(lcProgress, 1.0f);
    return a + (b - a) * t * t * (3.0f - 2.0f * t);
}

// IDM: aceleración para ir a v0 sin acercarse al de adelante (gap: de paragolpe a paragolpe; vLead su velocidad).
float Traffic::Idm(const Car& c, float v, float gap, float vLead) const
{
    const float x = v / std::max(c.v0, 1.0f);
    const float x2 = x * x;
    float a = c.accel * (1.0f - x2 * x2);
    if (gap < 1e8f) {
        const float g = std::max(gap, 0.1f);
        const float sStar = kS0 + std::max(0.0f, v * c.headway + v * (v - vLead) / (2.0f * std::sqrt(c.accel * kComfort)));
        const float r = sStar / g;
        a -= c.accel * r * r;
    }
    return std::max(a, -kMaxBrake);
}

void Traffic::BaseStep()
{
    const int lanes = layout.lanes;
    const float dt = (float)kBaseDt;
    // Listas por mano y carril, ordenadas por pos (desempate por número: el mismo orden en todas las PCs).
    for (auto& l : laneLists) l.clear();
    for (int i = 0; i < (int)cars.size(); ++i) {
        const Car& c = cars[i];
        const int d = c.dir > 0 ? 0 : 1;
        laneLists[(size_t)d * lanes + c.lane].push_back(i);
        if (c.target >= 0 && c.lc >= 0.0f) laneLists[(size_t)d * lanes + c.target].push_back(i);
    }
    for (auto& l : laneLists)
        std::sort(l.begin(), l.end(), [&](int a, int b) { return cars[a].pos < cars[b].pos || (cars[a].pos == cars[b].pos && a < b); });

    // El de adelante y el de atrás de un auto en una lista (sin contarlo a él): huecos de paragolpe a paragolpe.
    struct Near {
        float gapAhead = 1e9f, vAhead = 0.0f, gapBehind = 1e9f;
        int behind = -1;
    };
    auto around = [&](int i, const std::vector<int>& l) {
        Near r;
        const Car& c = cars[i];
        if (l.empty()) return r;
        const auto it = std::upper_bound(l.begin(), l.end(), i, [&](int me, int o) {
            return cars[me].pos < cars[o].pos || (cars[me].pos == cars[o].pos && me < o);
        });
        const int n = (int)l.size();
        int k = (int)(it - l.begin()) % n;
        for (int tries = 0; tries < n; ++tries, k = (k + 1) % n) {
            if (l[k] == i) continue;
            const Car& o = cars[l[k]];
            r.gapAhead = Ahead(c.pos, o.pos, L) - 0.5f * (kinds[c.kind].length + kinds[o.kind].length);
            r.vAhead = o.v;
            break;
        }
        k = ((int)(it - l.begin()) - 1 + n) % n;
        for (int tries = 0; tries < n; ++tries, k = (k - 1 + n) % n) {
            if (l[k] == i) continue;
            const Car& o = cars[l[k]];
            r.gapBehind = Ahead(o.pos, c.pos, L) - 0.5f * (kinds[c.kind].length + kinds[o.kind].length);
            r.behind = l[k];
            break;
        }
        return r;
    };

    const double now = (double)baseSteps * kBaseDt;
    for (int i = 0; i < (int)cars.size(); ++i) {
        Car& c = cars[i];
        const int d = c.dir > 0 ? 0 : 1;
        const std::vector<int>& here = laneLists[(size_t)d * lanes + c.lane];
        const Near cur = around(i, here);
        float a = Idm(c, c.v, cur.gapAhead, cur.vAhead);
        if (c.target >= 0 && c.lc >= 0.0f) {
            const Near tg = around(i, laneLists[(size_t)d * lanes + c.target]);
            a = std::min(a, Idm(c, c.v, tg.gapAhead, tg.vAhead));
        }
        // Cambio de carril: ¿hay lugar? (el de atrás en el carril nuevo no tiene que frenar fuerte).
        auto safe = [&](int lane, float& aNew) {
            const Near n = around(i, laneLists[(size_t)d * lanes + lane]);
            aNew = Idm(c, c.v, n.gapAhead, n.vAhead);
            if (n.gapAhead < 6.0f + 0.3f * c.v) return false;
            if (n.behind >= 0) {
                const Car& f = cars[n.behind];
                if (n.gapBehind < 6.0f + 0.3f * f.v) return false;
                if (Idm(f, f.v, n.gapBehind, c.v) < -2.5f) return false;
            }
            return true;
        };
        if (c.signal > 0.0f) {                         // guiño puesto: al terminar, si sigue habiendo lugar, arranca
            c.signal -= dt;
            if (c.signal <= 0.0f) {
                float aNew;
                if (c.target >= 0 && safe(c.target, aNew)) c.lc = 0.0f;
                else c.target = -1;
                c.signal = 0.0f;
            }
        } else if (c.target < 0 && now >= c.nextThink) {
            c.nextThink = now + (double)(laneChangeEvery * (0.5f + Rand01(c.rng)));
            const float aCur = a;
            const bool truck = c.kind == 5;
            const float lively = Rand01(c.rng), keepRight = Rand01(c.rng);
            int best = -1;
            for (int side : {-1, 1}) {
                const int nl = c.lane + side;
                if (nl < 0 || nl >= lanes) continue;
                if (truck && nl < lanes - 2) continue;
                float aNew = 0.0f;
                if (!safe(nl, aNew)) continue;
                const bool pass = side < 0 && aNew > aCur + 0.3f && c.v < c.v0 - 2.0f;
                const bool right = side > 0 && aNew >= aCur - 0.05f && keepRight < 0.5f;
                const bool random = lively < 0.12f && aNew > aCur - 0.2f;
                if (pass) {
                    best = nl;
                    break;
                }
                if ((right || random) && best < 0) best = nl;
            }
            if (best >= 0) {
                c.target = (int8_t)best;
                c.signal = 1.2f + 0.8f * Rand01(c.rng);
                c.lcDur = 3.5f + 2.0f * Rand01(c.rng);
            }
        }
        c.a = a;
    }
    // Avance balístico (el mismo que usa la interpolación entre pasos).
    for (Car& c : cars) {
        float ds = c.v * dt + 0.5f * c.a * dt * dt;
        if (c.v + c.a * dt < 0.0f) {                   // se frena en este paso
            ds = c.a < 0.0f ? c.v * c.v / (-2.0f * c.a) : 0.0f;
            c.a = -c.v / dt;
        }
        c.pos = WrapPos(c.pos + std::max(0.0f, ds), L);
        c.v = std::max(0.0f, c.v + c.a * dt);
        if (c.lc >= 0.0f) {
            c.lc += dt / c.lcDur;
            if (c.lc >= 1.0f) {
                c.lane = c.target;
                c.target = -1;
                c.lc = -1.0f;
                ++laneChanges;
            }
        }
    }
    ++baseSteps;
    baseTime = (double)baseSteps * kBaseDt;
    // Pruebas de red: una suma de la base cada 30 s del reloj (la del anfitrión y la del invitado tienen que dar igual).
    if (log && baseSteps > 0 && baseSteps % 300 == 0) {
        uint32_t sum = 0;
        for (const Car& c : cars) {
            uint32_t a, b;
            std::memcpy(&a, &c.pos, 4);
            std::memcpy(&b, &c.v, 4);
            sum = (sum ^ a) * 16777619u ^ b ^ ((uint32_t)c.lane << 24);
        }
        std::printf("TRAFICO base t=%.1f suma=%08x\n", baseTime, sum);
    }
}

void Traffic::Advance(double to)
{
    while (baseTime + kBaseDt <= to + 1e-9) BaseStep();
}

void Traffic::SyncClock(double hostClock)
{
    if (!Active()) return;
    const double err = hostClock - clock;
    if (err > 2.0) {                               // atrasado: al día de una
        clock = hostClock;
        Advance(clock);
        clockRate = 1.0f;
    } else if (err < -2.0) {                       // adelantado: la base no vuelve atrás, se rehace desde el reloj 0
        BaseReset();
        clock = std::max(0.0, hostClock);
        Advance(clock);
    } else {
        clockRate = (float)(1.0 + std::clamp(err * 0.5, -0.05, 0.05));
    }
}

// ======================================================================================= capa local y cuerpos
Traffic::Pose Traffic::CarPose(const Car& c, float pos, float lat, float yawOffset) const
{
    const float s = c.dir > 0 ? pos : L - pos;
    const TrackPoint p = track->At(s);
    const float fx0 = p.tx * (float)c.dir, fz0 = p.tz * (float)c.dir;
    // Rumbo con el cambio de carril (yawOffset, rad, + hacia la izquierda del auto).
    const float cy = std::cos(yawOffset), sy = std::sin(yawOffset);
    const float fx = fx0 * cy + fz0 * sy, fz = -fx0 * sy + fz0 * cy;
    const float lx = fz, lz = -fx;                 // izquierda del auto (+X local)
    const float x = p.x - p.tz * lat, z = p.z + p.tx * lat;
    const Kind& k = kinds[c.kind];
    const float half = 0.5f * k.length * 0.6f, side = 0.5f * k.width * 0.8f;
    const float hF = terrain->Height(x + fx * half, z + fz * half), hB = terrain->Height(x - fx * half, z - fz * half);
    const float hL = terrain->Height(x + lx * side, z + lz * side), hR = terrain->Height(x - lx * side, z - lz * side);
    const float y = 0.25f * (hF + hB + hL + hR);
    Vec3 F = Vec3(fx * 2.0f * half, hF - hB, fz * 2.0f * half).Normalized();
    Vec3 Lf = Vec3(lx * 2.0f * side, hL - hR, lz * 2.0f * side).Normalized();
    const Vec3 U = F.Cross(Lf).Normalized();
    Lf = U.Cross(F).Normalized();
    JPH::Mat44 m = JPH::Mat44::sIdentity();
    m.SetColumn3(0, Lf);
    m.SetColumn3(1, U);
    m.SetColumn3(2, F);
    Pose out;
    out.pos = Vec3(x, y, z);
    out.rot = m.GetQuaternion().Normalized();
    return out;
}

void Traffic::Step(float dt, PhysicsWorld& world, const Vec3* bikes, const Vec3* bikeVel, int count)
{
    if (!Active()) return;
    ++stepCount;
    clock += (double)(dt * clockRate);
    Advance(clock);
    const float tau = (float)(clock - baseTime);

    // Dónde está cada moto sobre el trazado.
    if ((int)bikeHint.size() != count) {
        bikeHint.assign((size_t)count, -1);
        bikeRoad.assign((size_t)count, {});
    }
    for (int b = 0; b < count; ++b) {
        const float x = bikes[b].GetX(), z = bikes[b].GetZ();
        int i = track->Nearest(x, z, bikeHint[b], 120);
        const TrackPoint* p = &track->Points()[i];
        if ((p->x - x) * (p->x - x) + (p->z - z) * (p->z - z) > 40.0f * 40.0f) {
            i = track->Nearest(x, z);
            p = &track->Points()[i];
        }
        bikeHint[b] = i;
        bikeRoad[b].s = p->s;
        bikeRoad[b].lat = (x - p->x) * -p->tz + (z - p->z) * p->tx;
        bikeRoad[b].vAlong = bikeVel[b].GetX() * p->tx + bikeVel[b].GetZ() * p->tz;
    }
    const Vec3 focus = count > 0 ? bikes[0] : Vec3::sZero();
    const bool teleport = !focusSet || (focus - lastFocus).Length() > 25.0f;
    lastFocus = focus;
    focusSet = true;

    // Base al instante de ahora (entre pasos): posición balística, velocidad, avance del cambio de carril.
    auto basePos = [&](const Car& c) {
        float d = c.v * tau + 0.5f * c.a * tau * tau;
        if (c.a < 0.0f && c.v + c.a * tau < 0.0f) d = c.v * c.v / (-2.0f * c.a);
        return WrapPos(c.pos + std::max(0.0f, d), L);
    };
    auto baseV = [&](const Car& c) { return std::max(0.0f, c.v + c.a * tau); };
    auto baseLc = [&](const Car& c) { return c.lc >= 0.0f ? std::min(1.0f, c.lc + tau / c.lcDur) : 0.0f; };
    auto along = [&](const Car& c, float s) { return c.dir > 0 ? s : L - s; };

    const int n = (int)cars.size();
    std::vector<float> dPos((size_t)n);
    for (int i = 0; i < n; ++i) dPos[i] = WrapPos(basePos(cars[i]) - cars[i].lag, L);

    // Al aparecer (o reaparecer) la moto: los autos que quedan encima o justo detrás se corren 35 m para atrás.
    if (teleport && count > 0) {
        for (int i = 0; i < n; ++i) {
            Car& c = cars[i];
            const float bp = along(c, bikeRoad[0].s);
            const float ahead = Ahead(dPos[i], bp, L);             // de este auto hasta la moto
            const float rel = ahead > L * 0.5f ? ahead - L : ahead;  // + la moto está adelante
            const float lat = LaneLat(c, baseLc(c));
            if (rel > -8.0f && rel < 35.0f && std::fabs(lat - bikeRoad[0].lat) < 0.5f * kinds[c.kind].width + 1.5f) {
                c.lag += 35.0f - rel;
                c.local = true;
                c.dispV = baseV(c) * 0.5f;
                c.dispLat = lat;
                dPos[i] = WrapPos(basePos(c) - c.lag, L);
            }
        }
    }

    // Quiénes tienen capa local: cerca de una moto (a lo largo), atrasados, o cerca (detrás hasta 200 m, al lado 60 m)
    // de un atrasado de su mano.
    std::vector<int> lagged[2];
    for (int i = 0; i < n; ++i)
        if (cars[i].lag > 0.0f) lagged[cars[i].dir > 0 ? 0 : 1].push_back(i);
    for (auto& l : lagged) std::sort(l.begin(), l.end(), [&](int a, int b) { return dPos[a] < dPos[b]; });
    std::vector<int> act;
    act.reserve(256);
    for (int i = 0; i < n; ++i) {
        Car& c = cars[i];
        bool on = c.lag > 0.0f;
        if (!on && react)
            for (int b = 0; b < count && !on; ++b) {
                const float d = Ahead(along(c, bikeRoad[b].s), dPos[i], L);
                on = d < kLocalRadius || d > L - kLocalRadius;
            }
        const std::vector<int>& lg = lagged[c.dir > 0 ? 0 : 1];
        if (!on && !lg.empty()) {
            // El atrasado más cercano adelante (o al lado, hasta 60 m atrás).
            auto it = std::lower_bound(lg.begin(), lg.end(), dPos[i] - 60.0f, [&](int a, float p) { return dPos[a] < p; });
            for (int tries = 0; tries < 3 && !on; ++tries) {
                if (it == lg.end()) it = lg.begin();
                const float d = Ahead(dPos[i], dPos[*it], L);
                on = d < 200.0f || d > L - 60.0f;
                ++it;
            }
        }
        if (on && !c.local) {
            c.local = true;
            c.dispV = baseV(c);
            c.dispLat = LaneLat(c, baseLc(c));
        }
        if (!on && c.local && c.lag <= 0.0f) c.local = false;
        if (c.local) act.push_back(i);
    }

    // Capa local: el líder en pantalla (si alguno está atrasado), las motos adelante en su franja, y alcanzar a la base.
    reacting = 0;
    lagging = (int)(lagged[0].size() + lagged[1].size());
    std::vector<float> newLat(act.size()), newV(act.size()), newLag(act.size());
    std::vector<char> brake(act.size());
    for (size_t q = 0; q < act.size(); ++q) {
        Car& c = cars[act[q]];
        const Kind& k = kinds[c.kind];
        const float vb = baseV(c), latBase = LaneLat(c, baseLc(c));
        float aCmd = 1e9f;
        bool forBike = false;
        // Motos adelante en su franja (o encimadas al costado de la trompa).
        if (react)
            for (int b = 0; b < count; ++b) {
                // La moto (medio ancho ~0.4 m) le tapa la trompa: entre dos carriles (sobre la línea) no frena a nadie.
                if (std::fabs(bikeRoad[b].lat - c.dispLat) > 0.5f * k.width + 0.55f) continue;
                const float d = Ahead(dPos[act[q]], along(c, bikeRoad[b].s), L);
                if (d > 420.0f) continue;
                const float gap = d - 0.5f * k.length - 1.1f;
                const float aB = Idm(c, c.dispV, gap, std::max(0.0f, bikeRoad[b].vAlong * (float)c.dir));
                if (aB < aCmd) {
                    aCmd = aB;
                    forBike = aB < -0.3f;
                }
            }
        // El de adelante en pantalla (otro con capa local de su mano que le tapa la franja), si él o éste están atrasados.
        for (size_t o = 0; o < act.size(); ++o) {
            if (o == q) continue;
            const Car& e = cars[act[o]];
            if (e.dir != c.dir) continue;
            if (std::fabs(e.dispLat - c.dispLat) > 0.5f * (k.width + kinds[e.kind].width) + 0.2f) continue;
            const float d = Ahead(dPos[act[q]], dPos[act[o]], L);
            if (d > 200.0f) continue;
            const float gap = d - 0.5f * (k.length + kinds[e.kind].length);
            if (e.lag <= 0.0f && c.lag <= 0.0f && gap > 2.0f) continue;
            aCmd = std::min(aCmd, Idm(c, c.dispV, gap, e.dispV));
        }
        // Volver a la base: hasta 5 m/s más rápido mientras esté atrasado.
        // Sin nada que lo frene y casi en su lugar (menos de 1 m y 1 m/s): vuelve a la base de una (si no, el atraso de
        // seguirla con demora cuando la base acelera no se iba nunca del todo).
        const float vTarget = vb + (c.lag > 0.0f ? std::min(5.0f, 0.25f * c.lag + 0.5f) : 0.0f);
        const float aCatch = mu::Clamp((vTarget - c.dispV) * 2.0f, -4.0f, 2.0f);
        const bool free = aCmd >= aCatch;
        aCmd = std::min(aCmd, aCatch);
        float v = std::max(0.0f, c.dispV + aCmd * dt);
        float lag = c.lag + (vb - v) * dt;
        if (lag <= 0.0f || (free && lag < 1.0f && std::fabs(v - vb) < 1.0f)) {
            lag = 0.0f;
            v = vb;
        }
        reacting += forBike ? 1 : 0;
        brake[q] = aCmd < -0.8f;
        // Lateral: sigue a la base (hasta 2.6 m/s) salvo que el lado al que va esté ocupado por una moto o un auto.
        float lat = c.dispLat;
        const float want = latBase - lat;
        if (std::fabs(want) > 1e-3f) {
            const float step = mu::Clamp(want, -2.6f * dt, 2.6f * dt);
            const float next = lat + step;
            bool blocked = false;
            for (int b = 0; b < count && !blocked && react; ++b) {
                const float d = Ahead(dPos[act[q]], along(c, bikeRoad[b].s), L);
                const float rel = d > L * 0.5f ? d - L : d;
                if (std::fabs(rel) > 0.5f * k.length + 2.5f) continue;
                const float side = bikeRoad[b].lat - lat;
                blocked = side * want > 0.0f && std::fabs(bikeRoad[b].lat - next) < 0.5f * k.width + 0.8f;
            }
            for (size_t o = 0; o < act.size() && !blocked; ++o) {
                if (o == q) continue;
                const Car& e = cars[act[o]];
                if (e.dir != c.dir) continue;
                const float d = Ahead(dPos[act[q]], dPos[act[o]], L);
                const float rel = d > L * 0.5f ? d - L : d;
                if (std::fabs(rel) > 0.5f * (k.length + kinds[e.kind].length) + 1.5f) continue;
                const float side = e.dispLat - lat;
                blocked = side * want > 0.0f && std::fabs(e.dispLat - next) < 0.5f * (k.width + kinds[e.kind].width) + 0.25f;
            }
            if (!blocked) lat = next;
        }
        newLat[q] = lat;
        newV[q] = v;
        newLag[q] = lag;
    }
    for (size_t q = 0; q < act.size(); ++q) {
        Car& c = cars[act[q]];
        c.dispLat = newLat[q];
        c.dispV = newV[q];
        c.lag = newLag[q];
        c.braking = brake[q];
        dPos[act[q]] = WrapPos(basePos(c) - c.lag, L);
    }

    // Los que se muestran (cerca de la moto propia): pose de antes y de ahora.
    std::vector<int> want;
    want.reserve(256);
    const float sF = count > 0 ? bikeRoad[0].s : 0.0f;
    for (int i = 0; i < n; ++i) {
        const Car& c = cars[i];
        const float s = c.dir > 0 ? dPos[i] : L - dPos[i];
        const float d = Ahead(s, sF, L);
        if (d < kShowRadius || d > L - kShowRadius) want.push_back(i);
    }
    std::vector<Shown> next;
    next.reserve(want.size());
    for (int i : want) {
        Car& c = cars[i];
        if (!c.local) c.braking = c.a < -0.8f;
        const float lat = c.local ? c.dispLat : LaneLat(c, baseLc(c));
        // Rumbo en el cambio de carril: la velocidad de costado contra la de avance.
        float latRate = 0.0f;
        if (c.target >= 0 && c.lc >= 0.0f) {
            const float t = baseLc(c), dl = layout.LaneLat(c.dir, (float)c.target) - layout.LaneLat(c.dir, (float)c.lane);
            latRate = dl * 6.0f * t * (1.0f - t) / c.lcDur;
        }
        const float v = c.local ? c.dispV : baseV(c);
        // + lateral = a la derecha del trazado; para el auto, + yaw = a su izquierda.
        const float yawOff = std::atan2(-latRate * (float)c.dir, std::max(v, 3.0f));
        Shown sh;
        sh.car = i;
        sh.cur = CarPose(c, dPos[i], lat, yawOff);
        sh.s = c.dir > 0 ? dPos[i] : L - dPos[i];
        sh.lat = lat;
        sh.v = v;
        const int was = i < (int)shownIndex.size() ? shownIndex[i] : -1;
        sh.prev = was >= 0 ? shown[(size_t)was].cur : sh.cur;
        sh.fresh = was < 0;
        next.push_back(sh);
    }
    // Pruebas (--telemetry): cuando un auto cerca de la moto propia pone el guiño para cambiar de carril.
    if (log) {
        if (signalShown.size() != cars.size()) signalShown.assign(cars.size(), 0);
        for (const Shown& sh : next) {
            const Car& c = cars[(size_t)sh.car];
            const bool on = c.target >= 0;
            if (on && !signalShown[(size_t)sh.car] && (sh.cur.pos - focus).Length() < 60.0f) {
                const float d = Ahead(bikeRoad.empty() ? 0.0f : bikeRoad[0].s, sh.s, L);
                std::printf("GUINO t=%.2f auto %d (%s) del carril %d al %d, a %+.0f m de la moto (mano %+d), %.0f km/h\n", clock, sh.car,
                            c.kind == 5 ? "camión" : "auto", c.lane, c.target, d > L * 0.5f ? d - L : d, (int)c.dir, sh.v * 3.6f);
            }
            signalShown[(size_t)sh.car] = on ? 1 : 0;
        }
    }
    for (const Shown& sh : shown) shownIndex[sh.car] = -1;
    shown.swap(next);
    for (size_t q = 0; q < shown.size(); ++q) shownIndex[shown[q].car] = (int)q;

    // Cuerpos: los autos más cerca de la moto propia (y de su piloto, que puede estar lejos de ella).
    if (!bodies.empty()) {
        struct Cand {
            float d;
            int shownAt;
        };
        std::vector<Cand> cand;
        for (size_t q = 0; q < shown.size(); ++q) {
            float d = (shown[q].cur.pos - focus).Length();
            for (int b = 1; b < count && b < 2; ++b) d = std::min(d, (shown[q].cur.pos - bikes[b]).Length());
            if (d < kBodyRadius) cand.push_back({d, (int)q});
        }
        std::sort(cand.begin(), cand.end(), [](const Cand& a, const Cand& b) { return a.d < b.d || (a.d == b.d && a.shownAt < b.shownAt); });
        if (cand.size() > bodies.size()) cand.resize(bodies.size());
        std::vector<char> keep(bodies.size(), 0);
        std::vector<int> needs;
        for (const Cand& cd : cand) {
            Car& c = cars[shown[(size_t)cd.shownAt].car];
            if (c.body >= 0 && bodyCar[(size_t)c.body] == shown[(size_t)cd.shownAt].car) keep[(size_t)c.body] = 1;
            else needs.push_back(cd.shownAt);
        }
        JPH::BodyInterface& bi = world.Bodies();
        for (size_t b = 0; b < bodies.size(); ++b)
            if (!keep[b] && bodyCar[b] >= 0) {                   // suelta el cuerpo: lejos y dormido
                cars[(size_t)bodyCar[b]].body = -1;
                bodyCar[b] = -1;
                bi.SetPositionAndRotation(bodies[b], JPH::RVec3(0.0f, -2000.0f - 10.0f * (float)b, 0.0f), Quat::sIdentity(), JPH::EActivation::DontActivate);
                bi.DeactivateBody(bodies[b]);
            }
        size_t freeB = 0;
        for (int at : needs) {
            while (freeB < bodies.size() && bodyCar[freeB] >= 0) ++freeB;
            if (freeB >= bodies.size()) break;
            const int ci = shown[(size_t)at].car;
            Car& c = cars[(size_t)ci];
            bodyCar[freeB] = ci;
            c.body = (int)freeB;
            if (kinds[c.kind].shape) bi.SetShape(bodies[freeB], kinds[c.kind].shape, false, JPH::EActivation::DontActivate);
            bi.SetUserData(bodies[freeB], kTrafficTag | (JPH::uint64)ci);
            bi.SetPositionAndRotation(bodies[freeB], shown[(size_t)at].prev.pos, shown[(size_t)at].prev.rot, JPH::EActivation::Activate);
        }
        bodiesUsed = 0;
        for (size_t b = 0; b < bodies.size(); ++b) {
            if (bodyCar[b] < 0) continue;
            ++bodiesUsed;
            const int at = shownIndex[(size_t)bodyCar[b]];
            if (at < 0) continue;
            bi.MoveKinematic(bodies[b], shown[(size_t)at].cur.pos, shown[(size_t)at].cur.rot, dt);
        }
    }
}

bool Traffic::BikeHit(PhysicsWorld& world, bool log, float simTime)
{
    if (!Active()) return false;
    const PhysicsWorld::CarHit h = world.LastCarHit();
    if (h.car < 0) return false;
    const bool fall = h.speed > kHitSpeed || h.normal > kHitNormal;
    if (log && h.speed > 1.0f) {
        // Dónde estaba el auto respecto de la moto (adelante + / atrás -) y los laterales desde la línea central.
        float rel = 0.0f, carLat = 0.0f, carV = 0.0f;
        const int at = h.car < (int)shownIndex.size() ? shownIndex[(size_t)h.car] : -1;
        if (at >= 0 && !bikeRoad.empty()) {
            const Shown& sh = shown[(size_t)at];
            const float d = Ahead(bikeRoad[0].s, sh.s, L);
            rel = d > L * 0.5f ? d - L : d;
            carLat = sh.lat;
            carV = sh.v;
        }
        std::printf("AUTO t=%.2f toque con el auto %d (%s) a %.1f m/s (%.1f en la normal), el auto a %+.1f m y %.0f km/h en lat %.1f, la moto "
                    "en lat %.1f%s\n",
                    simTime, h.car, h.car < (int)cars.size() ? (cars[(size_t)h.car].kind == 5 ? "camión" : "auto") : "?", h.speed, h.normal, rel,
                    carV * 3.6f, carLat, bikeRoad.empty() ? 0.0f : bikeRoad[0].lat, fall ? ": el piloto se cae" : "");
    }
    if (fall) ++hits;
    (void)world;
    return fall;
}

void Traffic::CreateBodies(PhysicsWorld& world)
{
    bodies.clear();
    bodyCar.clear();
    if (!Active() || !kinds[0].shape) return;
    for (int b = 0; b < kBodies; ++b) {
        JPH::BodyCreationSettings bcs(kinds[0].shape, JPH::RVec3(0.0f, -2000.0f - 10.0f * (float)b, 0.0f), Quat::sIdentity(),
                                      JPH::EMotionType::Kinematic, Layers::PROP);
        bcs.mFriction = 0.5f;
        bcs.mRestitution = 0.1f;
        bcs.mUserData = kTrafficTag;
        bcs.mAllowSleeping = false;
        const JPH::BodyID id = world.Bodies().CreateAndAddBody(bcs, JPH::EActivation::DontActivate);
        bodies.push_back(id);
        bodyCar.push_back(-1);
    }
    for (Car& c : cars) c.body = -1;
}

void Traffic::BotAdvice(float s, float v, float lat, float& outLat, float& outSpeed)
{
    outLat = lat;
    outSpeed = 1e9f;
    if (!Active()) return;
    if (!botLatSet || std::fabs(lat - botBikeLat) > 2.5f) {   // al empezar o al reaparecer (en la banquina)
        botLat = lat;
        botLatSet = true;
    }
    botBikeLat = lat;
    const float dt = adviceClock < 0.0 ? 0.0f : (float)std::min(0.2, std::max(0.0, clock - adviceClock));
    adviceClock = clock;
    // En la banquina, primero agarrar velocidad (80 km/h) y después entrar al carril de la derecha.
    const float lanesEnd = layout.LanesFrom() + (float)layout.lanes * layout.laneWidth;
    if (botLat > lanesEnd && v < 22.0f) {
        outLat = botLat;
        return;
    }
    // Opciones: los carriles de su mano y las líneas entre carriles (pasar entre dos autos).
    std::vector<float> opts;
    for (int k = 0; k < layout.lanes; ++k) opts.push_back(layout.LaneLat(1, (float)k));
    for (int k = 1; k < layout.lanes; ++k) opts.push_back(layout.LaneLat(1, (float)k - 0.5f));
    auto freeAhead = [&](float o, float& vLead) {
        float best = 300.0f;
        vLead = 99.0f;
        for (const Shown& sh : shown) {
            const Car& c = cars[(size_t)sh.car];
            if (c.dir < 0) continue;
            if (std::fabs(sh.lat - o) > 0.5f * kinds[c.kind].width + kBotMargin) continue;
            const float d = Ahead(s, sh.s, L);
            const float rel = d > L * 0.5f ? d - L : d;    // + adelante
            if (rel > 300.0f || rel < -(0.5f * kinds[c.kind].length + 4.0f)) continue;
            const float gap = std::max(0.0f, rel - 0.5f * kinds[c.kind].length - 1.0f);   // al costado: no hay lugar
            if (gap < best) {
                best = gap;
                vLead = sh.v;
            }
        }
        return best;
    };
    // ¿Viene alguno de atrás por esa franja que no llegue a frenar? (los autos frenan por la moto, pero no de golpe)
    auto clearBehind = [&](float o) {
        for (const Shown& sh : shown) {
            const Car& c = cars[(size_t)sh.car];
            if (c.dir < 0) continue;
            if (std::fabs(sh.lat - o) > 0.5f * kinds[c.kind].width + kBotMargin) continue;
            const float d = Ahead(sh.s, s, L);                  // del auto hasta la moto
            if (d > 250.0f) continue;
            const float gap = d - 0.5f * kinds[c.kind].length - 1.0f, closing = sh.v - v;
            if (gap < 8.0f + std::max(0.0f, closing) * 3.0f) return false;
        }
        return true;
    };
    // La velocidad que deja una franja: la del de adelante más lo que falta para alcanzarlo (a 18 m se le pega).
    auto reachable = [](float free, float vLead) { return free >= 299.0f ? 1e9f : std::max(0.0f, vLead + (free - kBotFollow) * 0.35f); };
    float chosen = botLat, chosenScore = -1e9f;
    float nearest = opts[0];
    for (float o : opts)
        if (std::fabs(o - botLat) < std::fabs(nearest - botLat)) nearest = o;
    for (float o : opts) {
        if (std::fabs(o - botLat) > layout.laneWidth * 1.05f && o != nearest) continue;   // hasta un carril por vez
        float vLead, vMid;
        const float free = freeAhead(o, vLead);
        const bool stay = std::fabs(o - botLat) < 0.1f;
        if (!stay && (free <= 0.0f || freeAhead(0.5f * (o + botLat), vMid) <= 0.0f || !clearBehind(o))) continue;   // al costado o atrás
        // Meterse detrás de uno más lento sólo si da para frenar antes (6 m/s²).
        if (!stay && v > vLead && (v - vLead) * (v - vLead) > 12.0f * std::max(0.0f, free - 8.0f)) continue;
        const bool line = std::fabs(std::remainder(std::fabs(o) - layout.LanesFrom(), layout.laneWidth)) < 0.1f;
        // Puntaje en m/s: la velocidad que deja (hasta 70), quedarse vale 3 y pasar entre dos autos cuesta 2.
        float score = std::min(70.0f, reachable(free, vLead)) + 0.01f * std::min(free, 300.0f);
        if (stay) score += 3.0f;
        if (line) score -= 2.0f;
        if (score > chosenScore) {
            chosenScore = score;
            chosen = o;
        }
    }
    // Moverse de costado de a poco (1.6 m/s; a más de 108 km/h, 0.7) hacia la opción, salvo frenando (frenar y tirar la moto
    // de costado a la vez la tumbaba contra el separador); la velocidad, la que deja la franja de donde va ahora y la de la
    // elegida.
    float vHere, vThere;
    const float fHere = freeAhead(lat, vHere);
    const bool braking = v > reachable(fHere, vHere) + 2.0f;
    // En las curvas (radio de menos de ~1300 m, acá o 150 m adelante) tampoco se cambia: a 160 km/h, tirar la moto de
    // costado acostada la hacía oscilar hasta el separador o el guardarraíl.
    const bool curve = std::max(std::fabs(track->At(s).curvature), std::fabs(track->At(s + 150.0f).curvature)) > 0.00075f && v > 25.0f;
    if ((braking || curve) && std::fabs(chosen - botLat) > 0.1f) chosen = botLat;
    if (dt > 0.0f) botLat = mu::MoveTowards(botLat, chosen, (v > 30.0f ? 0.7f : 1.6f) * dt);
    outLat = botLat;
    const float fThere = freeAhead(chosen, vThere);
    outSpeed = std::min(reachable(fHere, vHere), reachable(fThere, vThere));
}

// ======================================================================================= mallas
void Traffic::BuildMeshKinds()
{
    kindMeshes.assign(kinds.size(), {});
    const Color white = {255, 255, 255, 255}, glass = {34, 44, 56, 255}, trim = {38, 38, 42, 255};
    for (size_t ki = 0; ki < kinds.size(); ++ki) {
        const Kind& k = kinds[ki];
        KindMesh& km = kindMeshes[ki];
        CarParts b;
        const float hl = 0.5f * k.length, hwd = 0.5f * k.width;
        auto use = [&](PartMesh& p) { b.Use(p.pos, p.nrm, p.uv, p.col); };
        if (ki == 5) {
            // Camión: cabina (pintura) adelante, caja blanca, chasis y tres ejes.
            const float cab0 = hl - 2.3f;
            use(km.paint);
            b.Box({-hwd + 0.05f, 0.55f, cab0}, {hwd - 0.05f, 2.95f, hl}, white, 0.45f, 0x1F);
            use(km.always);
            b.Box({-hwd, 1.05f, -hl}, {hwd, 3.9f, cab0 - 0.25f}, {226, 228, 226, 255}, 0.3f, 0x3F);
            b.Quad({-hwd + 0.2f, 1.95f, hl + 0.012f}, {hwd - 0.2f, 1.95f, hl + 0.012f}, {hwd - 0.2f, 2.75f, hl + 0.012f}, {-hwd + 0.2f, 2.75f, hl + 0.012f},
                   {0, 0, 1}, glass, 0.9f);
            for (float sx : {-1.0f, 1.0f})
                b.Quad({sx * (hwd - 0.05f + 0.012f), 1.95f, hl - 1.2f}, {sx * (hwd - 0.05f + 0.012f), 1.95f, hl - 0.15f},
                       {sx * (hwd - 0.05f + 0.012f), 2.65f, hl - 0.15f}, {sx * (hwd - 0.05f + 0.012f), 2.65f, hl - 1.2f}, {sx, 0, 0}, glass, 0.9f);
            use(km.nearOnly);
            b.Box({-hwd + 0.3f, 0.5f, -hl + 0.3f}, {hwd - 0.3f, 1.06f, cab0}, trim, 0.1f, 0x0F);
            for (float z : {hl - 1.3f, -hl + 2.6f, -hl + 1.4f})
                for (float sx : {-1.0f, 1.0f}) b.Wheel({sx * (hwd - 0.25f), 0.5f, z}, 0.5f, 0.4f);
            b.Box({-hwd + 0.05f, 0.35f, hl - 0.02f}, {hwd - 0.05f, 0.62f, hl + 0.1f}, trim, 0.2f);
            use(km.head);
            for (float sx : {-1.0f, 1.0f}) b.Quad({sx * (hwd - 0.45f), 0.75f, hl + 0.012f}, {sx * (hwd - 0.1f), 0.75f, hl + 0.012f}, {sx * (hwd - 0.1f), 0.95f, hl + 0.012f}, {sx * (hwd - 0.45f), 0.95f, hl + 0.012f}, {0, 0, 1}, {255, 246, 220, 255}, 0.9f, kGlow);
            for (PartMesh* lp : {&km.tail, &km.brake}) {
                use(*lp);
                const bool on = lp == &km.brake;
                for (float sx : {-1.0f, 1.0f})
                    b.Quad({sx * (hwd - 0.5f), 1.2f, -hl - 0.012f}, {sx * (hwd - 0.1f), 1.2f, -hl - 0.012f}, {sx * (hwd - 0.1f), 1.45f, -hl - 0.012f},
                           {sx * (hwd - 0.5f), 1.45f, -hl - 0.012f}, {0, 0, -1}, on ? Color{255, 40, 30, 255} : Color{130, 20, 20, 255}, 0.8f, on ? kGlow : 2.0f);
            }
            for (int side = 0; side < 2; ++side) {
                use(side == 0 ? km.blinkL : km.blinkR);
                const float sx = side == 0 ? 1.0f : -1.0f;
                b.Quad({sx * (hwd - 0.1f), 1.5f, -hl - 0.012f}, {sx * (hwd - 0.35f), 1.5f, -hl - 0.012f}, {sx * (hwd - 0.35f), 1.7f, -hl - 0.012f},
                       {sx * (hwd - 0.1f), 1.7f, -hl - 0.012f}, {0, 0, -1}, {255, 150, 20, 255}, 0.8f, kGlow);
                b.Quad({sx * (hwd - 0.05f), 0.75f, hl + 0.012f}, {sx * (hwd - 0.3f), 0.75f, hl + 0.012f}, {sx * (hwd - 0.3f), 0.95f, hl + 0.012f},
                       {sx * (hwd - 0.05f), 0.95f, hl + 0.012f}, {0, 0, 1}, {255, 150, 20, 255}, 0.8f, kGlow);
            }
            continue;
        }
        // Autos: carrocería (trompa más baja que la cola), cabina vidriada con techo pintado, ruedas, paragolpes, espejos.
        struct P {
            float bot, belt, nose, tail, cz0, cz1, rz0, rz1, roof, wr, ax;
            bool van;
        };
        const P pr[5] = {{0.28f, 0.84f, 0.72f, 0.86f, -1.55f, 1.05f, -0.95f, 0.35f, 1.44f, 0.32f, 1.38f, false},
                         {0.30f, 0.86f, 0.74f, 0.92f, -1.85f, 0.90f, -1.72f, 0.22f, 1.49f, 0.31f, 1.25f, false},
                         {0.40f, 1.02f, 0.95f, 1.05f, -2.15f, 0.95f, -2.05f, 0.20f, 1.72f, 0.37f, 1.42f, false},
                         {0.42f, 1.05f, 0.98f, 1.02f, -0.60f, 1.05f, -0.45f, 0.40f, 1.80f, 0.38f, 1.62f, false},
                         {0.36f, 1.05f, 0.98f, 1.05f, -2.55f, 1.75f, -2.55f, 0.95f, 2.36f, 0.34f, 1.65f, true}};
        const P& p = pr[ki];
        use(km.paint);
        {
            const Vector3 lo[4] = {{-hwd, p.bot, -hl}, {hwd, p.bot, -hl}, {hwd, p.bot, hl}, {-hwd, p.bot, hl}};
            const Vector3 hi[4] = {{-hwd + 0.03f, p.tail, -hl}, {hwd - 0.03f, p.tail, -hl}, {hwd - 0.03f, p.nose, hl}, {-hwd + 0.03f, p.nose, hl}};
            b.Hexa(lo, hi, white, 0.55f, 0x1F);
        }
        // Cabina: costados de vidrio (la del utilitario, de chapa con el parabrisas), techo pintado.
        const float cw0 = hwd - 0.05f, cw1 = hwd - (p.van ? 0.08f : 0.22f);
        const float cy0 = std::min(p.nose, p.tail) - 0.02f;
        const Vector3 clo[4] = {{-cw0, cy0, p.cz0}, {cw0, cy0, p.cz0}, {cw0, cy0, p.cz1}, {-cw0, cy0, p.cz1}};
        const Vector3 chi[4] = {{-cw1, p.roof, p.rz0}, {cw1, p.roof, p.rz0}, {cw1, p.roof, p.rz1}, {-cw1, p.roof, p.rz1}};
        b.Hexa(clo, chi, white, 0.55f, 0x10);                       // techo
        if (p.van) {
            b.Hexa(clo, chi, white, 0.55f, 0x0B);                   // atrás y costados de chapa
            use(km.always);
            b.Hexa(clo, chi, glass, 0.9f, 0x04);                    // parabrisas
            // Ventanillas de la cabina, 1 cm afuera de la chapa.
            for (float sx : {-1.0f, 1.0f}) {
                const float x = sx * (cw0 + 0.012f);
                b.Quad({x, cy0 + 0.15f, p.cz1 - 1.0f}, {x, cy0 + 0.15f, p.cz1 - 0.15f}, {x, cy0 + 0.75f, p.cz1 - 0.35f}, {x, cy0 + 0.75f, p.cz1 - 1.0f}, {sx, 0, 0},
                       glass, 0.9f);
            }
        } else {
            use(km.always);
            b.Hexa(clo, chi, glass, 0.9f, 0x0F);
        }
        if (ki == 3) {                                               // pickup: cubre caja oscuro 1 cm arriba de la caja
            use(km.always);
            b.Quad({-hwd + 0.06f, p.tail + 0.012f, -hl + 0.05f}, {hwd - 0.06f, p.tail + 0.012f, -hl + 0.05f}, {hwd - 0.06f, p.tail + 0.012f, p.cz0 - 0.05f},
                   {-hwd + 0.06f, p.tail + 0.012f, p.cz0 - 0.05f}, {0, 1, 0}, {44, 44, 48, 255}, 0.3f);
        }
        use(km.nearOnly);
        for (float z : {p.ax, -p.ax})
            for (float sx : {-1.0f, 1.0f}) b.Wheel({sx * (hwd - 0.14f), p.wr, z}, p.wr, 0.23f);
        b.Box({-hwd + 0.02f, p.bot - 0.02f, hl - 0.05f}, {hwd - 0.02f, p.bot + 0.24f, hl + 0.03f}, trim, 0.2f);
        b.Box({-hwd + 0.02f, p.bot - 0.02f, -hl - 0.03f}, {hwd - 0.02f, p.bot + 0.24f, -hl + 0.05f}, trim, 0.2f);
        for (float sx : {-1.0f, 1.0f}) b.Box({sx > 0 ? hwd - 0.02f : -hwd - 0.16f, cy0 + 0.04f, p.cz1 - 0.25f}, {sx > 0 ? hwd + 0.16f : -hwd + 0.02f, cy0 + 0.2f, p.cz1 - 0.1f}, trim, 0.3f);
        // Luces 1.2 cm afuera de la carrocería.
        const float fz = hl + 0.012f, rz = -hl - 0.012f;
        const float hy0 = p.nose - 0.16f, hy1 = p.nose - 0.05f, ty0 = p.tail - 0.2f, ty1 = p.tail - 0.06f;
        use(km.head);
        for (float sx : {-1.0f, 1.0f})
            b.Quad({sx * (hwd - 0.36f), hy0, fz}, {sx * (hwd - 0.1f), hy0, fz}, {sx * (hwd - 0.1f), hy1, fz}, {sx * (hwd - 0.36f), hy1, fz}, {0, 0, 1},
                   {255, 246, 224, 255}, 0.9f, kGlow);
        for (PartMesh* lp : {&km.tail, &km.brake}) {
            use(*lp);
            const bool on = lp == &km.brake;
            for (float sx : {-1.0f, 1.0f})
                b.Quad({sx * (hwd - 0.42f), ty0, rz}, {sx * (hwd - 0.08f), ty0, rz}, {sx * (hwd - 0.08f), ty1, rz}, {sx * (hwd - 0.42f), ty1, rz}, {0, 0, -1},
                       on ? Color{255, 36, 28, 255} : Color{128, 22, 22, 255}, 0.8f, on ? kGlow : 2.0f);
        }
        for (int side = 0; side < 2; ++side) {
            use(side == 0 ? km.blinkL : km.blinkR);
            const float sx = side == 0 ? 1.0f : -1.0f;              // +X: la izquierda del auto
            // Atrás debajo de la luz; adelante al costado del faro (más abajo lo tapaba el paragolpes); y un repetidor en cada
            // guardabarros delantero, que se ve de costado.
            b.Quad({sx * (hwd - 0.08f), ty0 - 0.12f, rz}, {sx * (hwd - 0.3f), ty0 - 0.12f, rz}, {sx * (hwd - 0.3f), ty0 - 0.02f, rz},
                   {sx * (hwd - 0.08f), ty0 - 0.02f, rz}, {0, 0, -1}, {255, 150, 20, 255}, 0.8f, kGlow);
            b.Quad({sx * (hwd - 0.01f), hy0, fz}, {sx * (hwd - 0.1f), hy0, fz}, {sx * (hwd - 0.1f), hy1, fz}, {sx * (hwd - 0.01f), hy1, fz}, {0, 0, 1},
                   {255, 150, 20, 255}, 0.8f, kGlow);
            const float xs = sx * (hwd + 0.012f), zr = p.ax + p.wr + 0.15f, yr = 0.5f * (p.bot + p.nose) + 0.05f;
            b.Quad({xs, yr - 0.03f, zr}, {xs, yr - 0.03f, zr + 0.16f}, {xs, yr + 0.03f, zr + 0.16f}, {xs, yr + 0.03f, zr}, {sx, 0, 0},
                   {255, 150, 20, 255}, 0.8f, kGlow);
        }
    }
}

void Traffic::CreateMeshes()
{
    Unload();
    if (!Active()) return;
    BuildMeshKinds();
    int most = 0;
    for (const KindMesh& km : kindMeshes)
        most = std::max(most, (int)(km.paint.pos.size() + km.always.pos.size() + km.nearOnly.pos.size() + km.tail.pos.size() + km.head.pos.size() +
                                    km.blinkL.pos.size() + km.blinkR.pos.size()) / 3);
    // Tres mallas que se turnan cuadro a cuadro: rescribir la que la placa todavía está dibujando (la del cuadro anterior)
    // la obliga a esperar.
    batchCap = std::clamp(most * 120, 3, 60000) / 3 * 3;
    for (Mesh& batch : batches) {
        batch = Mesh{};
        batch.vertexCount = batchCap;
        batch.triangleCount = batchCap / 3;
        batch.vertices = (float*)MemAlloc((unsigned)batchCap * 3 * sizeof(float));
        batch.normals = (float*)MemAlloc((unsigned)batchCap * 3 * sizeof(float));
        batch.texcoords = (float*)MemAlloc((unsigned)batchCap * 2 * sizeof(float));
        batch.colors = (unsigned char*)MemAlloc((unsigned)batchCap * 4);
        UploadMesh(&batch, true);
    }
    meshes = true;
    batchStep = -1;
}

void Traffic::Unload()
{
    if (!meshes) return;
    for (Mesh& batch : batches) {
        UnloadMesh(batch);
        batch = Mesh{};
    }
    meshes = false;
}

void Traffic::Draw(Renderer& r, float alpha, bool shadowPass)
{
    if (!meshes || shown.empty()) return;
    const bool rebuild = batchStep != stepCount || batchAlpha != alpha;
    if (rebuild) batchIdx = (batchIdx + 1) % 3;
    Mesh& batch = batches[batchIdx];
    if (rebuild) {
        batchStep = stepCount;
        batchAlpha = alpha;
        // De los más cerca a los más lejos (la pasada de sombras dibuja sólo los primeros).
        std::vector<std::pair<float, int>> order;
        order.reserve(shown.size());
        for (size_t q = 0; q < shown.size(); ++q) order.push_back({(shown[q].cur.pos - lastFocus).Length(), (int)q});
        std::sort(order.begin(), order.end());
        int used = 0;
        batchShadow = 0;
        const float blinkPhase = (float)std::fmod(clock * 1.6, 1.0);
        auto append = [&](const PartMesh& part, const JPH::Mat44& m, const Color* tint) {
            const int nv = (int)part.pos.size() / 3;
            if (used + nv > batchCap) return;
            for (int v = 0; v < nv; ++v) {
                const Vec3 p = m * Vec3(part.pos[v * 3], part.pos[v * 3 + 1], part.pos[v * 3 + 2]);
                const Vec3 n = m.Multiply3x3(Vec3(part.nrm[v * 3], part.nrm[v * 3 + 1], part.nrm[v * 3 + 2]));
                float* P = batch.vertices + (size_t)(used + v) * 3;
                P[0] = p.GetX();
                P[1] = p.GetY();
                P[2] = p.GetZ();
                float* N = batch.normals + (size_t)(used + v) * 3;
                N[0] = n.GetX();
                N[1] = n.GetY();
                N[2] = n.GetZ();
                batch.texcoords[(used + v) * 2] = part.uv[v * 2];
                batch.texcoords[(used + v) * 2 + 1] = part.uv[v * 2 + 1];
                unsigned char* C = batch.colors + (size_t)(used + v) * 4;
                const unsigned char* src = &part.col[(size_t)v * 4];
                if (tint) {
                    C[0] = (unsigned char)(src[0] * tint->r / 255);
                    C[1] = (unsigned char)(src[1] * tint->g / 255);
                    C[2] = (unsigned char)(src[2] * tint->b / 255);
                } else {
                    C[0] = src[0];
                    C[1] = src[1];
                    C[2] = src[2];
                }
                C[3] = 255;
            }
            used += nv;
        };
        for (const auto& [d, q] : order) {
            if (d > kShowRadius) break;
            const Shown& sh = shown[(size_t)q];
            const Car& c = cars[(size_t)sh.car];
            const Vec3 pos = sh.fresh ? sh.cur.pos : sh.prev.pos + (sh.cur.pos - sh.prev.pos) * alpha;
            const Quat rot = sh.fresh ? sh.cur.rot : sh.prev.rot.SLERP(sh.cur.rot, alpha);
            const JPH::Mat44 m = JPH::Mat44::sRotationTranslation(rot, pos);
            const KindMesh& km = kindMeshes[c.kind];
            const Color paint = kPaints[c.color % kPaintCount];
            append(km.paint, m, &paint);
            append(km.always, m, nullptr);
            if (d < kNearRadius) append(km.nearOnly, m, nullptr);
            append(c.braking ? km.brake : km.tail, m, nullptr);
            if (d < 300.0f) append(km.head, m, nullptr);
            const bool signalling = c.target >= 0 && (c.signal > 0.0f || c.lc >= 0.0f);
            if (signalling && std::fmod(blinkPhase + (float)(sh.car % 7) * 0.09f, 1.0f) < 0.5f)
                append(c.target < c.lane ? km.blinkL : km.blinkR, m, nullptr);
            if (d < kShadowRadius) batchShadow = used;
        }
        batchUsed = used;
        if (used > 0) {
            UpdateMeshBuffer(batch, 0, batch.vertices, used * 3 * (int)sizeof(float), 0);
            UpdateMeshBuffer(batch, 1, batch.texcoords, used * 2 * (int)sizeof(float), 0);
            UpdateMeshBuffer(batch, 2, batch.normals, used * 3 * (int)sizeof(float), 0);
            UpdateMeshBuffer(batch, 3, batch.colors, used * 4, 0);
        }
    }
    const int count = shadowPass ? batchShadow : batchUsed;
    if (count <= 0) return;
    Mesh view = batch;
    view.vertexCount = count;
    view.triangleCount = count / 3;
    r.SetGloss(0.5f);
    r.DrawMeshColored(view, MatrixIdentity(), WHITE);
    r.SetGloss(0.3f);
}
