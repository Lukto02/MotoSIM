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

} // namespace

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

float Terrain::Ground(float x, float z) const
{
    float h = 0.0f;
    if (def.type == "favela") {
        h = FavelaHill(x, z);
    } else if (def.type == "heightmap") {
        h = def.imageBase + ImageHeight(x, z) * def.imageHeight;
    } else if (def.type == "hills") {
        h = (Fbm(x * def.scale + 3.1f, z * def.scale - 7.7f, 3) - 0.5f) * def.height;   // lomas grandes
        h += (ValueNoise(x * 0.09f + 17.0f, z * 0.09f - 9.0f) - 0.5f) * def.detail;     // irregularidad
    }
    // Terreno que sube en los bordes del mapa para contener al piloto.
    const float edge = Size() * 0.5f;
    const float d = std::max(std::fabs(x), std::fabs(z));
    const float t = mu::Smoothstep(edge - def.edgeHeight - 2.0f, edge - 1.0f, d);
    return h + t * t * def.edgeHeight;
}

void Terrain::Build(const Track& track, const MapDef& map, bool flat)
{
    def = map.terrain;
    terraces = map.terraces;
    streets = map.roadStyle == "street" && !flat;
    grassOffRoad = map.look.groundTextures == "circuit";
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

    for (int z = 0; z < N; ++z)
        for (int x = 0; x < N; ++x)
            heights[z * N + x] = flat ? 0.0f : Ground(origin + x * Cell, origin + z * Cell);
    if (!flat) {
        if (streets) StampStreets(track);
        else StampTrack(track);
    }
    original = heights;
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
    JPH::BodyCreationSettings bcs(result.Get(), JPH::RVec3::sZero(), JPH::Quat::sIdentity(), JPH::EMotionType::Static, Layers::STATIC);
    bcs.mFriction = 0.9f;
    body = world.Bodies().CreateAndAddBody(bcs, JPH::EActivation::DontActivate);
}

float Terrain::Height(float x, float z) const
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
    const float e = Cell;
    float hl = Height(x - e, z), hr = Height(x + e, z), hd = Height(x, z - e), hu = Height(x, z + e);
    return JPH::Vec3(hl - hr, 2.0f * e, hd - hu).Normalized();
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

