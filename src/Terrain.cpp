#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>

#include "Terrain.h"

#include "MathUtil.h"
#include "PhysicsWorld.h"
#include "Render.h"
#include "Track.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {

constexpr int kChunk = 64;              // celdas por chunk de malla
constexpr float kShoulder = 9.0f;       // m desde el borde de pista hasta el terreno natural
constexpr float kGrassCell = 0.55f;     // m entre matas (grilla con jitter)
constexpr int kGrassBlock = 32;         // celdas por bloque de la caché de matas
// Tono del pasto (y del pasto seco): el color de vértice del terreno y el de las matas 3D salen de los mismos dos, así
// el borde del campo de matas no contrasta. Verde de campo, no lima.
constexpr Color kGrass = {80, 112, 62, 255}, kGrassDry = {124, 124, 80, 255};

float Hash(int x, int z)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)z * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float)(h & 0xffffff) / (float)0xffffff;
}

float ValueNoise(float x, float z)
{
    int ix = (int)std::floor(x), iz = (int)std::floor(z);
    float fx = x - (float)ix, fz = z - (float)iz;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fz = fz * fz * (3.0f - 2.0f * fz);
    float a = mu::Lerp(Hash(ix, iz), Hash(ix + 1, iz), fx);
    float b = mu::Lerp(Hash(ix, iz + 1), Hash(ix + 1, iz + 1), fx);
    return mu::Lerp(a, b, fz);
}

float Fbm(float x, float z, int octaves)
{
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += ValueNoise(x, z) * amp;
        norm += amp;
        x = x * 2.03f + 11.7f;
        z = z * 2.03f - 5.3f;
        amp *= 0.5f;
    }
    return sum / norm;
}

// Altura del peralte a lo largo del ancho de pista. bank > 0: berm del lado izquierdo.
float Berm(float bank, float lateral, float halfWidth)
{
    if (std::fabs(bank) < 1e-3f) return 0.0f;
    float outside = bank > 0.0f ? -lateral : lateral;          // coordenada hacia el exterior
    float t = mu::Clamp((outside + halfWidth) / (2.0f * halfWidth), 0.0f, 1.2f);
    return std::fabs(bank) * t * t;
}

// Morro de la favela: sube ~20% hacia el norte (z), un poco más alto en el medio, con ondulaciones
// suaves (las calles se nivelan contra la ladera; lo demás queda debajo de las casas).
float FavelaHill(float x, float z)
{
    const float t = (z + 100.0f) / 200.0f;
    float h = 2.0f + 40.0f * (t + 0.035f * std::sin(6.2831853f * t));
    h += 3.0f * (1.0f - std::min(1.0f, (x / 110.0f) * (x / 110.0f)));
    h += (Fbm(x * 0.02f + 5.3f, z * 0.02f - 2.1f, 2) - 0.5f) * 2.0f;
    return h;
}

constexpr float kFavelaShoulder = 2.5f;     // m desde el borde de la calle hasta la ladera natural
constexpr float kShapeStep = 0.1f;          // m entre muestras de la tabla del perfil de cada forma

// Dirección de un rumbo en grados (0 = norte, +z; 90 = este, +x). Los múltiplos de 90° salen exactos: así
// las formas y los médanos alineados con los ejes no dependen del sin/cos de cada sistema (Mac/Windows).
void Heading(float deg, float& x, float& z)
{
    const float q = deg / 90.0f;
    if (q == std::floor(q)) {
        const int k = (((int)q % 4) + 4) % 4;
        x = k == 1 ? 1.0f : (k == 3 ? -1.0f : 0.0f);
        z = k == 0 ? 1.0f : (k == 2 ? -1.0f : 0.0f);
        return;
    }
    x = std::sin(mu::Rad(deg));
    z = std::cos(mu::Rad(deg));
}

} // namespace

// Médanos transversales: crestas perpendiculares al viento que serpentean, con la subida suave de
// barlovento (72% del largo de onda, cada vez más empinada: la cresta es un labio) y la cara de
// sotavento más empinada, que arranca plana en la cresta (se cae sobre ella en bajada). La altura
// crece y se achica de a zonas (entre 35% y 100%) y hacia el borde del mapa los médanos crecen
// ("border": un mar de médanos que tapa la pared del borde). Sin trigonometría (sale igual en todas
// las PCs).
float Terrain::Dunes(float x, float z) const
{
    const float lambda = def.wavelength;
    const float along = x * duneDirX + z * duneDirZ, across = x * duneDirZ - z * duneDirX;
    const float phase = along / lambda + def.meander * 2.0f * (Fbm(across * 0.8f / lambda + duneSeedX, along * 0.3f / lambda + duneSeedZ, 2) - 0.5f);
    const float row = std::floor(phase), f = phase - row;
    float amp = 0.35f + 0.65f * mu::Smoothstep(0.3f, 0.7f, Fbm(x * 0.45f / lambda + duneSeedZ + 7.3f, z * 0.45f / lambda + duneSeedX - 1.9f, 2));
    // Cada fila de médanos se corta en tramos (crestas que suben y bajan a lo largo, como barjanes
    // pegados). Entre filas (f = 0) la altura es 0, así que cambiar de fila no deja escalones.
    amp *= 0.3f + 0.7f * mu::Smoothstep(0.25f, 0.65f, Fbm(across / (1.3f * lambda) + row * 7.31f, row * 3.17f + duneSeedX, 2));
    const float half = Size() * 0.5f, d = std::max(std::fabs(x), std::fabs(z));
    const float border = mu::Smoothstep(half - 110.0f, half - 25.0f, d);
    amp = mu::Lerp(amp, 1.0f + def.border, border);
    constexpr float kStoss = 0.72f;
    float y;
    if (f < kStoss) {
        const float t = f / kStoss;
        y = t * t * (1.2f - 0.2f * t);                          // pendiente 0 abajo, 1.8/kStoss en la cresta
    } else {
        y = 1.0f - mu::Smoothstep(0.0f, 1.0f, (f - kStoss) / (1.0f - kStoss));
    }
    return def.height * amp * y + (ValueNoise(x * 0.09f + 17.0f + duneSeedX, z * 0.09f - 9.0f + duneSeedZ) - 0.5f) * def.detail;
}

