// Autódromo: todo lo que rodea la pista de un mapa con "generator": "circuit". Se ubica a partir del
// trazado ya armado (curvatura, distancia a los otros tramos, velocidad estimada) y del terreno, con
// semillas fijas: igual en todas las PCs. Las mallas van por bloques del mapa con el color en los
// vértices (sólidas, calcos en el piso y lo que lleva textura: carteles y banderas).
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>

#include "Circuit.h"

#include "Coplanar.h"
#include "Json.h"
#include "MathUtil.h"
#include "MeshBuilder.h"
#include "PhysicsWorld.h"
#include "Render.h"
#include "Terrain.h"
#include "Track.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <tuple>

namespace {

constexpr float kTau = 6.2831853f;
constexpr float kPlain = 2.0f, kPlaster = 4.0f;   // modos del shader iluminado (v de la UV), ver kLitFS
constexpr float kStep = 2.0f;                      // m entre muestras de los perfiles a lo largo de la pista
constexpr int kMaxVerts = 60000;                   // por malla (índices de 16 bits)

struct Rng {
    uint32_t s;
    float F()
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return (float)(s & 0xffffff) / (float)0xffffff;
    }
    float R(float a, float b) { return a + (b - a) * F(); }
    int I(int n) { return std::min(n - 1, (int)(F() * (float)n)); }
    bool Chance(float p) { return F() < p; }
};

float Hash(int x, int z)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)z * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float)(h & 0xffffff) / (float)0xffffff;
}

float ValueNoise(float x, float z)
{
    const int ix = (int)std::floor(x), iz = (int)std::floor(z);
    float fx = x - (float)ix, fz = z - (float)iz;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fz = fz * fz * (3.0f - 2.0f * fz);
    const float a = mu::Lerp(Hash(ix, iz), Hash(ix + 1, iz), fx), b = mu::Lerp(Hash(ix, iz + 1), Hash(ix + 1, iz + 1), fx);
    return mu::Lerp(a, b, fz);
}

Color Shade(Color c, float k)
{
    auto f = [k](unsigned char v) { return (unsigned char)std::min(255.0f, (float)v * k); };
    return {f(c.r), f(c.g), f(c.b), c.a};
}

// Paleta del autódromo.
const Color kWhite = {236, 236, 230, 255};
const Color kRed = {206, 36, 30, 255};
const Color kBlack = {22, 22, 24, 255};
const Color kConcrete = {172, 170, 162, 255};
const Color kPaint = {226, 226, 220, 255};        // muros pintados de blanco
const Color kSteel = {104, 108, 114, 255};
const Color kDarkSteel = {50, 53, 58, 255};
const Color kTyre = {28, 28, 30, 255};
const Color kGravel = {192, 172, 134, 255};
const Color kPitAsphalt = {72, 72, 76, 255};
const Color kGlass = {46, 64, 84, 255};
const Color kAirBlue = {28, 86, 184, 255};
const Color kTeam[] = {{200, 30, 36, 255}, {24, 70, 160, 255}, {250, 190, 20, 255}, {30, 30, 34, 255}, {24, 140, 70, 255},
                       {236, 108, 20, 255}, {120, 40, 150, 255}, {20, 160, 190, 255}, {220, 220, 214, 255}, {150, 20, 40, 255}};
constexpr int kTeams = sizeof(kTeam) / sizeof(kTeam[0]);
const Color kCrowd[] = {{210, 40, 40, 255}, {240, 240, 236, 255}, {40, 80, 170, 255}, {250, 200, 40, 255}, {30, 30, 30, 255},
                        {90, 150, 210, 255}, {230, 120, 40, 255}, {60, 140, 70, 255}, {200, 180, 150, 255}, {160, 60, 140, 255}};
constexpr int kCrowds = sizeof(kCrowd) / sizeof(kCrowd[0]);
const Color kSeat[] = {{196, 40, 40, 255}, {40, 90, 180, 255}, {230, 180, 30, 255}, {60, 150, 90, 255}};

// Atlas de carteles (1024 x 1024): 12 carteles de 512 x 128, la banda con el nombre (1024 x 128) y
// cuadrados de 128 (carteles de distancia, un blanco para lo que sólo lleva color).
constexpr int kAtlas = 1024;
constexpr int kBillboardCells = 12;
constexpr int kNameCell = 12;
constexpr int kDistanceCell = 13;                  // 13..16: 300, 200, 100, 50
constexpr int kWhiteCell = 17;
const char* const kDistanceText[] = {"300", "200", "100", "50"};

Rectangle CellPixels(int i)
{
    if (i < kBillboardCells) return {(float)(i % 2) * 512.0f, (float)(i / 2) * 128.0f, 512.0f, 128.0f};
    if (i == kNameCell) return {0.0f, 768.0f, 1024.0f, 128.0f};
    return {(float)(i - kDistanceCell) * 128.0f, 896.0f, 128.0f, 128.0f};
}

Rectangle CellUV(int i)                            // en 0..1, con un margen para que no sangre el vecino
{
    const Rectangle p = CellPixels(i);
    const float m = 3.0f;
    return {(p.x + m) / kAtlas, (p.y + m) / kAtlas, (p.width - 2 * m) / kAtlas, (p.height - 2 * m) / kAtlas};
}

// ------------------------------------------------------------------------ primitivas
// Ejes de una caja con rumbo yaw (giro alrededor de Y): +Z local -> (sin, 0, cos).
struct Frame {
    Vector3 x, y, z;
};
Frame YawFrame(float yaw) { return {{std::cos(yaw), 0.0f, -std::sin(yaw)}, {0.0f, 1.0f, 0.0f}, {std::sin(yaw), 0.0f, std::cos(yaw)}}; }

void Face(MeshBuilder& b, Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, Vector3 n, Color col, float gloss, float mode)
{
    const unsigned short i0 = b.Vertex(p0, n, {gloss, mode}, col), i1 = b.Vertex(p1, n, {gloss, mode}, col);
    const unsigned short i2 = b.Vertex(p2, n, {gloss, mode}, col), i3 = b.Vertex(p3, n, {gloss, mode}, col);
    const Vector3 face = Vector3CrossProduct(Vector3Subtract(p1, p0), Vector3Subtract(p2, p0));
    if (Vector3DotProduct(face, n) >= 0.0f) {
        b.Tri(i0, i1, i2);
        b.Tri(i0, i2, i3);
    } else {
        b.Tri(i0, i2, i1);
        b.Tri(i0, i3, i2);
    }
}

// Caja con ejes f (unitarios) y medio tamaño h. bottom: con la cara de abajo (casi nunca se ve).
void Box(MeshBuilder& b, Vector3 c, Vector3 h, const Frame& f, Color col, float gloss = 0.1f, float mode = kPlain, bool bottom = false)
{
    const Vector3 ax[3] = {f.x, f.y, f.z};
    const float he[3] = {h.x, h.y, h.z};
    for (int a = 0; a < 3; ++a)
        for (float sgn : {-1.0f, 1.0f}) {
            if (a == 1 && sgn < 0.0f && !bottom) continue;
            const int u = (a + 1) % 3, v = (a + 2) % 3;
            const Vector3 n = Vector3Scale(ax[a], sgn);
            const Vector3 fc = Vector3Add(c, Vector3Scale(ax[a], sgn * he[a]));
            const Vector3 du = Vector3Scale(ax[u], he[u]), dv = Vector3Scale(ax[v], he[v]);
            // Las caras de arriba un poco más claras, las de los costados según miran (se leen mejor de lejos).
            const Color cc = a == 1 ? Shade(col, 1.06f) : col;
            Face(b, Vector3Subtract(Vector3Subtract(fc, du), dv), Vector3Subtract(Vector3Add(fc, du), dv), Vector3Add(Vector3Add(fc, du), dv),
                 Vector3Add(Vector3Subtract(fc, du), dv), n, cc, gloss, mode);
        }
}

void YawBox(MeshBuilder& b, Vector3 c, Vector3 h, float yaw, Color col, float gloss = 0.1f, float mode = kPlain)
{
    Box(b, c, h, YawFrame(yaw), col, gloss, mode);
}

// Barra entre dos puntos (sección cuadrada de lado 2r, sin tapas).
void Bar(MeshBuilder& b, Vector3 p0, Vector3 p1, float r, Color col, float gloss = 0.3f)
{
    const Vector3 d = Vector3Subtract(p1, p0);
    const float len = Vector3Length(d);
    if (len < 1e-4f) return;
    const Vector3 z = Vector3Scale(d, 1.0f / len);
    const Vector3 x = Vector3Normalize(Vector3CrossProduct(std::fabs(z.y) < 0.9f ? Vector3{0.0f, 1.0f, 0.0f} : Vector3{1.0f, 0.0f, 0.0f}, z));
    const Vector3 y = Vector3CrossProduct(z, x);
    for (int k = 0; k < 4; ++k) {
        const Vector3 n = k == 0 ? x : (k == 1 ? y : (k == 2 ? Vector3Negate(x) : Vector3Negate(y)));
        const Vector3 t = k == 0 ? y : (k == 1 ? Vector3Negate(x) : (k == 2 ? Vector3Negate(y) : x));
        const Vector3 o = Vector3Scale(n, r), w = Vector3Scale(t, r);
        Face(b, Vector3Add(Vector3Add(p0, o), w), Vector3Add(Vector3Subtract(p0, w), o), Vector3Add(Vector3Subtract(p1, w), o),
             Vector3Add(Vector3Add(p1, o), w), n, col, gloss, kPlain);
    }
}

// Cilindro vertical (gomas, postes redondos). Base en c.
void Cylinder(MeshBuilder& b, Vector3 c, float r, float h, Color col, float gloss = 0.3f, int slices = 8)
{
    const unsigned short first = (unsigned short)b.VertexCount();
    for (int i = 0; i <= slices; ++i) {
        const float a = kTau * (float)i / (float)slices;
        const Vector3 n = {std::cos(a), 0.0f, std::sin(a)};
        b.Vertex({c.x + n.x * r, c.y, c.z + n.z * r}, n, {gloss, kPlain}, col);
        b.Vertex({c.x + n.x * r, c.y + h, c.z + n.z * r}, n, {gloss, kPlain}, col);
    }
    for (int i = 0; i < slices; ++i) {
        const unsigned short a0 = (unsigned short)(first + 2 * i), a1 = a0 + 1, b0 = a0 + 2, b1 = a0 + 3;
        b.Tri(a0, b1, b0);
        b.Tri(a0, a1, b1);
    }
    const Color top = Shade(col, 1.1f);
    const unsigned short ct = b.Vertex({c.x, c.y + h, c.z}, {0.0f, 1.0f, 0.0f}, {gloss, kPlain}, top);
    for (int i = 0; i <= slices; ++i) {
        const float a = kTau * (float)i / (float)slices;
        b.Vertex({c.x + std::cos(a) * r, c.y + h, c.z + std::sin(a) * r}, {0.0f, 1.0f, 0.0f}, {gloss, kPlain}, top);
    }
    for (int i = 0; i < slices; ++i) b.Tri(ct, (unsigned short)(ct + 2 + i), (unsigned short)(ct + 1 + i));
    b.FixWinding(b.idx.size() - (size_t)slices * 9);
}

// Cono de facetas (copas de los árboles): base en c, radio r, alto h.
void Cone(MeshBuilder& b, Vector3 c, float r, float h, Color col, int sides, float twist, bool cap = false)
{
    const size_t first = b.idx.size();
    const Vector3 tip = {c.x, c.y + h, c.z};
    for (int i = 0; i < sides; ++i) {
        const float a0 = twist + kTau * (float)i / (float)sides, a1 = twist + kTau * (float)(i + 1) / (float)sides;
        const Vector3 p0 = {c.x + std::cos(a0) * r, c.y, c.z + std::sin(a0) * r}, p1 = {c.x + std::cos(a1) * r, c.y, c.z + std::sin(a1) * r};
        const Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(p1, p0), Vector3Subtract(tip, p0)));
        const Vector3 radial = {std::cos(0.5f * (a0 + a1)), 0.0f, std::sin(0.5f * (a0 + a1))};
        const Vector3 nn = Vector3DotProduct(n, radial) < 0.0f ? Vector3Negate(n) : n;      // hacia afuera
        const Color cc = Shade(col, 0.9f + 0.2f * (0.5f + 0.5f * std::cos(a0 - 2.2f)));
        b.Tri(b.Vertex(p0, nn, {0.05f, kPlain}, cc), b.Vertex(p1, nn, {0.05f, kPlain}, cc), b.Vertex(tip, nn, {0.05f, kPlain}, cc));
        if (!cap) continue;
        // Tapa de la base (abajo, o arriba si el cono va para abajo).
        const Vector3 dn = {0.0f, h > 0.0f ? -1.0f : 1.0f, 0.0f};
        const Color under = Shade(col, 0.6f);
        b.Tri(b.Vertex(p0, dn, {0.05f, kPlain}, under), b.Vertex({c.x, c.y, c.z}, dn, {0.05f, kPlain}, under), b.Vertex(p1, dn, {0.05f, kPlain}, under));
    }
    b.FixWinding(first);
}

// Rectángulo del atlas (medio ancho hw a lo largo de right, medio alto hh hacia arriba), mirando a n.
void Sign(MeshBuilder& b, Vector3 c, Vector3 right, Vector3 n, float hw, float hh, const Rectangle& r, Color tint = WHITE)
{
    const Vector3 up = {0.0f, hh, 0.0f}, rt = Vector3Scale(right, hw);
    const unsigned short a = b.Vertex(Vector3Subtract(Vector3Subtract(c, rt), up), n, {r.x, r.y + r.height}, tint);
    const unsigned short bb = b.Vertex(Vector3Subtract(Vector3Add(c, rt), up), n, {r.x + r.width, r.y + r.height}, tint);
    const unsigned short cc = b.Vertex(Vector3Add(Vector3Add(c, rt), up), n, {r.x + r.width, r.y}, tint);
    const unsigned short d = b.Vertex(Vector3Add(Vector3Subtract(c, rt), up), n, {r.x, r.y}, tint);
    b.Tri(a, bb, cc);
    b.Tri(a, cc, d);
}

// Malla en armado de un bloque: se abre otra cuando la de ahora se llena (deque: las referencias a
// las que ya están siguen valiendo).
struct Batch {
    std::deque<MeshBuilder> parts;
    MeshBuilder& Get(int verts)
    {
        if (parts.empty() || parts.back().VertexCount() + verts > kMaxVerts) parts.emplace_back();
        return parts.back();
    }
};

} // namespace