void Terrain::GrassTufts(float cx, float cz, float radius, std::vector<Matrix>& out) const
{
    out.clear();
    constexpr float kCell = 0.55f;
    const Color grass = {92, 116, 58, 255}, grassDry = {132, 128, 76, 255};
    const float lo = origin + 1.0f, hi = origin + Size() - 1.0f;
    const int i0 = (int)std::floor((cx - radius) / kCell), i1 = (int)std::ceil((cx + radius) / kCell);
    const int j0 = (int)std::floor((cz - radius) / kCell), j1 = (int)std::ceil((cz + radius) / kCell);
    for (int j = j0; j <= j1; ++j) {
        for (int i = i0; i <= i1; ++i) {
            const float x = ((float)i + Hash(i, j)) * kCell, z = ((float)j + Hash(j + 71, i - 33)) * kCell;
            const float dist = std::sqrt((x - cx) * (x - cx) + (z - cz) * (z - cz));
            if (dist > radius || x < lo || x > hi || z < lo || z > hi) continue;
            const float patches = mu::Smoothstep(0.3f, 0.7f, ValueNoise(x * 0.18f + 3.0f, z * 0.18f - 7.0f));
            const float density = streets && !grassOffRoad ? (1.0f - Sample(streetMask, x, z)) * 0.35f * patches
                                         : (1.0f - TrackMask(x, z)) * (0.25f + 0.75f * patches);
            const float pick = Hash(i * 7 + 1, j * 13 + 5);
            if (pick > density) continue;
            const float s = (0.7f + 0.6f * Hash(i + 911, j - 177)) * (1.0f - mu::Smoothstep(radius * 0.65f, radius, dist));
            if (s < 0.05f) continue;
            Matrix m = MatrixMultiply(MatrixMultiply(MatrixScale(s, s * (0.75f + 0.6f * pick), s), MatrixRotateY(Hash(i - 5, j + 9) * 6.2832f)),
                                      MatrixTranslate(x, Height(x, z) - 0.02f, z));
            // Mismo tono que el color de vértice del terreno en ese lugar (va en la fila de abajo).
            const float t = mu::Clamp(ValueNoise(x * 0.15f, z * 0.15f) * 1.2f - 0.2f, 0.0f, 1.0f);
            const float shade = 0.92f + ValueNoise(x * 1.3f + 5.0f, z * 1.3f - 3.0f) * 0.16f;
            m.m3 = mu::Lerp(grass.r, grassDry.r, t) * shade / 255.0f;
            m.m7 = mu::Lerp(grass.g, grassDry.g, t) * shade / 255.0f;
            m.m11 = mu::Lerp(grass.b, grassDry.b, t) * shade / 255.0f;
            out.push_back(m);
        }
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
    const Color grass = {92, 116, 58, 255};
    const Color grassDry = {132, 128, 76, 255};

    auto mixc = [](Color a, Color b, float t) {
        t = mu::Clamp(t, 0.0f, 1.0f);
        return Color{(unsigned char)mu::Lerp(a.r, b.r, t), (unsigned char)mu::Lerp(a.g, b.g, t),
                     (unsigned char)mu::Lerp(a.b, b.b, t), 255};
    };

    for (int cz = 0; cz < N - 1; cz += kChunk) {
        for (int cx = 0; cx < N - 1; cx += kChunk) {
            const int x1 = std::min(cx + kChunk, N - 1), z1 = std::min(cz + kChunk, N - 1);
            Chunk c{new Mesh{}, cx, cz, x1 - cx + 1, z1 - cz + 1};
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
                    if (streets) {
                        const Color soil = grassOffRoad ? g : mixc({150, 88, 58, 255}, {126, 96, 70, 255}, noise * 1.3f - 0.15f);
                        const Color paved = mixc({186, 170, 150, 255}, {78, 76, 78, 255}, streetTone[z * N + x]);
                        g = mixc(soil, paved, trackMask[z * N + x] * 1.4f);
                    }
                    float shade = 0.92f + fine * 0.16f;
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
    for (const Chunk& c : chunks) {
        if (radius > 0.0f) {                       // sólo los chunks que tocan el círculo
            const float x0 = origin + c.x0 * Cell, x1 = origin + (c.x0 + c.w - 1) * Cell;
            const float z0 = origin + c.z0 * Cell, z1 = origin + (c.z0 + c.h - 1) * Cell;
            const float dx = std::max({x0 - centerX, 0.0f, centerX - x1}), dz = std::max({z0 - centerZ, 0.0f, centerZ - z1});
            if (dx * dx + dz * dz > radius * radius) continue;
        }
        r.DrawTerrain(*c.mesh, marks);
    }
}

void Terrain::Unload()
{
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
            const float target = std::max(original[i] - maxDepth, heights[i] - depth * w);
            if (target < heights[i]) {
                heights[i] = target;
                changed = true;
            }
        }
    }
    if (!changed) return;
    if (!dirty) {
        dirtyX0 = x0; dirtyZ0 = z0; dirtyX1 = x1; dirtyZ1 = z1;
        dirty = true;
    } else {
        dirtyX0 = std::min(dirtyX0, x0); dirtyZ0 = std::min(dirtyZ0, z0);
        dirtyX1 = std::max(dirtyX1, x1); dirtyZ1 = std::max(dirtyZ1, z1);
    }
}

void Terrain::CommitDeformation(PhysicsWorld& world)
{
    if (!dirty || !shape) return;
    dirty = false;

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
    for (size_t i = 0; i < heights.size(); ++i) {
        float d = original[i] - heights[i];
        if (d > 1e-4f) {
            ++samples;
            deepest = std::max(deepest, d);
        }
    }
}