// Formas esculpidas: cada perfil (tramos rectos) se pasa a una tabla cada kShapeStep m y se redondea
// con dos pasadas de promedio móvil de "smooth" m (un quiebre queda como una curva de ~2·smooth).
void Terrain::PrepareShapes()
{
    shapes.clear();
    for (const MapShape& ms : def.shapes) {
        Shape s;
        s.def = ms;
        Heading(ms.yaw, s.dirX, s.dirZ);
        s.first = ms.profile.front().first;
        s.last = ms.profile.back().first;
        const float pad = ms.smooth * 1.5f + 2.0f * kShapeStep;
        s.u0 = ms.round ? -pad : s.first - pad;
        const int n = std::max(2, (int)std::ceil((s.last + pad - s.u0) / kShapeStep) + 1);
        auto linear = [&](float u) {
            const auto& p = ms.profile;
            if (ms.round) u = std::fabs(u);                      // espejado en el centro: la cima queda redonda
            if (u <= p.front().first) return p.front().second;
            for (size_t k = 1; k < p.size(); ++k)
                if (u <= p[k].first) return mu::Lerp(p[k - 1].second, p[k].second, (u - p[k - 1].first) / (p[k].first - p[k - 1].first));
            return p.back().second;
        };
        s.table.resize(n);
        for (int i = 0; i < n; ++i) s.table[i] = linear(s.u0 + (float)i * kShapeStep);
        const int r = (int)std::lround(ms.smooth * 0.5f / kShapeStep);
        if (r > 0) {
            std::vector<float> tmp(n);
            for (int pass = 0; pass < 2; ++pass) {
                for (int i = 0; i < n; ++i) {
                    const int a = std::max(0, i - r), b = std::min(n - 1, i + r);
                    float sum = 0.0f;
                    for (int k = a; k <= b; ++k) sum += s.table[k];
                    tmp[i] = sum / (float)(b - a + 1);
                }
                s.table.swap(tmp);
            }
        }
        // Hasta dónde llega (caja en el mundo, con el borde que se desvanece).
        if (ms.round) {
            const float reach = (s.last + ms.edge) * std::max(ms.stretch[0], ms.stretch[1]);
            s.minX = ms.x - reach; s.maxX = ms.x + reach;
            s.minZ = ms.z - reach; s.maxZ = ms.z + reach;
        } else {
            s.minX = s.minZ = 1e9f;
            s.maxX = s.maxZ = -1e9f;
            const float side = ms.width * 0.5f + ms.edge;
            for (float u : {s.first - ms.edge + std::min(0.0f, ms.bend), s.last + ms.edge + std::max(0.0f, ms.bend)})
                for (float v : {-side, side}) {
                    const float wx = ms.x + u * s.dirX + v * s.dirZ, wz = ms.z + u * s.dirZ - v * s.dirX;
                    s.minX = std::min(s.minX, wx); s.maxX = std::max(s.maxX, wx);
                    s.minZ = std::min(s.minZ, wz); s.maxZ = std::max(s.maxZ, wz);
                }
        }
        if (ms.round && ms.hasArc) {
            s.arcWidth = std::fmod(ms.arc[1] - ms.arc[0], 360.0f);
            if (s.arcWidth <= 0.0f) s.arcWidth += 360.0f;
        }
        // Nivelar: a la altura dada o a la del suelo en "at" (con las formas anteriores).
        s.base = ms.hasBase ? ms.base : ApplyShapes(ms.x, ms.z, Natural(ms.x, ms.z), shapes.size());
        shapes.push_back(s);
    }
}

// Cuánto cubre la forma el punto (x, z): 1 adentro, bajando a 0 a "edge" m por fuera (0: no llega), y la
// coordenada u de su perfil ahí.
float Terrain::ShapeCover(const Shape& s, float x, float z, float& u) const
{
    if (x < s.minX || x > s.maxX || z < s.minZ || z > s.maxZ) return 0.0f;
    const MapShape& d = s.def;
    const float dx = x - d.x, dz = z - d.z;
    const float along = dx * s.dirX + dz * s.dirZ, across = dx * s.dirZ - dz * s.dirX;
    float out;                                               // cuánto queda afuera
    if (d.round) {
        const float a = across / d.stretch[0], b = along / d.stretch[1];
        u = std::sqrt(a * a + b * b);
        out = std::max(0.0f, u - s.last);
        if (d.hasArc && s.arcWidth < 360.0f) {
            // Sólo el sector: el rumbo del punto visto desde el centro (0 = norte, 90 = este), de arc[0]
            // a arc[1] en el sentido del reloj. Pasando un borde, se desvanece con la distancia al rayo
            // de ese borde (como las puntas de una "line").
            const float deg = mu::Deg(std::atan2(dx, dz)) - d.arc[0];
            const float rel = deg - 360.0f * std::floor(deg / 360.0f);
            if (rel > s.arcWidth) {
                const float off = std::min(rel - s.arcWidth, 360.0f - rel), r = std::sqrt(dx * dx + dz * dz);
                const float side = off >= 90.0f ? r : r * std::sin(mu::Rad(off));
                out = std::sqrt(out * out + side * side);
            }
        }
    } else {
        // bend: las puntas se corren a lo largo (medialuna, como un barján: cuernos hacia donde crece u).
        const float q = std::min(1.0f, std::fabs(across) / std::max(d.width * 0.5f + d.edge, 0.1f));
        u = along - d.bend * q * q;
        const float du = std::max({0.0f, s.first - u, u - s.last}), dv = std::max(0.0f, std::fabs(across) - d.width * 0.5f);
        out = std::sqrt(du * du + dv * dv);
    }
    return d.edge > 0.0f ? 1.0f - mu::Smoothstep(0.0f, d.edge, out) : (out > 0.0f ? 0.0f : 1.0f);
}

float Terrain::ApplyShapes(float x, float z, float h, size_t count) const
{
    for (size_t i = 0; i < count; ++i) {
        const Shape& s = shapes[i];
        float u = 0.0f;
        const float w = ShapeCover(s, x, z, u);
        if (w <= 0.0f) continue;
        const float f = mu::Clamp((u - s.u0) / kShapeStep, 0.0f, (float)(s.table.size() - 1) - 0.001f);
        const int k = (int)f;
        const float p = mu::Lerp(s.table[k], s.table[k + 1], f - (float)k);
        h = s.def.level ? mu::Lerp(h, s.base + p, w) : h + p * w;
    }
    return h;
}

float Terrain::ImageHeight(float x, float z) const
{
    if (image.empty()) return 0.0f;
    // La imagen cubre todo el mapa: arriba de la imagen = norte (+z), izquierda = oeste (-x).
    const float u = mu::Clamp((x - origin) / Size(), 0.0f, 1.0f) * (float)(imageW - 1);
    const float v = mu::Clamp(1.0f - (z - origin) / Size(), 0.0f, 1.0f) * (float)(imageH - 1);
    const int ix = std::clamp((int)u, 0, std::max(imageW - 2, 0)), iy = std::clamp((int)v, 0, std::max(imageH - 2, 0));
    const int ix1 = std::min(ix + 1, imageW - 1), iy1 = std::min(iy + 1, imageH - 1);
    const float tx = u - (float)ix, ty = v - (float)iy;
    const float a = mu::Lerp(image[iy * imageW + ix], image[iy * imageW + ix1], tx);
    const float b = mu::Lerp(image[iy1 * imageW + ix], image[iy1 * imageW + ix1], tx);
    return mu::Lerp(a, b, ty);
}

float Terrain::Natural(float x, float z) const
{
    float h = 0.0f;
    if (def.type == "favela") {
        h = FavelaHill(x, z);
    } else if (def.type == "heightmap") {
        h = def.imageBase + ImageHeight(x, z) * def.imageHeight;
    } else if (def.type == "hills") {
        h = (Fbm(x * def.scale + 3.1f, z * def.scale - 7.7f, 3) - 0.5f) * def.height;   // lomas grandes
        h += (ValueNoise(x * 0.09f + 17.0f, z * 0.09f - 9.0f) - 0.5f) * def.detail;     // irregularidad
    } else if (def.type == "dunes") {
        h = Dunes(x, z);
    }
    return h;
}

float Terrain::Ground(float x, float z) const
{
    const float h = shapes.empty() ? Natural(x, z) : ApplyShapes(x, z, Natural(x, z), shapes.size());
    // Terreno que sube en los bordes del mapa para contener al piloto.
    const float edge = Size() * 0.5f;
    const float d = std::max(std::fabs(x), std::fabs(z));
    const float t = mu::Smoothstep(edge - def.edgeHeight - 2.0f, edge - 1.0f, d);
    return h + t * t * def.edgeHeight;
}

