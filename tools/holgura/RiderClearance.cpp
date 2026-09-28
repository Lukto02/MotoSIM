// Pruebas: cuánto se mete el piloto en la moto (ver RiderClearance.h). No es parte del juego: lo compila
// tools/holgura/armar.py en una copia aparte (ver docs/PILOTO.md, "Medir la holgura"). Variables de entorno:
//   MOTOSIM_HOLGURA=1                 durante una prueba mide cada 0.05 s; al salir imprime lo peor;
//   MOTOSIM_HOLGURA_BARRIDO=a.csv     barrido de poses de la moto elegida (--bike), CSV y tablas, y sale;
//   MOTOSIM_HOLGURA_FILTRO=a|b        sólo los grupos o poses que contienen alguno de esos textos;
//   MOTOSIM_HOLGURA_VOLCAR=prefijo    vuelca cada pose medida (para tools/holgura/vista.py);
//   MOTOSIM_HOLGURA_DEBUG=1           primitivas abiertas, perfil de la forma del chasis, puntos raros.
#include "RiderClearance.h"

#include "BikeStyleDef.h"
#include "MathUtil.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <tuple>
#include <vector>

using JPH::Mat44;
using JPH::Quat;
using JPH::Vec3;

namespace {

enum Region { Casco, Torso, Cadera, BrazoI, BrazoD, AntebrazoI, AntebrazoD, ManoI, ManoD, MusloI, MusloD, CanillaI, CanillaD, BotaI, BotaD,
              AgarreI, AgarreD, EstriberaI, EstriberaD, kRegions };
const char* kRegionName[kRegions] = {"casco", "torso", "cadera", "brazoI", "brazoD", "antebrI", "antebrD", "manoI", "manoD", "musloI",
                                     "musloD", "canillaI", "canillaD", "botaI", "botaD", "agarreI", "agarreD", "estribI", "estribD"};
const BikeMesh::Id kParts[] = {BikeMesh::Body, BikeMesh::Swingarm, BikeMesh::ForkUpper, BikeMesh::ForkLower, BikeMesh::Cockpit,
                               BikeMesh::FrontFender, BikeMesh::TireFront, BikeMesh::TireRear};
const char* PartName(int id)
{
    switch (id) {
    case BikeMesh::Body: return "chasis";
    case BikeMesh::Swingarm: return "basculante";
    case BikeMesh::ForkUpper: return "tijas";
    case BikeMesh::ForkLower: return "botellas";
    case BikeMesh::Cockpit: return "manubrio";
    case BikeMesh::FrontFender: return "guardabarros";
    case BikeMesh::TireFront: return "rueda_del";
    case BikeMesh::TireRear: return "rueda_tras";
    default: return "?";
    }
}

// Tres rayos un poco torcidos (no pasan justo por aristas de cajas): adentro si al menos dos dan impar.
const Vector3 kRays[3] = {Vector3Normalize({0.0123f, 1.0f, 0.0371f}), Vector3Normalize({1.0f, 0.0231f, 0.0437f}),
                          Vector3Normalize({0.0311f, 0.0173f, 1.0f})};

bool RayHits(Vector3 o, Vector3 d, Vector3 a, Vector3 b, Vector3 c)
{
    const Vector3 e1 = Vector3Subtract(b, a), e2 = Vector3Subtract(c, a);
    const Vector3 p = Vector3CrossProduct(d, e2);
    const float det = Vector3DotProduct(e1, p);
    if (std::fabs(det) < 1e-14f) return false;
    const float inv = 1.0f / det;
    const Vector3 s = Vector3Subtract(o, a);
    const float u = Vector3DotProduct(s, p) * inv;
    if (u < 0.0f || u > 1.0f) return false;
    const Vector3 q = Vector3CrossProduct(s, e1);
    const float v = Vector3DotProduct(d, q) * inv;
    if (v < 0.0f || u + v > 1.0f) return false;
    return Vector3DotProduct(e2, q) * inv > 0.0f;
}

float PointTriDist(Vector3 p, Vector3 a, Vector3 b, Vector3 c)
{
    const Vector3 ab = Vector3Subtract(b, a), ac = Vector3Subtract(c, a), ap = Vector3Subtract(p, a);
    const float d1 = Vector3DotProduct(ab, ap), d2 = Vector3DotProduct(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return Vector3Distance(p, a);
    const Vector3 bp = Vector3Subtract(p, b);
    const float d3 = Vector3DotProduct(ab, bp), d4 = Vector3DotProduct(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return Vector3Distance(p, b);
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) return Vector3Distance(p, Vector3Add(a, Vector3Scale(ab, d1 / (d1 - d3))));
    const Vector3 cp = Vector3Subtract(p, c);
    const float d5 = Vector3DotProduct(ab, cp), d6 = Vector3DotProduct(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return Vector3Distance(p, c);
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) return Vector3Distance(p, Vector3Add(a, Vector3Scale(ac, d2 / (d2 - d6))));
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
        return Vector3Distance(p, Vector3Add(b, Vector3Scale(Vector3Subtract(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6)))));
    const float den = 1.0f / (va + vb + vc);
    return Vector3Distance(p, Vector3Add(a, Vector3Add(Vector3Scale(ab, vb * den), Vector3Scale(ac, vc * den))));
}

// Adentro de una superficie cerrada (triángulos de a 3 en tri, índices a pos): dos de tres rayos impares.
template <class GetTri>
bool Inside(Vector3 p, int count, GetTri tri)
{
    int odd = 0;
    for (int r = 0; r < 3; ++r) {
        int hits = 0;
        for (int t = 0; t < count; ++t) {
            Vector3 a, b, c;
            tri(t, a, b, c);
            hits += RayHits(p, kRays[r], a, b, c) ? 1 : 0;
        }
        odd += hits & 1;
        if (odd >= 2 || (r == 1 && odd == 0)) break;
    }
    return odd >= 2;
}

// Soldar vértices repetidos (costuras, caras de cajas) y separar en piezas conexas.
struct Pieces {
    std::vector<int> weld;                   // vértice -> representante
    std::vector<int> pieceOf;                // triángulo -> pieza
    std::vector<std::vector<int>> tris;      // pieza -> triángulos
    std::vector<bool> closed;                // cada arista soldada en exactamente dos triángulos
};
Pieces Split(const std::vector<Vector3>& pos, const std::vector<int>& idx, float q)
{
    Pieces P;
    const int n = (int)pos.size();
    std::map<std::tuple<long long, long long, long long>, int> seen;
    P.weld.resize(n);
    for (int v = 0; v < n; ++v) {
        const auto key = std::make_tuple(std::llround(pos[v].x / q), std::llround(pos[v].y / q), std::llround(pos[v].z / q));
        auto it = seen.find(key);
        if (it == seen.end()) it = seen.emplace(key, v).first;
        P.weld[v] = it->second;
    }
    std::vector<int> uf(n);
    for (int v = 0; v < n; ++v) uf[v] = v;
    auto find = [&](int v) {
        while (uf[v] != v) v = uf[v] = uf[uf[v]];
        return v;
    };
    const int nt = (int)idx.size() / 3;
    for (int t = 0; t < nt; ++t) {
        const int a = find(P.weld[idx[3 * t]]);
        for (int k = 1; k < 3; ++k) {
            const int b = find(P.weld[idx[3 * t + k]]);
            if (a != b) uf[b] = a;
        }
    }
    std::map<int, int> pieceId;
    P.pieceOf.resize(nt);
    for (int t = 0; t < nt; ++t) {
        const int root = find(P.weld[idx[3 * t]]);
        auto it = pieceId.find(root);
        if (it == pieceId.end()) {
            it = pieceId.emplace(root, (int)P.tris.size()).first;
            P.tris.emplace_back();
        }
        P.pieceOf[t] = it->second;
        P.tris[it->second].push_back(t);
    }
    P.closed.assign(P.tris.size(), true);
    for (size_t k = 0; k < P.tris.size(); ++k) {
        std::map<std::pair<int, int>, int> edges;
        for (int t : P.tris[k])
            for (int e = 0; e < 3; ++e) {
                int a = P.weld[idx[3 * t + e]], b = P.weld[idx[3 * t + (e + 1) % 3]];
                if (a == b) continue;
                if (a > b) std::swap(a, b);
                ++edges[{a, b}];
            }
        for (const auto& e : edges)
            if (e.second != 2) {
                P.closed[k] = false;
                break;
            }
    }
    return P;
}

// Bordes de una pieza abierta (aristas en un solo triángulo) encadenados en lazos, para taparlos con un
// abanico al centro (los tubos de la moto no tienen tapas; el cuerpo del piloto tiene agujeros en el
// cuello, los puños y los tobillos, que tapan el casco, los guantes y las botas). ok = todos cierran.
std::vector<std::vector<int>> BoundaryLoops(const Pieces& P, const std::vector<int>& idx, int k, bool& ok)
{
    std::map<std::pair<int, int>, int> edges;
    for (int t : P.tris[k])
        for (int e = 0; e < 3; ++e) {
            const int a = P.weld[idx[3 * t + e]], b = P.weld[idx[3 * t + (e + 1) % 3]];
            if (a != b) ++edges[{std::min(a, b), std::max(a, b)}];
        }
    std::map<int, int> next;
    ok = true;
    for (int t : P.tris[k])
        for (int e = 0; e < 3; ++e) {
            const int a = P.weld[idx[3 * t + e]], b = P.weld[idx[3 * t + (e + 1) % 3]];
            if (a == b) continue;
            const int c = edges[{std::min(a, b), std::max(a, b)}];
            if (c == 1) {
                if (next.count(a)) ok = false;
                next[a] = b;
            }                                // más de dos: triángulos degenerados en los polos (se ignoran)
        }
    std::vector<std::vector<int>> loops;
    while (!next.empty()) {
        std::vector<int> loop;
        int v = next.begin()->first;
        while (next.count(v)) {
            loop.push_back(v);
            const int n = next[v];
            next.erase(v);
            v = n;
        }
        if (v != loop.front()) ok = false;
        loops.push_back(loop);
    }
    return loops;
}

// Una primitiva de una pieza de la moto (soldada, con los bordes tapados): sus triángulos y puntos de su superficie.
struct Solid {
    int part;
    int index;                               // número dentro de la pieza (para identificarla)
    std::vector<int> tri;                    // índices a los vértices de la pieza (de a 3), con las tapas
    std::vector<Vector3> samples;            // superficie original, espacio de la pieza
    bool closed;
    bool peg[2] = {false, false};            // estribera izquierda / derecha
    Vector3 lo{}, hi{};                      // caja en espacio de la moto (este cuadro)
};
struct PartGeo {
    int part;
    std::vector<Vector3> local, world;       // vértices de la pieza (más los centros de las tapas) en su espacio / de la moto
    std::vector<Solid> solids;
};

struct Hit {
    float depth = 0.0f;
    int part = -1, solid = -1;
};
struct Result {
    Hit h[kRegions];
};

struct Geometry {
    int style = -1;
    std::vector<PartGeo> parts;
    int openBike = 0, cappedBike = 0, closedBike = 0;
    // Piloto
    bool riderBuilt = false;
    std::vector<int> riderIdx;               // triángulos (de a 3)
    Pieces rider;
    std::vector<int> region;                 // por vértice
    std::vector<int> uniqueVerts;            // un vértice por posición soldada
    std::vector<std::vector<int>> loops;     // bordes de las piezas abiertas del piloto (se tapan)
    std::vector<int> loopPiece;
    bool debug = std::getenv("MOTOSIM_HOLGURA_DEBUG") != nullptr;
};
Geometry G;

// Pieza de la moto -> espacio de la moto (como Bike::Draw con W = identidad).
Matrix PartMatrix(const Bike& b, int id)
{
    const Wheel& fw = b.wheels[Bike::FRONT];
    const Wheel& rw = b.wheels[Bike::REAR];
    const Vec3 frontAxle = fw.AxleLocal(), rearAxle = rw.AxleLocal();
    const Vec3 forkUp = -fw.axisLocal;
    const Vec3 steerHead = fw.mountLocal + forkUp * 0.53f;
    const Mat44 steerM = Mat44::sTranslation(steerHead) * Mat44::sRotation(Quat::sRotation(forkUp, -b.steerAngle)) * Mat44::sTranslation(-steerHead);
    const Mat44 forkTilt = Mat44::sRotation(Quat::sFromTo(Vec3::sAxisY(), forkUp));
    auto wheelM = [](const Mat44& frame, Vec3 axle, float angle, float scale) {
        return frame * Mat44::sTranslation(axle) * Mat44::sRotation(Quat::sRotation(Vec3::sAxisX(), angle)) * Mat44::sScale(scale);
    };
    switch (id) {
    case BikeMesh::TireRear: return ToRl(wheelM(Mat44::sIdentity(), rearAxle, rw.angle, rw.radius / BikeMesh::kRearRadius));
    case BikeMesh::TireFront: return ToRl(wheelM(steerM, frontAxle, fw.angle, fw.radius / BikeMesh::kFrontRadius));
    case BikeMesh::Swingarm: {
        const Vec3 pivot = ToJph(BikeMesh::kSwingarmPivot);
        const Vec3 arm = rearAxle - pivot;
        return ToRl(Mat44::sTranslation(pivot) * Mat44::sRotation(Quat::sRotation(Vec3::sAxisX(), std::atan2(arm.GetY(), -arm.GetZ()))) *
                    Mat44::sScale(Vec3(1.0f, 1.0f, arm.Length() / BikeMesh::kSwingarmLength)));
    }
    case BikeMesh::ForkUpper: return ToRl(steerM * Mat44::sTranslation(steerHead) * forkTilt);
    case BikeMesh::ForkLower: return ToRl(steerM * Mat44::sTranslation(frontAxle) * forkTilt);
    case BikeMesh::Cockpit: return ToRl(steerM * Mat44::sTranslation(steerHead));
    case BikeMesh::FrontFender: return ToRl(steerM * Mat44::sTranslation(steerHead - forkUp * BikeMesh::kFenderDrop));
    default: return MatrixIdentity();
    }
}

int RegionOfBone(const char* name)
{
    const bool left = std::strncmp(name, "Left", 4) == 0, right = std::strncmp(name, "Right", 5) == 0;
    const int s = right ? 1 : 0;
    if (left || right) {
        if (std::strstr(name, "Hand")) return ManoI + s;
        if (std::strstr(name, "ForeArm")) return AntebrazoI + s;
        if (std::strstr(name, "Arm") || std::strstr(name, "Shoulder")) return BrazoI + s;
        if (std::strstr(name, "UpLeg")) return MusloI + s;
        if (std::strstr(name, "Leg")) return CanillaI + s;
        if (std::strstr(name, "Foot") || std::strstr(name, "Toe")) return BotaI + s;
    }
    if (std::strstr(name, "Spine")) return Torso;
    if (std::strstr(name, "Hips")) return Cadera;
    return Casco;                            // Head, neck, head_end, headfront
}

void BuildBike(const Bike& b, Renderer& r)
{
    const int style = b.Params() ? b.Params()->visualStyle : 0;
    if (G.style == style) return;
    G.style = style;
    G.parts.clear();
    G.openBike = G.cappedBike = G.closedBike = 0;
    const BikeStyleDef& st = GetBikeStyle(style);
    r.bikeStyle = style;
    const Vec3 forkUp = -b.wheels[Bike::FRONT].axisLocal;
    r.UpdateBikeBody(ToRl(b.wheels[Bike::FRONT].mountLocal + forkUp * 0.53f), ToRl(forkUp));
    for (BikeMesh::Id id : kParts) {
        const Mesh& m = r.BikePart(id, 0);
        PartGeo pg;
        pg.part = id;
        for (int v = 0; v < m.vertexCount; ++v) pg.local.push_back({m.vertices[3 * v], m.vertices[3 * v + 1], m.vertices[3 * v + 2]});
        std::vector<int> idx;
        if (m.indices)
            for (int t = 0; t < m.triangleCount * 3; ++t) idx.push_back(m.indices[t]);
        else
            for (int t = 0; t < m.vertexCount; ++t) idx.push_back(t);
        const Pieces P = Split(pg.local, idx, 1e-4f);
        for (size_t k = 0; k < P.tris.size(); ++k) {
            Solid s;
            s.part = id;
            s.index = (int)k;
            s.closed = P.closed[k];
            Vector3 c = {0, 0, 0};
            for (int t : P.tris[k]) {
                Vector3 v[3];
                for (int e = 0; e < 3; ++e) {
                    s.tri.push_back(idx[3 * t + e]);
                    v[e] = pg.local[idx[3 * t + e]];
                    c = Vector3Add(c, v[e]);
                }
                const float L = std::max({Vector3Distance(v[0], v[1]), Vector3Distance(v[1], v[2]), Vector3Distance(v[2], v[0])});
                const int n = std::max(1, (int)std::ceil(L / 0.012f));
                for (int i = 0; i <= n; ++i)
                    for (int j = 0; i + j <= n; ++j)
                        s.samples.push_back(Vector3Add(v[0], Vector3Add(Vector3Scale(Vector3Subtract(v[1], v[0]), (float)i / n),
                                                                     Vector3Scale(Vector3Subtract(v[2], v[0]), (float)j / n))));
            }
            c = Vector3Scale(c, 1.0f / (float)(3 * P.tris[k].size()));
            if (s.closed) {
                ++G.closedBike;
            } else {
                bool ok = false;
                const auto loops = BoundaryLoops(P, idx, (int)k, ok);
                // Sólo se tapan bocas chicas (puntas de tubos): una lámina (el carenado) tapada encerraría un
                // volumen falso, con los semimanillares y las manos adentro.
                for (const auto& loop : loops) {
                    Vector3 lo = {1e9f, 1e9f, 1e9f}, hi = {-1e9f, -1e9f, -1e9f};
                    for (int v : loop) {
                        lo = Vector3Min(lo, pg.local[v]);
                        hi = Vector3Max(hi, pg.local[v]);
                    }
                    if (Vector3Distance(lo, hi) > 0.12f) ok = false;
                }
                for (const auto& loop : loops) {
                    if (!ok) break;
                    Vector3 lc = {0, 0, 0};
                    for (int v : loop) lc = Vector3Add(lc, pg.local[v]);
                    const int ci = (int)pg.local.size();
                    pg.local.push_back(Vector3Scale(lc, 1.0f / (float)loop.size()));
                    for (size_t i = 0; i < loop.size(); ++i) {
                        s.tri.push_back(ci);
                        s.tri.push_back(loop[i]);
                        s.tri.push_back(loop[(i + 1) % loop.size()]);
                    }
                }
                s.closed = ok;
                (ok ? G.cappedBike : G.openBike)++;
                if (G.debug) {
                    Vector3 lo = {1e9f, 1e9f, 1e9f}, hi = {-1e9f, -1e9f, -1e9f};
                    for (int i : s.tri) {
                        lo = Vector3Min(lo, pg.local[i]);
                        hi = Vector3Max(hi, pg.local[i]);
                    }
                    std::printf("holgura DEBUG: %s %s#%d, %d triángulos, %d bordes, (%.2f %.2f %.2f)..(%.2f %.2f %.2f)\n", ok ? "tapada" : "ABIERTA",
                                PartName(id), (int)k, (int)P.tris[k].size(), (int)loops.size(), lo.x, lo.y, lo.z, hi.x, hi.y, hi.z);
                }
            }
            if (id == BikeMesh::Body)
                for (int i = 0; i < 2; ++i) {
                    const float side = i == 0 ? 1.0f : -1.0f;
                    const Vector3 peg = {st.peg.GetX() * side, st.peg.GetY(), st.peg.GetZ()};
                    if (Vector3Distance(c, peg) < 0.08f) s.peg[i] = true;
                }
            pg.solids.push_back(std::move(s));
        }
        G.parts.push_back(std::move(pg));
    }
    std::printf("holgura: estilo %s, %d primitivas cerradas, %d tapadas y %d abiertas (las abiertas sólo cuentan en B)\n", st.name, G.closedBike,
                G.cappedBike, G.openBike);
}

void BuildRider(RiderModel& rm)
{
    const RiderClearance::View model = RiderClearance::Of(rm);
    if (G.riderBuilt) return;
    G.riderBuilt = true;
    const Mesh& m = model.model.meshes[0];
    G.riderIdx.clear();
    if (m.indices)
        for (int t = 0; t < m.triangleCount * 3; ++t) G.riderIdx.push_back(m.indices[t]);
    else
        for (int t = 0; t < m.vertexCount; ++t) G.riderIdx.push_back(t);
    G.rider = Split(model.bindVerts, G.riderIdx, 1e-5f);
    G.region.resize(m.vertexCount);
    for (int v = 0; v < m.vertexCount; ++v) {
        int best = 0;
        float bw = -1.0f;
        for (int k = 0; k < 4; ++k)
            if (model.skinWeight[v * 4 + k] > bw) {
                bw = model.skinWeight[v * 4 + k];
                best = model.skinBone[v * 4 + k];
            }
        // Los dedos virtuales (modelo 2) son de la mano.
        G.region[v] = best < model.realBones ? RegionOfBone(model.model.bones[best].name) : (model.bindPos[best].x > 0 ? ManoI : ManoD);
    }
    G.uniqueVerts.clear();
    for (int v = 0; v < m.vertexCount; ++v)
        if (G.rider.weld[v] == v) G.uniqueVerts.push_back(v);
    std::printf("holgura: piloto con %d vértices (%d soldados), %d triángulos, %d piezas\n", m.vertexCount, (int)G.uniqueVerts.size(),
                (int)G.riderIdx.size() / 3, (int)G.rider.tris.size());
    for (size_t k = 0; k < G.rider.tris.size(); ++k) {
        std::map<int, int> count;
        for (int t : G.rider.tris[k]) ++count[G.region[G.riderIdx[3 * t]]];
        int best = 0, bc = -1;
        for (auto& c : count)
            if (c.second > bc) {
                bc = c.second;
                best = c.first;
            }
        std::string holes;
        if (!G.rider.closed[k]) {
            bool ok = false;
            for (const auto& loop : BoundaryLoops(G.rider, G.riderIdx, (int)k, ok)) {
                G.loops.push_back(loop);
                G.loopPiece.push_back((int)k);
                holes += std::string(" ") + kRegionName[G.region[loop[0]]] + "(" + std::to_string(loop.size()) + ")";
            }
            if (!ok) holes += " NO CIERRAN";
        }
        std::printf("  pieza %d: %d triángulos, sobre todo %s%s%s\n", (int)k, (int)G.rider.tris[k].size(), kRegionName[best],
                    holes.empty() ? ", cerrada" : ", agujeros tapados:", holes.c_str());
    }
}

// Malla del piloto con la pose actual (como RiderModel::Skin, sin subirla).
void SkinRider(RiderModel& rm, std::vector<Vector3>& out)
{
    const RiderClearance::View model = RiderClearance::Of(rm);
    const int n = (int)model.pos.size();
    std::vector<Matrix> xf(n);
    for (int b = 0; b < n; ++b)
        xf[b] = MatrixMultiply(MatrixMultiply(MatrixTranslate(-model.bindPos[b].x, -model.bindPos[b].y, -model.bindPos[b].z), QuaternionToMatrix(model.delta[b])),
                               MatrixTranslate(model.pos[b].x, model.pos[b].y, model.pos[b].z));
    const int nv = (int)model.bindVerts.size();
    out.resize(nv);
    for (int v = 0; v < nv; ++v) {
        Vector3 p = {0, 0, 0};
        for (int k = 0; k < 4; ++k) {
            const float w = model.skinWeight[v * 4 + k];
            if (w <= 0.0f) continue;
            p = Vector3Add(p, Vector3Scale(Vector3Transform(model.bindVerts[v], xf[model.skinBone[v * 4 + k]]), w));
        }
        out[v] = p;
    }
}

void Record(Result& R, int region, float depth, int part, int solid)
{
    if (depth > R.h[region].depth) R.h[region] = {depth, part, solid};
}

int Contact(int reg, const Solid& s)
{
    if ((reg == ManoI || reg == ManoD) && s.part == BikeMesh::Cockpit) return reg - ManoI + AgarreI;
    if ((reg == BotaI || reg == BotaD) && (s.peg[0] || s.peg[1])) return reg - BotaI + EstriberaI;
    return reg;
}

// Volcado para mirar (tools/holgura/vista.py): piloto, moto y lo que se mete, en espacio de la moto.
struct Dump {
    bool on = false;
    std::vector<float> depthA;               // por vértice del piloto
    std::vector<float> pointsB;              // x, y, z, profundidad
};
Dump gDump;
double gTime[4] = {0, 0, 0, 0};   // pruebas de velocidad: pose, A, B, total
double gPoseMax = 0.0;
double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

Result Measure(const Bike& b, RiderModel& model, Renderer& r)
{
    BuildBike(b, r);
    BuildRider(model);
    Result R;
    std::vector<Vector3> rv;
    SkinRider(model, rv);
    if (gDump.on) {
        gDump.depthA.assign(rv.size(), 0.0f);
        gDump.pointsB.clear();
    }

    // Piezas de la moto en el espacio de la moto.
    std::vector<Matrix> partM(G.parts.size());
    for (size_t pi = 0; pi < G.parts.size(); ++pi) {
        PartGeo& pg = G.parts[pi];
        partM[pi] = PartMatrix(b, pg.part);
        pg.world.resize(pg.local.size());
        for (size_t v = 0; v < pg.local.size(); ++v) pg.world[v] = Vector3Transform(pg.local[v], partM[pi]);
        for (Solid& s : pg.solids) {
            s.lo = {1e9f, 1e9f, 1e9f};
            s.hi = {-1e9f, -1e9f, -1e9f};
            for (int i : s.tri) {
                s.lo = Vector3Min(s.lo, pg.world[i]);
                s.hi = Vector3Max(s.hi, pg.world[i]);
            }
        }
    }

    // Triángulos del piloto este cuadro (con los agujeros tapados), por pieza.
    std::vector<Vector3> rt;
    std::vector<int> rtPiece, rtRegion;
    const int nt0 = (int)G.riderIdx.size() / 3;
    for (int t = 0; t < nt0; ++t) {
        for (int e = 0; e < 3; ++e) rt.push_back(rv[G.riderIdx[3 * t + e]]);
        rtPiece.push_back(G.rider.pieceOf[t]);
        rtRegion.push_back(G.region[G.riderIdx[3 * t]]);
    }
    for (size_t l = 0; l < G.loops.size(); ++l) {
        const std::vector<int>& loop = G.loops[l];
        Vector3 c = {0, 0, 0};
        for (int v : loop) c = Vector3Add(c, rv[v]);
        c = Vector3Scale(c, 1.0f / (float)loop.size());
        for (size_t i = 0; i < loop.size(); ++i) {
            rt.push_back(c);
            rt.push_back(rv[loop[i]]);
            rt.push_back(rv[loop[(i + 1) % loop.size()]]);
            rtPiece.push_back(G.loopPiece[l]);
            rtRegion.push_back(G.region[loop[i]]);
        }
    }
    const int nt = (int)rtPiece.size();
    const int np = (int)G.rider.tris.size();
    std::vector<std::vector<int>> pieceTris(np);
    std::vector<Vector3> plo(np, {1e9f, 1e9f, 1e9f}), phi(np, {-1e9f, -1e9f, -1e9f});
    for (int t = 0; t < nt; ++t) {
        pieceTris[rtPiece[t]].push_back(t);
        for (int e = 0; e < 3; ++e) {
            plo[rtPiece[t]] = Vector3Min(plo[rtPiece[t]], rt[3 * t + e]);
            phi[rtPiece[t]] = Vector3Max(phi[rtPiece[t]], rt[3 * t + e]);
        }
    }
    Vector3 rlo = {1e9f, 1e9f, 1e9f}, rhi = {-1e9f, -1e9f, -1e9f};
    for (int k = 0; k < np; ++k) {
        rlo = Vector3Min(rlo, plo[k]);
        rhi = Vector3Max(rhi, phi[k]);
    }
    auto inBox = [](Vector3 p, Vector3 lo, Vector3 hi) { return p.x >= lo.x && p.y >= lo.y && p.z >= lo.z && p.x <= hi.x && p.y <= hi.y && p.z <= hi.z; };

    const double t0 = Now();
    // A) vértices del piloto adentro de primitivas cerradas (o tapadas) de la moto.
    for (int v : G.uniqueVerts) {
        const Vector3 p = rv[v];
        const int reg = G.region[v];
        for (const PartGeo& pg : G.parts)
            for (const Solid& s : pg.solids) {
                if (!s.closed || !inBox(p, s.lo, s.hi)) continue;
                const int ntri = (int)s.tri.size() / 3;
                auto tri = [&](int t, Vector3& a, Vector3& bb, Vector3& c) {
                    a = pg.world[s.tri[3 * t]];
                    bb = pg.world[s.tri[3 * t + 1]];
                    c = pg.world[s.tri[3 * t + 2]];
                };
                if (!Inside(p, ntri, tri)) continue;
                float d = 1e9f;
                for (int t = 0; t < ntri; ++t) {
                    Vector3 a, bb, c;
                    tri(t, a, bb, c);
                    d = std::min(d, PointTriDist(p, a, bb, c));
                }
                Record(R, Contact(reg, s), d, s.part, s.index);
                if (gDump.on) gDump.depthA[v] = std::max(gDump.depthA[v], d);
            }
    }

    const double t1 = Now();
    gTime[1] += t1 - t0;
    // B) puntos de la superficie de la moto adentro de las piezas del piloto. Paridad con los tres rayos,
    // cada uno con los triángulos proyectados a lo largo de él en una grilla de 2 cm; la profundidad, con
    // los triángulos en una grilla 3D de 3 cm (se busca en cáscaras hasta que no puede haber uno más cerca).
    struct RayGrid {
        Vector3 d;
        int ax, u, v;                        // eje dominante del rayo y los otros dos
        Vector2 lo{1e9f, 1e9f}, hi{-1e9f, -1e9f};
        int w = 0, h = 0;
        std::vector<std::vector<int>> cells;
        Vector2 Proj(Vector3 p) const
        {
            const float c[3] = {p.x, p.y, p.z}, dd[3] = {d.x, d.y, d.z};
            const float k = c[ax] / dd[ax];
            return {c[u] - dd[u] * k, c[v] - dd[v] * k};
        }
    };
    const float cell = 0.02f;
    RayGrid rg[3];
    for (int ray = 0; ray < 3; ++ray) {
        RayGrid& g = rg[ray];
        g.d = kRays[ray];
        g.ax = ray == 0 ? 1 : (ray == 1 ? 0 : 2);
        g.u = g.ax == 0 ? 1 : 0;
        g.v = g.ax == 2 ? 1 : 2;
        for (const Vector3& p : rt) {
            const Vector2 q = g.Proj(p);
            g.lo = Vector2Min(g.lo, q);
            g.hi = Vector2Max(g.hi, q);
        }
        g.w = (int)((g.hi.x - g.lo.x) / cell) + 1;
        g.h = (int)((g.hi.y - g.lo.y) / cell) + 1;
        g.cells.assign((size_t)g.w * g.h, {});
        for (int t = 0; t < nt; ++t) {
            Vector2 lo = {1e9f, 1e9f}, hi = {-1e9f, -1e9f};
            for (int e = 0; e < 3; ++e) {
                const Vector2 q = g.Proj(rt[3 * t + e]);
                lo = Vector2Min(lo, q);
                hi = Vector2Max(hi, q);
            }
            for (int gx = (int)((lo.x - g.lo.x) / cell); gx <= (int)((hi.x - g.lo.x) / cell); ++gx)
                for (int gy = (int)((lo.y - g.lo.y) / cell); gy <= (int)((hi.y - g.lo.y) / cell); ++gy) g.cells[(size_t)gy * g.w + gx].push_back(t);
        }
    }
    auto rayCell = [&](const RayGrid& g, Vector3 p) -> const std::vector<int>* {
        const Vector2 q = g.Proj(p);
        const int gx = (int)((q.x - g.lo.x) / cell), gy = (int)((q.y - g.lo.y) / cell);
        if (q.x < g.lo.x || q.y < g.lo.y || gx >= g.w || gy >= g.h) return nullptr;
        return &g.cells[(size_t)gy * g.w + gx];
    };
    // Grilla 3D para la distancia.
    const float c3 = 0.03f;
    const int n3x = (int)((rhi.x - rlo.x) / c3) + 1, n3y = (int)((rhi.y - rlo.y) / c3) + 1, n3z = (int)((rhi.z - rlo.z) / c3) + 1;
    std::vector<std::vector<int>> g3((size_t)n3x * n3y * n3z);
    auto c3i = [&](float v, float lo, int n) { return std::clamp((int)((v - lo) / c3), 0, n - 1); };
    for (int t = 0; t < nt; ++t) {
        Vector3 lo = rt[3 * t], hi = rt[3 * t];
        for (int e = 1; e < 3; ++e) {
            lo = Vector3Min(lo, rt[3 * t + e]);
            hi = Vector3Max(hi, rt[3 * t + e]);
        }
        for (int x = c3i(lo.x, rlo.x, n3x); x <= c3i(hi.x, rlo.x, n3x); ++x)
            for (int y = c3i(lo.y, rlo.y, n3y); y <= c3i(hi.y, rlo.y, n3y); ++y)
                for (int z = c3i(lo.z, rlo.z, n3z); z <= c3i(hi.z, rlo.z, n3z); ++z) g3[((size_t)z * n3y + y) * n3x + x].push_back(t);
    }
    auto nearestTri = [&](Vector3 p, int piece, float& best) {
        const int px = c3i(p.x, rlo.x, n3x), py = c3i(p.y, rlo.y, n3y), pz = c3i(p.z, rlo.z, n3z);
        best = 1e9f;
        int bt = -1;
        const int maxR = std::max({n3x, n3y, n3z});
        for (int rr = 0; rr <= maxR; ++rr) {
            for (int z = pz - rr; z <= pz + rr; ++z)
                for (int y = py - rr; y <= py + rr; ++y)
                    for (int x = px - rr; x <= px + rr; ++x) {
                        if (std::max({std::abs(x - px), std::abs(y - py), std::abs(z - pz)}) != rr) continue;
                        if (x < 0 || y < 0 || z < 0 || x >= n3x || y >= n3y || z >= n3z) continue;
                        for (int t : g3[((size_t)z * n3y + y) * n3x + x]) {
                            if (rtPiece[t] != piece) continue;
                            const float dt = PointTriDist(p, rt[3 * t], rt[3 * t + 1], rt[3 * t + 2]);
                            if (dt < best) {
                                best = dt;
                                bt = t;
                            }
                        }
                    }
            if (bt >= 0 && best <= rr * c3) break;
        }
        return bt;
    };
    std::vector<int> count(np);
    for (size_t pi = 0; pi < G.parts.size(); ++pi) {
        const PartGeo& pg = G.parts[pi];
        for (const Solid& s : pg.solids) {
            if (s.hi.x < rlo.x || s.hi.y < rlo.y || s.hi.z < rlo.z || s.lo.x > rhi.x || s.lo.y > rhi.y || s.lo.z > rhi.z) continue;
            for (const Vector3& sl : s.samples) {
                const Vector3 p = Vector3Transform(sl, partM[pi]);
                if (!inBox(p, rlo, rhi)) continue;
                const std::vector<int>* c0 = rayCell(rg[0], p);
                if (!c0) continue;
                std::fill(count.begin(), count.end(), 0);
                bool any = false;
                for (int t : *c0)
                    if (RayHits(p, rg[0].d, rt[3 * t], rt[3 * t + 1], rt[3 * t + 2])) {
                        ++count[rtPiece[t]];
                        any = true;
                    }
                if (!any) continue;
                for (int k = 0; k < np; ++k) {
                    if (!(count[k] & 1) || !inBox(p, plo[k], phi[k])) continue;
                    int odd = 1;
                    for (int ray = 1; ray < 3 && odd < 2; ++ray) {
                        const std::vector<int>* cc = rayCell(rg[ray], p);
                        if (!cc) continue;
                        int hits = 0;
                        for (int t : *cc)
                            if (rtPiece[t] == k) hits += RayHits(p, rg[ray].d, rt[3 * t], rt[3 * t + 1], rt[3 * t + 2]) ? 1 : 0;
                        odd += hits & 1;
                    }
                    if (odd < 2) continue;
                    float d = 1e9f;
                    const int nearest = nearestTri(p, k, d);
                    if (nearest < 0) continue;
                    if (d > 0.10f && G.debug)
                        std::printf("holgura DEBUG B: punto (%.3f %.3f %.3f) de %s#%d adentro de la pieza %d (%.3f %.3f %.3f)..(%.3f %.3f %.3f) a %.3f\n", p.x,
                                    p.y, p.z, PartName(s.part), s.index, k, plo[k].x, plo[k].y, plo[k].z, phi[k].x, phi[k].y, phi[k].z, d);
                    Record(R, Contact(rtRegion[nearest], s), d, s.part, s.index);
                    if (gDump.on) gDump.pointsB.insert(gDump.pointsB.end(), {p.x, p.y, p.z, d});
                }
            }
        }
    }
    gTime[2] += Now() - t1;
    if (gDump.on) {
        // Formato: int32 nv, nt, nbt, nb; float32 piloto[nv*3]; int32 tris[nt*3]; int32 región[nv]; float32 profA[nv];
        // float32 moto[nbt*9]; int32 pieza[nbt]; float32 B[nb*4].
        static int n = 0;
        char path[512];
        std::snprintf(path, sizeof path, "%s_%03d.bin", std::getenv("MOTOSIM_HOLGURA_VOLCAR"), n++);
        if (FILE* f = std::fopen(path, "wb")) {
            std::vector<float> bt;
            std::vector<int> bp;
            for (const PartGeo& pg : G.parts)
                for (const Solid& sd : pg.solids) {
                    const size_t nOrig = sd.tri.size();
                    for (size_t t = 0; t < nOrig; t += 3) {
                        for (int e = 0; e < 3; ++e) bt.insert(bt.end(), {pg.world[sd.tri[t + e]].x, pg.world[sd.tri[t + e]].y, pg.world[sd.tri[t + e]].z});
                        bp.push_back(pg.part);
                    }
                }
            const int hdr[4] = {(int)rv.size(), (int)G.riderIdx.size() / 3, (int)bp.size(), (int)gDump.pointsB.size() / 4};
            std::fwrite(hdr, sizeof(int), 4, f);
            std::fwrite(rv.data(), sizeof(float), rv.size() * 3, f);
            std::fwrite(G.riderIdx.data(), sizeof(int), G.riderIdx.size(), f);
            std::fwrite(G.region.data(), sizeof(int), G.region.size(), f);
            std::fwrite(gDump.depthA.data(), sizeof(float), gDump.depthA.size(), f);
            std::fwrite(bt.data(), sizeof(float), bt.size(), f);
            std::fwrite(bp.data(), sizeof(int), bp.size(), f);
            std::fwrite(gDump.pointsB.data(), sizeof(float), gDump.pointsB.size(), f);
            std::fclose(f);
            std::printf("holgura: volcado %s\n", path);
        }
    }
    return R;
}

std::string Where(const Hit& h)
{
    if (h.part < 0) return "-";
    char buf[64];
    std::snprintf(buf, sizeof buf, "%s#%d", PartName(h.part), h.solid);
    return buf;
}

const Solid* FindSolid(int part, int index)
{
    for (const PartGeo& pg : G.parts)
        if (pg.part == part && index >= 0 && index < (int)pg.solids.size()) return &pg.solids[index];
    return nullptr;
}

// ------------------------------------------------------------------ durante una prueba
struct FrameState {
    float next = 0.0f;
    Hit worst[kRegions];
    float when[kRegions] = {};
    bool registered = false;
} F;

void PrintFrameSummary()
{
    std::printf("holgura: lo peor de la prueba (cm, cuándo, dónde)\n");
    for (int k = 0; k < kRegions; ++k)
        if (F.worst[k].part >= 0) std::printf("  %-9s %5.1f  t=%5.2f  %s\n", kRegionName[k], 100.0f * F.worst[k].depth, F.when[k], Where(F.worst[k]).c_str());
    std::fflush(stdout);
}

} // namespace

