// Morro do Grau: todo lo que hay alrededor de las calles de la favela. Las casas se ubican con una
// semilla fija sobre el terreno ya armado (primero las que dan a la calle, después las de atrás, que
// llenan el morro), y se dibujan en mallas por bloques de 32 m con el color en los vértices. Los
// muros usan dos modos del shader iluminado (ladrillo a la vista y revoque), ver kLitFS.
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>

#include "Favela.h"

#include "MathUtil.h"
#include "MeshBuilder.h"
#include "PhysicsWorld.h"
#include "Render.h"
#include "Terrain.h"
#include "Track.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

constexpr float kFloor = 2.8f;            // alto de un piso
constexpr float kTau = 6.2831853f;
// Modos del shader iluminado (v de la UV): 2 = brillo propio, 3 = ladrillo, 4 = revoque.
constexpr float kPlain = 2.0f, kBrick = 3.0f, kPlaster = 4.0f;

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

uint32_t HashSeed(uint32_t a, uint32_t b) { return (a * 2654435761u) ^ (b * 2246822519u) ^ 0x9e3779b9u; }

Color Shade(Color c, float k)
{
    auto f = [k](unsigned char v) { return (unsigned char)std::min(255.0f, (float)v * k); };
    return {f(c.r), f(c.g), f(c.b), c.a};
}

// Paleta del morro: revoques pintados (colores vivos gastados), ladrillo cerámico y cemento.
const Color kPaint[] = {
    {226, 186, 72, 255}, {92, 158, 196, 255}, {214, 118, 138, 255}, {124, 176, 108, 255}, {236, 230, 214, 255},
    {226, 138, 62, 255}, {150, 112, 184, 255}, {196, 82, 70, 255}, {72, 132, 158, 255}, {232, 214, 150, 255},
    {182, 206, 120, 255}, {244, 170, 110, 255},
};
const Color kBrickTone = {184, 92, 58, 255};
const Color kConcrete = {150, 146, 138, 255};
const Color kSlab = {158, 154, 146, 255};
const Color kGlass = {34, 40, 50, 255};
const Color kGate[] = {{42, 92, 160, 255}, {58, 118, 70, 255}, {118, 72, 44, 255}, {88, 88, 94, 255}, {176, 52, 42, 255}, {200, 170, 50, 255}};
const Color kTank = {44, 112, 186, 255};
const Color kWire = {24, 24, 26, 255};
const Color kBunting[] = {{226, 40, 40, 255}, {250, 204, 30, 255}, {40, 110, 210, 255}, {40, 160, 70, 255}, {245, 245, 240, 255}, {240, 110, 30, 255}};

// ------------------------------------------------------------------------ primitivas
// Ejes de una caja con rumbo yaw (giro alrededor de Y): +Z local -> (sin, 0, cos).
struct Frame {
    Vector3 x, y, z;
};
Frame YawFrame(float yaw) { return {{std::cos(yaw), 0.0f, -std::sin(yaw)}, {0.0f, 1.0f, 0.0f}, {std::sin(yaw), 0.0f, std::cos(yaw)}}; }

Vector3 Add3(Vector3 a, Vector3 b, Vector3 c, Vector3 d) { return Vector3Add(Vector3Add(a, b), Vector3Add(c, d)); }

// Caja con ejes f (unitarios) y medio tamaño h. uv = (brillo, modo) en todos los vértices.
void Box(MeshBuilder& b, Vector3 c, Vector3 h, const Frame& f, Color col, float gloss = 0.1f, float mode = kPlain)
{
    const Vector3 ax[3] = {f.x, f.y, f.z};
    const float he[3] = {h.x, h.y, h.z};
    for (int a = 0; a < 3; ++a)
        for (float sgn : {-1.0f, 1.0f}) {
            const int u = (a + 1) % 3, v = (a + 2) % 3;
            const Vector3 n = Vector3Scale(ax[a], sgn);
            const Vector3 fc = Vector3Add(c, Vector3Scale(ax[a], sgn * he[a]));
            const Vector3 du = Vector3Scale(ax[u], he[u]), dv = Vector3Scale(ax[v], he[v]);
            const unsigned short i0 = b.Vertex(Vector3Subtract(Vector3Subtract(fc, du), dv), n, {gloss, mode}, col);
            const unsigned short i1 = b.Vertex(Vector3Subtract(Vector3Add(fc, du), dv), n, {gloss, mode}, col);
            const unsigned short i2 = b.Vertex(Vector3Add(Vector3Add(fc, du), dv), n, {gloss, mode}, col);
            const unsigned short i3 = b.Vertex(Vector3Add(Vector3Subtract(fc, du), dv), n, {gloss, mode}, col);
            // Orden según la normal (los generadores no cuidan el sentido de giro).
            const Vector3 face = Vector3CrossProduct(Vector3Subtract(b.P(i1), b.P(i0)), Vector3Subtract(b.P(i2), b.P(i0)));
            if (Vector3DotProduct(face, n) >= 0.0f) {
                b.Tri(i0, i1, i2);
                b.Tri(i0, i2, i3);
            } else {
                b.Tri(i0, i2, i1);
                b.Tri(i0, i3, i2);
            }
        }
}

void YawBox(MeshBuilder& b, Vector3 c, Vector3 h, float yaw, Color col, float gloss = 0.1f, float mode = kPlain)
{
    Box(b, c, h, YawFrame(yaw), col, gloss, mode);
}

// Barra entre dos puntos (sección cuadrada de lado 2r).
void Bar(MeshBuilder& b, Vector3 p0, Vector3 p1, float r, Color col, float gloss = 0.2f)
{
    const Vector3 d = Vector3Subtract(p1, p0);
    const float len = Vector3Length(d);
    if (len < 1e-4f) return;
    const Vector3 z = Vector3Scale(d, 1.0f / len);
    Vector3 x = Vector3CrossProduct(std::fabs(z.y) < 0.9f ? Vector3{0.0f, 1.0f, 0.0f} : Vector3{1.0f, 0.0f, 0.0f}, z);
    x = Vector3Normalize(x);
    const Vector3 y = Vector3CrossProduct(z, x);
    Box(b, Vector3Lerp(p0, p1, 0.5f), {r, r, len * 0.5f}, {x, y, z}, col, gloss);
}

// Cilindro vertical (tanques de agua, postes redondos). Base en c.
void Cylinder(MeshBuilder& b, Vector3 c, float r, float h, Color col, float gloss = 0.3f, int slices = 12)
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
    const Color top = Shade(col, 0.85f);
    const unsigned short ct = b.Vertex({c.x, c.y + h, c.z}, {0.0f, 1.0f, 0.0f}, {gloss, kPlain}, top);
    for (int i = 0; i <= slices; ++i) {
        const float a = kTau * (float)i / (float)slices;
        b.Vertex({c.x + std::cos(a) * r, c.y + h, c.z + std::sin(a) * r}, {0.0f, 1.0f, 0.0f}, {gloss, kPlain}, top);
    }
    for (int i = 0; i < slices; ++i) b.Tri(ct, (unsigned short)(ct + 2 + i), (unsigned short)(ct + 1 + i));
    b.FixWinding(b.idx.size() - (size_t)slices * 3);
}

// Cuadrilátero de dos caras (banderines, ropa, banderas): p0..p3 en orden.
void Quad2(MeshBuilder& b, Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, Color col)
{
    const Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(p1, p0), Vector3Subtract(p3, p0)));
    const unsigned short a = b.Vertex(p0, n, {0.05f, kPlain}, col), bb = b.Vertex(p1, n, {0.05f, kPlain}, col);
    const unsigned short c = b.Vertex(p2, n, {0.05f, kPlain}, col), d = b.Vertex(p3, n, {0.05f, kPlain}, col);
    b.Tri(a, bb, c);
    b.Tri(a, c, d);
}

void Tri2(MeshBuilder& b, Vector3 p0, Vector3 p1, Vector3 p2, Color col)
{
    const Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(p1, p0), Vector3Subtract(p2, p0)));
    b.Tri(b.Vertex(p0, n, {0.05f, kPlain}, col), b.Vertex(p1, n, {0.05f, kPlain}, col), b.Vertex(p2, n, {0.05f, kPlain}, col));
}