void Terrain::Build(const Track& track, const MapDef& map, bool flat)
{
    grassCache.reset();
    noGrass.clear();
    def = map.terrain;
    terraces = map.terraces;
    streets = map.roadStyle == "street" && !flat;
    guide = map.roadStyle == "guide";
    grassOffRoad = map.look.groundTextures == "circuit";
    sand = map.look.groundTextures == "sand";
    Heading(def.wind, duneDirX, duneDirZ);
    duneSeedX = (float)def.seed * 37.13f;
    duneSeedZ = (float)def.seed * -23.71f;
    // Tamaño del mapa: muestras por lado múltiplo de 8 (bloques del heightfield de Jolt).
    Cell = def.resolution;
    N = std::clamp(((int)std::ceil(def.size / Cell) + 1 + 7) / 8 * 8, 64, 2048);
    origin = -Size() * 0.5f;
    heights.assign(N * N, 0.0f);
    trackMask.assign(N * N, 0.0f);
    streetMask.assign(N * N, 0.0f);
    streetTone.assign(N * N, 0.0f);
    roadDist.assign(N * N, 1e9f);

    image.clear();
    imageW = imageH = 0;
    if (def.type == "heightmap" && !flat) {
        Image img = LoadImage(def.image.c_str());
        if (img.data) {
            ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE);
            imageW = img.width;
            imageH = img.height;
            image.resize((size_t)imageW * imageH);
            const unsigned char* px = (const unsigned char*)img.data;
            for (size_t i = 0; i < image.size(); ++i) image[i] = px[i] / 255.0f;
            UnloadImage(img);
        } else {
            std::printf("terrain: no se pudo leer %s (queda plano)\n", def.image.c_str());
        }
    }
    PrepareShapes();

    for (int z = 0; z < N; ++z)
        for (int x = 0; x < N; ++x)
            heights[z * N + x] = flat ? 0.0f : Ground(origin + x * Cell, origin + z * Cell);
    if (!flat) {
        if (streets) StampStreets(track);
        else if (!guide) StampTrack(track);                  // la guía no toca el terreno
    }
    // Formas con "dirt": se pintan con la tierra de la pista (la máscara de pista da la textura, el agarre y el
    // polvo), hasta donde llegan.
    bool dirtShapes = false;
    for (const Shape& s : shapes) dirtShapes |= s.def.dirt;
    if (dirtShapes && !flat) {
        for (int z = 0; z < N; ++z)
            for (int x = 0; x < N; ++x) {
                float& m = trackMask[z * N + x];
                for (const Shape& s : shapes) {
                    float u = 0.0f;
                    if (s.def.dirt) m = std::max(m, ShapeCover(s, origin + x * Cell, origin + z * Cell, u));
                }
            }
    }
    ClearFineSoil();
    soilSubdivision = std::max(1, (int)std::ceil(Cell / 0.05f));
    soilTileCells = 8;
    while (soilTileCells > 1 && soilTileCells * Cell > 4.0f) soilTileCells /= 2;
    // SubShapeID de Jolt tiene 32 bits en total, incluidos los dos niveles del compuesto.
    // Sólo mapas enormes de mods requieren bajar el detalle para no desbordar esos IDs.
    auto bits=[](int n) { int b=0; for (--n;n>0;n>>=1) ++b; return b; };
    const int count=(N-2)/kChunk+1, children=kChunk/soilTileCells;
    while (soilSubdivision>1 && bits(count*count)+bits(children*children)+
           2*bits(((soilTileCells*soilSubdivision+4)/4)*4)+1>32) --soilSubdivision;
    original = heights;
    pendingHeights = heights;
    dirty = deformed = false;
    deformationRevision = 0;
}

void Terrain::StampTrack(const Track& track)
{
    // Altura base de la pista: el terreno natural bajo la línea central, muy suavizado.
    const auto& pts = track.Points();
    const int M = (int)pts.size();
    std::vector<float> base(M);
    for (int i = 0; i < M; ++i) base[i] = Ground(pts[i].x, pts[i].z);
    std::vector<float> tmp(M);
    for (int pass = 0; pass < 4; ++pass) {
        const int r = 30;
        for (int i = 0; i < M; ++i) {
            float sum = 0.0f;
            for (int k = -r; k <= r; ++k) sum += base[(i + k + M) % M];
            tmp[i] = sum / (float)(2 * r + 1);
        }
        base.swap(tmp);
    }
    ApplyTerraces(track, base);

    // "Estampado": para cada vértice cerca de la pista, el segmento de línea central más cercano.
    const float hw = track.HalfWidth();
    const float influence = hw + kShoulder + 2.0f;
    std::vector<float> bestD(N * N, 1e9f), bestS(N * N, 0.0f), bestLat(N * N, 0.0f);
    for (int i = 0; i < M; ++i) {
        const TrackPoint& a = pts[i];
        const TrackPoint& b = pts[(i + 1) % M];
        float abx = b.x - a.x, abz = b.z - a.z;
        float len2 = std::max(abx * abx + abz * abz, 1e-6f);
        float len = std::sqrt(len2);
        float rx = -abz / len, rz = abx / len;                   // normal derecha
        int gx0 = std::max(0, (int)((std::min(a.x, b.x) - influence - origin) / Cell));
        int gx1 = std::min(N - 1, (int)((std::max(a.x, b.x) + influence - origin) / Cell) + 1);
        int gz0 = std::max(0, (int)((std::min(a.z, b.z) - influence - origin) / Cell));
        int gz1 = std::min(N - 1, (int)((std::max(a.z, b.z) + influence - origin) / Cell) + 1);
        for (int gz = gz0; gz <= gz1; ++gz) {
            for (int gx = gx0; gx <= gx1; ++gx) {
                float px = origin + gx * Cell, pz = origin + gz * Cell;
                float t = mu::Clamp(((px - a.x) * abx + (pz - a.z) * abz) / len2, 0.0f, 1.0f);
                float cx = a.x + abx * t, cz = a.z + abz * t;
                float dx = px - cx, dz = pz - cz;
                float d = std::sqrt(dx * dx + dz * dz);
                int idx = gz * N + gx;
                if (d < bestD[idx]) {
                    bestD[idx] = d;
                    bestS[idx] = a.s + t * track.Spacing();
                    bestLat[idx] = dx * rx + dz * rz;
                }
            }
        }
    }

    for (int idx = 0; idx < N * N; ++idx) {
        roadDist[idx] = bestD[idx];
        if (bestD[idx] > influence) continue;
        float s = bestS[idx], lat = bestLat[idx];
        float f = track.Wrap(s) / track.Spacing();
        int i0 = (int)f % M, i1 = (i0 + 1) % M;
        float baseH = mu::Lerp(base[i0], base[i1], f - std::floor(f));
        float surface = baseH + track.Profile(s) + Berm(track.Bank(s), lat, hw);
        float edge = std::fabs(lat) - hw;
        float blend = 1.0f - mu::Smoothstep(0.3f, kShoulder, edge);
        heights[idx] = mu::Lerp(heights[idx], surface, blend);
        trackMask[idx] = 1.0f - mu::Smoothstep(-0.3f, 2.2f, edge);
    }
}

// Mesetas (la ladeira de la favela): entre los puntos "from" y "to" del trazado, una meseta a nivel en
// cada cruce y, entre una y otra, una bajada que sale por un labio (arranca empinada: se vuela) y se va
// aplanando hasta la meseta de abajo (donde se cae).
void Terrain::ApplyTerraces(const Track& track, std::vector<float>& base) const
{
    if (!terraces.on) return;
    const auto& pts = track.Points();
    const int M = (int)pts.size();
    const int i0 = track.Nearest(terraces.from[0], terraces.from[1]), i1 = track.Nearest(terraces.to[0], terraces.to[1]);
    const float plateau = terraces.plateauHalf;
    struct Knot { float s, h; int shape; };                 // tramo que empieza acá: 0 plano, 1 recto, 2 labio
    std::vector<Knot> knots = {{pts[i0].s, base[i0], 1}};
    for (const auto& pz : terraces.plateaus) {
        // El punto de este tramo más cercano (el más cercano de toda la pista podría ser de la calle que cruza).
        int ic = i0;
        auto d2 = [&](int i) { return (pts[i].x - pz.first) * (pts[i].x - pz.first) + (pts[i].z - pz.second) * (pts[i].z - pz.second); };
        for (int i = i0; i != i1; i = (i + 1) % M)
            if (d2(i) < d2(ic)) ic = i;
        knots.push_back({pts[ic].s - plateau, base[ic], 0});
        knots.push_back({pts[ic].s + plateau, base[ic], 2});
    }
    knots.push_back({pts[i1].s, base[i1], 0});
    for (int i = i0; i != i1; i = (i + 1) % M) {
        const float s = pts[i].s;
        size_t k = 0;
        while (k + 2 < knots.size() && s >= knots[k + 1].s) ++k;
        const Knot& a = knots[k];
        const Knot& b = knots[k + 1];
        const float t = mu::Clamp((s - a.s) / std::max(b.s - a.s, 0.1f), 0.0f, 1.0f);
        const float f = a.shape == 0 ? 0.0f : (a.shape == 1 ? t : 1.0f - std::pow(1.0f - t, 1.6f));
        base[i] = a.h + (b.h - a.h) * f;
    }
}