void RiderClearance::Frame(const Bike& bike, RiderModel& model, Renderer& r, float t)
{
    if (!model.Loaded() || t < F.next) return;
    F.next = t + 0.05f;
    if (!F.registered) {
        F.registered = true;
        std::atexit(PrintFrameSummary);
    }
    const int prevStyle = r.bikeStyle;
    const Result R = Measure(bike, model, r);
    r.bikeStyle = prevStyle;
    std::string line;
    for (int k = 0; k < kRegions; ++k) {
        if (R.h[k].depth > F.worst[k].depth) {
            F.worst[k] = R.h[k];
            F.when[k] = t;
        }
        if (R.h[k].depth > 0.01f && k < AgarreI) {
            char buf[96];
            std::snprintf(buf, sizeof buf, " %s=%.1f@%s", kRegionName[k], 100.0f * R.h[k].depth, Where(R.h[k]).c_str());
            line += buf;
        }
    }
    if (!line.empty()) std::printf("holgura t=%.2f lean=%.2f side=%.2f leg=%.2f tilt=%.0f steer=%.0f%s\n", t, bike.riderLean, bike.riderSide, bike.legOut,
                                   mu::Deg(bike.bodyTilt), mu::Deg(bike.steerAngle), line.c_str());
}

// ------------------------------------------------------------------ barrido de poses
void RiderClearance::Sweep(const Bike& bike, RiderModel& model, Renderer& r, const char* csvPath)
{
    if (!model.Loaded()) {
        std::printf("holgura: sin el modelo del piloto\n");
        return;
    }
    const BikeParams* P = bike.Params();
    const int style = P ? P->visualStyle : 0;
    const BikeStyleDef& st = GetBikeStyle(style);
    const float maxSteer = mu::Rad(P ? P->maxSteerDeg : 42.0f);
    const float maxTilt = mu::Rad(P ? P->airBodyTiltDeg : 35.0f);
    {   // la forma de la moto para el ajuste del piloto (en el juego la arma Draw)
        r.bikeStyle = style;
        const Vec3 forkUp = -bike.wheels[Bike::FRONT].axisLocal;
        r.UpdateBikeBody(ToRl(bike.wheels[Bike::FRONT].mountLocal + forkUp * 0.53f), ToRl(forkUp));
        model.BuildShape(r);
        if (G.debug) {                       // perfil de la forma: techo en x = 0 y ancho de cada lado a varias alturas
            const RiderModel::BikeShape& sh = model.shape;
            std::printf("forma: z    techo(x=0) techo(x=.1) | ancho izq/der a y = -0.3 -0.1 0.0 0.1 0.2\n");
            for (float z = -0.8f; z <= 0.7f; z += 0.05f) {
                float dl, dv;
                auto topAt = [&](float x) {
                    const int ix = (int)((x - sh.x0) / 0.01f), iz = (int)((z - sh.z0) / 0.01f);
                    return ix >= 0 && iz >= 0 && ix < sh.nx && iz < sh.nz ? sh.top[(size_t)iz * sh.nx + ix] : -9.0f;
                };
                std::printf("  %5.2f  %6.2f %6.2f |", z, topAt(0.0f), topAt(0.1f));
                for (float y : {-0.3f, -0.1f, 0.0f, 0.1f, 0.2f}) {
                    const float wl = sh.Inside({0.0001f, y, z}, dl, dv) ? dl : 0.0f, wr = sh.Inside({-0.0001f, y, z}, dl, dv) ? dl : 0.0f;
                    std::printf(" %.2f/%.2f", wl, wr);
                }
                std::printf("\n");
            }
        }
    }
    FILE* csv = std::fopen(csvPath, "w");
    if (csv) {
        std::fprintf(csv, "estilo,grupo,pose,mano_lejos,alcance,sube,centro,abreI,abreD,tobI,tobD,pAsiento,pPiernaI,pPiernaD,pBotaI,pBotaD,pManubrio");
        for (int k = 0; k < kRegions; ++k) std::fprintf(csv, ",%s,%s_donde", kRegionName[k], kRegionName[k]);
        std::fprintf(csv, "\n");
    }
    struct Worst {
        Hit h;
        std::string pose;
    };
    std::map<std::string, std::vector<Worst>> groups;
    std::vector<std::string> order;
    int poses = 0;

    gDump.on = std::getenv("MOTOSIM_HOLGURA_VOLCAR") != nullptr;
    const char* filter = std::getenv("MOTOSIM_HOLGURA_FILTRO");   // sólo los grupos o poses que contengan esto
    auto run = [&](const char* group, Bike b, const std::string& label) {
        if (filter) {                        // alternativas separadas por '|'
            const std::string full = std::string(group) + " " + label, f = filter;
            bool match = false;
            for (size_t a = 0; a <= f.size() && !match;) {
                const size_t e = std::min(f.find('|', a), f.size());
                if (full.find(f.substr(a, e - a)) != std::string::npos) match = true;
                a = e + 1;
            }
            if (!match) return;
        }
        RiderClearance::Of(model).throttleS = b.gripThrottle;
        const double tp = Now();
        model.PoseOnBike(b.RiderPoseLocal());
        const double dp = Now() - tp;
        gTime[0] += dp;
        gPoseMax = std::max(gPoseMax, dp);
        const double ts = Now();
        const Result R = Measure(b, model, r);
        gTime[3] += Now() - ts;
        ++poses;
        auto it = groups.find(group);
        if (it == groups.end()) {
            it = groups.emplace(group, std::vector<Worst>(kRegions)).first;
            order.push_back(group);
        }
        for (int k = 0; k < kRegions; ++k)
            if (R.h[k].depth > it->second[k].h.depth) it->second[k] = {R.h[k], label};
        if (csv) {
            const RiderModel::Fit& f = model.lastFit;
            std::fprintf(csv, "%s,%s,%s,%.2f,%.3f,%.3f,%.2f,%.2f,%.2f,%.3f,%.3f", st.name, group, label.c_str(), 100.0f * RiderClearance::HandGap(model, b.RiderPoseLocal()), model.lastReach, f.lift, f.center, f.open[0], f.open[1],
                         f.ankleOut[0], f.ankleOut[1]);
            std::fprintf(csv, ",%.2f,%.2f,%.2f,%.2f,%.2f,%.2f", 100.0f * model.SeatDepth(RiderModel::ProbeDrape), 100.0f * model.SideDepth(RiderModel::ProbeLeg),
                         100.0f * model.SideDepth(RiderModel::ProbeLeg + 1), 100.0f * model.SideDepth(RiderModel::ProbeFoot),
                         100.0f * model.SideDepth(RiderModel::ProbeFoot + 1), 100.0f * model.ChestDepth(b.RiderPoseLocal()));
            for (int k = 0; k < kRegions; ++k) std::fprintf(csv, ",%.2f,%s", 100.0f * R.h[k].depth, Where(R.h[k]).c_str());
            std::fprintf(csv, "\n");
        }
    };
    auto base = [&]() {
        Bike b = bike;
        b.riderLean = b.riderSide = b.legOut = b.bodyTilt = b.steerAngle = b.gripThrottle = 0.0f;
        b.roll = b.pitch = 0.0f;
        b.supported = 1.0f;
        b.forwardSpeed = 8.0f;
        for (Wheel& w : b.wheels) w.extension = 0.7f * w.travel;   // con el peso encima
        return b;
    };
    char label[160];
    const float leans[5] = {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f};
    const float sides[3] = {-1.0f, 0.0f, 1.0f};

    // Suelo: cuerpo adelante/atrás, de costado, manubrio y gas.
    for (float lean : leans)
        for (float side : sides)
            for (float steer : {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f})
                for (float thr : {0.0f, 1.0f}) {
                    Bike b = base();
                    b.riderLean = lean;
                    b.riderSide = side;
                    b.steerAngle = steer * maxSteer;
                    b.gripThrottle = thr;
                    std::snprintf(label, sizeof label, "L%+.1f S%+.0f st%+.1f g%.0f", lean, side, steer, thr);
                    run("suelo", b, label);
                }
    // Pata afuera (despacio), con el cuerpo y el manubrio, y la suspensión con el peso o a tope.
    for (float leg : {-1.0f, -0.5f, 0.5f, 1.0f})
        for (float lean : {-1.0f, 0.0f, 1.0f})
            for (float side : sides)
                for (float steer : {-1.0f, 0.0f, 1.0f})
                    for (int comp = 0; comp < 2; ++comp) {
                        Bike b = base();
                        b.forwardSpeed = 5.0f;
                        b.legOut = leg;
                        b.riderLean = lean;
                        b.riderSide = side;
                        b.steerAngle = steer * maxSteer;
                        if (comp)
                            for (Wheel& w : b.wheels) w.extension = 0.0f;
                        std::snprintf(label, sizeof label, "pata%+.1f L%+.0f S%+.0f st%+.0f%s", leg, lean, side, steer, comp ? " tope" : "");
                        run("pata afuera", b, label);
                    }
    // En el aire: whip (cuerpo corrido), adelante/atrás, de costado; la moto derecha o parada (70°).
    for (float pitchDeg : {0.0f, 70.0f})
        for (float tilt : {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f})
            for (float lean : leans)
                for (float side : sides)
                    for (float steer : {-0.5f, 0.0f, 0.5f}) {
                        Bike b = base();
                        b.supported = 0.0f;
                        b.pitch = mu::Rad(pitchDeg);
                        b.bodyTilt = tilt * maxTilt;
                        b.riderLean = lean;
                        b.riderSide = side;
                        b.steerAngle = steer * maxSteer;
                        for (Wheel& w : b.wheels) w.extension = w.travel;
                        std::snprintf(label, sizeof label, "aire p%.0f tilt%+.1f L%+.1f S%+.0f st%+.1f", pitchDeg, tilt, lean, side, steer);
                        run("aire", b, label);
                    }
    // Sobre la cola (wheelie pasado, apoyada): de pie en los pedales.
    for (float lean : {-1.0f, 0.0f, 1.0f})
        for (float side : sides)
            for (float steer : {-0.5f, 0.0f, 0.5f}) {
                Bike b = base();
                b.pitch = mu::Rad(80.0f);
                b.riderLean = lean;
                b.riderSide = side;
                b.steerAngle = steer * maxSteer;
                std::snprintf(label, sizeof label, "cola L%+.0f S%+.0f st%+.1f", lean, side, steer);
                run("cola", b, label);
            }
    // Aterrizaje: la suspensión a tope.
    for (float lean : {-1.0f, 0.0f, 1.0f})
        for (float side : sides)
            for (float steer : {-1.0f, 0.0f, 1.0f}) {
                Bike b = base();
                for (Wheel& w : b.wheels) w.extension = 0.0f;
                b.riderLean = lean;
                b.riderSide = side;
                b.steerAngle = steer * maxSteer;
                std::snprintf(label, sizeof label, "tope L%+.0f S%+.0f st%+.0f", lean, side, steer);
                run("aterrizaje", b, label);
            }
    // Rápido (carreras: agachado y colgado en la curva).
    if (st.tuck > 0.0f || st.hangOff > 0.0f)
        for (float rollDeg : {-60.0f, -40.0f, -20.0f, 0.0f, 20.0f, 40.0f, 60.0f})
            for (float lean : {-1.0f, 0.0f, 1.0f})
                for (float side : sides)
                    for (float steer : {-0.2f, 0.0f, 0.2f}) {
                        Bike b = base();
                        b.forwardSpeed = rollDeg == 0.0f ? 30.0f : 10.0f;   // colgado: la mezcla es a 6-12 m/s; agachado, a más de 25
                        b.roll = mu::Rad(rollDeg);
                        b.riderLean = lean;
                        b.riderSide = side;
                        b.steerAngle = steer * maxSteer;
                        std::snprintf(label, sizeof label, "rapido roll%+.0f L%+.0f S%+.0f st%+.1f", rollDeg, lean, side, steer);
                        run("rapido", b, label);
                    }
    if (csv) std::fclose(csv);

    std::printf("holgura: %s, %d poses (cm: lo más adentro de cada zona, la pose y la pieza); %.0f ms por pose (A %.0f, B %.0f); PoseOnBike %.3f ms (máx %.3f)\n", st.name, poses,
                1000.0 * gTime[3] / std::max(poses, 1), 1000.0 * gTime[1] / std::max(poses, 1), 1000.0 * gTime[2] / std::max(poses, 1), 1000.0 * gTime[0] / std::max(poses, 1), 1000.0 * gPoseMax);
    for (const std::string& g : order) {
        std::printf("== %s\n", g.c_str());
        for (int k = 0; k < kRegions; ++k) {
            const Worst& w = groups[g][k];
            if (w.h.part < 0) continue;
            const Solid* s = FindSolid(w.h.part, w.h.solid);
            std::printf("  %-9s %5.1f  %-28s %-16s", kRegionName[k], 100.0f * w.h.depth, w.pose.c_str(), Where(w.h).c_str());
            if (s) std::printf(" caja (%.2f %.2f %.2f)..(%.2f %.2f %.2f)", s->lo.x, s->lo.y, s->lo.z, s->hi.x, s->hi.y, s->hi.z);
            std::printf("\n");
        }
    }
    std::fflush(stdout);
}