// Cable colgando entre a y b (parábola) hecho de tramos finos.
void Cable(MeshBuilder& b, Vector3 a, Vector3 c, float sag, float r, Color col)
{
    const int n = 8;
    Vector3 prev = a;
    for (int i = 1; i <= n; ++i) {
        const float t = (float)i / (float)n;
        Vector3 p = Vector3Lerp(a, c, t);
        p.y -= sag * 4.0f * t * (1.0f - t);
        Bar(b, prev, p, r, col, 0.1f);
        prev = p;
    }
}

Vector3 SagPoint(Vector3 a, Vector3 c, float sag, float t)
{
    Vector3 p = Vector3Lerp(a, c, t);
    p.y -= sag * 4.0f * t * (1.0f - t);
    return p;
}

// Carteles y grafitis del atlas (1024 x 1024, celdas de 512 x 128).
struct SignSpec {
    const char* text;
    Color bg, fg;
    int style;                            // 0 = cartel con fondo, 1 = grafiti de burbuja, 2 = pixo (letras altas y finas), 3 = bandera
};
const SignSpec kSigns[] = {
    {"BORRACHARIA 24H", {250, 206, 36, 255}, {20, 20, 20, 255}, 0},
    {"OFICINA DE MOTOS", {28, 78, 168, 255}, {245, 245, 240, 255}, 0},
    {"MOTO TÁXI", {240, 118, 22, 255}, {20, 20, 20, 255}, 0},
    {"BAR DO ZÉ", {188, 30, 32, 255}, {250, 240, 220, 255}, 0},
    {"AÇAÍ", {104, 30, 108, 255}, {250, 250, 250, 255}, 0},
    {"LAVA JATO", {20, 146, 188, 255}, {250, 250, 250, 255}, 0},
    {"PEÇAS PARA MOTOS", {245, 245, 240, 255}, {196, 30, 30, 255}, 0},
    {"MERCADINHO", {30, 128, 62, 255}, {252, 214, 40, 255}, 0},
    {"GRAU É ARTE", {0, 0, 0, 0}, {60, 200, 90, 255}, 1},
    {"FAVELA VENCEU", {0, 0, 0, 0}, {250, 200, 30, 255}, 1},
    {"BONDE DO GRAU", {0, 0, 0, 0}, {40, 150, 240, 255}, 1},
    {"É NÓIS", {0, 0, 0, 0}, {236, 64, 120, 255}, 1},
    {"VIDA LOKA", {0, 0, 0, 0}, {18, 18, 18, 255}, 2},
    {"PAZ NO MORRO", {0, 0, 0, 0}, {18, 18, 18, 255}, 2},
    {"MORRO DO GRAU", {22, 120, 58, 255}, {252, 214, 40, 255}, 0},
    {"", {0, 0, 0, 0}, {0, 0, 0, 0}, 3},
};
constexpr int kSignCount = sizeof(kSigns) / sizeof(kSigns[0]);
constexpr int kShopSigns = 8;             // los primeros: carteles de negocios
constexpr int kGraffitiFirst = 8, kGraffitiCount = 6;
constexpr int kBannerSign = 14, kFlagSign = 15;

} // namespace

// ------------------------------------------------------------------------ ubicación
int Favela::ChunkOf(float x, float z) const
{
    const int cx = std::clamp((int)((x - origin) / (size / kChunks)), 0, kChunks - 1);
    const int cz = std::clamp((int)((z - origin) / (size / kChunks)), 0, kChunks - 1);
    return cz * kChunks + cx;
}

bool Favela::FootprintFree(const Terrain& terrain, float x, float z, float yaw, float w, float d, float minRoad) const
{
    const Frame f = YawFrame(yaw);
    const int nu = std::max(2, (int)std::ceil(w / 0.8f)), nv = std::max(2, (int)std::ceil(d / 0.8f));
    for (int i = 0; i <= nu; ++i)
        for (int j = 0; j <= nv; ++j) {
            const float u = -w * 0.5f + w * (float)i / (float)nu, v = -d * 0.5f + d * (float)j / (float)nv;
            const float px = x + f.x.x * u + f.z.x * v, pz = z + f.x.z * u + f.z.z * v;
            if (std::fabs(px) > 124.0f || std::fabs(pz) > 124.0f) return false;
            if (terrain.RoadDistance(px, pz) < minRoad) return false;
            const int gx = (int)((px - origin) / 0.5f), gz = (int)((pz - origin) / 0.5f);
            if (gx < 0 || gz < 0 || gx >= 512 || gz >= 512 || occupied[gz * 512 + gx]) return false;
        }
    return true;
}

void Favela::MarkFootprint(float x, float z, float yaw, float w, float d)
{
    const Frame f = YawFrame(yaw);
    const float margin = 0.25f, r = 0.5f * std::sqrt(w * w + d * d) + margin;
    const int gx0 = std::max(0, (int)((x - r - origin) / 0.5f)), gx1 = std::min(511, (int)((x + r - origin) / 0.5f));
    const int gz0 = std::max(0, (int)((z - r - origin) / 0.5f)), gz1 = std::min(511, (int)((z + r - origin) / 0.5f));
    for (int gz = gz0; gz <= gz1; ++gz)
        for (int gx = gx0; gx <= gx1; ++gx) {
            const float px = origin + (gx + 0.5f) * 0.5f - x, pz = origin + (gz + 0.5f) * 0.5f - z;
            const float u = px * f.x.x + pz * f.x.z, v = px * f.z.x + pz * f.z.z;
            if (std::fabs(u) <= w * 0.5f + margin && std::fabs(v) <= d * 0.5f + margin) occupied[gz * 512 + gx] = 1;
        }
}

void Favela::Build(const Track& track, const Terrain& terrain)
{
    houses.clear();
    solids.clear();
    wires.clear();
    buntings.clear();
    signs.clear();
    kites.clear();
    steps.clear();
    parked.clear();
    poles.clear();
    stripes.clear();
    stripeSize.clear();
    occupied.assign(512 * 512, 0);
    origin = terrain.OriginX();
    size = terrain.Size();
    PlaceStreetStuff(track, terrain);     // antes: la escadaria y los postes reservan su lugar
    PlaceHouses(track, terrain);
    built = true;
    std::printf("favela: %zu casas, %zu postes, %zu cables, %zu carteles\n", houses.size(), poles.size(), wires.size(), signs.size());
}