// Calles (como las de la favela): cada punto de la calle toma la altura de la ladera en su línea
// central (la calle queda nivelada de costado, cortada contra el morro) más el perfil (lomos de burro,
// rampas).
// En los cruces se mezclan las dos calles según la distancia a cada una y pesa más la más plana: el
// cruce queda como una meseta a nivel de la calle que atraviesa la ladeira y, bajando, se entra
// empinado y se sale por un labio (un salto), sin escalones.
void Terrain::StampStreets(const Track& track)
{
    const auto& pts = track.Points();
    const int M = (int)pts.size();
    // Altura de cada punto de la línea central: la ladera, apenas suavizada a lo largo de la calle.
    std::vector<float> base(M), tmp(M);
    for (int i = 0; i < M; ++i) base[i] = Ground(pts[i].x, pts[i].z);
    for (int pass = 0; pass < 2; ++pass) {
        const int r = 6;
        for (int i = 0; i < M; ++i) {
            float sum = 0.0f;
            for (int k = -r; k <= r; ++k) sum += base[(i + k + M) % M];
            tmp[i] = sum / (float)(2 * r + 1);
        }
        base.swap(tmp);
    }

    ApplyTerraces(track, base);

    // Por vértice, las dos calles más cercanas que no sean el mismo tramo (distintas en s).
    const float hw = track.HalfWidth();
    const float influence = hw + kFavelaShoulder + 3.0f;
    struct Near { float d = 1e9f, s = 0.0f, lat = 0.0f; };
    std::vector<Near> n1(N * N), n2(N * N);
    const float L = track.Length();
    auto sameBranch = [&](float a, float b) {
        const float d = std::fabs(a - b);
        return std::min(d, L - d) < 25.0f;
    };
    for (int i = 0; i < M; ++i) {
        const TrackPoint& a = pts[i];
        const TrackPoint& b = pts[(i + 1) % M];
        const float abx = b.x - a.x, abz = b.z - a.z;
        const float len2 = std::max(abx * abx + abz * abz, 1e-6f), len = std::sqrt(len2);
        const float rx = -abz / len, rz = abx / len;
        const int gx0 = std::max(0, (int)((std::min(a.x, b.x) - influence - origin) / Cell));
        const int gx1 = std::min(N - 1, (int)((std::max(a.x, b.x) + influence - origin) / Cell) + 1);
        const int gz0 = std::max(0, (int)((std::min(a.z, b.z) - influence - origin) / Cell));
        const int gz1 = std::min(N - 1, (int)((std::max(a.z, b.z) + influence - origin) / Cell) + 1);
        for (int gz = gz0; gz <= gz1; ++gz)
            for (int gx = gx0; gx <= gx1; ++gx) {
                const float px = origin + gx * Cell, pz = origin + gz * Cell;
                const float t = mu::Clamp(((px - a.x) * abx + (pz - a.z) * abz) / len2, 0.0f, 1.0f);
                const float dx = px - (a.x + abx * t), dz = pz - (a.z + abz * t);
                const Near c{std::sqrt(dx * dx + dz * dz), a.s + t * track.Spacing(), dx * rx + dz * rz};
                const int idx = gz * N + gx;
                Near& b1 = n1[idx];
                Near& b2 = n2[idx];
                if (c.d < b1.d) {
                    if (!sameBranch(c.s, b1.s) && b1.d < 1e8f) b2 = b1;
                    b1 = c;
                } else if (!sameBranch(c.s, b1.s) && c.d < b2.d) {
                    b2 = c;
                }
            }
    }

    auto baseAt = [&](float s) {
        const float f = track.Wrap(s) / track.Spacing();
        const int i0 = (int)f % M, i1 = (i0 + 1) % M;
        return mu::Lerp(base[i0], base[i1], f - std::floor(f));
    };
    auto gradeAt = [&](float s) {                      // pendiente de la calle ahí (|dh/ds|)
        return std::fabs(baseAt(s + 2.0f) - baseAt(s - 2.0f)) / 4.0f;
    };
    for (int idx = 0; idx < N * N; ++idx) {
        const Near& a = n1[idx];
        roadDist[idx] = a.d;
        if (a.d > influence) continue;
        const Near& b = n2[idx];
        // Peso de cada calle: pleno sobre la calle, 0 pasando la banquina; la más plana pesa más (con un
        // piso chico para no dividir por 0).
        auto weight = [&](const Near& n) {
            if (n.d > influence) return 0.0f;
            return (1.0f - mu::Smoothstep(hw * 0.7f, hw + kFavelaShoulder, n.d)) / (0.03f + gradeAt(n.s)) + 1e-3f;
        };
        const float wa = weight(a), wb = weight(b);
        const float ha = baseAt(a.s) + track.Profile(a.s), hb = b.d < 1e8f ? baseAt(b.s) + track.Profile(b.s) : ha;
        const float surface = (ha * wa + hb * wb) / (wa + wb);
        const float edge = a.d - hw;
        const float blend = 1.0f - mu::Smoothstep(0.3f, kFavelaShoulder, edge);
        heights[idx] = mu::Lerp(heights[idx], surface, blend);
        // Máscaras: la calle más pavimentada de las dos manda (en el cruce se ve el pavimento).
        const float streetA = 1.0f - mu::Smoothstep(-0.3f, 0.9f, edge);
        const float streetB = b.d < 1e8f ? 1.0f - mu::Smoothstep(-0.3f, 0.9f, b.d - hw) : 0.0f;
        streetMask[idx] = std::max(streetA, streetB);
        const float pa = track.Pavement(a.s) * streetA, pb = b.d < 1e8f ? track.Pavement(b.s) * streetB : 0.0f;
        trackMask[idx] = std::max(pa, pb);
        streetTone[idx] = pa >= pb ? track.Asphalt(a.s) : track.Asphalt(b.s);
    }
}

float Terrain::Sample(const std::vector<float>& a, float x, float z) const
{
    if (a.empty()) return 0.0f;
    float fx = mu::Clamp((x - origin) / Cell, 0.0f, N - 1.001f), fz = mu::Clamp((z - origin) / Cell, 0.0f, N - 1.001f);
    int ix = (int)fx, iz = (int)fz;
    float tx = fx - ix, tz = fz - iz;
    float r0 = mu::Lerp(a[iz * N + ix], a[iz * N + ix + 1], tx);
    float r1 = mu::Lerp(a[(iz + 1) * N + ix], a[(iz + 1) * N + ix + 1], tx);
    return mu::Lerp(r0, r1, tz);
}

float Terrain::RoadDistance(float x, float z) const { return Sample(roadDist, x, z); }

float Terrain::Dustiness(float x, float z) const
{
    const float m = TrackMask(x, z);
    // Arena: poco, como el pasto (el color del polvo lo pone Game.cpp y es de tierra, más oscuro que la
    // arena: con más polvo la moto deja una estela de humo gris).
    if (sand) return 0.25f;
    return streets ? mu::Lerp(1.0f, 0.12f, m) : mu::Lerp(0.3f, 1.0f, m);   // calles: el pavimento casi no levanta tierra
}

