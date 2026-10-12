// Autopista: todo lo que rodea el trazado de un mapa con "generator": "highway" (ver Highway.h). Se ubica a partir
// del trazado (la línea central es el medio del separador), el corte transversal de la sección "highway" del mapa y
// el terreno, con semillas fijas: igual en todas las PCs. Las mallas van por bloques del mapa con el color en los
// vértices, como las del autódromo (Circuit.cpp): sólidas, calcos en el piso, lo que lleva textura (carteles) y lo
// chico (postes), que se dibuja sólo cerca.
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>

#include "Highway.h"

#include "Json.h"
#include "MathUtil.h"
#include "MeshBuilder.h"
#include "PhysicsWorld.h"
#include "Render.h"
#include "Terrain.h"
#include "Track.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>

namespace {

constexpr float kTau = 6.2831853f;
constexpr float kPlain = 2.0f, kPlaster = 4.0f;   // modos del shader iluminado (v de la UV), ver kLitFS
constexpr float kStep = 2.0f;                      // m entre muestras del trazado
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

// Paleta de la ruta.
const Color kConcrete = {178, 176, 168, 255};
const Color kSteel = {156, 162, 166, 255};
const Color kDarkSteel = {84, 88, 94, 255};
const Color kLineWhite = {232, 232, 226, 255};
const Color kLineYellow = {226, 182, 40, 255};
const Color kAsphalt = {64, 64, 68, 255};
const Color kSignGreen = {18, 96, 58, 255};
const Color kTrunk = {104, 90, 72, 255};

// Atlas de carteles (1024 x 1024): 12 carteles verdes de 512 x 128 (pórticos) y 16 cuadrados de 128 (kilómetros,
// velocidad máxima, blanco).
constexpr int kAtlas = 1024;
constexpr int kBigCells = 12;
constexpr int kKmCell = 12;                        // 12..23: "KM 1".."KM 12" (los cuadrados)
constexpr int kKmCells = 12;
constexpr int kSpeedCell = 24;                     // 130
constexpr int kExitCell = 25;                      // flecha de salida
constexpr int kWhiteCell = 26;

Rectangle CellPixels(int i)
{
    if (i < kBigCells) return {(float)(i % 2) * 512.0f, (float)(i / 2) * 128.0f, 512.0f, 128.0f};
    const int k = i - kBigCells;                   // 0..15: dos filas de 8 cuadrados
    return {(float)(k % 8) * 128.0f, 768.0f + (float)(k / 8) * 128.0f, 128.0f, 128.0f};
}

Rectangle CellUV(int i)                            // en 0..1, con un margen para que no sangre el vecino
{
    const Rectangle p = CellPixels(i);
    const float m = 3.0f;
    return {(p.x + m) / kAtlas, (p.y + m) / kAtlas, (p.width - 2 * m) / kAtlas, (p.height - 2 * m) / kAtlas};
}

// ------------------------------------------------------------------------ primitivas (las del autódromo)
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

// Cara plana con la normal que sale de sus vértices, mirando para el lado de `out`.
void FaceOut(MeshBuilder& b, Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, Vector3 out, Color col, float gloss, float mode)
{
    Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(p2, p0), Vector3Subtract(p3, p1)));
    if (Vector3DotProduct(n, out) < 0.0f) n = Vector3Negate(n);
    Face(b, p0, p1, p2, p3, n, col, gloss, mode);
}

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
            const Color cc = a == 1 ? Shade(col, 1.06f) : col;
            Face(b, Vector3Subtract(Vector3Subtract(fc, du), dv), Vector3Subtract(Vector3Add(fc, du), dv), Vector3Add(Vector3Add(fc, du), dv),
                 Vector3Add(Vector3Subtract(fc, du), dv), n, cc, gloss, mode);
        }
}

void YawBox(MeshBuilder& b, Vector3 c, Vector3 h, float yaw, Color col, float gloss = 0.1f, float mode = kPlain, bool bottom = false)
{
    Box(b, c, h, YawFrame(yaw), col, gloss, mode, bottom);
}

// Hexaedro de 8 esquinas (abajo a0..a3, arriba b0..b3, en el mismo orden alrededor): tableros, estribos.
void Hexa(MeshBuilder& b, const Vector3 lo[4], const Vector3 hi[4], Color col, float gloss = 0.08f, float mode = kPlaster, bool bottom = true)
{
    Vector3 c = {0.0f, 0.0f, 0.0f};
    for (int k = 0; k < 4; ++k) c = Vector3Add(c, Vector3Add(lo[k], hi[k]));
    c = Vector3Scale(c, 1.0f / 8.0f);
    auto out = [&](Vector3 p0, Vector3 p2) { return Vector3Subtract(Vector3Lerp(p0, p2, 0.5f), c); };
    for (int k = 0; k < 4; ++k) {
        const int n = (k + 1) % 4;
        FaceOut(b, lo[k], lo[n], hi[n], hi[k], out(lo[k], hi[n]), col, gloss, mode);
    }
    FaceOut(b, hi[0], hi[1], hi[2], hi[3], {0.0f, 1.0f, 0.0f}, Shade(col, 1.05f), gloss, mode);
    if (bottom) FaceOut(b, lo[0], lo[1], lo[2], lo[3], {0.0f, -1.0f, 0.0f}, Shade(col, 0.8f), gloss, mode);
}

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

void Cylinder(MeshBuilder& b, Vector3 c, float r, float h, Color col, float gloss = 0.3f, int slices = 8, bool cap = true)
{
    const unsigned short first = (unsigned short)b.VertexCount();
    for (int i = 0; i <= slices; ++i) {
        const float a = kTau * (float)i / (float)slices;
        const Vector3 n = {std::cos(a), 0.0f, std::sin(a)};
        b.Vertex({c.x + n.x * r, c.y, c.z + n.z * r}, n, {gloss, kPlain}, col);
        b.Vertex({c.x + n.x * r, c.y + h, c.z + n.z * r}, n, {gloss, kPlain}, col);
    }
    const size_t firstIdx = b.idx.size();
    for (int i = 0; i < slices; ++i) {
        const unsigned short a0 = (unsigned short)(first + 2 * i), a1 = a0 + 1, b0 = a0 + 2, b1 = a0 + 3;
        b.Tri(a0, b1, b0);
        b.Tri(a0, a1, b1);
    }
    if (cap) {
        const Color top = Shade(col, 1.1f);
        const unsigned short ct = b.Vertex({c.x, c.y + h, c.z}, {0.0f, 1.0f, 0.0f}, {gloss, kPlain}, top);
        for (int i = 0; i <= slices; ++i) {
            const float a = kTau * (float)i / (float)slices;
            b.Vertex({c.x + std::cos(a) * r, c.y + h, c.z + std::sin(a) * r}, {0.0f, 1.0f, 0.0f}, {gloss, kPlain}, top);
        }
        for (int i = 0; i < slices; ++i) b.Tri(ct, (unsigned short)(ct + 2 + i), (unsigned short)(ct + 1 + i));
    }
    b.FixWinding(firstIdx);
}

