#pragma once
// Diagnóstico de superficies que se pisan (z-fighting) en las mallas armadas por código (autódromo,
// favela): busca pares de triángulos casi en el mismo plano que se superponen, mirando para el mismo
// lado (o para cualquiera si uno se dibuja de las dos caras), y los resume por colores. Se activa con la
// variable de entorno MOTOSIM_COPLANARES (ver docs/RENDER.md, "Buscar superficies que se pisan"); no
// cambia nada de lo que se dibuja.
#include "MeshBuilder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

struct CoplanarCheck {
    struct Tri {
        Vector3 p[3], n, lo, hi;
        float d;
        Color c;
        Vector2 uv;                       // del primer vértice: brillo y modo del shader (o la del atlas)
        int layer;
    };
    struct Layer {
        std::string name;
        bool twoSided, textured;
    };
    std::vector<Tri> tris;
    std::vector<Layer> layers;

    static bool Enabled() { return std::getenv("MOTOSIM_COPLANARES") != nullptr; }

    int AddLayer(const char* name, bool twoSided, bool textured)
    {
        layers.push_back({name, twoSided, textured});
        return (int)layers.size() - 1;
    }

    void Add(const MeshBuilder& b, int layer)
    {
        for (size_t i = 0; i + 2 < b.idx.size(); i += 3) {
            Tri t;
            for (int k = 0; k < 3; ++k) t.p[k] = b.P(b.idx[i + k]);
            const Vector3 cr = Vector3CrossProduct(Vector3Subtract(t.p[1], t.p[0]), Vector3Subtract(t.p[2], t.p[0]));
            const float len = Vector3Length(cr);
            if (len < 2e-5f) continue;
            t.n = Vector3Scale(cr, 1.0f / len);          // la cara de adelante (la que no se descarta)
            t.d = Vector3DotProduct(t.n, t.p[0]);
            t.lo = Vector3Min(Vector3Min(t.p[0], t.p[1]), t.p[2]);
            t.hi = Vector3Max(Vector3Max(t.p[0], t.p[1]), t.p[2]);
            const unsigned char* c = &b.col[4 * b.idx[i]];
            t.c = {c[0], c[1], c[2], c[3]};
            t.uv = {b.uv[2 * b.idx[i]], b.uv[2 * b.idx[i] + 1]};
            t.layer = layer;
            tris.push_back(t);
        }
    }