void Terrain::CreateCollision(PhysicsWorld& world)
{
    JPH::HeightFieldShapeSettings settings(heights.data(), JPH::Vec3(origin, 0.0f, origin), JPH::Vec3(Cell, 1.0f, Cell), N);
    // Margen por debajo del mínimo para poder cavar surcos con SetHeights sin que se recorten.
    settings.mMinHeightValue = *std::min_element(heights.begin(), heights.end()) - 1.0f;
    JPH::ShapeSettings::ShapeResult result = settings.Create();
    if (result.HasError()) {
        std::printf("terrain: error creando heightfield: %s\n", result.GetError().c_str());
        return;
    }
    // Jolt entrega el shape como const; lo modificamos sólo entre pasos, desde el hilo principal.
    shape = static_cast<JPH::HeightFieldShape*>(const_cast<JPH::Shape*>(result.Get().GetPtr()));
    baseShape = shape;
    soilCollision = false;
    JPH::BodyCreationSettings bcs(result.Get(), JPH::RVec3::sZero(), JPH::Quat::sIdentity(), JPH::EMotionType::Static, Layers::STATIC);
    bcs.mFriction = 0.9f;
    body = world.Bodies().CreateAndAddBody(bcs, JPH::EActivation::DontActivate);
}

float Terrain::Height(float x, float z) const
{
    return BaseHeight(x,z) + SoilDelta(x,z);
}

float Terrain::BaseHeight(float x, float z) const
{
    float fx = (x - origin) / Cell, fz = (z - origin) / Cell;
    int ix = std::clamp((int)std::floor(fx), 0, N - 2);
    int iz = std::clamp((int)std::floor(fz), 0, N - 2);
    float tx = mu::Clamp(fx - (float)ix, 0.0f, 1.0f), tz = mu::Clamp(fz - (float)iz, 0.0f, 1.0f);
    float h00 = H(ix, iz), h10 = H(ix + 1, iz), h01 = H(ix, iz + 1), h11 = H(ix + 1, iz + 1);
    // Diagonal (x,z)-(x+1,z+1), igual que HeightFieldShape.
    if (tx > tz) return h00 + (h10 - h00) * tx + (h11 - h10) * tz;
    return h00 + (h11 - h01) * tx + (h01 - h00) * tz;
}

JPH::Vec3 Terrain::Normal(float x, float z) const
{
    // Normal suave (diferencias centrales), sin el facetado de los triángulos.
    float dx=BaseHeight(x-Cell,z)-BaseHeight(x+Cell,z);
    float dz=BaseHeight(x,z-Cell)-BaseHeight(x,z+Cell);
    if (!soilHeights.empty()) {
        const float e=SoilCell();
        dx+=(SoilDelta(x-e,z)-SoilDelta(x+e,z))*Cell/e;
        dz+=(SoilDelta(x,z-e)-SoilDelta(x,z+e))*Cell/e;
    }
    return JPH::Vec3(dx,2.0f*Cell,dz).Normalized();
}

float Terrain::TrackMask(float x, float z) const
{
    float fx = mu::Clamp((x - origin) / Cell, 0.0f, N - 1.001f), fz = mu::Clamp((z - origin) / Cell, 0.0f, N - 1.001f);
    int ix = (int)fx, iz = (int)fz;
    float tx = fx - ix, tz = fz - iz;
    float a = mu::Lerp(trackMask[iz * N + ix], trackMask[iz * N + ix + 1], tx);
    float b = mu::Lerp(trackMask[(iz + 1) * N + ix], trackMask[(iz + 1) * N + ix + 1], tx);
    return mu::Lerp(a, b, tz);
}

void Terrain::ExcludeGrass(const float q[8], float margin) const
{
    const int side = (int)std::ceil(Size()) + 1;
    if (noGrass.empty()) noGrass.assign((size_t)side * side, 0);
    grassCache.reset();                    // las matas ya armadas no sabían de esta zona
    float x0 = q[0], x1 = q[0], z0 = q[1], z1 = q[1], area = 0.0f;
    for (int k = 0; k < 4; ++k) {
        x0 = std::min(x0, q[k * 2]);
        x1 = std::max(x1, q[k * 2]);
        z0 = std::min(z0, q[k * 2 + 1]);
        z1 = std::max(z1, q[k * 2 + 1]);
        const int n = (k + 1) % 4;
        area += q[k * 2] * q[n * 2 + 1] - q[n * 2] * q[k * 2 + 1];
    }
    if (std::fabs(area) < 1e-4f) return;
    const float orient = area > 0.0f ? 1.0f : -1.0f;
    const int i0 = std::max(0, (int)std::floor(x0 - margin - origin)), i1 = std::min(side - 1, (int)std::floor(x1 + margin - origin));
    const int j0 = std::max(0, (int)std::floor(z0 - margin - origin)), j1 = std::min(side - 1, (int)std::floor(z1 + margin - origin));
    for (int j = j0; j <= j1; ++j)
        for (int i = i0; i <= i1; ++i) {
            const float px = origin + (float)i + 0.5f, pz = origin + (float)j + 0.5f;
            bool inside = true;
            for (int k = 0; k < 4 && inside; ++k) {         // a lo sumo margin afuera de cada lado
                const int n = (k + 1) % 4;
                const float ex = q[n * 2] - q[k * 2], ez = q[n * 2 + 1] - q[k * 2 + 1];
                const float len = std::sqrt(ex * ex + ez * ez);
                if (len < 1e-4f) continue;
                inside = orient * (ex * (pz - q[k * 2 + 1]) - ez * (px - q[k * 2])) / len >= -margin;
            }
            if (inside) noGrass[(size_t)j * side + i] = 1;
        }
}

bool Terrain::GrassExcluded(float x, float z) const
{
    if (noGrass.empty()) return false;
    const int side = (int)std::ceil(Size()) + 1;
    const int i = (int)std::floor(x - origin), j = (int)std::floor(z - origin);
    return i >= 0 && j >= 0 && i < side && j < side && noGrass[(size_t)j * side + i] != 0;
}

// Caché de las matas: el pasto se arma por bloques de kGrassBlock x kGrassBlock celdas de la grilla de matas (17.6 m),
// una sola vez por mapa, y cada lista (Game::Draw, cada 6 m de cámara) se arma juntando los bloques que entran en el
// radio: rehacer todo cada vez costaba 3 ms con 45 m de pasto y 8-10 con 100 (medido), un tirón cada pocos metros.
struct Terrain::GrassCache {
    struct Block {
        std::vector<Matrix> tufts;     // ordenadas por hash de raleo (m15) ascendente
        unsigned stamp = 0;            // última lista que la usó (para soltar las más viejas)
    };
    std::unordered_map<int64_t, Block> blocks;
    unsigned stamp = 0;
};