void Favela::PlaceHouses(const Track& track, const Terrain& terrain)
{
    const float hw = track.HalfWidth();
    Rng rng{0xC0FFEEu};
    const float L = track.Length();

    auto addHouse = [&](float x, float z, float yaw, float w, float d, int floors, bool front) {
        const Frame f = YawFrame(yaw);
        // Cimientos: el punto más bajo del terreno bajo la casa. Entrada: la vereda (de frente) o el
        // punto más alto (las de atrás, para que el techo quede arriba del morro).
        float lo = 1e9f, hi = -1e9f;
        for (float u : {-0.5f, 0.0f, 0.5f})
            for (float v : {-0.5f, 0.0f, 0.5f}) {
                const float px = x + f.x.x * u * w + f.z.x * v * d, pz = z + f.x.z * u * w + f.z.z * v * d;
                const float h = std::min(terrain.Height(px, pz), terrain.NaturalHeight(px, pz));
                lo = std::min(lo, h);
                hi = std::max(hi, std::max(terrain.Height(px, pz), terrain.NaturalHeight(px, pz)));
            }
        float entry = hi + 0.05f;
        if (front) {
            const float fx = x - f.z.x * d * 0.5f, fz = z - f.z.z * d * 0.5f;
            entry = terrain.Height(fx - f.z.x * 0.3f, fz - f.z.z * 0.3f) + 0.05f;
            entry = std::max(entry, hi - kFloor * 1.2f);           // del lado de arriba de la calle: que no quede enterrada
        }
        House h{x, z, yaw, w, d, lo - 0.6f, entry, floors, rng.s ^ HashSeed((uint32_t)houses.size(), 77u), front};
        houses.push_back(h);
        MarkFootprint(x, z, yaw, w, d);
        // Colisión: un bloque de los cimientos al techo (los pisos de arriba pueden volar un poco hacia la
        // calle, ver CreateMeshes; la caja de colisión es la del cuerpo principal).
        const float top = entry + floors * kFloor + 0.15f;
        solids.push_back({{x, (h.base + top) * 0.5f, z}, {w * 0.5f, (top - h.base) * 0.5f, d * 0.5f}, yaw});
    };

    // 1) Las que dan a la calle, de los dos lados, pegadas una al lado de la otra (con algún pasillo).
    for (float side : {-1.0f, 1.0f}) {
        float s = side > 0.0f ? 0.0f : 2.5f;
        while (s < L) {
            const TrackPoint p = track.At(s);
            const float lx = -p.tz * side, lz = p.tx * side;         // hacia afuera de la calle
            const float yaw = std::atan2(lx, lz);
            float w = rng.R(3.8f, 6.6f), d = rng.R(5.5f, 9.0f);
            const float setback = hw + rng.R(0.9f, 1.4f);
            bool placed = false;
            for (int attempt = 0; attempt < 3 && !placed; ++attempt) {
                const float cx = p.x + lx * (setback + d * 0.5f), cz = p.z + lz * (setback + d * 0.5f);
                if (FootprintFree(terrain, cx, cz, yaw, w, d, hw + 0.8f)) {
                    const float r = rng.F();
                    addHouse(cx, cz, yaw, w, d, r < 0.3f ? 1 : (r < 0.75f ? 2 : 3), true);
                    placed = true;
                } else {
                    w *= 0.75f;
                    d *= 0.75f;
                }
            }
            s += placed ? w + (rng.Chance(0.25f) ? rng.R(1.3f, 2.4f) : 0.05f) : 1.2f;
        }
    }

    // 2) Las de atrás: llenan el morro entre calles, alineadas con la calle más cercana.
    auto alignedYaw = [&](float x, float z) {
        const TrackPoint& p = track.Points()[track.Nearest(x, z)];
        float lx = -p.tz, lz = p.tx;
        if ((x - p.x) * lx + (z - p.z) * lz < 0.0f) {
            lx = -lx;
            lz = -lz;
        }
        return std::atan2(lx, lz);
    };
    for (int pass = 0; pass < 2; ++pass) {
        const float step = pass == 0 ? 7.0f : 4.5f;
        for (float gz = -122.0f; gz < 122.0f; gz += step)
            for (float gx = -122.0f; gx < 122.0f; gx += step) {
                const float x = gx + rng.R(-1.5f, 1.5f), z = gz + rng.R(-1.5f, 1.5f);
                if (terrain.RoadDistance(x, z) < hw + 2.0f) continue;
                const float yaw = alignedYaw(x, z) + (rng.Chance(0.2f) ? rng.R(-0.3f, 0.3f) : 0.0f);
                float w = pass == 0 ? rng.R(4.5f, 7.5f) : rng.R(3.2f, 5.0f), d = pass == 0 ? rng.R(4.5f, 8.0f) : rng.R(3.2f, 5.5f);
                for (int attempt = 0; attempt < 3; ++attempt) {
                    if (FootprintFree(terrain, x, z, yaw, w, d, hw + 0.8f)) {
                        const float r = rng.F();
                        addHouse(x, z, yaw, w, d, r < 0.2f ? 1 : (r < 0.6f ? 2 : (r < 0.9f ? 3 : 4)), false);
                        break;
                    }
                    w *= 0.78f;
                    d *= 0.78f;
                    if (w < 2.8f) break;
                }
            }
    }
}

void Favela::PlaceStreetStuff(const Track& track, const Terrain& terrain)
{
    const float hw = track.HalfWidth();
    const float L = track.Length();
    Rng rng{0xBEEF01u};

    // Escadaria: escalones de 0.9 m a todo el ancho, con murito y baranda a los costados.
    for (const TrackFeature& f : track.Features()) {
        if (f.kind != FeatureKind::Escadaria) continue;
        const float tread = 0.9f;
        for (float u = 0.0f; u + tread <= f.length + 0.01f; u += tread) {
            const TrackPoint a = track.At(f.start + u), b = track.At(f.start + u + tread);
            const float top = std::max(terrain.Height(a.x, a.z), terrain.Height(b.x, b.z)) + 0.02f;
            const float yaw = std::atan2(a.tx, a.tz);
            const Vector3 c = {(a.x + b.x) * 0.5f, top - 0.6f, (a.z + b.z) * 0.5f};
            // Hasta el murito (su cara de adentro está a hw + 0.33): sin canaleta al costado, donde la rueda
            // caía al terreno en pendiente y pegaba contra la punta de los escalones.
            steps.push_back({c, {hw + 0.36f, 0.6f, tread * 0.5f + 0.01f}, yaw});
            solids.push_back({c, {hw + 0.36f, 0.6f, tread * 0.5f + 0.01f}, yaw});
            MarkFootprint(c.x, c.z, yaw, 2.0f * hw + 1.6f, tread);
        }
        // Muritos laterales (también colisión): siguen la bajada.
        for (float side : {-1.0f, 1.0f})
            for (float u = 0.0f; u < f.length; u += 3.0f) {
                const TrackPoint a = track.At(f.start + u), b = track.At(f.start + std::min(u + 3.0f, f.length));
                const float lx = -a.tz * side, lz = a.tx * side, off = hw + 0.45f;
                const Vector3 p0 = {a.x + lx * off, terrain.Height(a.x, a.z) + 0.4f, a.z + lz * off};
                const Vector3 p1 = {b.x + lx * off, terrain.Height(b.x, b.z) + 0.4f, b.z + lz * off};
                const Vector3 c = Vector3Lerp(p0, p1, 0.5f);
                const float len = Vector2Distance({p0.x, p0.z}, {p1.x, p1.z});
                solids.push_back({{c.x, c.y - 0.4f, c.z}, {0.12f, 0.9f, len * 0.5f}, std::atan2(a.tx, a.tz)});
            }
    }

    // Postes de luz de cemento, de un lado de la calle (cambia de lado cada tanto), con cables entre
    // ellos (cruzan la calle cuando cambian de lado) y "gatos" a las casas (se agregan después).
    float side = 1.0f;
    int sinceSwitch = 0;
    Vector3 prevTop{};
    bool hasPrev = false;
    for (float s = 5.0f; s < L - 5.0f; s += rng.R(20.0f, 28.0f)) {
        if (++sinceSwitch > 2 + rng.I(3)) {
            side = -side;
            sinceSwitch = 0;
        }
        const TrackPoint p = track.At(s);
        const float lx = -p.tz * side, lz = p.tx * side, off = hw + 0.55f;
        const float x = p.x + lx * off, z = p.z + lz * off;
        if (terrain.RoadDistance(x, z) < hw + 0.4f) {           // en un cruce: sin poste
            hasPrev = false;
            continue;
        }
        bool nearFeature = false;
        for (const TrackFeature& f : track.Features())
            if (f.kind == FeatureKind::Escadaria && track.Wrap(s - f.start + 2.0f) < f.length + 4.0f) nearFeature = true;
        if (nearFeature) continue;
        const float y = terrain.Height(x, z);
        poles.push_back({x, y, z});
        solids.push_back({{x, y + 4.0f, z}, {0.15f, 4.2f, 0.15f}, 0.0f});
        MarkFootprint(x, z, 0.0f, 0.5f, 0.5f);
        const Vector3 top = {x, y + 8.0f, z};
        if (hasPrev && Vector3Distance(prevTop, top) < 45.0f)
            for (int k = 0; k < 3; ++k) {
                const Vector3 o = {0.0f, -0.35f * k, 0.0f};
                wires.push_back({Vector3Add(prevTop, o), Vector3Add(top, o), rng.R(0.4f, 1.1f), 0.012f, kWire});
            }
        prevTop = top;
        hasPrev = true;
    }

    // Banderines: cruzando la calle cada tanto, a 5.5 m (a veces dos, en X).
    for (float s = 30.0f; s < L; s += rng.R(55.0f, 85.0f)) {
        const TrackPoint p = track.At(s);
        const float lx = -p.tz, lz = p.tx, off = hw + 0.9f;
        const float y = terrain.Height(p.x, p.z) + 5.6f;
        const Vector3 a = {p.x + lx * off, y, p.z + lz * off}, b = {p.x - lx * off, y, p.z - lz * off};
        buntings.push_back({a, b, rng.R(0.5f, 0.9f)});
        if (rng.Chance(0.5f)) {
            const TrackPoint q = track.At(s + 6.0f);
            const Vector3 c = {q.x + lx * off, terrain.Height(q.x, q.z) + 5.4f, q.z + lz * off};
            buntings.push_back({b, c, rng.R(0.6f, 1.0f)});
        }
    }

    // Pintura en el piso: lomos de burro amarillo y negro, línea de largada a cuadros y la línea del
    // medio (a trazos) en la avenida y la ladeira de asfalto. stripes: (x, z, rumbo, tipo 0 amarillo,
    // 1 negro, 2 blanco); stripeSize: (medio largo, medio ancho, y).
    auto paint = [&](float x, float z, float yaw, float halfLen, float halfW, int kind) {
        stripes.push_back({x, z, yaw, (float)kind});
        stripeSize.push_back({halfLen, halfW, terrain.Height(x, z) + 0.015f});
    };
    for (const TrackFeature& f : track.Features()) {
        if (f.kind != FeatureKind::QuebraMola) continue;
        for (int k = 0; k < 10; ++k) {
            const float lat = -hw + (k + 0.5f) * (2.0f * hw / 10.0f);
            for (float u : {0.45f, 0.9f, 1.35f}) {
                const TrackPoint p = track.At(f.start + u);
                const float x = p.x - p.tz * lat, z = p.z + p.tx * lat;
                paint(x, z, std::atan2(p.tx, p.tz), 0.24f, hw / 10.0f, k % 2);
            }
        }
    }
    {
        const TrackPoint p = track.At(track.startLine);
        for (int k = 0; k < 12; ++k) {
            const float lat = -hw + (k + 0.5f) * (2.0f * hw / 12.0f);
            for (int row = 0; row < 2; ++row) {
                const TrackPoint q = track.At(track.startLine + (row - 0.5f) * 0.36f);
                paint(q.x - q.tz * lat, q.z + q.tx * lat, std::atan2(p.tx, p.tz), 0.18f, hw / 12.0f, (k + row) % 2 ? 1 : 2);
            }
        }
        const float off = hw + 0.6f;
        startA = {p.x - p.tz * off, terrain.Height(p.x - p.tz * off, p.z + p.tx * off), p.z + p.tx * off};
        startB = {p.x + p.tz * off, terrain.Height(p.x + p.tz * off, p.z - p.tx * off), p.z - p.tx * off};
        for (const Vector3& q : {startA, startB}) {
            solids.push_back({{q.x, q.y + 2.5f, q.z}, {0.12f, 2.6f, 0.12f}, 0.0f});
            MarkFootprint(q.x, q.z, 0.0f, 0.6f, 0.6f);
        }
    }
    for (float s = 0.0f; s < L; s += 4.0f) {
        if (track.Asphalt(s) < 0.8f || track.Pavement(s) < 0.8f) continue;
        const TrackPoint p = track.At(s);
        if (terrain.RoadDistance(p.x + 6.0f * p.tz, p.z - 6.0f * p.tx) < 1.0f) continue;   // cerca de un cruce
        bool onFeature = false;
        for (const TrackFeature& f : track.Features())
            if (track.Wrap(s - f.start + 1.0f) < f.length + 2.0f) onFeature = true;
        if (!onFeature) paint(p.x, p.z, std::atan2(p.tx, p.tz), 0.9f, 0.06f, 3);
    }

    // Motos estacionadas contra el cordón, en tramos rectos lejos de cruces y obstáculos.
    int placed = 0;
    for (float s = 60.0f; s < L && placed < 9; s += rng.R(90.0f, 150.0f)) {
        const TrackPoint p = track.At(s);
        if (std::fabs(p.curvature) > 0.015f) continue;
        const float sd = rng.Chance(0.5f) ? 1.0f : -1.0f, off = hw - 0.45f;
        const float x = p.x - p.tz * sd * off, z = p.z + p.tx * sd * off;
        if (terrain.RoadDistance(x + 5.0f * p.tx, z + 5.0f * p.tz) < 2.0f) continue;
        const float yaw = std::atan2(p.tx, p.tz) + (rng.Chance(0.5f) ? mu::kPi : 0.0f) + rng.R(-0.12f, 0.12f);
        const float y = terrain.Height(x, z);
        parked.push_back({JPH::Vec3(x, y, z), yaw, rng.I(4), rng.Chance(0.4f)});
        solids.push_back({{x, y + 0.55f, z}, {0.28f, 0.55f, 0.95f}, yaw});
        ++placed;
    }

    // Pipas volando sobre el morro.
    for (int k = 0; k < 11; ++k) {
        const float x = rng.R(-95.0f, 95.0f), z = rng.R(-95.0f, 95.0f);
        const Color a = kBunting[rng.I(6)], b = kBunting[rng.I(6)];
        kites.push_back({{x + rng.R(-12.0f, 12.0f), terrain.NaturalHeight(x, z) + 7.0f, z + rng.R(-12.0f, 12.0f)},
                         {x, terrain.NaturalHeight(x, z) + rng.R(26.0f, 46.0f), z}, a, b, rng.R(0.0f, 6.28f)});
    }
}