void Cone(MeshBuilder& b, Vector3 c, float r, float h, Color col, int sides, float twist, bool cap = false)
{
    const size_t first = b.idx.size();
    const Vector3 tip = {c.x, c.y + h, c.z};
    for (int i = 0; i < sides; ++i) {
        const float a0 = twist + kTau * (float)i / (float)sides, a1 = twist + kTau * (float)(i + 1) / (float)sides;
        const Vector3 p0 = {c.x + std::cos(a0) * r, c.y, c.z + std::sin(a0) * r}, p1 = {c.x + std::cos(a1) * r, c.y, c.z + std::sin(a1) * r};
        const Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(p1, p0), Vector3Subtract(tip, p0)));
        const Vector3 radial = {std::cos(0.5f * (a0 + a1)), 0.0f, std::sin(0.5f * (a0 + a1))};
        const Vector3 nn = Vector3DotProduct(n, radial) < 0.0f ? Vector3Negate(n) : n;
        const Color cc = Shade(col, 0.9f + 0.2f * (0.5f + 0.5f * std::cos(a0 - 2.2f)));
        b.Tri(b.Vertex(p0, nn, {0.05f, kPlain}, cc), b.Vertex(p1, nn, {0.05f, kPlain}, cc), b.Vertex(tip, nn, {0.05f, kPlain}, cc));
        if (!cap) continue;
        const Vector3 dn = {0.0f, -1.0f, 0.0f};
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
    const Vector3 face = Vector3CrossProduct(Vector3Subtract(b.P(bb), b.P(a)), Vector3Subtract(b.P(cc), b.P(a)));
    if (Vector3DotProduct(face, n) >= 0.0f) {
        b.Tri(a, bb, cc);
        b.Tri(a, cc, d);
    } else {
        b.Tri(a, cc, bb);
        b.Tri(a, d, cc);
    }
}

struct Batch {
    std::deque<MeshBuilder> parts;
    MeshBuilder& Get(int verts)
    {
        if (parts.empty() || parts.back().VertexCount() + verts > kMaxVerts) parts.emplace_back();
        return parts.back();
    }
};

} // namespace

struct Highway::Geometry {
    std::vector<Batch> solid, decal, textured, detail;
};

// ------------------------------------------------------------------------ armado
void Highway::Build(const Track& t, const Terrain& g, const MapDef& def)
{
    track = &t;
    terrain = &g;
    solids.clear();
    origin = g.OriginX();
    size = g.Size();
    chunkCount = std::clamp((int)std::ceil(size / 256.0f), 1, 16);
    chunkSize = size / (float)chunkCount;

    layout = HighwayLayout{};
    name = "AUTOPISTA";
    destinations = {"BUENOS AIRES  87", "LA PLATA  42", "MAR DEL PLATA  312", "CHASCOMÚS  64", "SALIDA 23  RUTA 2", "DOLORES  158",
                    "SALIDA 31  RANCHOS", "PINAMAR  341", "SALIDA 12  BRANDSEN", "CAÑUELAS  58", "SALIDA 40  LEZAMA", "TANDIL  360"};
    bridgeS.clear();
    lightSpacing = 50.0f;
    gantrySpacing = 2400.0f;
    crossoverSpacing = 2000.0f;
    crossoverLength = 40.0f;
    trees = 1.0f;
    Json j;
    std::string err;
    if (Json::Load(def.file, j, err)) {
        const Json& h = j["highway"];
        layout.lanes = std::clamp((int)h.Num("lanes", (float)layout.lanes), 1, 5);
        layout.laneWidth = std::clamp(h.Num("lane_width", layout.laneWidth), 2.5f, 5.0f);
        layout.median = std::clamp(h.Num("median", layout.median), 0.8f, 30.0f);
        layout.innerShoulder = std::clamp(h.Num("inner_shoulder", layout.innerShoulder), 0.0f, 5.0f);
        layout.outerShoulder = std::clamp(h.Num("outer_shoulder", layout.outerShoulder), 0.0f, 6.0f);
        name = h.Str("name", name);
        lightSpacing = h.Num("lights", lightSpacing);
        crossoverSpacing = h.Num("crossovers", crossoverSpacing);
        crossoverLength = std::clamp(h.Num("crossover_length", crossoverLength), 10.0f, 200.0f);
        gantrySpacing = h.Num("gantries", gantrySpacing);
        trees = std::clamp(h.Num("trees", trees), 0.0f, 3.0f);
        const Json& d = h["signs"];
        if (d.IsArray() && d.Size() > 0) {
            destinations.clear();
            for (size_t i = 0; i < d.Size() && (int)destinations.size() < kBigCells; ++i)
                if (d[i].IsString()) destinations.push_back(d[i].string);
        }
        const Json& br = h["bridges"];
        for (size_t i = 0; i < br.Size(); ++i) {
            if (!br[i].IsArray() || br[i].Size() < 2) continue;
            const float x = br[i][0].AsNum() * def.layoutScale, z = br[i][1].AsNum() * def.layoutScale;
            bridgeS.push_back(t.Points()[t.Nearest(x, z)].s);
        }
    }
    if (destinations.empty()) destinations.push_back(name);
    const float paved = layout.PavedHalf();
    if (std::fabs(paved - t.HalfWidth()) > 0.3f)
        std::printf("autopista: ojo, el corte (%.1f m de cada lado) no coincide con la mitad del ancho de la pista (%.1f m)\n", paved,
                    t.HalfWidth());
    shoulder = def.streetShoulder;

    Generate(nullptr);
    built = true;
    std::printf("autopista: %d carriles por mano, %.0f m de guardarraíl y separador, %d cruces del separador, %d columnas de luz, %d pórticos, "
                "%zu puentes, %d árboles, %d casas y galpones, %zu cajas\n",
                layout.lanes, railLength, crossovers, lights, gantries, bridgeS.size(), treeCount, buildings, solids.size());
}