// Las matas de un bloque, con lo que no depende de dónde esté la cámara: dónde caen, la pendiente, el tono y el hash
// de raleo. El raleo con la distancia lo hace GrassTufts al juntar los bloques (y kGrassVS cada cuadro).
void Terrain::GrassBlock(int bi, int bj, std::vector<Matrix>& out) const
{
    out.clear();
    const Color grass = kGrass, grassDry = kGrassDry;
    const float lo = origin + 1.0f, hi = origin + Size() - 1.0f;
    for (int j = bj * kGrassBlock; j < (bj + 1) * kGrassBlock; ++j) {
        for (int i = bi * kGrassBlock; i < (bi + 1) * kGrassBlock; ++i) {
            const float x = ((float)i + Hash(i, j)) * kGrassCell, z = ((float)j + Hash(j + 71, i - 33)) * kGrassCell;
            if (x < lo || x > hi || z < lo || z > hi) continue;
            if (GrassExcluded(x, z)) continue;            // asfalto de una malla sobre el pasto (calle de boxes)
            const float patches = mu::Smoothstep(0.3f, 0.7f, ValueNoise(x * 0.18f + 3.0f, z * 0.18f - 7.0f));
            const float pick = Hash(i * 7 + 1, j * 13 + 5);
            if (sand) {                                   // arena: alguna mata seca suelta, sólo en lo plano
                if (pick > 0.04f * patches) continue;
                if (TrackMask(x, z) > 0.3f || Normal(x, z).GetY() < 0.96f) continue;
            } else {
                const float density = streets && !grassOffRoad ? (1.0f - Sample(streetMask, x, z)) * 0.35f * patches
                                             : (1.0f - TrackMask(x, z)) * (0.25f + 0.75f * patches);
                if (pick > density) continue;
                // En las lomas empinadas (el borde del mapa) el pasto plano de las matas se pega como manchas
                // brillantes: ralea con la pendiente (sólo se mira después de los filtros baratos, cuesta una normal).
                if (pick > density * mu::Smoothstep(0.75f, 0.90f, Normal(x, z).GetY())) continue;
            }
            const float lodH = Hash(i + 341, j - 829);   // hash de raleo: queda en m15 (ver GrassTufts y kGrassVS)
            const float s = 0.7f + 0.6f * Hash(i + 911, j - 177);
            if (s < 0.05f) continue;
            Matrix m = MatrixMultiply(MatrixMultiply(MatrixScale(s, s * (0.75f + 0.6f * pick), s), MatrixRotateY(Hash(i - 5, j + 9) * 6.2832f)),
                                      MatrixTranslate(x, Height(x, z) - 0.02f, z));
            // Mismo tono que el color de vértice del terreno en ese lugar (va en la fila de abajo).
            const float t = mu::Clamp(ValueNoise(x * 0.15f, z * 0.15f) * 1.2f - 0.2f, 0.0f, 1.0f);
            float shade = 0.92f + ValueNoise(x * 1.3f + 5.0f, z * 1.3f - 3.0f) * 0.16f;
            const Color a = sand ? Color{126, 116, 72, 255} : grass, b = sand ? Color{158, 140, 96, 255} : grassDry;   // arena: paja seca
            float r = mu::Lerp(a.r, b.r, t), g = mu::Lerp(a.g, b.g, t), bl = mu::Lerp(a.b, b.b, t);
            if (!sand && !streets) {                      // el bake de pendiente del terreno (CreateMeshes), con la misma cuenta
                const float slope = std::max(std::fabs(BaseHeight(x + Cell, z) - BaseHeight(x - Cell, z)),
                                             std::fabs(BaseHeight(x, z + Cell) - BaseHeight(x, z - Cell))) / (2.0f * Cell);
                const float steepT = mu::Smoothstep(0.35f, 0.9f, slope);
                if (steepT > 0.0f) {
                    const float k = steepT * 0.85f;
                    r = mu::Lerp(r, mu::Lerp(grass.r, grassDry.r, 0.3f), k);
                    g = mu::Lerp(g, mu::Lerp(grass.g, grassDry.g, 0.3f), k);
                    bl = mu::Lerp(bl, mu::Lerp(grass.b, grassDry.b, 0.3f), k);
                    shade = mu::Lerp(shade, 1.0f, steepT);
                }
            }
            m.m15 = lodH;
            m.m3 = r * shade / 255.0f;
            m.m7 = g * shade / 255.0f;
            m.m11 = bl * shade / 255.0f;
            out.push_back(m);
        }
    }
    std::sort(out.begin(), out.end(), [](const Matrix& a, const Matrix& b) { return a.m15 < b.m15; });
}

void Terrain::GrassTufts(float cx, float cz, float radius, std::vector<Matrix>& out, float densityScale) const
{
    out.clear();
    if (!grassCache) grassCache = std::make_shared<GrassCache>();
    GrassCache& cache = *grassCache;
    ++cache.stamp;
    constexpr float kBlockSize = kGrassBlock * kGrassCell;
    const int bi0 = (int)std::floor((cx - radius) / kBlockSize), bi1 = (int)std::floor((cx + radius) / kBlockSize);
    const int bj0 = (int)std::floor((cz - radius) / kBlockSize), bj1 = (int)std::floor((cz + radius) / kBlockSize);
    for (int bj = bj0; bj <= bj1; ++bj) {
        for (int bi = bi0; bi <= bi1; ++bi) {
            // Distancia del centro al bloque: si ni la más cercana entra en el radio, nada.
            const float x0 = (float)bi * kBlockSize, z0 = (float)bj * kBlockSize;
            const float dx = std::max({x0 - cx, 0.0f, cx - (x0 + kBlockSize)}), dz = std::max({z0 - cz, 0.0f, cz - (z0 + kBlockSize)});
            const float nearest = std::sqrt(dx * dx + dz * dz);
            if (nearest > radius) continue;
            auto [it, fresh] = cache.blocks.try_emplace((int64_t)(((uint64_t)(uint32_t)bj << 32) | (uint64_t)(uint32_t)bi));
            if (fresh) GrassBlock(bi, bj, it->second.tufts);
            it->second.stamp = cache.stamp;
            // Raleo con la distancia (~1/d² pasados los 30 m): kGrassVS funde cada mata con la distancia de AHORA (sin
            // saltos); acá pasan las que van a entrar antes de la próxima vez que se rehaga la lista: la cámara se mueve
            // hasta 6 m entre listas y el radio de la lista suma 10 m; +0.15 es el ancho del fundido. Como las de un
            // bloque están ordenadas por hash, con el umbral de su punto más cercano se corta el recorrido.
            auto threshold = [&](float dist) {
                const float dm = std::max(1.0f, dist - 10.0f);
                return std::min(1.0f, 900.0f / (dm * dm)) * densityScale + 0.15f;
            };
            const float limit = threshold(nearest);
            for (const Matrix& m : it->second.tufts) {
                if (m.m15 > limit) break;
                const float dist = std::sqrt((m.m12 - cx) * (m.m12 - cx) + (m.m14 - cz) * (m.m14 - cz));
                if (dist > radius || m.m15 > threshold(dist)) continue;
                out.push_back(m);
            }
        }
    }
    // Con el suelo deformado, las matas de la caché pueden tener la altura de antes.
    if (deformed || !soilHeights.empty())
        for (Matrix& m : out) m.m13 = Height(m.m12, m.m14) - 0.02f;
    // Un tope a la caché: se sueltan los bloques que hace más que no se usan.
    constexpr size_t kMaxBlocks = 500;
    if (cache.blocks.size() > kMaxBlocks) {
        std::vector<std::pair<unsigned, int64_t>> age;
        for (const auto& e : cache.blocks) age.push_back({e.second.stamp, e.first});
        std::sort(age.begin(), age.end());
        for (size_t k = 0; k < age.size() - kMaxBlocks * 3 / 4; ++k) cache.blocks.erase(age[k].second);
    }
}

float Terrain::SurfaceGrip(float x, float z) const
{
    if (streets) {                                    // pavimento > tierra apisonada de la calle > tierra suelta
        const float street = Sample(streetMask, x, z);
        return mu::Lerp(mu::Lerp(0.84f, 0.95f, street), 1.08f, TrackMask(x, z));
    }
    return mu::Lerp(0.82f, 1.0f, TrackMask(x, z));   // el pasto agarra menos que la tierra de pista
}