struct Circuit::Geometry {
    std::vector<Batch> solid, decal, textured;     // por bloque del mapa
    std::vector<Batch> detail;                      // lo chiquito (la gente): sólo se dibuja cerca
    MeshBuilder backdrop;
};

// ------------------------------------------------------------------------ armado
void Circuit::Build(const Track& t, const Terrain& g, const MapDef& def)
{
    track = &t;
    terrain = &g;
    solids.clear();
    lamps.clear();
    origin = g.OriginX();
    size = g.Size();
    chunkCount = std::clamp((int)std::ceil(size / 160.0f), 1, 16);
    chunkSize = size / (float)chunkCount;

    // La sección "circuit" del mapa (todo opcional).
    name = def.name;
    pits = "auto";
    billboards = {"MOTOSIM", "CAFÉ LA CHICANA", "NEUMÁTICOS ÁPICE", "RADIO CURVA 98.7", "ACEITES VIENTO SUR", "SEGUROS LA LARGADA"};
    Json j;
    std::string err;
    if (Json::Load(def.file, j, err)) {
        const Json& c = j["circuit"];
        name = c.Str("name", name);
        pits = c.Str("pits", pits);
        const Json& bb = c["billboards"];
        if (bb.IsArray() && bb.Size() > 0) {
            billboards.clear();
            for (size_t i = 0; i < bb.Size() && (int)billboards.size() < kBillboardCells; ++i)
                if (bb[i].IsString()) billboards.push_back(bb[i].string);
        }
    }
    if (billboards.empty()) billboards.push_back(name);

    Generate(nullptr);
    built = true;
    std::printf("circuito: %d pianos, %.0f m de barreras, %.0f m2 de leca, %d tribunas, %d carteles, %d postes, %d árboles, %zu cajas "
                "(la más cerca, a %.1f m del borde)\n",
                kerbs, barrierLength, gravelArea, stands, signs, posts, trees, solids.size(), edgeMargin);
}