    // Área de la superposición de dos triángulos proyectados en el plano de a, y la separación (mínima y
    // máxima) entre los dos planos sobre esa superposición.
    static float Overlap(const Tri& a, const Tri& b, float& sepMin, float& sepMax, Vector3& center)
    {
        const Vector3 ref = std::fabs(a.n.y) < 0.9f ? Vector3{0.0f, 1.0f, 0.0f} : Vector3{1.0f, 0.0f, 0.0f};
        const Vector3 u = Vector3Normalize(Vector3CrossProduct(ref, a.n)), v = Vector3CrossProduct(a.n, u);
        auto proj = [&](Vector3 p) { return Vector2{Vector3DotProduct(p, u), Vector3DotProduct(p, v)}; };
        std::vector<Vector2> clip = {proj(b.p[0]), proj(b.p[1]), proj(b.p[2])};
        Vector2 ta[3] = {proj(a.p[0]), proj(a.p[1]), proj(a.p[2])};
        const float orient = (ta[1].x - ta[0].x) * (ta[2].y - ta[0].y) - (ta[1].y - ta[0].y) * (ta[2].x - ta[0].x);
        if (orient < 0.0f) std::swap(ta[1], ta[2]);
        for (int e = 0; e < 3 && !clip.empty(); ++e) {
            const Vector2 p0 = ta[e], p1 = ta[(e + 1) % 3];
            auto inside = [&](Vector2 q) { return (p1.x - p0.x) * (q.y - p0.y) - (p1.y - p0.y) * (q.x - p0.x) >= 0.0f; };
            std::vector<Vector2> out;
            for (size_t k = 0; k < clip.size(); ++k) {
                const Vector2 c0 = clip[k], c1 = clip[(k + 1) % clip.size()];
                const bool i0 = inside(c0), i1 = inside(c1);
                if (i0) out.push_back(c0);
                if (i0 != i1) {
                    const float d0 = (p1.x - p0.x) * (c0.y - p0.y) - (p1.y - p0.y) * (c0.x - p0.x);
                    const float d1 = (p1.x - p0.x) * (c1.y - p0.y) - (p1.y - p0.y) * (c1.x - p0.x);
                    const float t = d0 / (d0 - d1);
                    out.push_back({c0.x + (c1.x - c0.x) * t, c0.y + (c1.y - c0.y) * t});
                }
            }
            clip.swap(out);
        }
        if (clip.size() < 3) return 0.0f;
        float area = 0.0f;
        Vector2 mid = {0.0f, 0.0f};
        for (size_t k = 0; k < clip.size(); ++k) {
            const Vector2 c0 = clip[k], c1 = clip[(k + 1) % clip.size()];
            area += c0.x * c1.y - c1.x * c0.y;
            mid = Vector2Add(mid, c0);
        }
        area = std::fabs(area) * 0.5f;
        mid = Vector2Scale(mid, 1.0f / (float)clip.size());
        sepMin = 1e9f;
        sepMax = 0.0f;
        const float cosab = Vector3DotProduct(a.n, b.n);
        for (const Vector2& q : clip) {
            const Vector3 p = Vector3Add(Vector3Add(Vector3Scale(u, q.x), Vector3Scale(v, q.y)), Vector3Scale(a.n, a.d));
            const float s = std::fabs((Vector3DotProduct(b.n, p) - b.d) / std::max(std::fabs(cosab), 0.5f));
            sepMin = std::min(sepMin, s);
            sepMax = std::max(sepMax, s);
        }
        // Si los planos se cruzan adentro de la superposición, la separación mínima es cero.
        {
            float lo = 1e9f, hi = -1e9f;
            for (const Vector2& q : clip) {
                const Vector3 p = Vector3Add(Vector3Add(Vector3Scale(u, q.x), Vector3Scale(v, q.y)), Vector3Scale(a.n, a.d));
                const float s = Vector3DotProduct(b.n, p) - b.d;
                lo = std::min(lo, s);
                hi = std::max(hi, s);
            }
            if (lo < 0.0f && hi > 0.0f) sepMin = 0.0f;
        }
        center = Vector3Add(Vector3Add(Vector3Scale(u, mid.x), Vector3Scale(v, mid.y)), Vector3Scale(a.n, a.d));
        return area;
    }