// ------------------------------------------------------------------------ colisión
void Favela::CreateCollision(PhysicsWorld& world)
{
    // Un cuerpo estático por bloque de 32 m con todas sus cajas.
    std::vector<JPH::Ref<JPH::StaticCompoundShapeSettings>> group(kChunks * kChunks);
    for (const Solid& s : solids) {
        const int c = ChunkOf(s.c.x, s.c.z);
        if (!group[c]) group[c] = new JPH::StaticCompoundShapeSettings;
        group[c]->AddShape(JPH::Vec3(s.c.x, s.c.y, s.c.z), JPH::Quat::sRotation(JPH::Vec3::sAxisY(), s.yaw),
                           new JPH::BoxShapeSettings(JPH::Vec3(std::max(s.half.x, 0.06f), std::max(s.half.y, 0.06f), std::max(s.half.z, 0.06f)), 0.02f));
    }
    for (auto& g : group) {
        if (!g) continue;
        JPH::ShapeSettings::ShapeResult r = g->Create();
        if (r.HasError()) {
            std::printf("favela: error de colisión: %s\n", r.GetError().c_str());
            continue;
        }
        JPH::BodyCreationSettings bcs(r.Get(), JPH::RVec3::sZero(), JPH::Quat::sIdentity(), JPH::EMotionType::Static, Layers::STATIC);
        bcs.mFriction = 0.8f;
        bodies.push_back(world.Bodies().CreateAndAddBody(bcs, JPH::EActivation::DontActivate));
    }
}

// ------------------------------------------------------------------------ atlas de carteles
void Favela::BuildAtlas(const Font& font)
{
    const int W = 1024, H = 1024, cw = 512, ch = 128;
    Image img = GenImageColor(W, H, Color{0, 0, 0, 0});
    atlasRects.clear();
    for (int i = 0; i < kSignCount; ++i) {
        const SignSpec& sp = kSigns[i];
        const int x0 = (i % 2) * cw, y0 = (i / 2) * ch;
        const Rectangle cell = {(float)x0 + 4, (float)y0 + 4, (float)cw - 8, (float)ch - 8};
        atlasRects.push_back({cell.x / W, cell.y / H, cell.width / W, cell.height / H});
        if (sp.style == 3) {                                  // bandeira do Brasil
            ImageDrawRectangleRec(&img, {cell.x, cell.y, cell.height * 1.43f, cell.height}, Color{0, 150, 64, 255});
            const float fw = cell.height * 1.43f, fh = cell.height;
            const Vector2 c = {cell.x + fw * 0.5f, cell.y + fh * 0.5f};
            ImageDrawTriangle(&img, {c.x, cell.y + 8}, {cell.x + 10, c.y}, {c.x, cell.y + fh - 8}, Color{254, 221, 0, 255});
            ImageDrawTriangle(&img, {c.x, cell.y + 8}, {c.x, cell.y + fh - 8}, {cell.x + fw - 10, c.y}, Color{254, 221, 0, 255});
            ImageDrawCircleV(&img, c, fh * 0.26f, Color{0, 39, 118, 255});
            ImageDrawRectangleRec(&img, {c.x - fh * 0.26f, c.y - 3, fh * 0.52f, 5}, Color{245, 245, 245, 255});
            atlasRects.back().width = (fw) / W;
            continue;
        }
        float size = 86.0f;
        Vector2 m = MeasureTextEx(font, sp.text, size, 2.0f);
        while (m.x > cell.width - 24 && size > 20.0f) {
            size -= 2.0f;
            m = MeasureTextEx(font, sp.text, size, 2.0f);
        }
        const Vector2 at = {cell.x + (cell.width - m.x) * 0.5f, cell.y + (cell.height - m.y) * 0.5f};
        if (sp.style == 0) {
            ImageDrawRectangleRec(&img, cell, sp.bg);
            ImageDrawRectangleLines(&img, cell, 5, Shade(sp.bg, 0.6f));
            ImageDrawTextEx(&img, font, sp.text, at, size, 2.0f, sp.fg);
        } else if (sp.style == 1) {                           // burbuja: contorno negro grueso
            for (int dy = -4; dy <= 4; dy += 2)
                for (int dx = -4; dx <= 4; dx += 2) ImageDrawTextEx(&img, font, sp.text, {at.x + dx, at.y + dy}, size, 2.0f, Color{15, 15, 15, 255});
            ImageDrawTextEx(&img, font, sp.text, at, size, 2.0f, sp.fg);
            ImageDrawTextEx(&img, font, sp.text, {at.x - 2, at.y - 3}, size, 2.0f, Shade(sp.fg, 1.25f));
        } else {                                              // pixo: letras altas y finas
            Image t = ImageTextEx(font, sp.text, 60.0f, 6.0f, sp.fg);
            ImageResize(&t, std::min((int)cell.width - 10, t.width), (int)cell.height - 10);
            ImageDraw(&img, t, {0, 0, (float)t.width, (float)t.height}, {cell.x + (cell.width - t.width) * 0.5f, cell.y + 5, (float)t.width, (float)t.height}, WHITE);
            UnloadImage(t);
        }
    }
    atlas = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&atlas);
    SetTextureFilter(atlas, TEXTURE_FILTER_TRILINEAR);
}