void Circuit::Generate(Geometry* geo)
{
    const Track& T = *track;
    const Terrain& G = *terrain;
    const float L = T.Length(), hw = T.HalfWidth();
    const int M = std::max(16, (int)std::floor(L / kStep));
    const float ds = L / (float)M;
    const float half = size * 0.5f;

    auto chunkOf = [&](float x, float z) {
        const int cx = std::clamp((int)((x - origin) / chunkSize), 0, chunkCount - 1);
        const int cz = std::clamp((int)((z - origin) / chunkSize), 0, chunkCount - 1);
        return cz * chunkCount + cx;
    };
    if (geo) {
        geo->solid.assign(chunkCount * chunkCount, Batch{});
        geo->decal.assign(chunkCount * chunkCount, Batch{});
        geo->textured.assign(chunkCount * chunkCount, Batch{});
        geo->detail.assign(chunkCount * chunkCount, Batch{});
    }
    // Dónde va la geometría de algo que está en (x, z) y usa unos `verts` vértices.
    auto S = [&](Vector3 p, int verts) -> MeshBuilder& { return geo->solid[chunkOf(p.x, p.z)].Get(verts); };
    auto D = [&](Vector3 p, int verts) -> MeshBuilder& { return geo->decal[chunkOf(p.x, p.z)].Get(verts); };
    auto X = [&](Vector3 p, int verts) -> MeshBuilder& { return geo->textured[chunkOf(p.x, p.z)].Get(verts); };
    auto Dt = [&](Vector3 p, int verts) -> MeshBuilder& { return geo->detail[chunkOf(p.x, p.z)].Get(verts); };
    auto solid = [&](Vector3 c, Vector3 h, float yaw) {
        if (!geo) solids.push_back({c, h, yaw});
    };
    const Rectangle white = CellUV(kWhiteCell);
    kerbs = stands = signs = trees = posts = 0;
    barrierLength = gravelArea = 0.0f;
    if (geo) lamps.clear();

    // ---------------------------------------------------------------- la pista muestreada
    struct Smp {
        float x, z, tx, tz, k;
    };
    std::vector<Smp> smp(M);
    for (int i = 0; i < M; ++i) {
        const TrackPoint p = T.At((float)i * ds);
        smp[i] = {p.x, p.z, p.tx, p.tz, p.curvature};
    }
    auto wrapI = [&](int i) { return ((i % M) + M) % M; };
    auto idxOf = [&](float s) { return wrapI((int)std::lround(T.Wrap(s) / ds)); };
    // Punto a una distancia lateral de la línea central (lat > 0: a la derecha), en el piso.
    auto W = [&](float s, float lat) {
        const TrackPoint p = T.At(s);
        const float x = p.x - p.tz * lat, z = p.z + p.tx * lat;
        return Vector3{x, G.Height(x, z), z};
    };
    auto yawAt = [&](float s) {
        const TrackPoint p = T.At(s);
        return std::atan2(p.tx, p.tz);
    };
    auto rightAt = [&](float s) {                   // normal a la derecha de la pista en s
        const TrackPoint p = T.At(s);
        return Vector3{-p.tz, 0.0f, p.tx};
    };
    auto fwdAt = [&](float s) {
        const TrackPoint p = T.At(s);
        return Vector3{p.tx, 0.0f, p.tz};
    };

    // Sentido del circuito: la vuelta gira 360° a un lado; el "adentro" del circuito está de ese lado.
    float turning = 0.0f;
    for (const Smp& p : smp) turning += p.k * ds;
    const float inside = turning >= 0.0f ? 1.0f : -1.0f;

    // Distancia libre a cada lado: hasta dónde se puede ir en línea recta de costado sin quedar más
    // cerca de otro tramo (o del otro lado de una curva) que de éste. Es la frontera de Voronoi de la
    // línea central: para cada muestra q, el punto c + n·l queda más cerca de q cuando
    // l > |q - c|² / (2 n·(q - c)).
    std::vector<float> clear[2];
    for (int sd = 0; sd < 2; ++sd) {
        const float side = sd == 0 ? -1.0f : 1.0f;
        clear[sd].assign(M, 400.0f);
        for (int i = 0; i < M; ++i) {
            const float nx = -smp[i].tz * side, nz = smp[i].tx * side;
            float best = 400.0f;
            for (int q = 0; q < M; ++q) {
                const float dx = smp[q].x - smp[i].x, dz = smp[q].z - smp[i].z;
                const float den = 2.0f * (nx * dx + nz * dz);
                if (den > 1e-3f) best = std::min(best, (dx * dx + dz * dz) / den);
            }
            clear[sd][i] = best;
        }
    }
    auto sideIdx = [](float side) { return side < 0.0f ? 0 : 1; };

    // Velocidad estimada de una moto rápida: lo que dejan las curvas (12 m/s² de costado), frenando
    // a 9 m/s² y acelerando a 4.5. Sirve para saber qué curvas son de frenada fuerte.
    std::vector<float> vel(M);
    for (int i = 0; i < M; ++i) vel[i] = std::min(84.0f, std::sqrt(12.0f / std::max(std::fabs(smp[i].k), 1e-4f)));
    for (int pass = 0; pass < 2; ++pass) {
        for (int k = 2 * M - 1; k >= 0; --k) {
            const int i = k % M, n = (i + 1) % M;
            vel[i] = std::min(vel[i], std::sqrt(vel[n] * vel[n] + 2.0f * 9.0f * ds));
        }
        for (int k = 0; k < 2 * M; ++k) {
            const int i = k % M, p = (i + M - 1) % M;
            vel[i] = std::min(vel[i], std::sqrt(vel[p] * vel[p] + 2.0f * 4.5f * ds));
        }
    }

    // ---------------------------------------------------------------- curvas
    // Tramos con radio de menos de 320 m, de un mismo lado. i0..i1 "desenrollados" (i1 puede pasar M).
    struct Corner {
        int i0, i1, apex;
        float k;                                    // curvatura en el vértice (con signo)
        float vIn, vMin;                            // velocidad antes de frenar y la mínima
    };
    std::vector<Corner> corners;
    {
        const float kC = 1.0f / 320.0f;
        int start = 0;
        for (int i = 0; i < M; ++i)
            if (std::fabs(smp[i].k) < std::fabs(smp[start].k)) start = i;
        int open = -1;
        float sign = 0.0f;
        for (int u = 0; u <= M; ++u) {
            const int i = (start + u) % M;
            const float k = smp[i].k;
            const bool in = u < M && std::fabs(k) > kC;
            if (open >= 0 && (!in || mu::Sign(k) != sign)) {
                corners.push_back({start + open, start + u - 1, 0, 0.0f, 0.0f, 0.0f});
                open = -1;
            }
            if (in && open < 0) {
                open = u;
                sign = mu::Sign(k);
            }
        }
        // Juntar las del mismo lado separadas por un pedacito casi recto.
        std::vector<Corner> merged;
        for (const Corner& c : corners) {
            if (!merged.empty() && c.i0 - merged.back().i1 < 7 && mu::Sign(smp[wrapI(c.i0)].k) == mu::Sign(smp[wrapI(merged.back().i0)].k))
                merged.back().i1 = c.i1;
            else
                merged.push_back(c);
        }
        corners.clear();
        for (Corner c : merged) {
            if (c.i1 - c.i0 < 3) continue;
            float best = 0.0f;
            c.vMin = 1e9f;
            for (int u = c.i0; u <= c.i1; ++u) {
                const int i = wrapI(u);
                if (std::fabs(smp[i].k) > best) {
                    best = std::fabs(smp[i].k);
                    c.apex = u;
                    c.k = smp[i].k;
                }
                c.vMin = std::min(c.vMin, vel[i]);
            }
            for (int u = c.i0 - 150; u <= c.i0; ++u) c.vIn = std::max(c.vIn, vel[wrapI(u)]);
            corners.push_back(c);
        }
    }
    auto severity = [](const Corner& c) { return c.vIn - c.vMin; };

    // ---------------------------------------------------------------- recta principal y boxes
    // La recta de la largada (curvatura casi nula alrededor de startLine), en s "desenrollado".
    const int iStart = idxOf(T.startLine);
    int ia = 0, ib = 0;
    while (ia > -M / 2 && std::fabs(smp[wrapI(iStart + ia - 1)].k) < 1.0f / 900.0f) --ia;
    while (ib < M / 2 && std::fabs(smp[wrapI(iStart + ib + 1)].k) < 1.0f / 900.0f) ++ib;
    const float sLine = T.startLine;
    const float sA = sLine + (float)ia * ds, sB = sLine + (float)ib * ds;
    const float pitSide = pits == "left" ? -1.0f : (pits == "right" ? 1.0f : -inside);
    const float standSide = -pitSide;
    // Boxes: calle de boxes de sPit0 a sPit1 (con entrada y salida en diagonal), garajes y torre.
    // La calle de boxes empieza lejos de la última curva (una moto que sale abierta no la encuentra).
    const float sPit0 = std::max(sA + 200.0f, sLine - 160.0f), sPit1 = std::min(sB - 190.0f, sLine + 430.0f);
    const bool hasPits = sPit1 - sPit0 > 200.0f;
    const int garages = hasPits ? std::clamp((int)((std::min(sPit1 - 40.0f, sLine + 180.0f) - std::max(sPit0 + 20.0f, sLine - 130.0f)) / 10.0f), 4, 30) : 0;
    const float sG0 = std::max(sPit0 + 20.0f, sLine - 130.0f), sG1 = sG0 + (float)garages * 10.0f;
    const float sSt0 = std::max(sA + 160.0f, sLine - 130.0f), sSt1 = std::min(sB - 150.0f, sLine + 150.0f);
    const bool hasStand = sSt1 - sSt0 > 60.0f && clear[sideIdx(standSide)][iStart] > hw + 40.0f;
    // Tramos de la vuelta (s desenrollado) donde no van las barreras comunes: lo reemplaza lo de la recta.
    auto inRange = [&](float s, float a, float b) { return T.Wrap(s - a) <= b - a; };
    auto pitZone = [&](float s) { return hasPits && inRange(s, sPit0 - 125.0f, sPit1 + 145.0f); };
    auto standZone = [&](float s) { return hasStand && inRange(s, sSt0 - 25.0f, sSt1 + 25.0f); };

    // ---------------------------------------------------------------- leca y barreras
    // Perfiles por muestra y por lado: dónde termina la leca y dónde está la barrera (0 = no hay).
    std::vector<float> gravel[2], barrier[2];
    std::vector<uint8_t> btype[2];                  // 0 muro, 1 gomas, 2 air fence, 3 muro alto (tribuna)
    for (int sd = 0; sd < 2; ++sd) {
        gravel[sd].assign(M, 0.0f);
        barrier[sd].assign(M, 0.0f);
        btype[sd].assign(M, 0);
    }
    std::vector<float> airZone[2] = {std::vector<float>(M, 0.0f), std::vector<float>(M, 0.0f)};
    for (const Corner& c : corners) {
        const float R = 1.0f / std::fabs(c.k), sev = severity(c);
        if (sev < 11.0f && R > 60.0f) continue;       // curva sin frenada fuerte: pasto nomás
        const int sd = sideIdx(-mu::Sign(c.k));       // afuera de la curva
        const float depth = std::clamp(12.0f + 0.8f * sev, 16.0f, 48.0f);
        const int u0 = c.i0 - 20, u1 = c.i1 + 16;
        for (int u = u0; u <= u1; ++u) {
            const int i = wrapI(u);
            if (pitZone((float)i * ds) || standZone((float)i * ds)) continue;
            const float t = mu::Smoothstep((float)u0, (float)u0 + 14.0f, (float)u) * (1.0f - mu::Smoothstep((float)u1 - 12.0f, (float)u1, (float)u));
            const float edge = hw + 3.0f + depth * t;
            gravel[sd][i] = std::max(gravel[sd][i], std::min(edge, clear[sd][i] - 8.0f));
            if (sev > 26.0f) airZone[sd][i] = 1.0f;
        }
    }
    for (int sd = 0; sd < 2; ++sd) {
        const float side = sd == 0 ? -1.0f : 1.0f;
        std::vector<float> b(M), tmp(M), wide(M, 0.0f);
        // Escapatorias anchas: 20 m de pasto en las rectas y hasta 35 m más afuera de las curvas, que
        // siguen ~150 m después de la salida (ahí es donde la moto se abre acelerando) y empiezan un poco
        // antes. La barrera queda lejos de donde termina una moto que se abrió.
        for (int i = 0; i < M; ++i) {
            const float kOut = -smp[i].k * side;        // > 0: este lado es el de afuera de la curva
            if (kOut <= 0.0f) continue;
            const float bonus = std::min(35.0f, kOut * 2000.0f);
            for (int k = -12; k <= 75; ++k) {
                const float fade = k < 0 ? 1.0f + (float)k / 12.0f : (k < 30 ? 1.0f : 1.0f - (float)(k - 30) / 45.0f);
                float& w = wide[wrapI(i + k)];
                w = std::max(w, bonus * fade);
            }
        }
        for (int i = 0; i < M; ++i) {
            float off = hw + 20.0f + wide[i];
            if (gravel[sd][i] > 0.0f) off = std::max(off, gravel[sd][i] + 4.0f);
            b[i] = off;
        }
        for (int pass = 0; pass < 2; ++pass) {          // suave a lo largo de la pista
            for (int i = 0; i < M; ++i) {
                float sum = 0.0f;
                for (int k = -8; k <= 8; ++k) sum += b[wrapI(i + k)];
                tmp[i] = sum / 17.0f;
            }
            b.swap(tmp);
        }
        for (int i = 0; i < M; ++i) {
            float kIn = 0.0f;                           // la curva más cerrada hacia este lado, cerca
            for (int k = -6; k <= 6; ++k) kIn = std::max(kIn, smp[wrapI(i + k)].k * side);
            float limit = clear[sd][i] - 3.0f;
            // Adentro de una curva: hasta 0.8 R (0.42 R en las cerradas: ahí la curvatura suavizada se queda corta).
            if (kIn > 0.0f) limit = std::min(limit, (kIn > 1.0f / 40.0f ? 0.42f : 0.8f) / kIn);
            barrier[sd][i] = limit >= hw + 7.0f ? std::min(b[i], limit) : 0.0f;
            const float s = (float)i * ds;
            if (side == pitSide && pitZone(s)) barrier[sd][i] = 0.0f;
            if (side == standSide && standZone(s)) barrier[sd][i] = std::min(hw + 9.5f, clear[sd][i] - 2.0f);
            if (barrier[sd][i] > 0.0f) gravel[sd][i] = std::min(gravel[sd][i], barrier[sd][i] - 2.5f);
        }
        // Sin pedacitos sueltos: tramos de menos de 30 m afuera.
        for (int i = 0; i < M; ++i) {
            if (barrier[sd][i] <= 0.0f || barrier[sd][wrapI(i - 1)] > 0.0f) continue;
            int n = 0;
            while (n < M && barrier[sd][wrapI(i + n)] > 0.0f) ++n;
            if (n < M && (float)n * ds < 30.0f)
                for (int k = 0; k < n; ++k) barrier[sd][wrapI(i + k)] = 0.0f;
        }
        for (int i = 0; i < M; ++i) {
            bool tyres = false, air = false;
            for (int k = -10; k <= 10 && !tyres; ++k) tyres = gravel[sd][wrapI(i + k)] > hw + 6.0f;
            for (int k = -6; k <= 6 && !air; ++k) air = airZone[sd][wrapI(i + k)] > 0.0f;
            const float s = (float)i * ds;
            btype[sd][i] = side == standSide && standZone(s) ? 3 : (air && tyres ? 2 : (tyres ? 1 : 0));
        }
    }

    // Grilla gruesa (4 m) de lo que ya está ocupado: 1 construcciones y tribunas, 2 barreras y leca. Los
    // árboles no van en ninguna; carteles y puestos sí van junto a las barreras.
    const int occN = (int)std::ceil(size / 4.0f) + 1;
    std::vector<uint8_t> occ((size_t)occN * occN, 0);
    auto occupy = [&](Vector3 c, float hx, float hz, float yaw, float margin, uint8_t bits = 1) {
        const Frame f = YawFrame(yaw);
        const float r = std::sqrt(hx * hx + hz * hz) + margin;
        const int x0 = std::max(0, (int)((c.x - r - origin) / 4.0f)), x1 = std::min(occN - 1, (int)((c.x + r - origin) / 4.0f) + 1);
        const int z0 = std::max(0, (int)((c.z - r - origin) / 4.0f)), z1 = std::min(occN - 1, (int)((c.z + r - origin) / 4.0f) + 1);
        for (int gz = z0; gz <= z1; ++gz)
            for (int gx = x0; gx <= x1; ++gx) {
                const float px = origin + gx * 4.0f - c.x, pz = origin + gz * 4.0f - c.z;
                const float u = px * f.x.x + pz * f.x.z, v = px * f.z.x + pz * f.z.z;
                if (std::fabs(u) <= hx + margin && std::fabs(v) <= hz + margin) occ[(size_t)gz * occN + gx] |= bits;
            }
    };
    auto occupied = [&](float x, float z, uint8_t bits = 3) {
        const int gx = (int)std::lround((x - origin) / 4.0f), gz = (int)std::lround((z - origin) / 4.0f);
        if (gx < 0 || gz < 0 || gx >= occN || gz >= occN) return true;
        return (occ[(size_t)gz * occN + gx] & bits) != 0;
    };

    // ---------------------------------------------------------------- calcos en el piso
    // Cuadrilátero pegado al terreno (dy arriba), con la normal hacia arriba.
    auto groundQuad = [&](Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, Color col, float gloss, float dy) {
        p0.y = G.Height(p0.x, p0.z) + dy;
        p1.y = G.Height(p1.x, p1.z) + dy;
        p2.y = G.Height(p2.x, p2.z) + dy;
        p3.y = G.Height(p3.x, p3.z) + dy;
        Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(p1, p0), Vector3Subtract(p3, p0)));
        if (n.y < 0.0f) n = Vector3Negate(n);
        Face(D(Vector3Lerp(p0, p2, 0.5f), 4), p0, p1, p2, p3, n, col, gloss, kPlain);
    };
    // Banda a lo largo de la pista entre las laterales lat0(s) y lat1(s), en tramos de hasta step m.
    auto band = [&](float s0, float s1, float step, float dy, float gloss, auto lat0, auto lat1, auto color) {
        const int n = std::max(1, (int)std::ceil((s1 - s0) / step));
        for (int q = 0; q < n; ++q) {
            const float a = s0 + (s1 - s0) * (float)q / (float)n, b = s0 + (s1 - s0) * (float)(q + 1) / (float)n;
            groundQuad(W(a, lat0(a)), W(a, lat1(a)), W(b, lat1(b)), W(b, lat0(b)), color(0.5f * (a + b), q), gloss, dy);
        }
    };

    if (geo) {
        // Líneas blancas de los bordes, toda la vuelta.
        for (float side : {-1.0f, 1.0f}) {
            band(0.0f, L, 2.0f, 0.012f, 0.25f, [&](float) { return side * (hw - 0.28f); }, [&](float) { return side * (hw - 0.08f); },
                 [&](float, int) { return kWhite; });
        }
    }

    // Pianos: adentro en el vértice (donde la curvatura pasa del 55% de la máxima) y afuera a la salida
    // (y a la entrada de las curvas cerradas). Franjas rojas y blancas de 1 m que se afinan en las puntas.
    auto kerb = [&](float s0, float s1, float side, float width) {
        if (s1 - s0 < 6.0f) return;
        ++kerbs;
        if (!geo) return;
        const float in = hw - 0.05f;
        const int n = std::max(2, (int)std::lround(s1 - s0));
        auto w = [&](float s) { return width * std::clamp(std::min(s - s0, s1 - s) / 3.0f, 0.15f, 1.0f); };
        const float lift = 0.008f * (float)(kerbs % 2);    // dos pianos que se cruzan (entrada y vértice) no quedan en el mismo plano
        for (int q = 0; q < n; ++q) {
            const float a = s0 + (s1 - s0) * (float)q / (float)n, b = s0 + (s1 - s0) * (float)(q + 1) / (float)n;
            const Color col = q % 2 ? kWhite : kRed;
            Vector3 ai = W(a, side * in), ao = W(a, side * (in + w(a))), bi = W(b, side * in), bo = W(b, side * (in + w(b)));
            ai.y += 0.014f + lift;
            bi.y += 0.014f + lift;
            ao.y += 0.06f + lift;
            bo.y += 0.06f + lift;
            MeshBuilder& mb = D(ai, 8);
            Vector3 n0 = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(ao, ai), Vector3Subtract(bi, ai)));
            if (n0.y < 0.0f) n0 = Vector3Negate(n0);
            Face(mb, ai, ao, bo, bi, n0, col, 0.3f, kPlain);
            // Canto de afuera (el piano está un poco levantado).
            const Vector3 out = Vector3Scale(rightAt(0.5f * (a + b)), side);
            Face(mb, ao, bo, Vector3{bo.x, bo.y - 0.07f, bo.z}, Vector3{ao.x, ao.y - 0.07f, ao.z}, out, Shade(col, 0.8f), 0.3f, kPlain);
        }
    };
    for (const Corner& c : corners) {
        const float R = 1.0f / std::fabs(c.k);
        if (R > 260.0f) continue;
        const float in = mu::Sign(c.k), out = -in;
        // Vértice: donde la curvatura pasa del 55% de la del vértice.
        int a = c.apex, b = c.apex;
        while (a > c.i0 && std::fabs(smp[wrapI(a - 1)].k) > 0.55f * std::fabs(c.k)) --a;
        while (b < c.i1 && std::fabs(smp[wrapI(b + 1)].k) > 0.55f * std::fabs(c.k)) ++b;
        const float sa = (float)a * ds - 4.0f, sb = (float)b * ds + 4.0f;
        kerb(sa, std::max(sb, sa + 14.0f), in, R < 60.0f ? 1.3f : 1.1f);
        // Salida: desde pasado el vértice hasta un poco después de que termina la curva.
        int e = c.apex;
        while (e < c.i1 && std::fabs(smp[wrapI(e + 1)].k) > 0.8f * std::fabs(c.k)) ++e;
        const float se0 = (float)e * ds, se1 = (float)c.i1 * ds + std::clamp(R * 0.25f, 8.0f, 24.0f);
        if (!pitZone(se1) || out != pitSide) kerb(se0, std::max(se1, se0 + 16.0f), out, R < 60.0f ? 1.6f : 1.3f);
        // Entrada de las cerradas (afuera, donde se tira la moto).
        if (R < 70.0f) kerb((float)c.i0 * ds - 14.0f, (float)(c.i0 + c.apex) * 0.5f * ds, out, 1.1f);
    }

    // Leca: arena beige afuera de las frenadas fuertes, de hw + 3 hasta donde diga el perfil.
    for (int sd = 0; sd < 2; ++sd) {
        const float side = sd == 0 ? -1.0f : 1.0f;
        for (int i = 0; i < M; ++i) {
            const int n = (i + 1) % M;
            const float g0 = gravel[sd][i], g1 = gravel[sd][n];
            const float in0 = hw + 3.0f;
            if (g0 < in0 + 2.0f || g1 < in0 + 2.0f) continue;
            const float s0 = (float)i * ds, s1 = s0 + ds;
            gravelArea += ds * 0.5f * (g0 + g1 - 2.0f * in0);
            // Subdividida de costado cada ~4 m (sigue el terreno).
            const int cols = std::max(1, (int)std::ceil(std::max(g0, g1) - in0) / 4);
            for (int q = 0; q < cols; ++q) occupy(W(s0 + 0.5f * ds, side * (in0 + (q + 0.5f) * (g0 - in0) / cols)), 2.0f, 2.0f, 0.0f, 2.0f, 2);
            if (!geo) continue;
            for (int q = 0; q < cols; ++q) {
                const float t0 = (float)q / (float)cols, t1 = (float)(q + 1) / (float)cols;
                const Vector3 p00 = W(s0, side * mu::Lerp(in0, g0, t0)), p01 = W(s0, side * mu::Lerp(in0, g0, t1));
                const Vector3 p11 = W(s1, side * mu::Lerp(in0, g1, t1)), p10 = W(s1, side * mu::Lerp(in0, g1, t0));
                const float grain = ValueNoise(p00.x * 0.2f, p00.z * 0.2f);
                Color col = Shade(kGravel, 0.9f + 0.16f * grain);
                if (q == 0) col = Shade(col, 0.93f);
                groundQuad(p00, p01, p11, p10, col, 0.02f, 0.035f);
            }
        }
    }

    // ---------------------------------------------------------------- barreras
    // Muro de cemento con alambrado (postes, el caño de arriba y dos alambres); en las escapatorias,
    // gomas apiladas adelante, o air fence en las frenadas más fuertes. Cada tramo (hasta 7 m) es una
    // caja de colisión alta: el piloto no pasa por arriba.
    Rng br{0xB0CA5u};
    for (int sd = 0; sd < 2; ++sd) {
        const float side = sd == 0 ? -1.0f : 1.0f;
        int post = 0;
        for (int i = 0; i < M;) {
            // Tramo de hasta tres muestras (y 7 m de largo) del mismo tipo.
            const int type = btype[sd][i];
            int e = i;
            while (e - i < 3 && e < M && barrier[sd][wrapI(e + 1)] > 0.0f && btype[sd][wrapI(e + 1)] == type) {
                const Vector3 pa = W((float)i * ds, side * barrier[sd][i]), pb = W((float)(e + 1) * ds, side * barrier[sd][wrapI(e + 1)]);
                if (e > i && Vector2Distance({pa.x, pa.z}, {pb.x, pb.z}) > 7.0f) break;
                ++e;
            }
            if (barrier[sd][i] <= 0.0f || e == i) {
                ++i;
                continue;
            }
            const int n = wrapI(e), i0 = i;
            const float s0 = (float)i * ds, s1 = (float)e * ds;
            i = e;
            const float front = type == 1 ? 0.75f : (type == 2 ? 1.05f : 0.0f);   // lo que va delante del muro
            const Vector3 a = W(s0, side * (barrier[sd][i0] + front)), b = W(s1, side * (barrier[sd][n] + front));
            const Vector3 fa = W(s0, side * barrier[sd][i0]), fb = W(s1, side * barrier[sd][n]);
            const float len = Vector2Distance({a.x, a.z}, {b.x, b.z});
            if (len < 0.05f) continue;
            barrierLength += len;
            const float yaw = std::atan2(b.x - a.x, b.z - a.z);
            const float y0 = std::min(a.y, b.y) - 0.3f, wall = type == 3 ? 1.3f : 1.0f;
            const Vector3 mid = Vector3Lerp(a, b, 0.5f);
            const Vector3 outDir = Vector3Scale(rightAt(0.5f * (s0 + s1)), side);
            // Colisión: el muro con el alambrado (alto: el piloto no pasa por arriba) y lo de adelante.
            solid({mid.x + outDir.x * 0.2f, y0 + 1.1f, mid.z + outDir.z * 0.2f}, {0.22f, 1.4f, len * 0.5f + 0.05f}, yaw);
            if (front > 0.0f) {
                const Vector3 fm = Vector3Lerp(fa, fb, 0.5f);
                const Vector3 c = Vector3Lerp(fm, mid, 0.5f);
                solid({c.x, y0 + 0.7f, c.z}, {front * 0.5f, 0.75f, len * 0.5f + 0.05f}, yaw);
            }
            occupy(mid, 3.0f, len * 0.5f, yaw, 1.0f, 2);
            if (!geo) continue;
            const float fenceTop = type == 3 ? 4.2f : 3.4f;
            MeshBuilder& m = S(mid, 200);
            const Vector3 c = {mid.x + outDir.x * 0.2f, y0 + (0.3f + wall) * 0.5f, mid.z + outDir.z * 0.2f};
            // Frente a la tribuna los tramos alternan blanco y cemento: los blancos, 2 cm más gruesos y más altos,
            // porque los vecinos se solapan 4 cm con las caras en el mismo plano y se pisaban en cada junta.
            const bool painted = (i0 / 3) % 2 && type == 3;
            const float thick = painted ? 0.02f : 0.0f;
            YawBox(m, {c.x, c.y + thick * 0.5f, c.z}, {0.2f + thick, (0.3f + wall + thick) * 0.5f, len * 0.5f + 0.02f}, yaw, painted ? kPaint : kConcrete, 0.08f,
                   kPlaster);
            const float top = y0 + 0.3f + wall;
            const Vector3 pa = {a.x + outDir.x * 0.2f, top, a.z + outDir.z * 0.2f}, pb = {b.x + outDir.x * 0.2f, top, b.z + outDir.z * 0.2f};
            if (post++ % 2 == 0 || len > 5.0f) Bar(m, pa, Vector3{pa.x, top + fenceTop - 0.3f, pa.z}, 0.05f, kDarkSteel, 0.4f);
            Bar(m, Vector3{pa.x, top + fenceTop - 0.35f, pa.z}, Vector3{pb.x, top + fenceTop - 0.35f, pb.z}, 0.035f, kSteel, 0.5f);
            for (float h : {0.6f, 1.7f}) {
                if (h > fenceTop - 0.6f) continue;
                Bar(m, Vector3{pa.x, top + h, pa.z}, Vector3{pb.x, top + h, pb.z}, 0.012f, kSteel, 0.5f);
            }
            if (type == 1) {                               // gomas apiladas: negras con alguna pintada
                const int stacks = std::max(1, (int)std::lround(len / 0.66f));
                for (int q = 0; q < stacks; ++q) {
                    const Vector3 p = Vector3Lerp(fa, fb, ((float)q + 0.5f) / (float)stacks);
                    const Vector3 at = {p.x + outDir.x * 0.38f, G.Height(p.x, p.z) - 0.05f, p.z + outDir.z * 0.38f};
                    const int k = (i0 * 7 + q) % 9;
                    const Color col = k == 0 ? kWhite : (k == 4 ? kRed : Shade(kTyre, br.R(0.9f, 1.15f)));
                    Cylinder(S(at, 24), at, 0.33f, 1.02f, col, 0.15f, 6);        // 3 cm debajo del borde del muro
                }
            } else if (type == 2) {                        // air fence: colchón azul con una banda blanca
                const Vector3 fm = Vector3Lerp(fa, fb, 0.5f);
                const Vector3 cc = {fm.x + outDir.x * 0.52f, y0 + 0.3f + 0.8f, fm.z + outDir.z * 0.52f};
                YawBox(m, cc, {0.5f, 0.8f + 0.3f, len * 0.5f + 0.03f}, yaw, kAirBlue, 0.25f);
                // La banda, 2 cm más ancha que el colchón de los dos lados. Antes se corría hacia la pista con outDir,
                // que no es perpendicular al tramo cuando la barrera se abre de golpe: quedaba en la cara del colchón.
                YawBox(m, {cc.x, y0 + 0.3f + 1.25f, cc.z}, {0.52f, 0.16f, len * 0.5f + 0.05f}, yaw, kWhite, 0.25f);
            }
        }
    }

    // ---------------------------------------------------------------- recta: largada, grilla, pórtico
    const float firstTurn = [&] {                   // de qué lado es la primera curva después de la largada
        for (const Corner& c : corners)
            if (T.Wrap((float)c.i0 * ds - sLine) < L * 0.5f && 1.0f / std::fabs(c.k) < 200.0f) return mu::Sign(c.k);
        return 1.0f;
    }();
    if (geo) {
        // Línea de llegada a cuadros (tres filas de 40 cm) y la grilla: 8 filas de 3, cada puesto con
        // su marca en forma de corchete, escalonados.
        const int cols = std::max(8, (int)std::lround(2.0f * hw / 0.4f));
        for (int r = 0; r < 3; ++r)
            for (int q = 0; q < cols; ++q) {
                const float l0 = -hw + 2.0f * hw * (float)q / (float)cols, l1 = -hw + 2.0f * hw * (float)(q + 1) / (float)cols;
                const float a = sLine - 0.6f + 0.4f * (float)r, b = a + 0.4f;
                groundQuad(W(a, l0), W(a, l1), W(b, l1), W(b, l0), (q + r) % 2 ? kBlack : kWhite, 0.2f, 0.022f);   // 1 cm sobre las líneas del borde
            }
        for (int slot = 0; slot < 24; ++slot) {
            const int row = slot / 3, col = slot % 3;
            const float s = sLine - 7.0f - 9.0f * (float)row - 2.0f * (float)col;
            const float lat = firstTurn * (1.0f - (float)col) * 4.3f;
            auto quad = [&](float a, float b, float l0, float l1) { groundQuad(W(a, lat + l0), W(a, lat + l1), W(b, lat + l1), W(b, lat + l0), kWhite, 0.2f, 0.015f); };
            quad(s, s + 0.14f, -0.8f, 0.8f);               // la de adelante
            quad(s - 1.1f, s, -0.8f, -0.66f);              // los costados, hacia atrás
            quad(s - 1.1f, s, 0.66f, 0.8f);
        }
    }

    // Pórtico de largada: dos columnas (una sobre el muro de boxes), la viga reticulada con el nombre y
    // el semáforo de cinco columnas de luces rojas mirando a la grilla.
    {
        const Vector3 fwd = fwdAt(sLine), right = rightAt(sLine);
        const float yaw = yawAt(sLine);
        const float latP = pitSide * (hw + 4.4f), latS = standSide * (hw + 3.2f);
        const Vector3 pa = W(sLine, latP), pb = W(sLine, latS);
        const float base = std::min(pa.y, pb.y), beamY = base + 7.4f;
        for (const Vector3& p : {pa, pb}) solid({p.x, base + 3.9f, p.z}, {0.4f, 4.2f, 0.4f}, yaw);
        if (geo) {
            MeshBuilder& m = S(pa, 900);
            for (const Vector3& p : {pa, pb}) {
                YawBox(m, {p.x, base + 3.9f, p.z}, {0.35f, 3.9f + 0.3f, 0.35f}, yaw, kDarkSteel, 0.4f);
                YawBox(m, {p.x, base + 0.25f, p.z}, {0.6f, 0.3f, 0.6f}, yaw, kConcrete, 0.1f);
            }
            const Vector3 ca = {pa.x, beamY, pa.z}, cb = {pb.x, beamY, pb.z};
            const Vector3 c = Vector3Lerp(ca, cb, 0.5f);
            const float span = Vector2Distance({pa.x, pa.z}, {pb.x, pb.z});
            // Viga reticulada: cuatro largueros y diagonales.
            for (float dy : {-0.7f, 0.7f})
                for (float dz : {-0.5f, 0.5f})
                    Bar(m, Vector3Add(ca, {fwd.x * dz, dy, fwd.z * dz}), Vector3Add(cb, {fwd.x * dz, dy, fwd.z * dz}), 0.07f, kSteel, 0.5f);
            const int bays = std::max(4, (int)(span / 1.4f));
            for (int q = 0; q < bays; ++q)
                for (float dz : {-0.5f, 0.5f}) {
                    const Vector3 p0 = Vector3Add(Vector3Lerp(ca, cb, (float)q / bays), {fwd.x * dz, -0.7f, fwd.z * dz});
                    const Vector3 p1 = Vector3Add(Vector3Lerp(ca, cb, (float)(q + 1) / bays), {fwd.x * dz, 0.7f, fwd.z * dz});
                    Bar(m, p0, p1, 0.04f, kSteel, 0.5f);
                }
            // Semáforo: caja negra sobre la pista, colgada de la viga.
            const Vector3 box = {c.x, beamY - 1.55f, c.z};
            YawBox(m, box, {2.6f, 0.75f, 0.28f}, yaw, kBlack, 0.3f);
            YawBox(m, {c.x, beamY - 0.75f, c.z}, {0.25f, 0.1f, 0.12f}, yaw, kDarkSteel, 0.3f);
            // Los carteles con el nombre, a los dos lados de la viga.
            for (float face : {-1.0f, 1.0f}) {
                const Vector3 n = Vector3Scale(fwd, face);
                // Tablero por delante de las diagonales de la viga (estaba 1 cm delante) y por debajo del larguero
                // de arriba; el cartel, 3 cm delante del tablero.
                const Vector3 at = Vector3Add({c.x, beamY + 0.05f, c.z}, Vector3Scale(n, 0.61f));
                YawBox(m, Vector3Add({c.x, beamY + 0.05f, c.z}, Vector3Scale(n, 0.56f)), {std::min(span * 0.42f, 9.0f), 0.57f, 0.02f}, yaw, kWhite, 0.2f);
                Sign(X(at, 4), at, Vector3Scale(right, -face), n, std::min(span * 0.42f, 9.0f) - 0.1f, 0.55f, CellUV(kNameCell));
            }
        }
        // Las luces (se prenden en Draw): dos filas por columna, del lado de la grilla.
        for (int col = 0; col < 5 && geo; ++col)
            for (int row = 0; row < 2; ++row) {
                const Vector3 c = Vector3Lerp(pa, pb, 0.5f);
                const float lat = ((float)col - 2.0f) * 1.0f;
                lamps.push_back({Vector3{c.x + right.x * lat - fwd.x * 0.32f, beamY - 1.25f - 0.6f * (float)row, c.z + right.z * lat - fwd.z * 0.32f}, col});
            }
    }

    // ---------------------------------------------------------------- boxes
    if (hasPits) {
        const float p = pitSide;
        // Calle de boxes (asfalto más oscuro) con la entrada y la salida en diagonal desde el borde.
        const float laneIn = hw + 6.5f, laneOut = hw + 18.5f;
        if (geo) {
            auto ramp = [&](float s0, float s1, bool entry) {
                band(s0, s1, 3.0f, 0.02f, 0.1f,
                     [&](float s) {
                         const float t = mu::Smoothstep(s0, s1, s), u = entry ? t : 1.0f - t;
                         return p * std::max(hw + 0.35f, mu::Lerp(hw - 1.0f, laneIn, u));
                     },
                     [&](float s) {
                         const float t = mu::Smoothstep(s0, s1, s), u = entry ? t : 1.0f - t;
                         return p * mu::Lerp(hw + 6.0f, laneOut, u);
                     },
                     [&](float, int) { return kPitAsphalt; });
            };
            ramp(sPit0 - 110.0f, sPit0, true);
            ramp(sPit1, sPit1 + 130.0f, false);
            band(sPit0, sPit1, 4.0f, 0.02f, 0.1f, [&](float) { return p * laneIn; }, [&](float) { return p * laneOut; }, [&](float, int) { return kPitAsphalt; });
            // Líneas: el borde del lado del muro y la que separa la vía rápida de la de trabajo.
            band(sPit0, sPit1, 4.0f, 0.03f, 0.2f, [&](float) { return p * (laneIn + 0.1f); }, [&](float) { return p * (laneIn + 0.28f); }, [&](float, int) { return kWhite; });
            // La de trazos, sólo los trazos (los huecos eran otra capa de asfalto 1 cm arriba del asfalto).
            {
                const int n = std::max(1, (int)std::ceil((sPit1 - sPit0) / 3.0f));
                for (int q = 0; q < n; q += 2) {
                    const float a = sPit0 + (sPit1 - sPit0) * (float)q / (float)n, b = sPit0 + (sPit1 - sPit0) * (float)(q + 1) / (float)n;
                    groundQuad(W(a, p * (hw + 12.4f)), W(a, p * (hw + 12.6f)), W(b, p * (hw + 12.6f)), W(b, p * (hw + 12.4f)), kWhite, 0.2f, 0.03f);
                }
            }
            // Línea de velocidad controlada a la entrada y a la salida (1 cm sobre las otras líneas, que cruza).
            for (float s : {sPit0 + 2.0f, sPit1 - 2.0f})
                groundQuad(W(s, p * laneIn), W(s, p * laneOut), W(s + 0.5f, p * laneOut), W(s + 0.5f, p * laneIn), kWhite, 0.2f, 0.04f);
        }
        // Muro de boxes con alambrado; del lado de la calle, los puestos de los equipos.
        for (float s = sPit0 + 6.0f; s < sPit1 - 6.0f; s += 6.0f) {
            const float e = std::min(s + 6.0f, sPit1 - 6.0f);
            const Vector3 a = W(s, p * (hw + 4.4f)), b = W(e, p * (hw + 4.4f));
            const Vector3 mid = Vector3Lerp(a, b, 0.5f);
            const float yaw = std::atan2(b.x - a.x, b.z - a.z), len = Vector2Distance({a.x, a.z}, {b.x, b.z});
            solid({mid.x, mid.y + 1.2f, mid.z}, {0.35f, 1.5f, len * 0.5f + 0.02f}, yaw);
            if (!geo) continue;
            MeshBuilder& m = S(mid, 250);
            YawBox(m, {mid.x, mid.y + 0.45f, mid.z}, {0.3f, 0.75f, len * 0.5f + 0.01f}, yaw, kPaint, 0.1f, kPlaster);
            // El borde rojo, apoyado arriba del muro y 3 cm más ancho (1 cm no alcanza de lejos).
            YawBox(m, {mid.x, mid.y + 1.26f, mid.z}, {0.33f, 0.06f, len * 0.5f + 0.03f}, yaw, kRed, 0.2f);
            Bar(m, {a.x, a.y + 1.2f, a.z}, {a.x, a.y + 4.2f, a.z}, 0.05f, kDarkSteel);
            for (float h : {1.9f, 2.8f, 3.7f}) Bar(m, {a.x, a.y + h, a.z}, {b.x, b.y + h, b.z}, 0.012f, kSteel);
            Bar(m, {a.x, a.y + 4.15f, a.z}, {b.x, b.y + 4.15f, b.z}, 0.035f, kSteel);
            if ((int)((s - sPit0) / 6.0f) % 2 == 0) {        // puesto: mesa con techito, del color del equipo
                const Color team = kTeam[(int)((s - sPit0) / 12.0f) % kTeams];
                const Vector3 st = W(s + 3.0f, p * (hw + 5.4f));
                YawBox(m, {st.x, st.y + 0.55f, st.z}, {0.55f, 0.55f, 1.6f}, yaw, kDarkSteel, 0.3f);
                YawBox(m, {st.x, st.y + 2.6f, st.z}, {0.8f, 0.06f, 1.8f}, yaw, team, 0.3f);
                Bar(m, {st.x, st.y + 1.1f, st.z}, {st.x, st.y + 2.55f, st.z}, 0.04f, kDarkSteel);
            }
        }
        // Garajes: módulos de 10 m (portón, dintel del color del equipo, pilares), piso de arriba vidriado,
        // alero sobre la calle de boxes y el cartel con el nombre. Al final, la torre de control.
        const float w0 = hw + 20.0f, w1 = hw + 36.0f;
        for (int q = 0; q < garages; ++q) {
            const float s0 = sG0 + 10.0f * (float)q, sc = s0 + 5.0f;
            const float yaw = yawAt(sc);
            const Vector3 fr = W(sc, p * w0), bk = W(sc, p * w1);
            const float y0 = std::min({fr.y, bk.y, W(s0, p * w0).y, W(s0 + 10.0f, p * w0).y}) - 0.2f;
            const Vector3 mid = Vector3Lerp(fr, bk, 0.5f);
            solid({mid.x, y0 + 5.9f, mid.z}, {8.0f, 5.9f, 5.0f}, yaw);
            occupy(mid, 8.0f, 5.0f, yaw, 3.0f);
            if (!geo) continue;
            MeshBuilder& m = S(mid, 900);
            const Color team = kTeam[(q / 2) % kTeams];
            auto at = [&](float w, float y) { const Vector3 v = W(sc, p * w); return Vector3{v.x, y0 + y, v.z}; };
            // Cada módulo 3 cm más corto de cada lado: la recta de boxes es una curva suave, así que los módulos
            // vecinos quedan girados apenas y, tocándose justo, se solapaban en una cuña con caras casi en el mismo
            // plano y de colores distintos (bordes dentados que titilaban).
            YawBox(m, at(w0 + 8.75f, 3.7f), {7.25f, 3.5f, 4.97f}, yaw, kPaint, 0.08f, kPlaster);            // cuerpo
            // El fondo oscuro, 5 cm delante del frente del cuerpo: con la cara justo en el mismo plano, blanco y
            // gris oscuro se pisaban en toda la abertura del garaje.
            YawBox(m, at(w0 + 1.47f, 2.35f), {0.02f, 2.15f, 4.2f}, yaw, {34, 36, 40, 255}, 0.05f);        // adentro del garaje
            YawBox(m, at(w0 + 1.4f, 4.05f), {0.05f, 0.45f, 4.2f}, yaw, {196, 198, 200, 255}, 0.3f);       // portón a medio subir
            YawBox(m, at(w0 + 0.75f, 5.85f), {0.75f, 1.35f, 4.97f}, yaw, team, 0.25f);                     // dintel
            YawBox(m, at(w0 + 0.7f, 5.3f), {0.76f, 0.12f, 5.0f}, yaw, kWhite, 0.25f);
            for (float dz : {-4.6f, 4.6f}) {             // pilares: hasta abajo del dintel (se metían 10 cm con la cara en su plano)
                const Vector3 f = fwdAt(sc);
                YawBox(m, Vector3Add(at(w0 + 0.75f, 2.25f), {f.x * dz, 0.0f, f.z * dz}), {0.75f, 2.25f, 0.4f}, yaw, kPaint, 0.08f, kPlaster);
            }
            YawBox(m, at(w0 + 9.25f, 9.0f), {6.75f, 1.8f, 4.97f}, yaw, kGlass, 0.85f);                    // piso de arriba vidriado
            YawBox(m, at(w0 - 0.4f, 7.35f), {2.1f, 0.15f, 4.97f}, yaw, kPaint, 0.1f);                     // balcón
            YawBox(m, at(w0 - 2.4f, 7.9f), {0.05f, 0.45f, 4.95f}, yaw, {160, 190, 210, 255}, 0.9f);        // baranda de vidrio
            // Alero y techo, y adelante la franja del color del equipo. Antes el alero, una tapa blanca y la franja
            // tenían el frente en el mismo plano (se pisaban los tres); ahora la franja va pegada delante del alero.
            YawBox(m, at(w0 + 6.05f, 11.1f), {9.95f, 0.3f, 4.99f}, yaw, {150, 154, 160, 255}, 0.2f);
            YawBox(m, at(w0 + 10.0f + (float)(q % 3), 11.75f), {1.1f, 0.4f, 1.3f}, yaw, {120, 124, 130, 255}, 0.3f);   // equipos del aire
            YawBox(m, at(w0 - 3.95f, 11.1f), {0.05f, 0.34f, 5.0f}, yaw, team, 0.3f);
        }
        if (garages > 0) {
            // Cartel con el nombre sobre el alero, mirando a la pista (cada 30 m).
            for (float s = sG0 + 15.0f; s + 15.0f <= sG1 + 0.1f; s += 30.0f) {
                const Vector3 fr = W(s, p * (w0 - 4.1f));
                const float y0 = std::min(fr.y, W(s, p * w0).y) - 0.2f;
                const Vector3 n = Vector3Scale(rightAt(s), -p);
                const Vector3 c = {fr.x, y0 + 12.7f, fr.z};
                if (geo) {
                    YawBox(S(c, 30), Vector3Add(c, Vector3Scale(n, -0.08f)), {0.06f, 1.35f, 10.8f}, yawAt(s), kWhite, 0.2f);
                    Sign(X(c, 4), Vector3Add(c, Vector3Scale(n, 0.02f)), Vector3Scale(fwdAt(s), p < 0.0f ? 1.0f : -1.0f), n, 10.6f, 1.3f, CellUV(kNameCell));
                }
                ++signs;
            }
            // Torre de control.
            const float st = sG1 + 12.0f, yaw = yawAt(st);
            const Vector3 c0 = W(st, p * (w0 + 7.0f));
            const float y0 = c0.y - 0.2f;
            solid({c0.x, y0 + 11.0f, c0.z}, {7.0f, 11.0f, 7.0f}, yaw);
            occupy(c0, 7.0f, 7.0f, yaw, 3.0f);
            if (geo) {
                MeshBuilder& m = S(c0, 300);
                YawBox(m, {c0.x, y0 + 7.0f, c0.z}, {6.5f, 7.0f, 6.5f}, yaw, kPaint, 0.08f, kPlaster);
                YawBox(m, {c0.x, y0 + 16.5f, c0.z}, {7.2f, 2.5f, 7.2f}, yaw, kGlass, 0.9f);
                YawBox(m, {c0.x, y0 + 19.3f, c0.z}, {7.8f, 0.3f, 7.8f}, yaw, {150, 154, 160, 255}, 0.2f);
                YawBox(m, {c0.x, y0 + 14.1f, c0.z}, {7.25f, 0.12f, 7.25f}, yaw, kRed, 0.2f);
                Bar(m, {c0.x, y0 + 19.6f, c0.z}, {c0.x, y0 + 25.0f, c0.z}, 0.06f, kSteel);   // antena
            }
            // Paddock: asfalto detrás de los garajes, camiones de los equipos y motorhomes.
            if (geo)
                band(sG0 - 20.0f, sG1 + 30.0f, 6.0f, 0.02f, 0.1f, [&](float) { return p * w1; }, [&](float) { return p * (w1 + 44.0f); },
                     [&](float, int) { return Shade(kPitAsphalt, 1.08f); });
            Rng tr{0x7A11E5u};
            int homes = 0;
            for (float s = sG0 + 6.0f; s < sG1 - 4.0f; s += 11.0f) {
                const Color team = kTeam[tr.I(kTeams)];
                const float yaw = yawAt(s) + mu::kPi * 0.5f;          // cruzados: la cola hacia los garajes
                const Vector3 tc = W(s, p * (w1 + 12.0f));
                const float y0 = tc.y;
                solid({tc.x, y0 + 2.0f, tc.z}, {1.3f, 2.0f, 8.8f}, yaw);
                occupy(tc, 1.4f, 9.0f, yaw, 2.0f);
                if (geo) {
                    MeshBuilder& m = S(tc, 200);
                    const Frame f = YawFrame(yaw);
                    const Vector3 along = Vector3Scale(f.z, p);          // de la cola a la cabina
                    YawBox(m, {tc.x - along.x * 1.2f, y0 + 2.25f, tc.z - along.z * 1.2f}, {1.3f, 1.75f, 7.5f}, yaw, team, 0.45f);
                    YawBox(m, {tc.x - along.x * 1.2f, y0 + 2.3f, tc.z - along.z * 1.2f}, {1.32f, 0.25f, 7.52f}, yaw, kWhite, 0.45f);
                    YawBox(m, {tc.x + along.x * 7.4f, y0 + 1.7f, tc.z + along.z * 7.4f}, {1.25f, 1.3f, 1.2f}, yaw, Shade(team, 0.8f), 0.6f);
                    YawBox(m, {tc.x + along.x * 7.9f, y0 + 2.2f, tc.z + along.z * 7.9f}, {1.2f, 0.45f, 0.72f}, yaw, kGlass, 0.9f);
                    for (float d : {-5.0f, 4.0f, 7.0f})
                        YawBox(m, {tc.x + along.x * d, y0 + 0.45f, tc.z + along.z * d}, {1.2f, 0.45f, 0.5f}, yaw, kBlack, 0.1f);
                }
                if (tr.Chance(0.7f)) {                              // motorhome más atrás
                    const Vector3 mc = W(s + 2.0f, p * (w1 + 32.0f));
                    const float myaw = yawAt(s);
                    solid({mc.x, mc.y + 1.8f, mc.z}, {1.3f, 1.8f, 6.0f}, myaw);
                    occupy(mc, 1.4f, 6.2f, myaw, 2.0f);
                    if (geo) {
                        // Van cada 11 m y miden 12: los vecinos se solapan 1 m. La franja del equipo alterna 2 cm de
                        // ancho y de alto entre uno y otro para que las de colores distintos no queden en el mismo plano.
                        const float alt = 0.02f * (float)(homes % 2);
                        MeshBuilder& m = S(mc, 100);
                        YawBox(m, {mc.x, mc.y + 1.95f, mc.z}, {1.3f, 1.55f, 6.0f}, myaw, {226, 226, 222, 255}, 0.5f);
                        YawBox(m, {mc.x, mc.y + 2.3f, mc.z}, {1.32f, 0.35f, 5.4f}, myaw, kGlass, 0.9f);
                        YawBox(m, {mc.x, mc.y + 1.2f, mc.z}, {1.33f + alt, 0.12f + alt, 6.02f}, myaw, team, 0.4f);
                    }
                    ++homes;
                }
            }
            occupy(W(0.5f * (sG0 + sG1), p * (w1 + 22.0f)), 26.0f, 0.5f * (sG1 - sG0) + 30.0f, yawAt(0.5f * (sG0 + sG1)), 4.0f);
        }
        // Entre la pista y los garajes no van árboles.
        for (float s = sPit0 - 110.0f; s < sPit1 + 130.0f; s += 10.0f) occupy(W(s, p * (hw + 12.0f)), 12.0f, 6.0f, yawAt(s), 2.0f);
    }

    // ---------------------------------------------------------------- tribunas
    // Un módulo de tribuna entre los puntos del frente A y B (en el piso), con las filas hacia `out`:
    // escalones de cemento, gente de colores (60%), paredes de los costados y atrás, y techo opcional.
    Rng crowd{0xFA15u};
    auto standModule = [&](Vector3 A, Vector3 B, Vector3 out, int rows, bool roof, Color seat) {
        const float len = Vector2Distance({A.x, A.z}, {B.x, B.z});
        if (len < 3.0f) return;
        const float yaw = std::atan2(B.x - A.x, B.z - A.z);
        const Vector3 along = Vector3Normalize({B.x - A.x, 0.0f, B.z - A.z});
        // out perpendicular a lo largo (por si el frente no es derecho).
        out = Vector3Normalize(Vector3Subtract(out, Vector3Scale(along, Vector3DotProduct(out, along))));
        const float depth = 0.85f, rise = 0.48f, h0 = 1.4f;
        const float total = depth * (float)rows;
        const Vector3 mid = Vector3Lerp(A, B, 0.5f);
        float y0 = std::min(A.y, B.y);
        for (const Vector3& q : {Vector3Add(A, Vector3Scale(out, total)), Vector3Add(B, Vector3Scale(out, total))}) y0 = std::min(y0, G.Height(q.x, q.z));
        y0 -= 0.3f;
        auto at = [&](float w, float y) { return Vector3{mid.x + out.x * w, y0 + y, mid.z + out.z * w}; };
        const float topY = h0 + rise * (float)rows;
        solid(at(total * 0.25f, (h0 + rise * rows * 0.5f) * 0.5f), {total * 0.25f, (h0 + rise * rows * 0.5f) * 0.5f, len * 0.5f}, yaw);
        solid(at(total * 0.75f, topY * 0.5f), {total * 0.25f, topY * 0.5f, len * 0.5f}, yaw);
        occupy(at(total * 0.5f, 0.0f), total * 0.5f + 2.0f, len * 0.5f, yaw, 3.0f);
        ++stands;
        if (!geo) return;
        MeshBuilder& m = S(mid, 600 + rows * 40);
        // Muro blanco del frente, 4 cm delante del primer escalón: con el frente en el mismo plano que el
        // escalón (que es más alto y lo contiene), blanco y cemento se pisaban en toda la base de la tribuna.
        YawBox(m, at(0.11f, h0 * 0.5f + 0.2f), {0.15f, h0 * 0.5f + 0.2f, len * 0.5f - 0.05f}, yaw, kPaint, 0.08f, kPlaster);
        for (int r = 0; r < rows; ++r) {
            const float w = depth * ((float)r + 0.5f), top = h0 + rise * (float)(r + 1);
            YawBox(m, at(w, top * 0.5f), {depth * 0.5f, top * 0.5f, len * 0.5f}, yaw, r % 2 ? kConcrete : Shade(kConcrete, 0.93f), 0.06f, kPlaster);
            YawBox(m, at(w + depth * 0.2f, top + 0.12f), {0.12f, 0.12f, len * 0.5f - 0.05f}, yaw, seat, 0.3f);     // butacas
        }
        YawBox(m, at(total + 0.15f, (topY + 2.4f) * 0.5f), {0.15f, (topY + 2.4f) * 0.5f, len * 0.5f}, yaw, kPaint, 0.08f, kPlaster);
        for (float e : {-0.5f, 0.5f}) {                                 // paredes de los costados
            const Vector3 c = Vector3Add(at(total * 0.5f, topY * 0.5f + 0.4f), Vector3Scale(along, e * len));
            YawBox(m, c, {total * 0.5f + 0.15f, topY * 0.5f + 0.4f, 0.15f}, yaw, kPaint, 0.08f, kPlaster);
        }
        // La gente: una cajita por lugar, 60% ocupado, sentados (en la mitad de adelante del escalón).
        const int places = std::max(1, (int)(len / 0.75f));
        for (int r = 0; r < rows; ++r)
            for (int q = 0; q < places; ++q) {
                if (!crowd.Chance(0.6f)) continue;
                const float w = depth * ((float)r + 0.45f), top = h0 + rise * (float)(r + 1);
                const float u = -0.5f * len + (len * ((float)q + 0.5f)) / (float)places + crowd.R(-0.12f, 0.12f);
                const Vector3 c = Vector3Add(at(w, top + 0.42f + crowd.R(-0.05f, 0.08f)), Vector3Scale(along, u));
                MeshBuilder& pm = Dt(c, 40);
                YawBox(pm, c, {0.2f, 0.36f, 0.2f}, yaw, kCrowd[crowd.I(kCrowds)], 0.1f);
                YawBox(pm, {c.x, c.y + 0.46f, c.z}, {0.11f, 0.12f, 0.11f}, yaw, {206, 160, 126, 255}, 0.1f);
            }
        if (roof) {                                                    // techo en voladizo sobre columnas de atrás
            // Los techos de módulos vecinos se pasan 0.2 m y en las curvas se solapan: alternan 4 cm de altura
            // para no quedar en el mismo plano (desde la pista se veía la cara de abajo pisándose).
            const float ry = topY + 3.6f + 0.04f * (float)(stands % 2);
            for (float e : {-0.45f, 0.0f, 0.45f}) {
                const Vector3 c = Vector3Add(at(total + 0.5f, ry * 0.5f), Vector3Scale(along, e * len));
                YawBox(m, c, {0.25f, ry * 0.5f, 0.25f}, yaw, kSteel, 0.4f);
            }
            // Con la cara de abajo (se ve desde la pista; sin ella el techo era transparente desde abajo). La
            // franja roja, 6 cm delante del techo y 3 cm más larga (antes 1 cm, y las puntas en su mismo plano).
            Box(m, at(total * 0.5f + 0.4f, ry), {total * 0.5f + 1.4f, 0.22f, len * 0.5f + 0.2f}, YawFrame(yaw), {176, 180, 188, 255}, 0.4f, kPlain, true);
            YawBox(m, at(-1.0f, ry - 0.3f), {0.06f, 0.5f, len * 0.5f + 0.23f}, yaw, kRed, 0.3f);
        }
    };
    // La principal, frente a los boxes (módulos de 20 m con pasillos de 1.5 m).
    if (hasStand) {
        const float st = standSide;
        for (float s = sSt0; s + 12.0f <= sSt1; s += 21.5f) {
            const float e = std::min(s + 20.0f, sSt1);
            standModule(W(s, st * (hw + 13.0f)), W(e, st * (hw + 13.0f)), Vector3Scale(rightAt(0.5f * (s + e)), st), 14, true, kSeat[1]);
        }
        occupy(W(0.5f * (sSt0 + sSt1), st * (hw + 22.0f)), 14.0f, 0.5f * (sSt1 - sSt0) + 10.0f, yawAt(0.5f * (sSt0 + sSt1)), 3.0f);
    }
    // En las curvas más frenadas (hasta tres), afuera, detrás de la barrera: módulos que siguen la curva.
    {
        std::vector<const Corner*> order;
        for (const Corner& c : corners) order.push_back(&c);
        std::sort(order.begin(), order.end(), [&](const Corner* a, const Corner* b) { return severity(*a) > severity(*b); });
        int placed = 0;
        for (const Corner* c : order) {
            if (placed >= 3 || severity(*c) < 14.0f) break;
            const float side = -mu::Sign(c->k);
            const int sd = sideIdx(side);
            const float sApex = (float)c->apex * ds;
            if (pitZone(sApex) || standZone(sApex)) continue;
            // Frente de la tribuna: 5 m detrás de la barrera; tiene que haber lugar para las filas.
            std::vector<Vector3> front;
            std::vector<float> fs;
            bool ok = true;
            for (float s = sApex - 50.0f; s <= sApex + 60.0f; s += 3.0f) {
                const int i = idxOf(s);
                const float b = barrier[sd][i];
                const Vector3 p = W(s, side * (b + 5.0f));
                if (b <= 0.0f || clear[sd][i] < b + 5.0f + 9.0f || std::fabs(p.x) > half - 30.0f || std::fabs(p.z) > half - 30.0f) {
                    if (!front.empty()) break;
                    continue;
                }
                front.push_back(p);
                fs.push_back(s);
            }
            if (front.size() < 6) ok = false;
            if (!ok) continue;
            size_t a = 0;
            for (size_t k = 1; k < front.size(); ++k) {
                const float chord = Vector2Distance({front[a].x, front[a].z}, {front[k].x, front[k].z});
                if (chord < 12.0f && k + 1 < front.size()) continue;
                if (chord > 6.0f) standModule(front[a], front[k], Vector3Scale(rightAt(0.5f * (fs[a] + fs[k])), side), 9, placed == 0, kSeat[(placed + 2) % 4]);
                a = k;
            }
            ++placed;
        }
    }

    // ---------------------------------------------------------------- carteles
    Rng sr{0x5161u};
    std::vector<Vector3> taken;                      // carteles, puestos y postes ya puestos (no encimar)
    auto freeSpot = [&](Vector3 p, float r) {
        for (const Vector3& q : taken)
            if ((q.x - p.x) * (q.x - p.x) + (q.z - p.z) * (q.z - p.z) < r * r) return false;
        return !occupied(p.x, p.z, 1);
    };
    // Cartel de 8 x 2 m con dos patas, mirando a la pista, detrás de la barrera.
    auto billboard = [&](float s, float side, int cell) {
        const int sd = sideIdx(side);
        const float b = barrier[sd][idxOf(s)];
        if (b <= 0.0f) return false;
        const Vector3 c = W(s, side * (b + 1.6f));
        if (!freeSpot(c, 14.0f) || clear[sd][idxOf(s)] < b + 5.0f) return false;
        const float yaw = yawAt(s);
        const Vector3 n = Vector3Scale(rightAt(s), -side), along = fwdAt(s);
        solid({c.x, c.y + 1.6f, c.z}, {0.15f, 1.6f, 4.0f}, yaw);
        taken.push_back(c);
        ++signs;
        if (!geo) return true;
        MeshBuilder& m = S(c, 120);
        YawBox(m, {c.x, c.y + 2.0f, c.z}, {0.08f, 1.05f, 4.1f}, yaw, {70, 72, 78, 255}, 0.2f);
        for (float e : {-3.2f, 3.2f}) YawBox(m, {c.x + along.x * e, c.y + 0.5f, c.z + along.z * e}, {0.06f, 0.5f, 0.06f}, yaw, kDarkSteel, 0.3f);
        const Vector3 face = Vector3Add({c.x, c.y + 2.0f, c.z}, Vector3Scale(n, 0.11f));   // 3 cm delante del tablero
        Sign(X(face, 4), face, Vector3Scale(along, -side), n, 4.0f, 1.0f, CellUV(cell));
        return true;
    };
    {
        int cell = 0;
        for (float s = 30.0f; s < L; s += 64.0f) {
            float kmax = 0.0f;
            for (float d = -6.0f; d <= 6.0f; d += 2.0f) kmax = std::max(kmax, std::fabs(T.At(s + d).curvature));
            if (kmax > 1.0f / 220.0f || pitZone(s) || standZone(s)) continue;
            const float first = sr.Chance(0.7f) ? -inside : inside;
            if (billboard(s, first, cell % (int)billboards.size()) || billboard(s, -first, cell % (int)billboards.size())) ++cell;
        }
    }
    // Carteles de distancia (300, 200, 100, 50 m) antes de las dos frenadas más fuertes, del lado de afuera.
    {
        std::vector<const Corner*> order;
        for (const Corner& c : corners) order.push_back(&c);
        std::sort(order.begin(), order.end(), [&](const Corner* a, const Corner* b) { return severity(*a) > severity(*b); });
        for (size_t k = 0; k < order.size() && k < 2; ++k) {
            const Corner& c = *order[k];
            const float side = -mu::Sign(c.k), sTurn = (float)c.i0 * ds;
            for (int d = 0; d < 4; ++d) {
                const float s = sTurn - (d == 0 ? 300.0f : (d == 1 ? 200.0f : (d == 2 ? 100.0f : 50.0f)));
                if (std::fabs(T.At(s).curvature) > 1.0f / 400.0f) continue;
                const Vector3 c0 = W(s, side * (hw + 5.5f));
                if (!freeSpot(c0, 3.0f)) continue;
                const float yaw = yawAt(s);
                solid({c0.x, c0.y + 1.1f, c0.z}, {0.1f, 1.1f, 0.1f}, yaw);
                taken.push_back(c0);
                ++signs;
                if (!geo) continue;
                const Vector3 back = Vector3Scale(fwdAt(s), -1.0f);       // mira a los que vienen
                MeshBuilder& m = S(c0, 60);
                Bar(m, c0, {c0.x, c0.y + 1.3f, c0.z}, 0.05f, kDarkSteel);
                YawBox(m, {c0.x, c0.y + 1.95f, c0.z}, {0.8f, 0.8f, 0.05f}, yaw, kWhite, 0.2f);
                const Vector3 face = Vector3Add({c0.x, c0.y + 1.95f, c0.z}, Vector3Scale(back, 0.08f));
                Sign(X(face, 4), face, rightAt(s), back, 0.75f, 0.75f, CellUV(kDistanceCell + d));
            }
        }
    }

    // ---------------------------------------------------------------- puente de publicidad
    // Sobre la recta más larga después de la principal: columnas afuera de la pista y un tablero con carteles.
    {
        float bestLen = 0.0f, bestS = -1.0f;
        for (size_t k = 0; k < corners.size(); ++k) {
            const Corner& a = corners[k];
            const Corner& b = corners[(k + 1) % corners.size()];
            const int gap = (k + 1 < corners.size() ? b.i0 : b.i0 + M) - a.i1;
            const float mid = ((float)a.i1 + 0.5f * (float)gap) * ds;
            if (gap * ds > bestLen && !pitZone(mid) && !standZone(mid) && T.Wrap(mid - sLine) > 200.0f && T.Wrap(sLine - mid) > 200.0f) {
                bestLen = (float)gap * ds;
                bestS = mid;
            }
        }
        if (bestLen > 180.0f) {
            const float s = bestS, yaw = yawAt(s), span = hw + 5.5f;
            const Vector3 a = W(s, -span), b = W(s, span);
            const float base = std::min(a.y, b.y), deck = base + 6.8f;
            for (const Vector3& p : {a, b}) {
                solid({p.x, base + 3.4f, p.z}, {0.7f, 3.8f, 0.7f}, yaw);
                taken.push_back(p);
            }
            if (geo) {
                MeshBuilder& m = S(a, 500);
                for (const Vector3& p : {a, b}) YawBox(m, {p.x, base + 3.4f, p.z}, {0.6f, 3.8f, 0.6f}, yaw, kPaint, 0.1f, kPlaster);
                const Vector3 c = Vector3Lerp(a, b, 0.5f), f = fwdAt(s), r = rightAt(s);
                YawBox(m, {c.x, deck + 0.35f, c.z}, {span + 0.9f, 0.35f, 1.8f}, yaw, kConcrete, 0.1f);
                YawBox(m, {c.x, deck + 1.55f, c.z}, {span + 0.9f, 0.85f, 1.75f}, yaw, kWhite, 0.2f);
                YawBox(m, {c.x, deck + 2.5f, c.z}, {span + 0.9f, 0.1f, 1.85f}, yaw, kRed, 0.3f);
                // Carteles a los dos lados del tablero.
                for (float face : {-1.0f, 1.0f}) {
                    const Vector3 n = Vector3Scale(f, face);
                    for (int q = 0; q < 3; ++q) {
                        const float lat = ((float)q - 1.0f) * (span * 0.66f);
                        const Vector3 at = {c.x + r.x * lat + n.x * 1.79f, deck + 1.55f, c.z + r.z * lat + n.z * 1.79f};
                        Sign(X(at, 4), at, Vector3Scale(r, -face), n, std::min(3.3f, span * 0.32f), 0.8f, CellUV((q + (face > 0 ? 3 : 0)) % (int)billboards.size()));
                    }
                }
            }
            signs += 6;
        }
    }

    // ---------------------------------------------------------------- torres de luz y banderilleros
    // Torres en el paddock y detrás de la tribuna principal.
    auto lightPost = [&](Vector3 p, float yaw) {                 // en lugares elegidos: sólo que no se encimen
        solid({p.x, p.y + 8.0f, p.z}, {0.25f, 8.0f, 0.25f}, yaw);
        taken.push_back(p);
        ++posts;
        if (!geo) return;
        MeshBuilder& m = S(p, 120);
        YawBox(m, {p.x, p.y + 8.0f, p.z}, {0.2f, 8.0f, 0.2f}, yaw, kSteel, 0.4f);
        YawBox(m, {p.x, p.y + 16.4f, p.z}, {1.4f, 0.5f, 0.35f}, yaw, kDarkSteel, 0.4f);
        YawBox(m, {p.x, p.y + 16.4f, p.z}, {1.3f, 0.4f, 0.37f}, yaw, {236, 236, 210, 255}, 0.9f);
    };
    if (hasPits && garages > 0)
        for (float s = sG0; s <= sG1 + 30.0f; s += 45.0f) lightPost(W(s, pitSide * (hw + 36.0f + 46.0f)), yawAt(s) + mu::kPi * 0.5f);
    if (hasStand)
        for (float s = sSt0 - 10.0f; s <= sSt1 + 10.0f; s += 50.0f) lightPost(W(s, standSide * (hw + 13.0f + 14.0f * 0.85f + 7.0f)), yawAt(s) + mu::kPi * 0.5f);
    // Puestos de banderilleros cada ~300 m, detrás de la barrera (casilla blanca, techo naranja, bandera).
    Rng mr{0xF1A6u};
    for (float s = 150.0f; s < L - 50.0f; s += 300.0f) {
        for (int attempt = 0; attempt < 6; ++attempt) {
            const float ss = s + (float)attempt * 20.0f;
            const float side = attempt % 2 ? inside : -inside;
            const int sd = sideIdx(side);
            const float b = barrier[sd][idxOf(ss)];
            if (b <= 0.0f || pitZone(ss) || standZone(ss) || clear[sd][idxOf(ss)] < b + 6.0f) continue;
            const Vector3 c = W(ss, side * (b + 3.2f));
            if (!freeSpot(c, 8.0f)) continue;
            const float yaw = yawAt(ss);
            solid({c.x, c.y + 1.3f, c.z}, {1.1f, 1.3f, 1.2f}, yaw);
            taken.push_back(c);
            ++posts;
            if (geo) {
                MeshBuilder& m = S(c, 200);
                YawBox(m, {c.x, c.y + 1.2f, c.z}, {1.1f, 1.2f, 1.2f}, yaw, kPaint, 0.1f, kPlaster);
                YawBox(m, {c.x, c.y + 1.75f, c.z}, {1.12f, 0.35f, 1.22f}, yaw, kGlass, 0.8f);
                YawBox(m, {c.x, c.y + 2.55f, c.z}, {1.35f, 0.12f, 1.45f}, yaw, {236, 118, 26, 255}, 0.3f);
                const Vector3 n = Vector3Scale(rightAt(ss), -side), f = fwdAt(ss);
                const Vector3 pole = {c.x + n.x * 1.4f + f.x * 1.0f, c.y, c.z + n.z * 1.4f + f.z * 1.0f};
                Bar(m, pole, {pole.x, pole.y + 4.2f, pole.z}, 0.03f, kSteel);
                const Color flag = mr.Chance(0.5f) ? Color{250, 214, 30, 255} : Color{40, 170, 70, 255};
                MeshBuilder& fm = X(pole, 4);
                const Vector3 p0 = {pole.x, pole.y + 4.1f, pole.z}, p1 = {pole.x + f.x * 1.1f, pole.y + 4.0f, pole.z + f.z * 1.1f};
                const Vector3 dn = {0.0f, -0.7f, 0.0f};
                const unsigned short i0 = fm.Vertex(p0, n, {white.x, white.y}, flag), i1 = fm.Vertex(p1, n, {white.x + white.width, white.y}, flag);
                const unsigned short i2 = fm.Vertex(Vector3Add(p1, dn), n, {white.x + white.width, white.y + white.height}, flag);
                const unsigned short i3 = fm.Vertex(Vector3Add(p0, dn), n, {white.x, white.y + white.height}, flag);
                fm.Tri(i0, i1, i2);
                fm.Tri(i0, i2, i3);
            }
            break;
        }
    }

    // ---------------------------------------------------------------- árboles
    // Grilla con jitter y manchones (ruido); más tupidos cerca del borde del mapa (tapan la loma del
    // borde). Sólo lejos de la pista: más allá de la barrera (o a 30 m si no hay) y fuera de lo ocupado.
    {
        // Muestras de la pista en baldes de 20 m para la distancia a la pista.
        const float bucket = 20.0f;
        const int bn = (int)std::ceil(size / bucket) + 1;
        std::vector<std::vector<int>> buckets((size_t)bn * bn);
        for (int i = 0; i < M; ++i) {
            const int bx = std::clamp((int)((smp[i].x - origin) / bucket), 0, bn - 1), bz = std::clamp((int)((smp[i].z - origin) / bucket), 0, bn - 1);
            buckets[(size_t)bz * bn + bx].push_back(i);
        }
        auto nearest = [&](float x, float z, float& dist) {
            const int bx = (int)((x - origin) / bucket), bz = (int)((z - origin) / bucket);
            int best = -1;
            float bd = 1e18f;
            for (int r = 0; r <= 4; ++r) {
                for (int dz = -r; dz <= r; ++dz)
                    for (int dx = -r; dx <= r; ++dx) {
                        if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
                        const int cx = bx + dx, cz = bz + dz;
                        if (cx < 0 || cz < 0 || cx >= bn || cz >= bn) continue;
                        for (int i : buckets[(size_t)cz * bn + cx]) {
                            const float d = (smp[i].x - x) * (smp[i].x - x) + (smp[i].z - z) * (smp[i].z - z);
                            if (d < bd) {
                                bd = d;
                                best = i;
                            }
                        }
                    }
                if (best >= 0 && std::sqrt(bd) < (float)r * bucket) break;
            }
            dist = best >= 0 ? std::sqrt(bd) : 1e9f;
            return best;
        };
        const Color leaf[] = {{58, 92, 44, 255}, {72, 104, 48, 255}, {48, 80, 44, 255}, {88, 110, 52, 255}, {64, 96, 60, 255}};
        Rng rt{0x7EE5u};
        const float cell = 11.0f;
        for (float gz = origin + 6.0f; gz < origin + size - 6.0f; gz += cell)
            for (float gx = origin + 6.0f; gx < origin + size - 6.0f; gx += cell) {
                const float x = gx + rt.R(-3.5f, 3.5f), z = gz + rt.R(-3.5f, 3.5f);
                const float edge = half - std::max(std::fabs(x), std::fabs(z));
                const float clump = mu::Smoothstep(0.52f, 0.78f, ValueNoise(x * 0.012f + 4.0f, z * 0.012f - 9.0f));
                float density = 0.03f + 0.5f * clump;
                if (edge < 55.0f) density = std::max(density, 0.7f * (1.0f - mu::Smoothstep(30.0f, 55.0f, edge)) + 0.2f);
                const float pick = rt.F();
                const float kind = rt.F(), scale = rt.R(0.75f, 1.3f), twist = rt.R(0.0f, kTau);
                const int tone = rt.I(5);
                if (pick > density || edge < 4.0f) continue;
                float dist;
                const int i = nearest(x, z, dist);
                if (i >= 0 && dist < 150.0f) {
                    const float sideOf = (x - smp[i].x) * -smp[i].tz + (z - smp[i].z) * smp[i].tx >= 0.0f ? 1.0f : -1.0f;
                    const float b = barrier[sideIdx(sideOf)][i];
                    if (dist < (b > 0.0f ? b + 7.0f : hw + 30.0f) || dist < gravel[sideIdx(sideOf)][i] + 8.0f) continue;
                }
                if (occupied(x, z)) continue;
                const float y = G.Height(x, z);
                ++trees;
                if (dist < 200.0f) solid({x, y + 2.0f, z}, {0.25f, 2.0f, 0.25f}, 0.0f);
                if (!geo) continue;
                MeshBuilder& m = S({x, y, z}, 100);
                const Color col = leaf[tone];
                if (kind < 0.45f) {                                      // pino: dos conos
                    const float h = 9.0f * scale;
                    YawBox(m, {x, y + h * 0.18f, z}, {0.22f, h * 0.18f, 0.22f}, twist, {92, 70, 50, 255}, 0.05f);
                    Cone(m, {x, y + h * 0.25f, z}, 2.6f * scale, h * 0.5f, Shade(col, 0.85f), 7, twist, true);
                    Cone(m, {x, y + h * 0.52f, z}, 1.9f * scale, h * 0.48f, Shade(col, 0.9f), 7, twist + 0.4f);
                } else if (kind < 0.8f) {                                // copa redonda: dos conos opuestos
                    const float h = 7.5f * scale;
                    YawBox(m, {x, y + h * 0.25f, z}, {0.25f, h * 0.25f, 0.25f}, twist, {98, 76, 54, 255}, 0.05f);
                    Cone(m, {x, y + h * 0.55f, z}, 3.0f * scale, h * 0.45f, col, 6, twist);
                    Cone(m, {x, y + h * 0.56f, z}, 3.0f * scale, -h * 0.25f, Shade(col, 0.8f), 6, twist);
                } else {                                                 // álamo: alto y finito
                    const float h = 13.0f * scale;
                    YawBox(m, {x, y + h * 0.12f, z}, {0.2f, h * 0.12f, 0.2f}, twist, {110, 96, 70, 255}, 0.05f);
                    Cone(m, {x, y + h * 0.2f, z}, 1.3f * scale, h * 0.8f, Shade(col, 1.08f), 6, twist, true);
                }
            }
    }

    // Control: qué tan cerca del borde quedó lo sólido (va en el resumen) y que ninguna caja tape la
    // pista ni un metro a cada lado, aunque sus puntas queden lejos (eso sí se avisa).
    if (!geo) {
        const float bucket = 20.0f;
        const int bn = (int)std::ceil(size / bucket) + 1;
        std::vector<std::vector<int>> buckets((size_t)bn * bn);
        for (int i = 0; i < M; ++i)
            buckets[(size_t)std::clamp((int)((smp[i].z - origin) / bucket), 0, bn - 1) * bn + std::clamp((int)((smp[i].x - origin) / bucket), 0, bn - 1)].push_back(i);
        edgeMargin = 1e9f;
        for (const Solid& b : solids) {
            const Frame f = YawFrame(b.yaw);
            for (int k = 0; k < 5; ++k) {                               // las cuatro puntas y el centro
                const float u = k == 4 ? 0.0f : (k & 1 ? b.half.x : -b.half.x), v = k == 4 ? 0.0f : (k & 2 ? b.half.z : -b.half.z);
                const float x = b.c.x + f.x.x * u + f.z.x * v, z = b.c.z + f.x.z * u + f.z.z * v;
                const int bx = (int)((x - origin) / bucket), bz = (int)((z - origin) / bucket);
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (bx + dx < 0 || bz + dz < 0 || bx + dx >= bn || bz + dz >= bn) continue;
                        for (int i : buckets[(size_t)(bz + dz) * bn + bx + dx])
                            edgeMargin = std::min(edgeMargin, std::sqrt((smp[i].x - x) * (smp[i].x - x) + (smp[i].z - z) * (smp[i].z - z)) - hw);
                    }
            }
        }
        int blocked = 0;
        for (int i = 0; i < M; ++i)
            for (float lat : {-hw - 1.0f, -hw * 0.5f, 0.0f, hw * 0.5f, hw + 1.0f}) {
                const float x = smp[i].x - smp[i].tz * lat, z = smp[i].z + smp[i].tx * lat;
                const float y = G.Height(x, z);
                for (const Solid& b : solids) {
                    const float dx = x - b.c.x, dz = z - b.c.z, r = b.half.x + b.half.z;
                    if (dx * dx + dz * dz > r * r) continue;
                    const Frame f = YawFrame(b.yaw);
                    const float u = dx * f.x.x + dz * f.x.z, v = dx * f.z.x + dz * f.z.z;
                    if (std::fabs(u) > b.half.x || std::fabs(v) > b.half.z || y + 1.5f < b.c.y - b.half.y || y > b.c.y + b.half.y) continue;
                    if (++blocked <= 4) std::printf("circuito: ojo, una caja tapa la pista en s=%.0f (%.1f, %.1f)\n", (float)i * ds, b.c.x, b.c.z);
                }
            }
    }

    // ---------------------------------------------------------------- sierras de fondo
    if (geo) {
        MeshBuilder& b = geo->backdrop;
        Rng rr{0x51E77Au};
        const int n = 200;
        std::vector<Vector3> base(n + 1), crest(n + 1);
        // Lejos (pero adentro del plano de corte de la cámara, 900 m, visto desde el medio del mapa) y ya
        // con la bruma del cielo en el color: de cerca (desde el borde del mapa) no tapa todo.
        const float R = std::min(half + 160.0f, 860.0f);
        for (int i = 0; i <= n; ++i) {
            const float a = kTau * (float)(i % n) / (float)n;
            // Al sur y al oeste la sierra (más alta); al norte lomas bajas.
            const float south = 0.5f - 0.5f * std::cos(a);
            float h = 10.0f + 22.0f * south + 16.0f * ValueNoise(a * 6.0f, 1.7f) + 9.0f * ValueNoise(a * 17.0f, 5.3f);
            h += 26.0f * std::exp(-std::pow((a - 3.6f) / 0.35f, 2.0f));
            const float r = R + 40.0f * ValueNoise(a * 4.0f, 9.1f);
            base[i] = {std::sin(a) * (r + 40.0f), -6.0f, std::cos(a) * (r + 40.0f)};
            crest[i] = {std::sin(a) * r, h, std::cos(a) * r};
        }
        for (int i = 0; i < n; ++i) {
            const Vector3 nn = Vector3Normalize({-crest[i].x, 120.0f, -crest[i].z});
            const Color low = {108, 128, 118, 255}, high = {138, 154, 160, 255};
            const unsigned short a0 = b.Vertex(base[i], nn, {0.05f, kPlain}, low), a1 = b.Vertex(crest[i], nn, {0.05f, kPlain}, high);
            const unsigned short b0 = b.Vertex(base[i + 1], nn, {0.05f, kPlain}, low), b1 = b.Vertex(crest[i + 1], nn, {0.05f, kPlain}, high);
            b.Tri(a0, b0, b1);
            b.Tri(a0, b1, a1);
        }
        b.FixWinding();
        (void)rr;
    }
}