void Highway::Generate(Geometry* geo)
{
    const Track& T = *track;
    const Terrain& G = *terrain;
    const float L = T.Length();
    const int M = std::max(16, (int)std::floor(L / kStep));
    const float ds = L / (float)M;
    const float half = size * 0.5f;
    const HighwayLayout& H = layout;
    const float hw = T.HalfWidth();
    const float railLat = hw + 0.7f;                // guardarraíl de afuera (en la banquina de pasto)

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
    auto S = [&](Vector3 p, int verts) -> MeshBuilder& { return geo->solid[chunkOf(p.x, p.z)].Get(verts); };
    auto D = [&](Vector3 p, int verts) -> MeshBuilder& { return geo->decal[chunkOf(p.x, p.z)].Get(verts); };
    auto X = [&](Vector3 p, int verts) -> MeshBuilder& { return geo->textured[chunkOf(p.x, p.z)].Get(verts); };
    auto Dt = [&](Vector3 p, int verts) -> MeshBuilder& { return geo->detail[chunkOf(p.x, p.z)].Get(verts); };
    auto solid = [&](Vector3 c, Vector3 h, float yaw) {
        if (!geo) solids.push_back({c, h, yaw});
    };
    lights = gantries = treeCount = buildings = 0;
    railLength = 0.0f;

    // Punto a una distancia lateral de la línea central (lat > 0: a la derecha), en el piso.
    auto W = [&](float s, float lat) {
        const TrackPoint p = T.At(s);
        const float x = p.x - p.tz * lat, z = p.z + p.tx * lat;
        return Vector3{x, G.Height(x, z), z};
    };
    auto rightAt = [&](float s) {
        const TrackPoint p = T.At(s);
        return Vector3{-p.tz, 0.0f, p.tx};
    };
    auto fwdAt = [&](float s) {
        const TrackPoint p = T.At(s);
        return Vector3{p.tx, 0.0f, p.tz};
    };
    // Cerca de un puente (s a menos de d m del eje de alguno): ahí no van columnas, carteles ni árboles.
    auto nearBridge = [&](float s, float d) {
        for (float b : bridgeS) {
            const float e = std::fabs(T.Wrap(s - b + L * 0.5f) - L * 0.5f);
            if (e < d) return true;
        }
        return false;
    };
    // Cruces del separador (un hueco de crossoverLength m para pasarse a la otra mano), cada crossoverSpacing m, lejos de los
    // puentes (la pila) y de los pórticos (el poste del medio).
    std::vector<float> crossS;
    if (crossoverSpacing > 100.0f)
        for (float s = crossoverSpacing * 0.5f; s < L - crossoverSpacing * 0.25f; s += crossoverSpacing) {
            float c = s;
            for (int tries = 0; tries < 20; ++tries) {
                bool bad = nearBridge(c, 150.0f);
                if (gantrySpacing > 100.0f)
                    for (float g = gantrySpacing * 0.25f; g < L - 200.0f && !bad; g += gantrySpacing) bad = std::fabs(c - g) < 120.0f;
                if (!bad) break;
                c += 60.0f;
            }
            crossS.push_back(T.Wrap(c));
        }
    crossovers = (int)crossS.size();
    auto inCrossover = [&](float s) {
        for (float c : crossS)
            if (std::fabs(T.Wrap(s - c + L * 0.5f) - L * 0.5f) < 0.5f * crossoverLength) return true;
        return false;
    };

    // ---------------------------------------------------------------- calcos: líneas de los carriles
    auto groundQuad = [&](Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, Color col, float gloss, float dy) {
        p0.y = G.Height(p0.x, p0.z) + dy;
        p1.y = G.Height(p1.x, p1.z) + dy;
        p2.y = G.Height(p2.x, p2.z) + dy;
        p3.y = G.Height(p3.x, p3.z) + dy;
        Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(p1, p0), Vector3Subtract(p3, p0)));
        if (n.y < 0.0f) n = Vector3Negate(n);
        Face(D(Vector3Lerp(p0, p2, 0.5f), 4), p0, p1, p2, p3, n, col, gloss, kPlain);
    };
    auto band = [&](float s0, float s1, float step, float dy, float gloss, float lat0, float lat1, Color col) {
        const int n = std::max(1, (int)std::ceil((s1 - s0) / step));
        for (int q = 0; q < n; ++q) {
            const float a = s0 + (s1 - s0) * (float)q / (float)n, b = s0 + (s1 - s0) * (float)(q + 1) / (float)n;
            groundQuad(W(a, lat0), W(a, lat1), W(b, lat1), W(b, lat0), col, gloss, dy);
        }
    };
    if (geo) {
        const float in = H.LanesFrom(), out = in + (float)H.lanes * H.laneWidth;
        for (float d : {-1.0f, 1.0f}) {
            // La amarilla junto al separador y la blanca del borde, enteras; entre carriles, trazos de 4.5 m cada 12 m.
            band(0.0f, L, 2.0f, 0.02f, 0.25f, d * (in - 0.15f), d * in, kLineYellow);
            band(0.0f, L, 2.0f, 0.02f, 0.25f, d * out, d * (out + 0.15f), kLineWhite);
            for (int k = 1; k < H.lanes; ++k) {
                const float c = in + (float)k * H.laneWidth;
                for (float s = 0.0f; s + 4.5f < L; s += 12.0f) band(s, s + 4.5f, 2.25f, 0.02f, 0.25f, d * (c - 0.075f), d * (c + 0.075f), kLineWhite);
            }
        }
    }

    // ---------------------------------------------------------------- separador New Jersey
    // Secciones cada 6 m (tres muestras) unidas cara a cara: sin huecos ni solapes. Perfil de costado: base de 0.30 m de
    // medio ancho, quiebre a 0.33 m de alto con 0.22, arriba 0.09 a 0.85 m. Enterrado 15 cm (el piso tiene peralte).
    {
        const int every = 3;
        const float prof[4][2] = {{0.30f, -0.15f}, {0.22f, 0.33f}, {0.09f, 0.85f}, {0.0f, 0.85f}};
        auto section = [&](int i, Vector3 out[7]) {
            const float s = (float)i * ds;
            const Vector3 c = W(s, 0.0f), r = rightAt(s);
            const float y0 = std::min({c.y, W(s, -0.3f).y, W(s, 0.3f).y});
            // izquierda abajo -> arriba -> derecha abajo
            int k = 0;
            for (int q = 0; q < 3; ++q) out[k++] = Vector3Add({c.x, y0 + prof[q][1], c.z}, Vector3Scale(r, -prof[q][0]));
            for (int q = 2; q >= 0; --q) out[k++] = Vector3Add({c.x, y0 + prof[q][1], c.z}, Vector3Scale(r, prof[q][0]));
            out[6] = c;
        };
        // Tramo (de i a i + every) que cae en un cruce del separador: no va.
        auto gap = [&](int i) { return inCrossover(((float)i + 0.5f * (float)every) * ds); };
        for (int i = 0; i < M; i += every) {
            const int n = std::min(i + every, M);
            if (gap(i)) continue;
            Vector3 a[7], b[7];
            section(i, a);
            section(n % M, b);
            const Vector3 mid = Vector3Lerp(a[6], b[6], 0.5f);
            const float len = Vector2Distance({a[6].x, a[6].z}, {b[6].x, b[6].z});
            railLength += len;
            const float yaw = std::atan2(b[6].x - a[6].x, b[6].z - a[6].z);
            const float yb = std::min(a[0].y, b[0].y);
            solid({mid.x, yb + 0.15f + 0.45f, mid.z}, {0.28f, 0.45f, len * 0.5f + 0.02f}, yaw);
            if (!geo) continue;
            MeshBuilder& m = S(mid, 40);
            for (int q = 0; q < 5; ++q) {
                const Vector3 o = Vector3Subtract(Vector3Lerp(a[q], a[q + 1], 0.5f), Vector3Lerp(a[6], a[6], 0.5f));
                const Vector3 outDir = q == 2 ? Vector3{0.0f, 1.0f, 0.0f} : Vector3{o.x, 0.0f, o.z};
                FaceOut(m, a[q], b[q], b[q + 1], a[q + 1], outDir, q == 2 ? Shade(kConcrete, 1.04f) : kConcrete, 0.06f, kPlaster);
            }
            // Punta del separador junto a un cruce: la cara del perfil, amarilla abajo y negra arriba (se ve de lejos).
            const Vector3 along = Vector3Normalize(Vector3Subtract(b[6], a[6]));
            for (int e = 0; e < 2; ++e) {
                if (!gap(e == 0 ? i - every : n)) continue;
                const Vector3* p = e == 0 ? a : b;
                const Vector3 out = e == 0 ? Vector3Negate(along) : along;
                FaceOut(m, p[0], p[1], p[4], p[5], out, {226, 182, 40, 255}, 0.2f, kPlain);
                FaceOut(m, p[1], p[2], p[3], p[4], out, {30, 30, 32, 255}, 0.2f, kPlain);
            }
        }
    }

    // ---------------------------------------------------------------- guardarraíles de afuera
    // La lámina (doble onda: dos caras inclinadas) de 0.47 a 0.79 m, en tramos de 8 m unidos punta con punta; postes
    // cada 4 m detrás (lo chico: sólo cerca). Colisión: una caja de 0.95 m de alto por tramo.
    for (float d : {-1.0f, 1.0f}) {
        const int every = 4;
        int post = 0;
        for (int i = 0; i < M; i += every) {
            const int n = std::min(i + every, M);
            const float s0 = (float)i * ds, s1 = (float)n * ds;
            const Vector3 a = W(s0, d * railLat), b = W(s1, d * railLat);
            const Vector3 mid = Vector3Lerp(a, b, 0.5f);
            const float len = Vector2Distance({a.x, a.z}, {b.x, b.z});
            const float yaw = std::atan2(b.x - a.x, b.z - a.z);
            const Vector3 ra = Vector3Scale(rightAt(s0), d), rb = Vector3Scale(rightAt(s1), d);   // hacia afuera
            railLength += len;
            solid({mid.x + ra.x * 0.1f, std::min(a.y, b.y) + 0.4f, mid.z + ra.z * 0.1f}, {0.16f, 0.55f, len * 0.5f + 0.02f}, yaw);
            if (!geo) continue;
            MeshBuilder& m = S(mid, 16);
            auto at = [&](Vector3 p, Vector3 r, float out, float up) { return Vector3{p.x + r.x * out, p.y + up, p.z + r.z * out}; };
            // Frente (hacia la pista): arriba inclinada para arriba, abajo para abajo.
            const Vector3 a0 = at(a, ra, 0.0f, 0.47f), a1 = at(a, ra, -0.07f, 0.63f), a2 = at(a, ra, 0.0f, 0.79f);
            const Vector3 b0 = at(b, rb, 0.0f, 0.47f), b1 = at(b, rb, -0.07f, 0.63f), b2 = at(b, rb, 0.0f, 0.79f);
            const Vector3 toRoad = Vector3Negate(ra);
            FaceOut(m, a1, b1, b2, a2, Vector3Add(toRoad, {0.0f, 0.5f, 0.0f}), kSteel, 0.55f, kPlain);
            FaceOut(m, a0, b0, b1, a1, Vector3Add(toRoad, {0.0f, -0.5f, 0.0f}), Shade(kSteel, 0.86f), 0.55f, kPlain);
            // Espalda (de afuera se ve) 4 cm detrás.
            const Vector3 c0 = at(a, ra, 0.04f, 0.47f), c2 = at(a, ra, 0.04f, 0.79f), e0 = at(b, rb, 0.04f, 0.47f), e2 = at(b, rb, 0.04f, 0.79f);
            FaceOut(m, c0, e0, e2, c2, ra, Shade(kSteel, 0.8f), 0.4f, kPlain);
            // Postes: dos por tramo (cada 4 m), detrás de la lámina.
            for (int q = 0; q < 2; ++q, ++post) {
                const float sp = mu::Lerp(s0, s1, (float)q * 0.5f);
                const Vector3 p = W(sp, d * (railLat + 0.12f));
                const float py = yaw;
                YawBox(Dt(p, 20), {p.x, p.y + 0.36f, p.z}, {0.07f, 0.46f, 0.06f}, py, kDarkSteel, 0.3f);
            }
        }
    }

    // ---------------------------------------------------------------- columnas de luz en el separador
    if (lightSpacing > 5.0f) {
        const float armOut = H.LanesFrom() + H.laneWidth * 0.8f;
        for (float s = 20.0f; s < L - 10.0f; s += lightSpacing) {
            if (nearBridge(s, 14.0f) || inCrossover(s) || inCrossover(s - 8.0f) || inCrossover(s + 8.0f)) continue;
            ++lights;
            if (!geo) continue;
            const Vector3 c = W(s, 0.0f), r = rightAt(s);
            MeshBuilder& m = S(c, 140);
            const float top = 11.0f;
            Cylinder(m, {c.x, c.y + 0.5f, c.z}, 0.11f, top - 0.5f, kSteel, 0.5f, 8, true);
            for (float d : {-1.0f, 1.0f}) {
                const Vector3 base = {c.x, c.y + top - 0.4f, c.z};
                const Vector3 tip = {c.x + r.x * d * armOut, c.y + top + 0.3f, c.z + r.z * d * armOut};
                Bar(m, base, tip, 0.05f, kSteel, 0.5f);
                YawBox(m, {tip.x, tip.y - 0.08f, tip.z}, {0.22f, 0.07f, 0.38f}, std::atan2(r.x, r.z), kDarkSteel, 0.6f);
                YawBox(m, {tip.x, tip.y - 0.17f, tip.z}, {0.16f, 0.02f, 0.3f}, std::atan2(r.x, r.z), {236, 232, 214, 255}, 0.8f);
            }
        }
    }

    // ---------------------------------------------------------------- pórticos con carteles verdes
    // De lado a lado de la autopista; en cada mano dos carteles mirando a los que vienen. Postes afuera del guardarraíl y
    // uno en el separador (con colisión).
    int signIdx = 0;
    if (gantrySpacing > 100.0f) {
        for (float s = gantrySpacing * 0.25f; s < L - 200.0f; s += gantrySpacing) {
            if (nearBridge(s, 60.0f)) continue;
            ++gantries;
            const Vector3 c = W(s, 0.0f), r = rightAt(s), f = fwdAt(s);
            const float yaw = std::atan2(f.x, f.z);
            const float span = railLat + 1.4f;
            const float y0 = c.y;
            for (float lat : {-span, 0.0f, span}) {
                const Vector3 p = W(s, lat);
                solid({p.x, p.y + 3.6f, p.z}, {0.3f, 3.8f, 0.3f}, yaw);
            }
            if (!geo) {
                signIdx += 4;
                continue;
            }
            MeshBuilder& m = S(c, 900);
            for (float lat : {-span, 0.0f, span}) {
                const Vector3 p = W(s, lat);
                YawBox(m, {p.x, p.y + 3.75f, p.z}, {0.22f, 3.8f, 0.22f}, yaw, kSteel, 0.45f);
                if (lat != 0.0f) YawBox(m, {p.x, p.y + 0.15f, p.z}, {0.5f, 0.3f, 0.5f}, yaw, kConcrete, 0.05f, kPlaster);
            }
            // Reticulado: cordones de arriba y de abajo, montantes y diagonales cada 2.5 m (a los dos lados de la viga).
            const Vector3 left = {c.x - r.x * span, y0, c.z - r.z * span}, right = {c.x + r.x * span, y0, c.z + r.z * span};
            for (float fo : {-0.45f, 0.45f})
                for (float h : {7.0f, 8.0f}) {
                    const Vector3 o = Vector3Scale(f, fo);
                    Bar(m, Vector3Add({left.x, left.y + h, left.z}, o), Vector3Add({right.x, right.y + h, right.z}, o), 0.07f, kSteel, 0.5f);
                }
            const int bays = (int)std::ceil(2.0f * span / 2.5f);
            for (int k = 0; k <= bays; ++k) {
                const float t0 = (float)k / (float)bays, t1 = (float)(k + 1) / (float)bays;
                const Vector3 p0 = Vector3Lerp(left, right, t0), p1 = Vector3Lerp(left, right, std::min(t1, 1.0f));
                for (float fo : {-0.45f, 0.45f}) {
                    const Vector3 o = Vector3Scale(f, fo);
                    Bar(m, Vector3Add({p0.x, p0.y + 7.0f, p0.z}, o), Vector3Add({p0.x, p0.y + 8.0f, p0.z}, o), 0.035f, kSteel, 0.5f);
                    if (k < bays) Bar(m, Vector3Add({p0.x, p0.y + 7.0f, p0.z}, o), Vector3Add({p1.x, p1.y + 8.0f, p1.z}, o), 0.03f, kSteel, 0.5f);
                }
            }
            // Carteles: mano +1 (va hacia +s) los mira desde atrás (s menor): cara hacia -f, delante de la viga.
            for (float d : {-1.0f, 1.0f}) {
                const Vector3 face = Vector3Scale(f, -d);
                const Vector3 sr = Vector3Scale(r, d);   // la derecha de quien lo mira es la derecha de su mano
                for (float lane : {0.5f, 2.0f}) {
                    const float lat = d * (H.LanesFrom() + lane * H.laneWidth + 0.5f);
                    const Vector3 p = W(s, lat);
                    const Vector3 cc = {p.x + face.x * 0.62f, y0 + 7.5f, p.z + face.z * 0.62f};
                    // Fondo: placa verde oscura de 8 cm; el texto 3 cm delante.
                    YawBox(m, {cc.x - face.x * 0.05f, cc.y, cc.z - face.z * 0.05f}, {2.9f, 0.78f, 0.04f}, yaw, Shade(kSignGreen, 0.7f), 0.2f);
                    Sign(X(cc, 4), {cc.x + face.x * 0.02f, cc.y, cc.z + face.z * 0.02f}, sr, face, 2.8f, 0.7f, CellUV(signIdx % kBigCells));
                    ++signIdx;
                }
            }
        }
    }

    // ---------------------------------------------------------------- carteles al costado: velocidad y kilómetros
    for (float d : {-1.0f, 1.0f}) {
        // Velocidad máxima (130) cada 3 km, un poco después de cada pórtico.
        for (float s = 900.0f; s < L - 100.0f; s += 3000.0f) {
            const float sp = d > 0.0f ? s : L - s;
            if (nearBridge(sp, 40.0f)) continue;
            const Vector3 p = W(sp, d * (railLat + 1.3f));
            const Vector3 f = fwdAt(sp), face = Vector3Scale(f, -d), sr = Vector3Scale(rightAt(sp), d);
            solid({p.x, p.y + 1.2f, p.z}, {0.08f, 1.2f, 0.08f}, std::atan2(f.x, f.z));
            if (!geo) continue;
            Bar(S(p, 16), p, {p.x, p.y + 2.9f, p.z}, 0.045f, kSteel, 0.4f);
            const Vector3 cc = {p.x + face.x * 0.07f, p.y + 2.6f, p.z + face.z * 0.07f};
            YawBox(S(p, 20), {cc.x - face.x * 0.03f, cc.y, cc.z - face.z * 0.03f}, {0.52f, 0.52f, 0.015f}, std::atan2(f.x, f.z), kDarkSteel, 0.2f);
            Sign(X(cc, 4), cc, sr, face, 0.5f, 0.5f, CellUV(kSpeedCell));
        }
        // Mojones de kilómetro: cada 1000 m de s, a los dos lados (los dos miran a los que vienen).
        for (int k = 1; (float)k * 1000.0f < L - 50.0f; ++k) {
            const float sp = (float)k * 1000.0f;
            if (nearBridge(sp, 30.0f)) continue;
            const Vector3 p = W(sp, d * (railLat + 0.8f));
            const Vector3 f = fwdAt(sp), face = Vector3Scale(f, -d), sr = Vector3Scale(rightAt(sp), d);
            if (!geo) continue;
            YawBox(Dt(p, 20), {p.x, p.y + 0.5f, p.z}, {0.12f, 0.6f, 0.05f}, std::atan2(f.x, f.z), {236, 236, 230, 255}, 0.2f);
            const Vector3 cc = {p.x + face.x * 0.075f, p.y + 0.85f, p.z + face.z * 0.075f};
            Sign(X(cc, 4), cc, sr, face, 0.18f, 0.18f, CellUV(kKmCell + (k - 1) % kKmCells));
        }
    }

    // ---------------------------------------------------------------- puentes de los cruces
    // El terraplén del camino que cruza es una forma del terreno (tools/autopista.py); la calzada lo corta y acá van los
    // estribos (paredes de hormigón desde la banquina hasta el terraplén), la pila del medio y el tablero, que se apoya en
    // la altura del terraplén de cada lado. Arriba, el asfalto del camino y sus parapetos; sobre el terraplén, el camino.
    for (float sb : bridgeS) {
        const Vector3 c = W(sb, 0.0f), r = rightAt(sb), f = fwdAt(sb);
        const float yaw = std::atan2(f.x, f.z);
        const float spanHalf = hw + shoulder + 2.0f, deckHalf = 6.0f;
        auto at = [&](float lat, float along, float y) { return Vector3{c.x + r.x * lat + f.x * along, y, c.z + r.z * lat + f.z * along}; };
        float top[2];
        for (int k = 0; k < 2; ++k) {
            const float side = k == 0 ? -1.0f : 1.0f;
            const Vector3 back = W(sb, side * (spanHalf + 5.0f));
            top[k] = std::max(back.y + 0.15f, c.y + 7.2f);
            if (back.y + 0.15f < c.y + 6.6f && !geo)
                std::printf("autopista: el terraplén del puente en s=%.0f queda bajo (%.1f m sobre la calzada)\n", sb, back.y - c.y);
        }
        const float bottomL = top[0] - 1.3f, bottomR = top[1] - 1.3f;
        // Estribos: de |lat| = hw + 1.6 hasta spanHalf + 1.5, a lo largo 7 m de cada lado del eje.
        for (int k = 0; k < 2; ++k) {
            const float side = k == 0 ? -1.0f : 1.0f;
            const float l0 = side * (hw + 1.6f), l1 = side * (spanHalf + 1.5f), yb = c.y - 1.0f, yt = (k == 0 ? bottomL : bottomR) + 0.05f;
            const Vector3 mid = at(0.5f * (l0 + l1), 0.0f, 0.5f * (yb + yt));
            solid(mid, {0.5f * std::fabs(l1 - l0), 0.5f * (yt - yb), 7.0f}, std::atan2(r.x, r.z));
            if (!geo) continue;
            const Vector3 lo[4] = {at(l0, -7.0f, yb), at(l1, -7.0f, yb), at(l1, 7.0f, yb), at(l0, 7.0f, yb)};
            const Vector3 hi[4] = {at(l0, -7.0f, yt), at(l1, -7.0f, yt), at(l1, 7.0f, yt), at(l0, 7.0f, yt)};
            Hexa(S(mid, 24), lo, hi, kConcrete, 0.06f, kPlaster, false);
        }
        // Pila en el separador (larga a lo largo de la autopista, 0.9 m de ancho).
        {
            const float yb = c.y - 0.5f, yt = 0.5f * (bottomL + bottomR) + 0.1f;
            const Vector3 mid = {c.x, 0.5f * (yb + yt), c.z};
            solid(mid, {0.45f, 0.5f * (yt - yb), 4.0f}, yaw);
            if (geo) YawBox(S(mid, 20), mid, {0.45f, 0.5f * (yt - yb), 4.0f}, yaw, kConcrete, 0.06f, kPlaster);
        }
        // Tablero (con la cara de abajo: se ve al pasar), asfalto arriba y parapetos.
        {
            const float L0 = -spanHalf - 1.5f, L1 = spanHalf + 1.5f;
            const Vector3 lo[4] = {at(L0, -deckHalf, bottomL), at(L1, -deckHalf, bottomR), at(L1, deckHalf, bottomR), at(L0, deckHalf, bottomL)};
            const Vector3 hi[4] = {at(L0, -deckHalf, top[0]), at(L1, -deckHalf, top[1]), at(L1, deckHalf, top[1]), at(L0, deckHalf, top[0])};
            const Vector3 mid = at(0.0f, 0.0f, 0.25f * (top[0] + top[1] + bottomL + bottomR));
            solid(mid, {spanHalf + 1.5f, 0.5f * (0.5f * (top[0] + top[1]) - 0.5f * (bottomL + bottomR)), deckHalf}, std::atan2(r.x, r.z));
            if (geo) {
                MeshBuilder& m = S(mid, 120);
                Hexa(m, lo, hi, Shade(kConcrete, 0.95f), 0.06f, kPlaster, true);
                const Vector3 q[4] = {at(L0, -4.5f, top[0] + 0.03f), at(L1, -4.5f, top[1] + 0.03f), at(L1, 4.5f, top[1] + 0.03f), at(L0, 4.5f, top[0] + 0.03f)};
                FaceOut(m, q[0], q[1], q[2], q[3], {0.0f, 1.0f, 0.0f}, kAsphalt, 0.15f, kPlain);
                for (float e : {-deckHalf + 0.15f, deckHalf - 0.15f}) {
                    const Vector3 plo[4] = {at(L0, e - 0.15f, top[0] - 0.02f), at(L1, e - 0.15f, top[1] - 0.02f), at(L1, e + 0.15f, top[1] - 0.02f),
                                            at(L0, e + 0.15f, top[0] - 0.02f)};
                    const Vector3 phi[4] = {at(L0, e - 0.15f, top[0] + 1.0f), at(L1, e - 0.15f, top[1] + 1.0f), at(L1, e + 0.15f, top[1] + 1.0f),
                                            at(L0, e + 0.15f, top[0] + 1.0f)};
                    Hexa(m, plo, phi, Shade(kConcrete, 1.08f), 0.06f, kPlaster, false);
                }
            }
        }
        // El camino sobre el terraplén, de cada lado: 9 m de asfalto que sigue el terreno, hasta 160 m.
        if (geo) {
            for (float side : {-1.0f, 1.0f}) {
                const float u0 = spanHalf + 1.5f;
                for (float u = u0; u < u0 + 160.0f; u += 4.0f) {
                    const Vector3 a = at(side * u, 0.0f, 0.0f), b = at(side * (u + 4.0f), 0.0f, 0.0f);
                    const Vector3 fa = Vector3Scale(f, 4.5f);
                    groundQuad(Vector3Subtract(a, fa), Vector3Add(a, fa), Vector3Add(b, fa), Vector3Subtract(b, fa), kAsphalt, 0.15f, 0.05f);
                }
            }
        }
    }

    // ---------------------------------------------------------------- el campo: árboles, casas, galpones y silos
    // Grilla de 24 m con azar fijo: montes de eucaliptos donde el ruido grande lo dice, hileras de álamos sobre los
    // alambrados (cada 400 m), y algún árbol suelto. Nada a menos de 48 m de la calzada, cerca de un puente ni en la
    // subida del borde del mapa.
    struct Coarse {
        float x, z, s;
    };
    std::vector<Coarse> coarse;                    // la línea central cada 10 m (buscar la más cercana sin recorrer todo)
    for (float s = 0.0f; s < L; s += 10.0f) {
        const TrackPoint p = T.At(s);
        coarse.push_back({p.x, p.z, s});
    }
    auto farFromRoad = [&](float x, float z, float m) {
        if (G.RoadDistance(x, z) < m) return false;
        float best = 1e18f, bs = 0.0f;
        for (const Coarse& c : coarse) {
            const float d = (c.x - x) * (c.x - x) + (c.z - z) * (c.z - z);
            if (d < best) {
                best = d;
                bs = c.s;
            }
        }
        if (best < (m + 5.0f) * (m + 5.0f)) return false;
        return !nearBridge(bs, 190.0f) || best > 260.0f * 260.0f;
    };
    if (trees > 0.0f) {
        const float cell = 24.0f;
        const int n = (int)std::floor((size - 120.0f) / cell);
        const float lim = half - 70.0f;
        Rng tr{0xE0CA11u};
        int roadChecks = 0;
        for (int gz = 0; gz < n; ++gz)
            for (int gx = 0; gx < n; ++gx) {
                const float x0 = -lim + (float)gx * cell, z0 = -lim + (float)gz * cell;
                const float grove = ValueNoise(x0 / 260.0f + 11.0f, z0 / 260.0f - 4.0f);
                const bool rowX = std::fabs(std::fmod(x0 + 4000.0f, 400.0f)) < cell, rowZ = std::fabs(std::fmod(z0 + 4000.0f, 400.0f)) < cell;
                const float h = Hash(gx * 7 + 3, gz * 13 + 1);
                int kind = -1;                                       // 0 eucalipto, 1 álamo
                if (grove > 0.70f && h < 0.75f * trees) kind = 0;
                else if ((rowX || rowZ) && ValueNoise(x0 / 500.0f, z0 / 500.0f + 3.0f) > 0.55f && h < 0.9f * trees) kind = 1;
                else if (h < 0.012f * trees) kind = 0;
                if (kind < 0) continue;
                const float x = x0 + (rowX && kind == 1 ? 0.0f : Hash(gx, gz + 77) * cell * 0.8f), z = z0 + (rowZ && kind == 1 ? 0.0f : Hash(gx + 31, gz) * cell * 0.8f);
                ++roadChecks;
                if (!farFromRoad(x, z, 48.0f)) continue;
                const float y = G.Height(x, z) - 0.2f;
                ++treeCount;
                if (!geo) continue;
                const float scale = 0.8f + 0.5f * Hash(gx + 5, gz + 9);
                const float twist = Hash(gx + 2, gz + 3) * kTau;
                MeshBuilder& m = S({x, y, z}, 60);
                if (kind == 0) {                                     // eucalipto: tronco alto y copa en dos conos
                    const float ht = 15.0f * scale;
                    YawBox(m, {x, y + ht * 0.25f, z}, {0.25f, ht * 0.25f, 0.25f}, twist, kTrunk, 0.05f);
                    const Color leaf = Shade({58, 86, 60, 255}, 0.85f + 0.3f * Hash(gx + 8, gz + 1));
                    Cone(m, {x, y + ht * 0.35f, z}, 3.6f * scale, ht * 0.45f, leaf, 6, twist, false);
                    Cone(m, {x, y + ht * 0.6f, z}, 2.8f * scale, ht * 0.4f, Shade(leaf, 1.08f), 6, twist + 0.5f, false);
                } else {                                             // álamo: alto y finito
                    const float ht = 17.0f * scale;
                    YawBox(m, {x, y + 1.0f, z}, {0.18f, 1.0f, 0.18f}, twist, kTrunk, 0.05f);
                    const Color leaf = Shade({92, 118, 62, 255}, 0.85f + 0.3f * Hash(gx + 8, gz + 1));
                    Cone(m, {x, y + 1.6f, z}, 1.7f * scale, ht, leaf, 6, twist, false);
                }
            }
        (void)roadChecks;
        (void)tr;
    }
    // Chacras: una casa con techo a dos aguas, un galpón y uno o dos silos, con un monte alrededor.
    {
        Rng fr{0xC4ACAu};
        const float lim = half - 140.0f;
        for (int k = 0; k < 60; ++k) {
            const float x = fr.R(-lim, lim), z = fr.R(-lim, lim);
            const float yaw = fr.R(0.0f, kTau);
            const bool ok = farFromRoad(x, z, 110.0f);
            const float roofHue = fr.F(), siloN = fr.F(), wall = fr.F();
            if (!ok) continue;
            ++buildings;
            const float y = G.Height(x, z);
            const Frame fm = YawFrame(yaw);
            auto P = [&](float u, float v, float h) { return Vector3{x + fm.x.x * u + fm.z.x * v, y + h, z + fm.x.z * u + fm.z.z * v}; };
            solid(P(0.0f, 0.0f, 1.5f), {5.0f, 2.0f, 4.0f}, yaw);
            solid(P(16.0f, 4.0f, 3.0f), {7.0f, 3.5f, 11.0f}, yaw);
            if (!geo) continue;
            MeshBuilder& m = S({x, y, z}, 400);
            const Color wallC = wall < 0.5f ? Color{226, 218, 198, 255} : Color{214, 196, 168, 255};
            Box(m, P(0.0f, 0.0f, 1.2f), {5.0f, 1.9f, 4.0f}, fm, wallC, 0.05f, kPlaster);
            // Techo a dos aguas (tejas o chapa).
            const Color roof = roofHue < 0.5f ? Color{168, 74, 52, 255} : Color{120, 124, 128, 255};
            const Vector3 r0 = P(-5.4f, -4.4f, 3.05f), r1 = P(5.4f, -4.4f, 3.05f), r2 = P(5.4f, 0.0f, 4.9f), r3 = P(-5.4f, 0.0f, 4.9f);
            const Vector3 r4 = P(5.4f, 4.4f, 3.05f), r5 = P(-5.4f, 4.4f, 3.05f);
            FaceOut(m, r0, r1, r2, r3, Vector3Add(Vector3Negate(fm.z), {0.0f, 1.0f, 0.0f}), roof, 0.2f, kPlain);
            FaceOut(m, r3, r2, r4, r5, Vector3Add(fm.z, {0.0f, 1.0f, 0.0f}), Shade(roof, 0.9f), 0.2f, kPlain);
            for (float u : {-5.0f, 5.0f}) {                          // los triángulos del frente y del fondo
                const Vector3 t0 = P(u, -4.0f, 3.1f), t1 = P(u, 4.0f, 3.1f), t2 = P(u, 0.0f, 4.85f);
                const Vector3 nn = Vector3Scale(fm.x, u > 0.0f ? 1.0f : -1.0f);
                m.Tri(m.Vertex(t0, nn, {0.05f, kPlaster}, wallC), m.Vertex(t1, nn, {0.05f, kPlaster}, wallC), m.Vertex(t2, nn, {0.05f, kPlaster}, wallC));
            }
            m.FixWinding(m.idx.size() - 6);
            // Galpón de chapa con techo curvo (tres aguas planas).
            const Color shed = {150, 156, 160, 255};
            Box(m, P(16.0f, 4.0f, 2.2f), {7.0f, 2.7f, 11.0f}, fm, shed, 0.35f);
            for (int q = 0; q < 3; ++q) {
                const float a0 = -7.0f + (float)q * 14.0f / 3.0f, a1 = a0 + 14.0f / 3.0f;
                auto hh = [](float u) { return 4.9f + 1.6f * (1.0f - (u / 7.0f) * (u / 7.0f)); };
                const Vector3 q0 = P(16.0f + a0, -7.0f, hh(a0)), q1 = P(16.0f + a1, -7.0f, hh(a1)), q2 = P(16.0f + a1, 15.0f, hh(a1)), q3 = P(16.0f + a0, 15.0f, hh(a0));
                FaceOut(m, q0, q1, q2, q3, {0.0f, 1.0f, 0.0f}, Shade(shed, 1.05f - 0.05f * (float)q), 0.4f, kPlain);
            }
            const int silos = siloN < 0.4f ? 1 : 2;
            for (int q = 0; q < silos; ++q) {
                const Vector3 p = P(28.0f + (float)q * 6.0f, -4.0f, -0.2f);
                Cylinder(m, p, 2.4f, 11.0f, {190, 194, 198, 255}, 0.5f, 10, false);
                Cone(m, {p.x, p.y + 11.0f, p.z}, 2.5f, 2.2f, {176, 180, 186, 255}, 10, 0.0f);
            }
        }
    }
}