void Terrain::FillChunkGeometry(const Chunk& c) const
{
    Mesh* m = c.mesh;
    int v = 0;
    for (int z = c.z0; z < c.z0 + c.h; ++z) {
        for (int x = c.x0; x < c.x0 + c.w; ++x, ++v) {
            float hl = H(std::max(x - 1, 0), z), hr = H(std::min(x + 1, N - 1), z);
            float hd = H(x, std::max(z - 1, 0)), hu = H(x, std::min(z + 1, N - 1));
            Vector3 n = Vector3Normalize({hl - hr, 2.0f * Cell, hd - hu});
            m->vertices[v * 3 + 0] = origin + x * Cell;
            m->vertices[v * 3 + 1] = H(x, z);
            m->vertices[v * 3 + 2] = origin + z * Cell;
            m->normals[v * 3 + 0] = n.x;
            m->normals[v * 3 + 1] = n.y;
            m->normals[v * 3 + 2] = n.z;
        }
    }
}

void Terrain::CreateMeshes()
{
    const Color grass = kGrass;
    const Color grassDry = kGrassDry;

    auto mixc = [](Color a, Color b, float t) {
        t = mu::Clamp(t, 0.0f, 1.0f);
        return Color{(unsigned char)mu::Lerp(a.r, b.r, t), (unsigned char)mu::Lerp(a.g, b.g, t),
                     (unsigned char)mu::Lerp(a.b, b.b, t), 255};
    };

    for (int cz = 0; cz < N - 1; cz += kChunk) {
        for (int cx = 0; cx < N - 1; cx += kChunk) {
            const int x1 = std::min(cx + kChunk, N - 1), z1 = std::min(cz + kChunk, N - 1);
            Chunk c{new Mesh{}, cx, cz, x1 - cx + 1, z1 - cz + 1};
            for (int z = cz; z <= z1; ++z)
                for (int x = cx; x <= x1; ++x) {
                    c.low = std::min(c.low, H(x, z));
                    c.high = std::max(c.high, H(x, z));
                }
            Mesh* m = c.mesh;
            m->vertexCount = c.w * c.h;
            m->triangleCount = (c.w - 1) * (c.h - 1) * 2;
            m->vertices = (float*)MemAlloc(m->vertexCount * 3 * sizeof(float));
            m->normals = (float*)MemAlloc(m->vertexCount * 3 * sizeof(float));
            m->texcoords = (float*)MemAlloc(m->vertexCount * 2 * sizeof(float));
            m->colors = (unsigned char*)MemAlloc(m->vertexCount * 4);
            m->indices = (unsigned short*)MemAlloc(m->triangleCount * 3 * sizeof(unsigned short));
            FillChunkGeometry(c);

            int v = 0;
            for (int z = cz; z <= z1; ++z) {
                for (int x = cx; x <= x1; ++x, ++v) {
                    const float wx = origin + x * Cell, wz = origin + z * Cell;
                    m->texcoords[v * 2 + 0] = (float)x / (float)(N - 1);
                    m->texcoords[v * 2 + 1] = (float)z / (float)(N - 1);

                    // RGB: tono del pasto (verde con manchas secas). A: cuánto es tierra (la pista); el
                    // shader mezcla las texturas de tierra y pasto con esto. Calles: tono de la calle
                    // (asfalto oscuro o cemento claro) o de la tierra roja del morro; A = pavimento.
                    float noise = ValueNoise(wx * 0.15f, wz * 0.15f);
                    float fine = ValueNoise(wx * 1.3f + 5.0f, wz * 1.3f - 3.0f);
                    Color g = mixc(grass, grassDry, noise * 1.2f - 0.2f);
                    if (sand) {
                        // Arena: dorada, más clara u ocre en manchones grandes (~80 m) y apenas manchada de
                        // cerca; la huella de una pista, apisonada y más oscura.
                        const float band = ValueNoise(wx * 0.012f + 3.7f, wz * 0.012f - 8.1f);
                        // (Albedo natural de la arena: con el albedo de antes, 0.75 lineal, las dunas salían casi blancas.)
                        const Color dune = mixc({186, 158, 120, 255}, {166, 128, 88, 255}, band * 1.5f - 0.25f);
                        const float spot = 0.955f + 0.09f * noise;
                        g = mixc({(unsigned char)(dune.r * spot), (unsigned char)(dune.g * spot), (unsigned char)(dune.b * spot), 255},
                                 {132, 104, 74, 255}, trackMask[z * N + x] * 0.85f);
                    } else if (streets) {
                        const Color soil = grassOffRoad ? g : mixc({150, 88, 58, 255}, {126, 96, 70, 255}, noise * 1.3f - 0.15f);
                        const Color paved = mixc({186, 170, 150, 255}, {78, 76, 78, 255}, streetTone[z * N + x]);
                        g = mixc(soil, paved, trackMask[z * N + x] * 1.4f);
                    }
                    // Taludes (el borde del mapa, laderas): el ruido del color, pensado para el plano, sale en rayas
                    // verticales (el suelo se proyecta por x, z y se estira); con la pendiente se apaga hacia un tono
                    // medio del pasto y el brillo se emparda. No en arena ni en calles (ahí el color es de otra cosa).
                    const float slope = std::max(std::fabs(H(std::min(x + 1, N - 1), z) - H(std::max(x - 1, 0), z)),
                                                 std::fabs(H(x, std::min(z + 1, N - 1)) - H(x, std::max(z - 1, 0)))) / (2.0f * Cell);
                    const float steepT = mu::Smoothstep(0.35f, 0.9f, slope);
                    if (steepT > 0.0f && !sand && !streets) g = mixc(g, mixc(grass, grassDry, 0.3f), steepT * 0.85f);
                    float shade = mu::Lerp(0.92f + fine * 0.16f, 1.0f, steepT);
                    m->colors[v * 4 + 0] = (unsigned char)mu::Clamp(g.r * shade, 0.0f, 255.0f);
                    m->colors[v * 4 + 1] = (unsigned char)mu::Clamp(g.g * shade, 0.0f, 255.0f);
                    m->colors[v * 4 + 2] = (unsigned char)mu::Clamp(g.b * shade, 0.0f, 255.0f);
                    m->colors[v * 4 + 3] = (unsigned char)(255.0f * trackMask[z * N + x]);
                }
            }

            int t = 0;
            for (int z = 0; z < c.h - 1; ++z) {
                for (int x = 0; x < c.w - 1; ++x) {
                    unsigned short i00 = (unsigned short)(z * c.w + x), i10 = i00 + 1;
                    unsigned short i01 = (unsigned short)((z + 1) * c.w + x), i11 = i01 + 1;
                    // Misma diagonal que Jolt; orden antihorario visto desde arriba.
                    m->indices[t++] = i00; m->indices[t++] = i01; m->indices[t++] = i11;
                    m->indices[t++] = i00; m->indices[t++] = i11; m->indices[t++] = i10;
                }
            }
            UploadMesh(m, true);           // dinámica: los surcos actualizan posiciones y normales
            chunks.push_back(c);
        }
    }
}

void Terrain::Draw(Renderer& r, const Texture& marks, float centerX, float centerZ, float radius) const
{
    for (const auto& entry : soilTiles) {
        const Mesh* mesh=entry.second.mesh;
        if (!mesh) continue;
        if (radius > 0.0f) {
            const float x=origin+(entry.first.first+0.5f)*soilTileCells*Cell;
            const float z=origin+(entry.first.second+0.5f)*soilTileCells*Cell;
            if (std::hypot(x-centerX,z-centerZ)>radius+soilTileCells*Cell) continue;
        }
        r.DrawTerrain(*mesh,marks);
    }
    for (const Chunk& c : chunks) {
        if (c.mesh->triangleCount == 0) continue;
        const float x0 = origin + c.x0 * Cell, x1 = origin + (c.x0 + c.w - 1) * Cell;
        const float z0 = origin + c.z0 * Cell, z1 = origin + (c.z0 + c.h - 1) * Cell;
        if (radius > 0.0f) {                       // sólo los chunks que pueden dejar sombra en el mapa de la luz
            // ±1.5 m de altura: los surcos bajan el suelo 35 cm como mucho y los bordes lo suben unos cm.
            if (!r.InShadowPrism({x0, c.low - 1.5f, z0}, {x1, c.high + 1.5f, z1})) continue;
        } else if (!r.BoxVisible({x0, c.low - 2.0f, z0}, {x1, c.high + 2.0f, z1})) {
            continue;                              // pasada principal: los que no tocan el frustum, afuera (±2 m: surcos)
        }
        r.DrawTerrain(*c.mesh, marks);
    }
}