// ------------------------------------------------------------------------ colisión
void Circuit::CreateCollision(PhysicsWorld& world)
{
    // Un cuerpo estático por bloque con todas sus cajas.
    std::vector<JPH::Ref<JPH::StaticCompoundShapeSettings>> group((size_t)chunkCount * chunkCount);
    for (const Solid& s : solids) {
        const int cx = std::clamp((int)((s.c.x - origin) / chunkSize), 0, chunkCount - 1);
        const int cz = std::clamp((int)((s.c.z - origin) / chunkSize), 0, chunkCount - 1);
        auto& g = group[(size_t)cz * chunkCount + cx];
        if (!g) g = new JPH::StaticCompoundShapeSettings;
        g->AddShape(JPH::Vec3(s.c.x, s.c.y, s.c.z), JPH::Quat::sRotation(JPH::Vec3::sAxisY(), s.yaw),
                    new JPH::BoxShapeSettings(JPH::Vec3(std::max(s.half.x, 0.06f), std::max(s.half.y, 0.06f), std::max(s.half.z, 0.06f)), 0.02f));
    }
    for (auto& g : group) {
        if (!g) continue;
        JPH::ShapeSettings::ShapeResult r = g->Create();
        if (r.HasError()) {
            std::printf("circuito: error de colisión: %s\n", r.GetError().c_str());
            continue;
        }
        JPH::BodyCreationSettings bcs(r.Get(), JPH::RVec3::sZero(), JPH::Quat::sIdentity(), JPH::EMotionType::Static, Layers::STATIC);
        bcs.mFriction = 0.7f;
        bodies.push_back(world.Bodies().CreateAndAddBody(bcs, JPH::EActivation::DontActivate));
    }
}