    // Busca los pares que se pisan (separación mínima de menos de tol, superposición de más de minArea)
    // y escribe un resumen agrupado por capa y color, los de más área primero. Los pares del mismo
    // aspecto (misma capa sin textura, mismo color, brillo y modo) no se ven y sólo se cuentan.
    void Report(const char* title, float tol = 0.012f, float minArea = 4e-4f, float cosMax = 0.9994f) const
    {
        const float cell = 2.0f;
        std::unordered_map<uint64_t, std::vector<int>> grid;
        auto key = [](int x, int z) { return (uint64_t)(uint32_t)x << 32 | (uint64_t)(uint32_t)z; };
        for (int i = 0; i < (int)tris.size(); ++i) {
            const Tri& t = tris[i];
            const int x0 = (int)std::floor(t.lo.x / cell), x1 = (int)std::floor(t.hi.x / cell);
            const int z0 = (int)std::floor(t.lo.z / cell), z1 = (int)std::floor(t.hi.z / cell);
            if ((x1 - x0 + 1) * (z1 - z0 + 1) > 4000) continue;     // el fondo lejano
            for (int z = z0; z <= z1; ++z)
                for (int x = x0; x <= x1; ++x) grid[key(x, z)].push_back(i);
        }
        struct Group {
            int count = 0;
            float area = 0.0f, sepMin = 1e9f, sepMax = 0.0f;
            Vector3 at{};
            float atArea = 0.0f;
            int ta = -1, tb = -1;
        };
        std::map<std::tuple<int, uint32_t, int, uint32_t>, Group> groups;
        int hidden = 0;
        float hiddenArea = 0.0f;
        auto rgba = [](Color c) { return (uint32_t)c.r << 24 | (uint32_t)c.g << 16 | (uint32_t)c.b << 8 | c.a; };
        for (const auto& kv : grid) {
            const std::vector<int>& list = kv.second;
            const int cx = (int)(int32_t)(uint32_t)(kv.first >> 32), cz = (int)(int32_t)(uint32_t)(kv.first & 0xffffffffu);
            for (size_t ia = 0; ia < list.size(); ++ia)
                for (size_t ib = ia + 1; ib < list.size(); ++ib) {
                    const Tri& a = tris[list[ia]];
                    const Tri& b = tris[list[ib]];
                    // Cada par, una vez: en el bloque de la esquina de su superposición.
                    const float ox = std::max(a.lo.x, b.lo.x), oz = std::max(a.lo.z, b.lo.z);
                    if ((int)std::floor(ox / cell) != cx || (int)std::floor(oz / cell) != cz) continue;
                    if (a.lo.x > b.hi.x + tol || b.lo.x > a.hi.x + tol || a.lo.y > b.hi.y + tol || b.lo.y > a.hi.y + tol ||
                        a.lo.z > b.hi.z + tol || b.lo.z > a.hi.z + tol)
                        continue;
                    const float dot = Vector3DotProduct(a.n, b.n);
                    const bool two = layers[a.layer].twoSided || layers[b.layer].twoSided;
                    if (dot < cosMax && !(two && dot < -cosMax)) continue;
                    // Rápido: los vértices de b, todos del mismo lado del plano de a y lejos.
                    float smin = 1e9f, smax = -1e9f;
                    for (const Vector3& p : b.p) {
                        const float s = Vector3DotProduct(a.n, p) - a.d;
                        smin = std::min(smin, s);
                        smax = std::max(smax, s);
                    }
                    if (smin > tol || smax < -tol) continue;
                    float sepMin, sepMax;
                    Vector3 at;
                    const float area = Overlap(a, b, sepMin, sepMax, at);
                    if (area < minArea || sepMin > tol) continue;
                    const bool same = a.layer == b.layer && !layers[a.layer].textured && rgba(a.c) == rgba(b.c) &&
                                      std::fabs(a.uv.x - b.uv.x) < 0.01f && std::fabs(a.uv.y - b.uv.y) < 0.01f && dot > 0.0f;
                    if (same) {
                        ++hidden;
                        hiddenArea += area;
                        continue;
                    }
                    const bool swap = std::make_pair(a.layer, rgba(a.c)) > std::make_pair(b.layer, rgba(b.c));
                    const Tri& p = swap ? b : a;
                    const Tri& q = swap ? a : b;
                    Group& g = groups[{p.layer, rgba(p.c), q.layer, rgba(q.c)}];
                    ++g.count;
                    g.area += area;
                    g.sepMin = std::min(g.sepMin, sepMin);
                    g.sepMax = std::max(g.sepMax, sepMax);
                    if (area > g.atArea) {
                        g.atArea = area;
                        g.at = at;
                        g.ta = (int)(&p - tris.data());
                        g.tb = (int)(&q - tris.data());
                    }
                }
        }
        std::vector<std::pair<std::tuple<int, uint32_t, int, uint32_t>, Group>> sorted(groups.begin(), groups.end());
        std::sort(sorted.begin(), sorted.end(), [](const auto& x, const auto& y) { return x.second.area > y.second.area; });
        const char* env = std::getenv("MOTOSIM_COPLANARES");
        const bool detail = env && !std::strcmp(env, "detalle");      // con los dos triángulos del par más grande
        std::printf("coplanares %s: %zu triángulos, %zu grupos que se ven (%d pares iguales que no se ven, %.1f m2)\n", title, tris.size(),
                    sorted.size(), hidden, hiddenArea);
        auto col = [](uint32_t c) {
            char s[32];
            std::snprintf(s, sizeof(s), "%u,%u,%u", c >> 24, (c >> 16) & 255, (c >> 8) & 255);
            return std::string(s);
        };
        for (size_t k = 0; k < sorted.size() && k < 60; ++k) {
            const auto& [id, g] = sorted[k];
            std::printf("  %-9s %-12s | %-9s %-12s  %5d pares %8.3f m2  sep %.1f-%.1f mm  en (%.2f, %.2f, %.2f)\n", layers[std::get<0>(id)].name.c_str(),
                        col(std::get<1>(id)).c_str(), layers[std::get<2>(id)].name.c_str(), col(std::get<3>(id)).c_str(), g.count, g.area,
                        g.sepMin * 1000.0f, g.sepMax * 1000.0f, g.at.x, g.at.y, g.at.z);
            if (detail)
                for (int t : {g.ta, g.tb}) {
                    const Tri& r = tris[t];
                    std::printf("      n (%.3f %.3f %.3f)  (%.2f %.2f %.2f) (%.2f %.2f %.2f) (%.2f %.2f %.2f)\n", r.n.x, r.n.y, r.n.z, r.p[0].x, r.p[0].y, r.p[0].z,
                                r.p[1].x, r.p[1].y, r.p[1].z, r.p[2].x, r.p[2].y, r.p[2].z);
                }
        }
    }
};