void Terrain::Unload()
{
    grassCache.reset();
    noGrass.clear();
    ClearFineSoil();
    for (Chunk& c : chunks) {
        UnloadMesh(*c.mesh);
        delete c.mesh;
    }
    chunks.clear();
}

// ------------------------------------------------------------------------ surcos (físico)
void Terrain::Dig(float x, float z, float depth, float radius, float maxDepth)
{
    const int x0 = std::max(0, (int)std::floor((x - radius - origin) / Cell));
    const int x1 = std::min(N - 1, (int)std::ceil((x + radius - origin) / Cell));
    const int z0 = std::max(0, (int)std::floor((z - radius - origin) / Cell));
    const int z1 = std::min(N - 1, (int)std::ceil((z + radius - origin) / Cell));
    bool changed = false;
    for (int iz = z0; iz <= z1; ++iz) {
        for (int ix = x0; ix <= x1; ++ix) {
            const float dx = origin + ix * Cell - x, dz = origin + iz * Cell - z;
            const float w = 1.0f - std::sqrt(dx * dx + dz * dz) / radius;
            if (w <= 0.0f) continue;
            const int i = iz * N + ix;
            const float target = std::max(original[i] - maxDepth, pendingHeights[i] - depth * w);
            if (target < pendingHeights[i]) {
                pendingHeights[i] = target;
                changed = true;
            }
        }
    }
    if (!changed) return;
    DirtyRegion(x0, z0, x1, z1);
}

void Terrain::DirtyRegion(int x0, int z0, int x1, int z1)
{
    deformed = true;
    if (!dirty) {
        dirtyX0 = x0; dirtyZ0 = z0; dirtyX1 = x1; dirtyZ1 = z1;
        dirty = true;
    } else {
        dirtyX0 = std::min(dirtyX0, x0); dirtyZ0 = std::min(dirtyZ0, z0);
        dirtyX1 = std::max(dirtyX1, x1); dirtyZ1 = std::max(dirtyZ1, z1);
    }
}

float Terrain::SoilAmount(float x, float z) const
{
    if (x < origin || z < origin || x > origin + Size() || z > origin + Size()) return 0.0f;
    // El pavimento y los taludes casi verticales no son barro excavable.
    const float ground = 1.0f - mu::Smoothstep(0.1f, 0.5f, PavedAmount(x, z));
    return ground * (sand ? 0.8f : mu::Lerp(0.45f, 1.0f, streets ? 1.0f : TrackMask(x, z)));
}

void Terrain::ResetDeformation(PhysicsWorld& world)
{
    if (!deformed && soilTiles.empty()) return;
    grassCache.reset();                    // las matas armadas con el suelo deformado tienen otra altura
    if (soilRoot) world.Bodies().SetShape(body,baseShape,false,JPH::EActivation::DontActivate);
    ClearFineSoil();
    FilterCoarseMeshes();
    ++deformationRevision;
    pendingHeights = original;
    DirtyRegion(0,0,N-1,N-1);
    CommitDeformation(world);
    if (soilCollision) {
        shape = baseShape;
        world.Bodies().SetShape(body,shape,false,JPH::EActivation::DontActivate);
        soilCollision = false;
    }
    deformed = false;
}

void Terrain::SoilStats(float& excavated, float& deposited, float& highestBank) const
{
    excavated=deposited=highestBank=0.0f;
    for (const auto& entry : soilHeights) {
        const float d=entry.second;
        excavated+=std::max(0.0f,-d)*SoilCell()*SoilCell();
        deposited+=std::max(0.0f,d)*SoilCell()*SoilCell();
        highestBank=std::max(highestBank,d);
    }
    for (size_t i=0; i<heights.size(); ++i) {
        const float d=heights[i]-original[i];
        excavated+=std::max(0.0f,-d)*Cell*Cell;
        deposited+=std::max(0.0f,d)*Cell*Cell;
        highestBank=std::max(highestBank,d);
    }
}

void Terrain::CommitDeformation(PhysicsWorld& world)
{
    if (!soilDirty.empty()) CommitFineSoil(world);
    if (!dirty || !shape) return;
    dirty = false;
    for (int z=dirtyZ0; z<=dirtyZ1; ++z)
        std::copy(pendingHeights.begin()+z*N+dirtyX0, pendingHeights.begin()+z*N+dirtyX1+1, heights.begin()+z*N+dirtyX0);
    ++deformationRevision;

    // Reserva altura para los bordes sólo al excavar por primera vez. El collider original
    // queda intacto: apagar el sistema y las pruebas antiguas conservan la misma física.
    if (!soilCollision) {
        JPH::HeightFieldShapeSettings settings(heights.data(),JPH::Vec3(origin,0,origin),JPH::Vec3(Cell,1,Cell),N);
        settings.mMinHeightValue=*std::min_element(original.begin(),original.end())-1.0f;
        settings.mMaxHeightValue=*std::max_element(original.begin(),original.end())+1.0f;
        settings.mBitsPerSample=16;
        auto result=settings.Create();
        if (result.HasError()) {
            heights=original; pendingHeights=original; deformed=false;
            TraceLog(LOG_WARNING,"suelo: no se pudo crear el collider deformable");
            return;
        }
        shape=static_cast<JPH::HeightFieldShape*>(const_cast<JPH::Shape*>(result.Get().GetPtr()));
        world.Bodies().SetShape(body,shape,false,JPH::EActivation::DontActivate);
        soilCollision=true;
    }
    // Jolt exige regiones alineadas al tamaño de bloque del heightfield.
    const int block = (int)shape->GetBlockSize();
    const int x0 = (dirtyX0 / block) * block, z0 = (dirtyZ0 / block) * block;
    const int x1 = std::min(N, ((dirtyX1 + block) / block) * block);
    const int z1 = std::min(N, ((dirtyZ1 + block) / block) * block);
    shape->SetHeights(x0, z0, x1 - x0, z1 - z0, &heights[z0 * N + x0], N, world.Temp());
    world.Bodies().NotifyShapeChanged(body, shape->GetCenterOfMass(), false, JPH::EActivation::DontActivate);

    // Mallas afectadas (las normales dependen de los vecinos: se expande una muestra).
    for (const Chunk& c : chunks) {
        if (c.x0 > x1 + 1 || c.x0 + c.w - 1 < x0 - 1 || c.z0 > z1 + 1 || c.z0 + c.h - 1 < z0 - 1) continue;
        FillChunkGeometry(c);
        UpdateMeshBuffer(*c.mesh, 0, c.mesh->vertices, c.mesh->vertexCount * 3 * (int)sizeof(float), 0);
        UpdateMeshBuffer(*c.mesh, 2, c.mesh->normals, c.mesh->vertexCount * 3 * (int)sizeof(float), 0);
    }
}

void Terrain::DeformationStats(int& samples, float& deepest) const
{
    samples = 0;
    deepest = 0.0f;
    for (const auto& entry : soilHeights) if (entry.second < -1e-4f) {
        ++samples;
        deepest=std::max(deepest,-entry.second);
    }
    for (size_t i = 0; i < heights.size(); ++i) {
        float d = original[i] - heights[i];
        if (d > 1e-4f) {
            ++samples;
            deepest = std::max(deepest, d);
        }
    }
}