// ------------------------------------------------------------------------ atlas de carteles
void Circuit::BuildAtlas()
{
    Image img = GenImageColor(kAtlas, kAtlas, Color{255, 255, 255, 255});
    // Fuente: una condensada y gruesa del sistema (Windows o Mac) si está (la de los carteles de publicidad); si no, la de raylib.
    std::string all = name;
    for (const std::string& b : billboards) all += b;
    all += " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz.,-'!&";
    int count = 0;
    int* cps = LoadCodepoints(all.c_str(), &count);
    std::vector<int> unique(cps, cps + count);
    UnloadCodepoints(cps);
    std::sort(unique.begin(), unique.end());
    unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
    Font font{};
    bool own = false;
    for (const char* path : {"C:/Windows/Fonts/impact.ttf", "C:/Windows/Fonts/arialbd.ttf", "C:/Windows/Fonts/segoeuib.ttf",
                             "/System/Library/Fonts/Supplemental/Impact.ttf", "/System/Library/Fonts/Supplemental/Arial Bold.ttf"}) {
        if (!FileExists(path)) continue;
        font = LoadFontEx(path, 112, unique.data(), (int)unique.size());
        if (font.texture.id != 0) {
            own = true;
            break;
        }
    }
    if (!own) font = GetFontDefault();

    auto fitText = [&](const char* text, Rectangle cell, float maxSize, Color fg, float spacing) {
        float sz = maxSize;
        Vector2 m = MeasureTextEx(font, text, sz, spacing);
        while ((m.x > cell.width - 36.0f || m.y > cell.height - 14.0f) && sz > 12.0f) {
            sz -= 2.0f;
            m = MeasureTextEx(font, text, sz, spacing);
        }
        ImageDrawTextEx(&img, font, text, {cell.x + (cell.width - m.x) * 0.5f, cell.y + (cell.height - m.y) * 0.5f}, sz, spacing, fg);
    };
    const Color bgs[] = {{200, 30, 36, 255}, {20, 60, 150, 255}, {250, 196, 20, 255}, {24, 24, 26, 255}, {20, 130, 70, 255}, {240, 110, 20, 255},
                         {236, 236, 232, 255}, {110, 30, 130, 255}, {0, 150, 190, 255}, {160, 20, 40, 255}, {40, 40, 44, 255}, {250, 240, 220, 255}};
    for (int i = 0; i < kBillboardCells; ++i) {
        const Rectangle cell = CellPixels(i);
        const std::string& text = billboards[(size_t)i % billboards.size()];
        const Color bg = bgs[i % 12];
        const bool light = (bg.r + bg.g + bg.b) > 450;
        const Color fg = light ? Color{24, 24, 26, 255} : Color{250, 250, 246, 255};
        ImageDrawRectangleRec(&img, cell, bg);
        switch (i % 3) {
        case 0:                                                   // franja abajo
            ImageDrawRectangleRec(&img, {cell.x, cell.y + cell.height - 22.0f, cell.width, 22.0f}, light ? Color{200, 30, 36, 255} : Color{250, 250, 246, 255});
            break;
        case 1:                                                   // borde
            ImageDrawRectangleLines(&img, {cell.x + 6.0f, cell.y + 6.0f, cell.width - 12.0f, cell.height - 12.0f}, 5, fg);
            break;
        default:                                                  // cuña de color a la izquierda
            ImageDrawTriangle(&img, {cell.x, cell.y}, {cell.x, cell.y + cell.height}, {cell.x + 90.0f, cell.y}, light ? Color{20, 60, 150, 255} : Color{250, 196, 20, 255});
            break;
        }
        fitText(text.c_str(), {cell.x, cell.y - (i % 3 == 0 ? 10.0f : 0.0f), cell.width, cell.height}, 104.0f, fg, 3.0f);
    }
    {
        const Rectangle cell = CellPixels(kNameCell);
        ImageDrawRectangleRec(&img, cell, Color{236, 236, 232, 255});
        ImageDrawRectangleRec(&img, {cell.x, cell.y, cell.width, 14.0f}, Color{200, 30, 36, 255});
        ImageDrawRectangleRec(&img, {cell.x, cell.y + cell.height - 14.0f, cell.width, 14.0f}, Color{200, 30, 36, 255});
        fitText(name.c_str(), cell, 100.0f, Color{24, 24, 26, 255}, 4.0f);
    }
    for (int d = 0; d < 4; ++d) {
        const Rectangle cell = CellPixels(kDistanceCell + d);
        ImageDrawRectangleRec(&img, cell, Color{246, 246, 242, 255});
        ImageDrawRectangleLines(&img, {cell.x + 4.0f, cell.y + 4.0f, cell.width - 8.0f, cell.height - 8.0f}, 8, Color{200, 30, 36, 255});
        fitText(kDistanceText[d], cell, 90.0f, Color{20, 20, 22, 255}, 2.0f);
    }
    ImageDrawRectangleRec(&img, CellPixels(kWhiteCell), WHITE);
    if (own) UnloadFont(font);
    atlas = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&atlas);
    SetTextureFilter(atlas, TEXTURE_FILTER_TRILINEAR);
}