// ------------------------------------------------------------------------ colisión
void Highway::CreateCollision(PhysicsWorld& world)
{
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
            std::printf("autopista: error de colisión: %s\n", r.GetError().c_str());
            continue;
        }
        JPH::BodyCreationSettings bcs(r.Get(), JPH::RVec3::sZero(), JPH::Quat::sIdentity(), JPH::EMotionType::Static, Layers::STATIC);
        bcs.mFriction = 0.6f;
        bodies.push_back(world.Bodies().CreateAndAddBody(bcs, JPH::EActivation::DontActivate));
    }
}

// ------------------------------------------------------------------------ atlas de carteles
void Highway::BuildAtlas()
{
    Image img = GenImageColor(kAtlas, kAtlas, Color{255, 255, 255, 255});
    std::string all = name;
    for (const std::string& b : destinations) all += b;
    all += " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZÁÉÍÓÚÑabcdefghijklmnopqrstuvwxyz.,-'";
    int count = 0;
    int* cps = LoadCodepoints(all.c_str(), &count);
    std::vector<int> unique(cps, cps + count);
    UnloadCodepoints(cps);
    std::sort(unique.begin(), unique.end());
    unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
    Font font{};
    bool own = false;
    // Los carteles viales usan una de palo seco: Arial negrita (Windows y Mac) o Segoe.
    for (const char* path : {"C:/Windows/Fonts/arialbd.ttf", "C:/Windows/Fonts/segoeuib.ttf", "/System/Library/Fonts/Supplemental/Arial Bold.ttf"}) {
        if (!FileExists(path)) continue;
        font = LoadFontEx(path, 96, unique.data(), (int)unique.size());
        if (font.texture.id != 0) {
            own = true;
            break;
        }
    }
    if (!own) font = GetFontDefault();
    auto fitText = [&](const char* text, Rectangle cell, float maxSize, Color fg, float spacing) {
        float sz = maxSize;
        Vector2 m = MeasureTextEx(font, text, sz, spacing);
        while ((m.x > cell.width - 40.0f || m.y > cell.height - 16.0f) && sz > 12.0f) {
            sz -= 2.0f;
            m = MeasureTextEx(font, text, sz, spacing);
        }
        ImageDrawTextEx(&img, font, text, {cell.x + (cell.width - m.x) * 0.5f, cell.y + (cell.height - m.y) * 0.5f}, sz, spacing, fg);
    };
    const Color green = {14, 104, 62, 255}, white = {246, 246, 240, 255};
    for (int i = 0; i < kBigCells; ++i) {
        const Rectangle cell = CellPixels(i);
        ImageDrawRectangleRec(&img, cell, green);
        ImageDrawRectangleLines(&img, {cell.x + 7.0f, cell.y + 7.0f, cell.width - 14.0f, cell.height - 14.0f}, 4, white);
        fitText(destinations[(size_t)i % destinations.size()].c_str(), cell, 70.0f, white, 2.0f);
    }
    for (int k = 0; k < kKmCells; ++k) {
        const Rectangle cell = CellPixels(kKmCell + k);
        ImageDrawRectangleRec(&img, cell, white);
        ImageDrawRectangleLines(&img, {cell.x + 4.0f, cell.y + 4.0f, cell.width - 8.0f, cell.height - 8.0f}, 5, Color{30, 30, 30, 255});
        fitText("KM", {cell.x, cell.y + 6.0f, cell.width, cell.height * 0.45f}, 40.0f, Color{30, 30, 30, 255}, 1.0f);
        fitText(std::to_string(k + 1).c_str(), {cell.x, cell.y + cell.height * 0.42f, cell.width, cell.height * 0.55f}, 64.0f, Color{30, 30, 30, 255}, 1.0f);
    }
    {
        const Rectangle cell = CellPixels(kSpeedCell);
        ImageDrawRectangleRec(&img, cell, white);
        ImageDrawCircle(&img, (int)(cell.x + 64), (int)(cell.y + 64), 60, Color{200, 30, 30, 255});
        ImageDrawCircle(&img, (int)(cell.x + 64), (int)(cell.y + 64), 47, white);
        fitText("130", {cell.x + 20.0f, cell.y + 20.0f, 88.0f + 40.0f, 88.0f}, 56.0f, Color{20, 20, 20, 255}, 0.0f);
    }
    {
        const Rectangle cell = CellPixels(kExitCell);
        ImageDrawRectangleRec(&img, cell, green);
        ImageDrawTriangle(&img, {cell.x + 64, cell.y + 20}, {cell.x + 30, cell.y + 70}, {cell.x + 98, cell.y + 70}, white);
        ImageDrawRectangleRec(&img, {cell.x + 54, cell.y + 68, 20, 40}, white);
    }
    ImageDrawRectangleRec(&img, CellPixels(kWhiteCell), WHITE);
    if (own) UnloadFont(font);
    atlas = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&atlas);
    SetTextureFilter(atlas, TEXTURE_FILTER_TRILINEAR);
    atlasAniso = -1;
}