// ------------------------------------------------------------------------ mallas
void Favela::CreateMeshes(const Font& font)
{
    Unload();
    BuildAtlas(font);
    MeshBuilder solid[kChunks * kChunks], decal[kChunks * kChunks], two[kChunks * kChunks];
    auto S = [&](float x, float z) -> MeshBuilder& { return solid[ChunkOf(x, z)]; };

    // Cartel o grafiti en una pared: rectángulo del atlas (medio ancho hw, medio alto hh).
    auto sign = [&](Vector3 c, Vector3 right, float hw, float hh, int index) {
        MeshBuilder& b = decal[ChunkOf(c.x, c.z)];
        const Rectangle r = atlasRects[index];
        const Vector3 up = {0.0f, hh, 0.0f}, rt = Vector3Scale(right, hw);
        const Vector3 n = Vector3Normalize(Vector3CrossProduct(right, {0.0f, 1.0f, 0.0f}));
        const unsigned short a = b.Vertex(Vector3Subtract(Vector3Subtract(c, rt), up), n, {r.x, r.y + r.height});
        const unsigned short bb = b.Vertex(Vector3Subtract(Vector3Add(c, rt), up), n, {r.x + r.width, r.y + r.height});
        const unsigned short cc = b.Vertex(Vector3Add(Vector3Add(c, rt), up), n, {r.x + r.width, r.y});
        const unsigned short d = b.Vertex(Vector3Add(Vector3Subtract(c, rt), up), n, {r.x, r.y});
        b.Tri(a, bb, cc);
        b.Tri(a, cc, d);
    };

    // ---------------------------------------------------------------- casas
    int signCounter = 0;
    for (const House& h : houses) {
        Rng rng{h.seed | 1u};
        MeshBuilder& b = S(h.x, h.z);
        const Frame f = YawFrame(h.yaw);
        auto P = [&](float u, float y, float v) { return Vector3{h.x + f.x.x * u + f.z.x * v, y, h.z + f.x.z * u + f.z.z * v}; };
        const float wallRoll = rng.F();
        const int type = wallRoll < 0.34f ? 0 : (wallRoll < 0.86f ? 1 : 2);      // 0 ladrillo, 1 pintada, 2 revoque gris
        const Color paint = kPaint[rng.I((int)(sizeof(kPaint) / sizeof(kPaint[0])))];
        const Color brick = Shade(kBrickTone, rng.R(0.88f, 1.1f));
        const Color gray = Shade(kConcrete, rng.R(0.9f, 1.08f));
        auto wallColor = [&](int t) { return t == 0 ? brick : (t == 1 ? Shade(paint, rng.R(0.92f, 1.04f)) : gray); };
        auto wallMode = [](int t) { return t == 0 ? kBrick : kPlaster; };
        const bool unfinishedTop = type != 0 && h.floors >= 2 && rng.Chance(0.4f);

        // Pisos: desde la entrada para arriba; debajo, lo que baje la ladera (pisos de abajo o cimientos).
        struct Block { float y0, y1, du, dv, dw, dd; int t; bool windows; };
        std::vector<Block> blocks;
        float y = h.entry;
        while (y - kFloor > h.base + 0.8f) {                 // pisos que asoman ladera abajo
            y -= kFloor;
            blocks.push_back({y, y + kFloor, 0.0f, 0.0f, h.w, h.d, type == 1 && rng.Chance(0.5f) ? 2 : type, true});
        }
        if (y > h.base) blocks.push_back({h.base, y, 0.0f, 0.0f, h.w, h.d, 2, false});   // cimiento
        float overhang = 0.0f;
        for (int k = 0; k < h.floors; ++k) {
            const float y0 = h.entry + k * kFloor;
            if (k >= 1 && h.front && overhang == 0.0f && rng.Chance(0.3f)) overhang = rng.R(0.4f, 0.8f);   // puxadinho sobre la vereda
            const bool top = k == h.floors - 1;
            const int t = top && unfinishedTop ? 0 : type;
            blocks.push_back({y0, y0 + kFloor, 0.0f, -overhang * 0.5f, h.w, h.d + overhang, t, true});
        }
        const float roof = h.entry + h.floors * kFloor;

        for (const Block& bl : blocks) {
            const Vector3 c = P(bl.du, (bl.y0 + bl.y1) * 0.5f, bl.dv);
            Box(b, c, {bl.dw * 0.5f, (bl.y1 - bl.y0) * 0.5f, bl.dd * 0.5f}, f, wallColor(bl.t), 0.06f, wallMode(bl.t));
            // Laje: borde de cemento en cada piso.
            Box(b, P(bl.du, bl.y1 - 0.05f, bl.dv), {bl.dw * 0.5f + 0.08f, 0.09f, bl.dd * 0.5f + 0.08f}, f, kSlab, 0.05f, kPlaster);
            if (!bl.windows) continue;
            // Ventanas: al frente (hacia la calle, -Z), a los costados y atrás.
            const float wy = bl.y0 + 1.5f;
            auto window = [&](float u, float v, bool sideFace, float sgn) {
                const float ww = rng.R(0.7f, 1.1f), wh = rng.R(0.8f, 1.1f);
                const float r = rng.F();
                const Color col = r < 0.6f ? kGlass : (r < 0.85f ? kGate[rng.I(6)] : Color{70, 70, 74, 255});
                const Vector3 half = sideFace ? Vector3{0.04f, wh * 0.5f, ww * 0.5f} : Vector3{ww * 0.5f, wh * 0.5f, 0.04f};
                Box(b, P(u, wy, v), half, f, col, r < 0.6f ? 0.8f : 0.2f);
                if (r >= 0.6f || rng.Chance(0.3f))              // marco claro
                    Box(b, P(u, wy - wh * 0.5f - 0.04f, v + (sideFace ? 0.0f : sgn * 0.03f)),
                        sideFace ? Vector3{0.06f, 0.04f, ww * 0.55f} : Vector3{ww * 0.55f, 0.04f, 0.06f}, f, Color{210, 206, 196, 255}, 0.1f);
            };
            const int nFront = std::max(1, (int)(bl.dw / 2.3f));
            const bool ground = std::fabs(bl.y0 - h.entry) < 0.01f;
            const int door = h.front && ground ? rng.I(nFront) : -1;
            const float front = bl.dv - bl.dd * 0.5f - 0.02f, back = bl.dv + bl.dd * 0.5f + 0.02f;
            for (int i = 0; i < nFront; ++i) {
                const float u = -bl.dw * 0.5f + (i + 0.5f) * bl.dw / nFront + rng.R(-0.2f, 0.2f);
                if (i == door) {                              // puerta / portón de chapa
                    Box(b, P(u, bl.y0 + 1.05f, front), {0.5f, 1.05f, 0.05f}, f, kGate[rng.I(6)], 0.3f);
                    continue;
                }
                if (rng.Chance(0.85f)) window(u, front, false, -1.0f);
            }
            if (rng.Chance(0.6f)) window(bl.du + rng.R(-bl.dw * 0.3f, bl.dw * 0.3f), back, false, 1.0f);
            for (float sgn : {-1.0f, 1.0f})
                if (rng.Chance(0.35f)) window(bl.du + sgn * (bl.dw * 0.5f + 0.02f), bl.dv + rng.R(-bl.dd * 0.25f, bl.dd * 0.25f), true, sgn);
            if (rng.Chance(0.12f))                           // aire acondicionado
                Box(b, P(rng.R(-bl.dw * 0.3f, bl.dw * 0.3f), bl.y0 + 2.2f, front - 0.14f), {0.36f, 0.22f, 0.14f}, f, Color{230, 230, 226, 255}, 0.3f);
        }

        // Techo: laje con murito, hierros esperando el próximo piso, caixa d'água, antena, tender.
        const Color topColor = wallColor(unfinishedTop ? 0 : type);
        if (rng.Chance(0.45f))
            for (int e = 0; e < 4; ++e) {
                const bool alongX = e < 2;
                const float sgn = e % 2 ? 1.0f : -1.0f;
                const Vector3 c = alongX ? P(0.0f, roof + 0.45f, sgn * (h.d * 0.5f - 0.06f)) : P(sgn * (h.w * 0.5f - 0.06f), roof + 0.45f, 0.0f);
                Box(b, c, alongX ? Vector3{h.w * 0.5f, 0.45f, 0.06f} : Vector3{0.06f, 0.45f, h.d * 0.5f}, f, topColor, 0.05f,
                    unfinishedTop || type == 0 ? kBrick : kPlaster);
            }
        if (unfinishedTop || type == 2 || rng.Chance(0.2f))
            for (float su : {-0.5f, 0.5f})
                for (float sv : {-0.5f, 0.5f}) {
                    const Vector3 base = P(su * (h.w - 0.3f), roof, sv * (h.d - 0.3f));
                    for (int k = 0; k < 3; ++k)
                        Bar(b, Vector3Add(base, {0.05f * k, 0.0f, 0.03f * k}), Vector3Add(base, {0.05f * k + 0.04f, rng.R(0.7f, 1.1f), 0.03f * k}), 0.012f,
                            Color{80, 52, 40, 255}, 0.2f);
                }
        if (rng.Chance(0.72f)) {
            const float u = rng.R(-0.3f, 0.3f) * h.w, v = rng.R(-0.3f, 0.3f) * h.d;
            const Vector3 c = P(u, roof + 0.1f, v);
            const float r = rng.R(0.5f, 0.65f);
            Box(b, Vector3Add(c, {0.0f, 0.12f, 0.0f}), {r * 0.8f, 0.12f, r * 0.8f}, f, kSlab, 0.05f, kPlaster);   // base
            Cylinder(b, Vector3Add(c, {0.0f, 0.24f, 0.0f}), r, rng.R(0.7f, 0.95f), Shade(kTank, rng.R(0.85f, 1.1f)), 0.5f);
        }
        if (rng.Chance(0.25f)) {                              // antena de TV
            const Vector3 base = P(rng.R(-0.4f, 0.4f) * h.w, roof, rng.R(-0.4f, 0.4f) * h.d);
            Bar(b, base, Vector3Add(base, {0.0f, 2.2f, 0.0f}), 0.02f, Color{150, 150, 156, 255});
            for (int k = 0; k < 3; ++k)
                Bar(b, Vector3Add(base, {-0.5f + 0.1f * k, 1.6f + 0.25f * k, 0.0f}), Vector3Add(base, {0.5f - 0.1f * k, 1.6f + 0.25f * k, 0.0f}), 0.012f,
                    Color{150, 150, 156, 255});
        }
        if (rng.Chance(0.2f)) {                               // ropa tendida
            const Vector3 a = P(-h.w * 0.35f, roof + 1.4f, 0.0f), c = P(h.w * 0.35f, roof + 1.4f, 0.0f);
            Bar(b, P(-h.w * 0.35f, roof, 0.0f), a, 0.02f, Color{120, 120, 126, 255});
            Bar(b, P(h.w * 0.35f, roof, 0.0f), c, 0.02f, Color{120, 120, 126, 255});
            Bar(b, a, c, 0.006f, Color{220, 220, 220, 255});
            const int n = 3 + rng.I(4);
            for (int k = 0; k < n; ++k) {
                const Vector3 p = Vector3Lerp(a, c, (k + 0.5f) / n);
                const float sw = rng.R(0.25f, 0.4f), sh = rng.R(0.35f, 0.7f);
                const Vector3 rt = Vector3Scale(f.x, sw * 0.5f);
                Quad2(two[ChunkOf(p.x, p.z)], Vector3Subtract(p, rt), Vector3Add(p, rt), Vector3Add(Vector3Add(p, rt), {0.0f, -sh, 0.0f}),
                      Vector3Add(Vector3Subtract(p, rt), {0.0f, -sh, 0.0f}), kBunting[rng.I(6)]);
            }
        }

        // Carteles de negocios, grafitis y banderas.
        if (h.front) {
            const float front = -h.d * 0.5f - 0.07f;
            if (rng.Chance(0.16f)) {
                const Vector3 c = P(0.0f, h.entry + 2.45f, front);
                sign(c, Vector3Negate(f.x), std::min(1.4f, h.w * 0.42f), 0.36f, signCounter++ % kShopSigns);
            } else if (rng.Chance(0.12f)) {
                const Vector3 c = P(rng.R(-0.2f, 0.2f) * h.w, h.entry + 1.1f, front);
                sign(c, Vector3Negate(f.x), std::min(1.5f, h.w * 0.4f), 0.4f, kGraffitiFirst + rng.I(kGraffitiCount));
            }
            if (h.floors >= 2 && rng.Chance(0.06f)) {
                const Vector3 c = P(rng.R(-0.25f, 0.25f) * h.w, h.entry + kFloor + 1.2f, front - 0.02f);
                sign(c, Vector3Negate(f.x), 0.72f, 0.5f, kFlagSign);
            }
        }
        if (rng.Chance(0.1f)) {                               // grafiti en un costado
            const float sgn = rng.Chance(0.5f) ? 1.0f : -1.0f;
            const Vector3 c = P(sgn * (h.w * 0.5f + 0.05f), h.entry + 1.0f, rng.R(-0.2f, 0.2f) * h.d);
            sign(c, Vector3Scale(f.z, sgn), std::min(1.6f, h.d * 0.4f), 0.45f, kGraffitiFirst + rng.I(kGraffitiCount));
        }
    }

    // ---------------------------------------------------------------- escadaria
    for (const Step& st : steps) {
        MeshBuilder& b = S(st.c.x, st.c.z);
        const Frame f = YawFrame(st.yaw);
        Box(b, st.c, st.half, f, Color{162, 158, 150, 255}, 0.06f, kPlaster);
        // Borde del escalón pintado de amarillo (arriba, del lado de abajo de la bajada).
        const Vector3 edge = Vector3Add(Vector3Add(st.c, {0.0f, st.half.y - 0.01f, 0.0f}), Vector3Scale(f.z, st.half.z - 0.06f));
        Box(b, edge, {st.half.x - 0.3f, 0.015f, 0.06f}, f, Color{226, 186, 40, 255}, 0.1f);
        // Muritos a los costados (escalonados como los escalones).
        for (float sgn : {-1.0f, 1.0f}) {
            const Vector3 w = Vector3Add(Vector3Add(st.c, Vector3Scale(f.x, sgn * (st.half.x + 0.25f))), {0.0f, st.half.y + 0.35f, 0.0f});
            Box(b, w, {0.12f, 0.45f, st.half.z}, f, Color{190, 186, 176, 255}, 0.05f, kPlaster);
        }
    }
    if (!steps.empty()) {                                     // baranda de caño: sigue la bajada, postes cada tres escalones
        const Step& first = steps.front();
        const Step& last = steps.back();
        const Frame f0 = YawFrame(first.yaw);
        const Color rail = {60, 110, 170, 255};
        for (float sgn : {-1.0f, 1.0f}) {
            auto railAt = [&](const Step& st, float along) {
                const Frame f = YawFrame(st.yaw);
                return Vector3Add(Vector3Add(Vector3Add(st.c, Vector3Scale(f.x, sgn * (st.half.x + 0.25f))), Vector3Scale(f.z, along * st.half.z)),
                                  {0.0f, st.half.y + 0.95f, 0.0f});
            };
            MeshBuilder& b = S(first.c.x, first.c.z);
            Bar(b, railAt(first, -1.0f), railAt(last, 1.0f), 0.025f, rail, 0.5f);
            for (size_t k = 0; k < steps.size(); k += 3) {
                const Vector3 top = railAt(steps[k], 0.0f);
                Bar(b, Vector3Add(top, {0.0f, -0.15f, 0.0f}), top, 0.02f, rail, 0.5f);
                Bar(b, Vector3Add(top, {0.0f, -0.16f, 0.0f}), Vector3Add(top, {0.0f, -0.16f, 0.0f}), 0.02f, rail, 0.5f);
            }
            (void)f0;
        }
    }

    // ---------------------------------------------------------------- postes, cables y gatos
    Rng wr{0x5EED5u};
    for (const Vector3& p : poles) {
        MeshBuilder& b = S(p.x, p.z);
        Box(b, {p.x, p.y + 4.1f, p.z}, {0.13f, 4.2f, 0.13f}, YawFrame(0.0f), Color{168, 164, 156, 255}, 0.05f, kPlaster);
        Box(b, {p.x, p.y + 7.9f, p.z}, {0.8f, 0.06f, 0.06f}, YawFrame(0.0f), Color{150, 146, 140, 255}, 0.05f);
        if (wr.Chance(0.35f)) Cylinder(b, {p.x + 0.35f, p.y + 6.2f, p.z}, 0.28f, 0.8f, Color{110, 116, 110, 255}, 0.3f, 10);   // transformador
        if (wr.Chance(0.5f)) Box(b, {p.x, p.y + 3.6f, p.z - 0.15f}, {0.2f, 0.3f, 0.08f}, YawFrame(0.0f), Color{230, 230, 226, 255}, 0.2f);   // luminaria/caja
        // Rollo de cable enredado en el poste.
        for (int k = 0; k < 4; ++k) {
            const float a = wr.R(0.0f, kTau);
            const Vector3 c0 = {p.x + std::cos(a) * 0.35f, p.y + 6.8f + wr.R(-0.3f, 0.3f), p.z + std::sin(a) * 0.35f};
            const Vector3 c1 = {p.x - std::cos(a) * 0.35f, p.y + 6.8f + wr.R(-0.3f, 0.3f), p.z - std::sin(a) * 0.35f};
            Cable(b, c0, c1, 0.3f, 0.015f, kWire);
        }
        // Gatos: de este poste a un par de casas cercanas.
        int links = 0;
        for (size_t i = 0; i < houses.size() && links < 3; ++i) {
            const House& h = houses[(i * 7919 + (size_t)(p.x * 13.0f)) % houses.size()];
            const float dx = h.x - p.x, dz = h.z - p.z;
            if (dx * dx + dz * dz > 18.0f * 18.0f || dx * dx + dz * dz < 9.0f) continue;
            const Vector3 to = {h.x - dx / std::sqrt(dx * dx + dz * dz) * std::min(h.w, h.d) * 0.5f, h.entry + wr.R(2.2f, 4.0f), h.z - dz / std::sqrt(dx * dx + dz * dz) * std::min(h.w, h.d) * 0.5f};
            Cable(b, {p.x, p.y + 7.2f + wr.R(-0.4f, 0.3f), p.z}, to, wr.R(0.3f, 1.2f), 0.01f, kWire);
            ++links;
        }
    }
    for (const Wire& w : wires) Cable(S((w.a.x + w.b.x) * 0.5f, (w.a.z + w.b.z) * 0.5f), w.a, w.b, w.sag, w.radius, w.color);

    // ---------------------------------------------------------------- banderines
    Rng br{0xF1A6u};
    for (const Bunting& bu : buntings) {
        MeshBuilder& line = S((bu.a.x + bu.b.x) * 0.5f, (bu.a.z + bu.b.z) * 0.5f);
        Cable(line, bu.a, bu.b, bu.sag, 0.006f, Color{230, 230, 230, 255});
        MeshBuilder& b = two[ChunkOf((bu.a.x + bu.b.x) * 0.5f, (bu.a.z + bu.b.z) * 0.5f)];
        const float len = Vector3Distance(bu.a, bu.b);
        const int n = std::max(4, (int)(len / 0.42f));
        for (int k = 0; k < n; ++k) {
            const float t0 = (k + 0.15f) / n, t1 = (k + 0.85f) / n;
            const Vector3 p0 = SagPoint(bu.a, bu.b, bu.sag, t0), p1 = SagPoint(bu.a, bu.b, bu.sag, t1);
            const Vector3 tip = Vector3Add(Vector3Lerp(p0, p1, 0.5f), {0.0f, -0.32f, 0.0f});
            Tri2(b, p0, p1, tip, kBunting[br.I(6)]);
        }
    }

    // ---------------------------------------------------------------- pintura del piso y pórtico
    for (size_t i = 0; i < stripes.size(); ++i) {
        const Vector4 s = stripes[i];
        const Vector3 sz = stripeSize[i];
        const int kind = (int)s.w;
        const Color c = kind == 0 ? Color{236, 196, 36, 255} : (kind == 1 ? Color{26, 26, 28, 255} : (kind == 2 ? Color{240, 240, 236, 255} : Color{232, 226, 200, 255}));
        YawBox(S(s.x, s.y), {s.x, sz.z, s.y}, {sz.y, 0.012f, sz.x}, s.z, c, 0.15f);
    }
    {
        MeshBuilder& b = S(startA.x, startA.z);
        for (const Vector3& q : {startA, startB}) Box(b, {q.x, q.y + 2.6f, q.z}, {0.1f, 2.6f, 0.1f}, YawFrame(0.0f), Color{40, 40, 46, 255}, 0.4f);
        const Vector3 a = {startA.x, std::max(startA.y, startB.y) + 4.6f, startA.z}, c = {startB.x, a.y, startB.z};
        Bar(b, a, c, 0.05f, Color{40, 40, 46, 255});
        const Vector3 mid = Vector3Lerp(a, c, 0.5f);
        const Vector3 right = Vector3Normalize(Vector3Subtract(c, a));
        sign(Vector3Add(mid, {0.0f, -0.55f, 0.0f}), right, Vector3Distance(a, c) * 0.46f, 0.5f, kBannerSign);
        sign(Vector3Add(mid, {0.0f, -0.55f, 0.0f}), Vector3Negate(right), Vector3Distance(a, c) * 0.46f, 0.5f, kBannerSign);
    }

    for (int i = 0; i < kChunks * kChunks; ++i) {
        Chunk& c = chunks[i];
        c.center = {origin + (i % kChunks + 0.5f) * size / kChunks, 0.0f, origin + (i / kChunks + 0.5f) * size / kChunks};
        if (solid[i].VertexCount() > 0) {
            c.solid = solid[i].Build();
            c.hasSolid = true;
        }
        if (decal[i].VertexCount() > 0) {
            c.decal = decal[i].Build();
            c.hasDecal = true;
        }
        if (two[i].VertexCount() > 0) {
            c.twoSided = two[i].Build();
            c.hasTwoSided = true;
        }
    }

    // ---------------------------------------------------------------- pipa (rombo con cola)
    {
        MeshBuilder b;
        const Color light = WHITE, dark = {200, 200, 200, 255};
        Tri2(b, {0.0f, 0.55f, 0.0f}, {-0.42f, 0.1f, 0.0f}, {0.0f, 0.0f, 0.0f}, light);
        Tri2(b, {0.0f, 0.55f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.42f, 0.1f, 0.0f}, dark);
        Tri2(b, {-0.42f, 0.1f, 0.0f}, {0.0f, -0.45f, 0.0f}, {0.0f, 0.0f, 0.0f}, dark);
        Tri2(b, {0.0f, 0.0f, 0.0f}, {0.0f, -0.45f, 0.0f}, {0.42f, 0.1f, 0.0f}, light);
        for (int k = 0; k < 6; ++k) {                         // rabiola: moñitos colgando
            const float y = -0.6f - 0.28f * k, x = 0.05f * std::sin(k * 1.3f);
            Tri2(b, {x, y, 0.0f}, {x - 0.1f, y + 0.06f, 0.0f}, {x - 0.1f, y - 0.06f, 0.0f}, light);
            Tri2(b, {x, y, 0.0f}, {x + 0.1f, y - 0.06f, 0.0f}, {x + 0.1f, y + 0.06f, 0.0f}, light);
        }
        kiteMesh = b.Build();
    }

    // ---------------------------------------------------------------- paisaje de fondo
    {
        MeshBuilder b;
        Rng rr{0xA11CEu};
        const int n = 180;
        std::vector<Vector3> base(n + 1), crest(n + 1);
        for (int i = 0; i <= n; ++i) {
            const float a = kTau * (float)(i % n) / (float)n;
            // Al sur (a ~ pi) cerros bajos con el mar adelante; al oeste el Corcovado; al sudeste el Pan de Azúcar.
            const float south = std::max(0.0f, -std::cos(a));
            float h = 40.0f + 70.0f * (0.5f + 0.5f * std::sin(a * 3.0f + 1.2f)) * (0.6f + 0.4f * std::sin(a * 7.0f + 0.4f));
            h *= 1.0f - 0.65f * south;
            const float corc = std::exp(-std::pow((a - 3.75f) / 0.12f, 2.0f)), loaf = std::exp(-std::pow((a - 2.55f) / 0.05f, 2.0f));
            h += 150.0f * corc + 120.0f * loaf;
            const float R = 420.0f + 50.0f * std::sin(a * 5.0f) - 60.0f * south;
            base[i] = {std::sin(a) * (R + 60.0f), -12.0f, std::cos(a) * (R + 60.0f)};
            crest[i] = {std::sin(a) * R, h, std::cos(a) * R};
        }
        for (int i = 0; i < n; ++i) {
            const Vector3 n0 = Vector3Normalize(Vector3Negate({crest[i].x, -150.0f, crest[i].z}));
            const Color low = {58, 70, 52, 255}, high = {44, 58, 44, 255};
            const unsigned short a0 = b.Vertex(base[i], n0, {0.05f, kPlain}, low), a1 = b.Vertex(crest[i], n0, {0.05f, kPlain}, high);
            const unsigned short b0 = b.Vertex(base[i + 1], n0, {0.05f, kPlain}, low), b1 = b.Vertex(crest[i + 1], n0, {0.05f, kPlain}, high);
            b.Tri(a0, b0, b1);
            b.Tri(a0, b1, a1);
        }
        b.FixWinding();
        // Otras favelas en las laderas de enfrente: cajitas de colores.
        for (int k = 0; k < 2100; ++k) {
            const int i = rr.I(n);
            const float t = rr.R(0.0f, 0.5f) * rr.R(0.3f, 1.0f);
            const float u = rr.F();
            const Vector3 lo = Vector3Lerp(base[i], base[i + 1], u), hi = Vector3Lerp(crest[i], crest[i + 1], u);
            if (hi.y < 30.0f) continue;
            Vector3 p = Vector3Lerp(lo, hi, t);
            p = Vector3Scale(p, 0.985f);
            const float s = rr.R(2.0f, 4.5f);
            const Color c = rr.Chance(0.4f) ? Shade(kBrickTone, rr.R(0.8f, 1.1f)) : kPaint[rr.I(12)];
            YawBox(b, {p.x, p.y + s * 0.4f, p.z}, {s * 0.5f, s * 0.4f, s * 0.5f}, std::atan2(p.x, p.z), c, 0.05f);
        }
        // El mar al sur, con brillo (refleja el atardecer).
        const Color sea = {30, 62, 92, 255};
        for (int i = 0; i < 24; ++i) {
            const float a0 = mu::kPi * 0.55f + mu::kPi * 0.9f * i / 24.0f, a1 = mu::kPi * 0.55f + mu::kPi * 0.9f * (i + 1) / 24.0f;
            const Vector3 p0 = {std::sin(a0) * 150.0f, -2.0f, std::cos(a0) * 150.0f}, p1 = {std::sin(a1) * 150.0f, -2.0f, std::cos(a1) * 150.0f};
            const Vector3 q0 = {std::sin(a0) * 580.0f, -2.0f, std::cos(a0) * 580.0f}, q1 = {std::sin(a1) * 580.0f, -2.0f, std::cos(a1) * 580.0f};
            const unsigned short v0 = b.Vertex(p0, {0.0f, 1.0f, 0.0f}, {0.85f, kPlain}, sea), v1 = b.Vertex(p1, {0.0f, 1.0f, 0.0f}, {0.85f, kPlain}, sea);
            const unsigned short v2 = b.Vertex(q1, {0.0f, 1.0f, 0.0f}, {0.85f, kPlain}, sea), v3 = b.Vertex(q0, {0.0f, 1.0f, 0.0f}, {0.85f, kPlain}, sea);
            b.Tri(v0, v2, v1);
            b.Tri(v0, v3, v2);
        }
        b.FixWinding(b.idx.size() - 24 * 6);
        // El Cristo en la punta del Corcovado, con los brazos abiertos (mirando al morro).
        const float ca = 3.75f, cR = 420.0f + 50.0f * std::sin(ca * 5.0f) - 60.0f * std::max(0.0f, -std::cos(ca));
        const Vector3 top = {std::sin(ca) * cR, 40.0f + 150.0f + 12.0f, std::cos(ca) * cR};
        const float yaw = std::atan2(-top.x, -top.z);
        const Color stone = {206, 204, 196, 255};
        YawBox(b, {top.x, top.y - 4.0f, top.z}, {5.0f, 6.0f, 5.0f}, yaw, Color{150, 148, 140, 255}, 0.05f);
        YawBox(b, {top.x, top.y + 13.0f, top.z}, {2.6f, 11.0f, 2.0f}, yaw, stone, 0.1f);
        YawBox(b, {top.x, top.y + 21.0f, top.z}, {14.0f, 1.3f, 1.3f}, yaw, stone, 0.1f);
        YawBox(b, {top.x, top.y + 25.2f, top.z}, {1.5f, 1.8f, 1.5f}, yaw, stone, 0.1f);
        backdrop = b.Build();
    }
    meshes = true;
}