// ------------------------------------------------------------------------ mallas
void Circuit::CreateMeshes()
{
    if (!built) return;
    Unload();
    BuildAtlas();
    Geometry geo;
    Generate(&geo);
    if (CoplanarCheck::Enabled()) {
        // Diagnóstico (MOTOSIM_COPLANARES): caras que se pisan y calcos del piso que el terreno tapa.
        CoplanarCheck cc;
        const int ls = cc.AddLayer("solido", false, false), ld = cc.AddLayer("calco", false, false);
        const int lt = cc.AddLayer("cartel", true, true), lp = cc.AddLayer("gente", false, false);
        for (size_t i = 0; i < geo.solid.size(); ++i) {
            for (const MeshBuilder& b : geo.solid[i].parts) cc.Add(b, ls);
            for (const MeshBuilder& b : geo.decal[i].parts) cc.Add(b, ld);
            for (const MeshBuilder& b : geo.textured[i].parts) cc.Add(b, lt);
            for (const MeshBuilder& b : geo.detail[i].parts) cc.Add(b, lp);
        }
        cc.Report("circuito");
        std::map<uint32_t, std::tuple<int, int, float, Vector3>> under;      // color: triángulos, tapados, peor, dónde
        for (const CoplanarCheck::Tri& t : cc.tris) {
            if (t.layer != ld || t.n.y < 0.5f) continue;
            const float edge = std::max({Vector3Distance(t.p[0], t.p[1]), Vector3Distance(t.p[1], t.p[2]), Vector3Distance(t.p[2], t.p[0])});
            const int n = std::clamp((int)std::ceil(edge / 0.25f), 1, 200);
            float worst = 1e9f;
            Vector3 at{};
            for (int a = 0; a <= n; ++a)
                for (int b = 0; a + b <= n; ++b) {
                    const float u = (float)a / n, v = (float)b / n;
                    const Vector3 p = Vector3Add(Vector3Add(Vector3Scale(t.p[0], 1.0f - u - v), Vector3Scale(t.p[1], u)), Vector3Scale(t.p[2], v));
                    const float c = p.y - terrain->Height(p.x, p.z);
                    if (c < worst) {
                        worst = c;
                        at = p;
                    }
                }
            auto& g = under[(uint32_t)t.c.r << 16 | (uint32_t)t.c.g << 8 | t.c.b];
            ++std::get<0>(g);
            if (worst < 0.004f) ++std::get<1>(g);
            if (std::get<0>(g) == 1 || worst < std::get<2>(g)) {
                std::get<2>(g) = worst;
                std::get<3>(g) = at;
            }
        }
        for (const auto& [c, g] : under)
            if (std::get<1>(g) > 0)
                std::printf("  calco %u,%u,%u: %d de %d triángulos a menos de 4 mm del terreno (el peor %.1f mm en %.1f, %.1f, %.1f)\n", c >> 16, (c >> 8) & 255,
                            c & 255, std::get<1>(g), std::get<0>(g), std::get<2>(g) * 1000.0f, std::get<3>(g).x, std::get<3>(g).y, std::get<3>(g).z);
    }
    chunks.assign((size_t)chunkCount * chunkCount, Chunk{});
    auto upload = [](std::vector<Batch>& from, size_t i, std::vector<Mesh>& to) {
        for (MeshBuilder& b : from[i].parts)
            if (b.VertexCount() > 0) to.push_back(b.Build());
    };
    int meshCount = 0, verts = 0;
    for (size_t i = 0; i < chunks.size(); ++i) {
        Chunk& c = chunks[i];
        c.lo = {1e9f, 1e9f, 1e9f};
        c.hi = {-1e9f, -1e9f, -1e9f};
        for (const auto* l : {&geo.solid, &geo.decal, &geo.textured, &geo.detail})
            for (const MeshBuilder& b : (*l)[i].parts) {
                verts += b.VertexCount();
                for (int v = 0; v < b.VertexCount(); ++v) {
                    c.lo = Vector3Min(c.lo, b.P(v));
                    c.hi = Vector3Max(c.hi, b.P(v));
                }
            }
        upload(geo.solid, i, c.solid);
        upload(geo.decal, i, c.decal);
        upload(geo.textured, i, c.textured);
        upload(geo.detail, i, c.detail);
        c.hasSolid = !c.solid.empty();
        meshCount += (int)(c.solid.size() + c.decal.size() + c.textured.size() + c.detail.size());
    }
    backdrop = geo.backdrop.Build();
    std::printf("circuito: %d mallas, %d vértices\n", meshCount, verts);
    meshes = true;
}