// ------------------------------------------------------------------------ mallas
void Highway::CreateMeshes()
{
    if (!built) return;
    Unload();
    BuildAtlas();
    Geometry geo;
    Generate(&geo);
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
        c.hasSolid = !c.solid.empty() || !c.detail.empty();
        meshCount += (int)(c.solid.size() + c.decal.size() + c.textured.size() + c.detail.size());
    }
    std::printf("autopista: %d mallas, %d vértices\n", meshCount, verts);
    meshes = true;
}

// ------------------------------------------------------------------------ dibujo
void Highway::Draw(Renderer& r) const
{
    if (!meshes) return;
    if (atlasAniso != r.AnisoRevision()) {
        r.ApplyAniso(atlas);
        atlasAniso = r.AnisoRevision();
    }
    const Vector3 cam = r.CameraPosition();
    auto distance = [&](const Chunk& c) {
        const float dx = std::max({c.lo.x - cam.x, 0.0f, cam.x - c.hi.x}), dz = std::max({c.lo.z - cam.z, 0.0f, cam.z - c.hi.z});
        return std::sqrt(dx * dx + dz * dz);
    };
    std::vector<const Chunk*> shown;
    shown.reserve(chunks.size());
    for (const Chunk& c : chunks)
        if (c.lo.x <= c.hi.x && distance(c) < 950.0f && r.BoxVisible(c.lo, c.hi)) shown.push_back(&c);
    for (const Chunk* c : shown)
        for (const Mesh& m : c->solid) r.DrawMeshColored(m, MatrixIdentity(), WHITE);
    // Lo chico (postes del guardarraíl, mojones) hasta graphics.detailRadius (0 = sin límite).
    const float detailRadius = (float)r.graphics.detailRadius;
    for (const Chunk* c : shown)
        if (detailRadius <= 0.0f || distance(*c) < detailRadius)
            for (const Mesh& m : c->detail) r.DrawMeshColored(m, MatrixIdentity(), WHITE);
    for (const Chunk* c : shown)
        for (const Mesh& m : c->decal) r.DrawMeshColored(m, MatrixIdentity(), WHITE);
    r.SetGloss(0.15f);
    for (const Chunk* c : shown)
        for (const Mesh& m : c->textured) r.DrawMeshTextured(m, MatrixIdentity(), atlas, false);
    r.SetGloss(0.3f);
}

void Highway::DrawShadows(Renderer& r) const
{
    if (!meshes) return;
    for (const Chunk& c : chunks) {
        if (!c.hasSolid) continue;
        if (!r.InShadowPrism(c.lo, c.hi)) continue;
        for (const Mesh& m : c.solid) r.DrawMeshColored(m, MatrixIdentity(), WHITE);
    }
}

void Highway::Unload()
{
    if (!meshes) return;
    for (Chunk& c : chunks) {
        for (Mesh& m : c.solid) UnloadMesh(m);
        for (Mesh& m : c.decal) UnloadMesh(m);
        for (Mesh& m : c.textured) UnloadMesh(m);
        for (Mesh& m : c.detail) UnloadMesh(m);
    }
    chunks.clear();
    UnloadTexture(atlas);
    atlas = Texture2D{};
    meshes = false;
}