// ------------------------------------------------------------------------ dibujo
void Favela::Draw(Renderer& r, float time) const
{
    if (!meshes) return;
    r.DrawMeshColored(backdrop, MatrixIdentity(), WHITE);
    for (const Chunk& c : chunks)
        if (c.hasSolid) r.DrawMeshColored(c.solid, MatrixIdentity(), WHITE);
    rlDisableBackfaceCulling();
    for (const Chunk& c : chunks)
        if (c.hasTwoSided) r.DrawMeshColored(c.twoSided, MatrixIdentity(), WHITE);
    // Pipas: se mecen con el viento; el hilo baja hasta una terraza.
    for (const Kite& k : kites) {
        const float t = time + k.phase;
        const Vector3 p = {k.center.x + std::sin(t * 0.7f) * 2.0f, k.center.y + std::sin(t * 1.3f) * 1.0f, k.center.z + std::cos(t * 0.5f) * 1.6f};
        const Matrix m = MatrixMultiply(MatrixMultiply(MatrixScale(1.4f, 1.4f, 1.4f), MatrixRotateZ(std::sin(t * 2.1f) * 0.35f)),
                                        MatrixMultiply(MatrixRotateY(k.phase * 3.0f), MatrixTranslate(p.x, p.y, p.z)));
        r.DrawMeshColored(kiteMesh, m, k.a);
        if (!r.InShadowPass()) DrawLine3D(k.anchor, p, Color{230, 230, 230, 160});
    }
    rlEnableBackfaceCulling();
    for (const Chunk& c : chunks)
        if (c.hasDecal) r.DrawMeshTextured(c.decal, MatrixIdentity(), atlas, false);   // de una cara: los banderines que
                                                                                        // cruzan la calle son dos espalda con espalda
}

void Favela::DrawShadows(Renderer& r, Vector3 focus, float radius) const
{
    if (!meshes) return;
    const float half = size / kChunks * 0.72f;                  // radio de un bloque
    for (const Chunk& c : chunks) {
        if (!c.hasSolid) continue;
        const float dx = c.center.x - focus.x, dz = c.center.z - focus.z;
        if (dx * dx + dz * dz > (radius + half) * (radius + half)) continue;
        r.DrawMeshColored(c.solid, MatrixIdentity(), WHITE);
    }
}

void Favela::Unload()
{
    if (!meshes) return;
    for (Chunk& c : chunks) {
        if (c.hasSolid) UnloadMesh(c.solid);
        if (c.hasDecal) UnloadMesh(c.decal);
        if (c.hasTwoSided) UnloadMesh(c.twoSided);
        c = Chunk{};
    }
    UnloadMesh(backdrop);
    UnloadMesh(kiteMesh);
    UnloadTexture(atlas);
    meshes = false;
}