// ------------------------------------------------------------------------ dibujo
void Circuit::Draw(Renderer& r) const
{
    if (!meshes) return;
    // Bloques fuera de la vista: afuera. La cámara y el frustum salen de las matrices de rlgl (estamos
    // dentro de BeginMode3D); una caja queda afuera si sus 8 esquinas caen del lado de afuera de un
    // mismo plano del recorte.
    const Matrix view = rlGetMatrixModelview(), proj = rlGetMatrixProjection();
    const Matrix vp = MatrixMultiply(view, proj);
    const Matrix inv = MatrixInvert(view);
    const Vector3 cam = {inv.m12, inv.m13, inv.m14};
    auto visible = [&](const Chunk& c) {
        int outside[6] = {0, 0, 0, 0, 0, 0};
        for (int k = 0; k < 8; ++k) {
            const Vector3 p = {k & 1 ? c.hi.x : c.lo.x, k & 2 ? c.hi.y : c.lo.y, k & 4 ? c.hi.z : c.lo.z};
            const float x = vp.m0 * p.x + vp.m4 * p.y + vp.m8 * p.z + vp.m12, y = vp.m1 * p.x + vp.m5 * p.y + vp.m9 * p.z + vp.m13;
            const float z = vp.m2 * p.x + vp.m6 * p.y + vp.m10 * p.z + vp.m14, w = vp.m3 * p.x + vp.m7 * p.y + vp.m11 * p.z + vp.m15;
            outside[0] += x < -w;
            outside[1] += x > w;
            outside[2] += y < -w;
            outside[3] += y > w;
            outside[4] += z < -w;
            outside[5] += z > w;
        }
        for (int o : outside)
            if (o == 8) return false;
        return true;
    };
    auto distance = [&](const Chunk& c) {
        const float dx = std::max({c.lo.x - cam.x, 0.0f, cam.x - c.hi.x}), dz = std::max({c.lo.z - cam.z, 0.0f, cam.z - c.hi.z});
        return std::sqrt(dx * dx + dz * dz);
    };
    std::vector<const Chunk*> shown;
    shown.reserve(chunks.size());
    for (const Chunk& c : chunks)
        if (c.lo.x <= c.hi.x && visible(c)) shown.push_back(&c);

    r.DrawMeshColored(backdrop, MatrixIdentity(), WHITE);
    for (const Chunk* c : shown)
        for (const Mesh& m : c->solid) r.DrawMeshColored(m, MatrixIdentity(), WHITE);
    for (const Chunk* c : shown)
        if (distance(*c) < 260.0f)
            for (const Mesh& m : c->detail) r.DrawMeshColored(m, MatrixIdentity(), WHITE);
    for (const Chunk* c : shown)
        for (const Mesh& m : c->decal) r.DrawMeshColored(m, MatrixIdentity(), WHITE);
    r.SetGloss(0.12f);
    for (const Chunk* c : shown)
        for (const Mesh& m : c->textured) r.DrawMeshTextured(m, MatrixIdentity(), atlas);
    // Semáforo: cada 14 s se prenden las cinco columnas de a una por segundo y se apagan juntas (¡largada!).
    const float t = std::fmod((float)GetTime(), 14.0f);
    r.SetGloss(0.9f);
    for (const Lamp& l : lamps) {
        if ((l.pos.x - cam.x) * (l.pos.x - cam.x) + (l.pos.z - cam.z) * (l.pos.z - cam.z) > 400.0f * 400.0f) continue;
        const bool on = t >= 2.0f + (float)l.column && t < 7.6f;
        const Matrix m = MatrixMultiply(MatrixScale(0.2f, 0.2f, 0.2f), MatrixTranslate(l.pos.x, l.pos.y, l.pos.z));
        r.DrawMeshColored(r.sphere, m, on ? Color{255, 40, 26, 255} : Color{46, 16, 16, 255});
    }
    r.SetGloss(0.3f);
}

void Circuit::DrawShadows(Renderer& r, Vector3 focus, float radius) const
{
    if (!meshes) return;
    for (const Chunk& c : chunks) {
        if (!c.hasSolid) continue;
        const float dx = std::max({c.lo.x - focus.x, 0.0f, focus.x - c.hi.x}), dz = std::max({c.lo.z - focus.z, 0.0f, focus.z - c.hi.z});
        if (dx * dx + dz * dz > radius * radius) continue;
        for (const Mesh& m : c.solid) r.DrawMeshColored(m, MatrixIdentity(), WHITE);
    }
}

void Circuit::Unload()
{
    if (!meshes) return;
    for (Chunk& c : chunks) {
        for (Mesh& m : c.solid) UnloadMesh(m);
        for (Mesh& m : c.decal) UnloadMesh(m);
        for (Mesh& m : c.textured) UnloadMesh(m);
        for (Mesh& m : c.detail) UnloadMesh(m);
    }
    chunks.clear();
    UnloadMesh(backdrop);
    backdrop = Mesh{};
    UnloadTexture(atlas);
    atlas = Texture2D{};
    meshes = false;
}
