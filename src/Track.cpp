#include "Track.h"

#include "MathUtil.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

struct P2 { float x, z; };

P2 CatmullRom(P2 p0, P2 p1, P2 p2, P2 p3, float u)
{
    // Catmull-Rom centrípeta (alpha 0.5): sin cúspides ni lazos.
    auto knot = [](P2 a, P2 b) {
        float d = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.z - a.z) * (b.z - a.z));
        return std::max(std::sqrt(d), 1e-4f);
    };
    float t0 = 0.0f, t1 = t0 + knot(p0, p1), t2 = t1 + knot(p1, p2), t3 = t2 + knot(p2, p3);
    float t = mu::Lerp(t1, t2, u);
    auto mix = [](P2 a, P2 b, float ta, float tb, float tt) {
        float wa = (tb - tt) / (tb - ta), wb = (tt - ta) / (tb - ta);
        return P2{a.x * wa + b.x * wb, a.z * wa + b.z * wb};
    };
    P2 a1 = mix(p0, p1, t0, t1, t), a2 = mix(p1, p2, t1, t2, t), a3 = mix(p2, p3, t2, t3, t);
    P2 b1 = mix(a1, a2, t0, t2, t), b2 = mix(a2, a3, t1, t3, t);
    return mix(b1, b2, t1, t2, t);
}

void BoxBlurWrapped(std::vector<float>& v, int radius, int passes)
{
    const int n = (int)v.size();
    std::vector<float> tmp(v.size());
    for (int p = 0; p < passes; ++p) {
        for (int i = 0; i < n; ++i) {
            float sum = 0.0f;
            for (int k = -radius; k <= radius; ++k) sum += v[(i + k + n) % n];
            tmp[i] = sum / (float)(2 * radius + 1);
        }
        v.swap(tmp);
    }
}

// ------------------------------------------------------------------ formas de los obstáculos
float Face(float u, float len, float h, float power) { return h * std::pow(mu::Clamp(u / len, 0.0f, 1.0f), power); }
float Down(float u, float len, float h) { return h * (1.0f - mu::Smoothstep(0.0f, 1.0f, u / len)); }
float Up(float u, float len, float h) { return h * mu::Smoothstep(0.0f, 1.0f, u / len); }
float Waves(float u, float wavelength, float h) { return h * 0.5f * (1.0f - std::cos(2.0f * mu::kPi * u / wavelength)); }

float FeatureHeight(FeatureKind kind, float u)
{
    switch (kind) {
    case FeatureKind::Rollers:                       // 3 lomas suaves
        return Waves(u, 8.0f, 0.45f);
    case FeatureKind::Whoops:                        // 10 whoops
        return Waves(u, 4.0f, 0.32f);
    case FeatureKind::Tabletop: {                    // cara 6.5 m, mesa 9 m, bajada 9 m
        const float a = 6.5f, h = 1.7f, top = 9.0f, b = 9.0f;
        if (u < a) return Face(u, a, h, 1.7f);
        if (u < a + top) return h;
        return Down(u - a - top, b, h);
    }
    case FeatureKind::Double: {                      // doble grande: 2.3 m, hueco de 10 m
        const float a = 7.5f, h1 = 2.3f, back = 2.0f, gap = 10.0f, up = 3.0f, h2 = 2.3f, top = 1.5f, b = 11.0f;
        if (u < a) return Face(u, a, h1, 1.8f);
        u -= a;
        if (u < back) return Down(u, back, h1);
        u -= back;
        if (u < gap) return 0.0f;
        u -= gap;
        if (u < up) return Up(u, up, h2);
        u -= up;
        if (u < top) return h2;
        return Down(u - top, b, h2);
    }
    case FeatureKind::StepUp: {                      // escalón: sube 1.4 m y baja largo
        const float a = 4.5f, h = 1.4f, top = 8.0f, b = 12.0f;
        if (u < a) return Face(u, a, h, 1.5f);
        if (u < a + top) return h;
        return Down(u - a - top, b, h);
    }
    case FeatureKind::Kicker: {                      // saltito
        const float a = 5.0f, h = 1.2f, b = 6.0f;
        if (u < a) return Face(u, a, h, 1.6f);
        return Down(u - a, b, h);
    }
    case FeatureKind::QuebraMola:                    // lomo de burro: 13 cm en 1.8 m
        return Waves(u, 1.8f, 0.13f);
    case FeatureKind::Escadaria:                     // los escalones son cajas (Favela.cpp)
        return 0.0f;
    case FeatureKind::Rampa: {                       // rampa de tierra con escombro: cara corta y empinada
        const float a = 3.5f, h = 1.0f, b = 3.0f;
        if (u < a) return Face(u, a, h, 1.5f);
        return Down(u - a, b, h);
    }
    default: break;
    }
    return 0.0f;
}

float FeatureLength(FeatureKind kind)
{
    switch (kind) {
    case FeatureKind::Rollers: return 3 * 8.0f;
    case FeatureKind::Whoops: return 10 * 4.0f;
    case FeatureKind::Tabletop: return 6.5f + 9.0f + 9.0f;
    case FeatureKind::Double: return 7.5f + 2.0f + 10.0f + 3.0f + 1.5f + 11.0f;
    case FeatureKind::StepUp: return 4.5f + 8.0f + 12.0f;
    case FeatureKind::Kicker: return 5.0f + 6.0f;
    case FeatureKind::QuebraMola: return 1.8f;
    case FeatureKind::Escadaria: return 11.5f;
    case FeatureKind::Rampa: return 3.5f + 3.0f;
    default: break;
    }
    return 0.0f;
}

} // namespace

void Track::Build(const MapDef& def)
{
    halfWidth = def.halfWidth;
    const int controlCount = (int)def.track.size();

    // Superficie de cada punto de control: la que dice o la del anterior.
    std::vector<Surface> surf(controlCount, Surface::Track);
    for (int i = 0; i < controlCount; ++i)
        surf[i] = def.track[i].surface != Surface::Keep ? def.track[i].surface : (i > 0 ? surf[i - 1] : Surface::Track);

    // 1) Spline densa (y de qué tramo sale cada muestra)
    std::vector<P2> dense;
    std::vector<int> denseSeg;
    for (int i = 0; i < controlCount; ++i) {
        auto cp = [&](int k) {
            const MapPoint& p = def.track[(k + controlCount) % controlCount];
            return P2{p.x, p.z};                     // ya con la escala del mapa (Maps.cpp)
        };
        for (int j = 0; j < 100; ++j) {
            dense.push_back(CatmullRom(cp(i - 1), cp(i), cp(i + 1), cp(i + 2), (float)j / 100.0f));
            denseSeg.push_back(i);
        }
    }
    dense.push_back(dense.front());
    denseSeg.push_back(0);

    // 2) Re-muestreo por longitud de arco
    std::vector<float> cum(dense.size(), 0.0f);
    for (size_t i = 1; i < dense.size(); ++i) {
        float dx = dense[i].x - dense[i - 1].x, dz = dense[i].z - dense[i - 1].z;
        cum[i] = cum[i - 1] + std::sqrt(dx * dx + dz * dz);
    }
    const float total = cum.back();
    const int count = (int)std::floor(total / spacing);
    length = (float)count * spacing;
    points.assign(count, TrackPoint{});
    std::vector<Surface> pointSurf(count);
    size_t seg = 1;
    for (int i = 0; i < count; ++i) {
        float s = (float)i * spacing * (total / length);
        while (seg < dense.size() - 1 && cum[seg] < s) ++seg;
        float t = (s - cum[seg - 1]) / std::max(cum[seg] - cum[seg - 1], 1e-6f);
        points[i].x = mu::Lerp(dense[seg - 1].x, dense[seg].x, t);
        points[i].z = mu::Lerp(dense[seg - 1].z, dense[seg].z, t);
        points[i].s = (float)i * spacing;
        pointSurf[i] = surf[denseSeg[seg - 1]];
    }

    // 3) Tangentes y curvatura (con signo: + = derecha)
    for (int i = 0; i < count; ++i) {
        const TrackPoint& a = points[(i - 1 + count) % count];
        const TrackPoint& b = points[(i + 1) % count];
        float dx = b.x - a.x, dz = b.z - a.z, len = std::sqrt(dx * dx + dz * dz);
        points[i].tx = dx / len;
        points[i].tz = dz / len;
    }
    std::vector<float> curv(count);
    for (int i = 0; i < count; ++i) {
        const TrackPoint& a = points[(i - 2 + count) % count];
        const TrackPoint& b = points[(i + 2) % count];
        curv[i] = (a.tx * b.tz - a.tz * b.tx) / (4.0f * spacing);
    }
    BoxBlurWrapped(curv, 6, 2);
    for (int i = 0; i < count; ++i) points[i].curvature = curv[i];

    // 4) Peraltes: berm en el lado exterior, proporcional a la curvatura
    bank.assign(count, 0.0f);
    if (def.banking) {
        for (int i = 0; i < count; ++i) {
            float k = curv[i];
            float mag = std::min(def.bankHeight, std::max(0.0f, std::fabs(k) - 0.012f) * def.bankGain);
            bank[i] = k > 0.0f ? mag : -mag;   // curva a la derecha => berm a la izquierda (+)
        }
        BoxBlurWrapped(bank, 14, 3);
    }

    // 5) Pavimento: asfalto o cemento según la superficie de cada tramo (con transiciones suaves).
    pavement.assign(count, 0.0f);
    asphalt.assign(count, 0.0f);
    for (int i = 0; i < count; ++i) {
        pavement[i] = pointSurf[i] == Surface::Asphalt || pointSurf[i] == Surface::Concrete ? 1.0f : 0.0f;
        asphalt[i] = pointSurf[i] == Surface::Asphalt ? 1.0f : 0.0f;
    }
    BoxBlurWrapped(pavement, 8, 2);
    BoxBlurWrapped(asphalt, 8, 2);

    // 6) Obstáculos y largada: a mano, con un plan por recta o ninguno. Una vuelta guía no toca el
    // terreno: no lleva obstáculos (si no, el bot frenaría y el reaparecer retrocedería por saltos que
    // no están).
    std::printf("track: %s, longitud %.0f m\n", def.id.c_str(), length);
    startLine = 14.0f;
    const bool guide = def.roadStyle == "guide";
    PlaceFeatures(def.features.empty() && !guide ? def.autoFeatures : std::vector<std::vector<FeatureKind>>{}, !def.hasStart);
    if (!def.features.empty() && !guide) PlaceFeatures(def.features);
    if (def.hasStart) startLine = NearestS(def.startX, def.startZ);
    BakeProfile();
}

float Track::NearestS(float x, float z) const { return points[Nearest(x, z)].s; }

void Track::PlaceFeatures(const std::vector<MapFeature>& list)
{
    features.clear();
    for (const MapFeature& mf : list) {
        const float len = FeatureLength(mf.kind);
        // En s: empieza ahí. En (x, z): centrado en el punto más cercano (la escadaria empieza ahí).
        const float start = mf.atS ? Wrap(mf.s)
                                   : Wrap(NearestS(mf.x, mf.z) - (mf.kind == FeatureKind::Escadaria ? 0.0f : len * 0.5f));
        features.push_back({mf.kind, start, len, FeatureName(mf.kind)});
        std::printf("track:   %-10s s=%6.1f .. %6.1f\n", FeatureName(mf.kind), start, Wrap(start + len));
    }
}

void Track::PlaceFeatures(const std::vector<std::vector<FeatureKind>>& plan, bool autoStart)
{
    features.clear();
    const int count = (int)points.size();

    // Rectas = tramos con curvatura baja. Se recorre desde una muestra curva para no partir
    // una recta al dar la vuelta al final del array.
    const float straightK = 1.0f / 70.0f;
    int startIdx = 0;
    while (startIdx < count && std::fabs(points[startIdx].curvature) < straightK) ++startIdx;

    struct Run { float s0, s1; };
    std::vector<Run> runs;
    bool inRun = false;
    float runStart = 0.0f;
    for (int k = 0; k <= count; ++k) {
        int i = (startIdx + k) % count;
        float s = (float)(startIdx + k) * spacing;          // s "desenrollada"
        bool straight = k < count && std::fabs(points[i].curvature) < straightK;
        if (straight && !inRun) { inRun = true; runStart = s; }
        if (!straight && inRun) { inRun = false; runs.push_back({runStart, s}); }
    }
    // Ordenar por posición a lo largo de la pista: la recta que contiene s = 0 va primero.
    std::sort(runs.begin(), runs.end(), [&](const Run& a, const Run& b) {
        return Wrap(a.s0 + 40.0f) < Wrap(b.s0 + 40.0f);
    });

    // Plan de obstáculos para cada recta útil (>= 40 m), en orden de recorrido. Si algo no entra
    // en su recta (por ejemplo tras cambiar el trazado) se descartan los últimos de esa recta. La
    // largada (si el mapa no dice dónde) va al principio de la primera.
    constexpr float kExitMargin = 18.0f, kBrakeMargin = 12.0f, kGap = 16.0f;
    size_t planIdx = 0;
    for (size_t r = 0; r < runs.size(); ++r) {
        if (runs[r].s1 - runs[r].s0 < 40.0f) continue;
        const bool startStraight = planIdx == 0;
        if (startStraight && autoStart) startLine = Wrap(runs[r].s0 + 10.0f);
        if (planIdx >= plan.size()) break;
        float s = runs[r].s0 + (startStraight ? 26.0f : kExitMargin);
        const float end = runs[r].s1 - kBrakeMargin;
        bool first = true;
        for (FeatureKind kind : plan[planIdx]) {
            const float start = s + (first ? 0.0f : kGap);
            const float len = FeatureLength(kind);
            if (start + len > end) break;
            features.push_back({kind, start, len, FeatureName(kind)});
            std::printf("track:   %-10s s=%6.1f .. %6.1f\n", FeatureName(kind), Wrap(start), Wrap(start + len));
            s = start + len;
            first = false;
        }
        ++planIdx;
    }
}

void Track::BakeProfile()
{
    const int count = (int)points.size();
    profile.assign(count, 0.0f);
    for (int i = 0; i < count; ++i) {
        float s = points[i].s, h = 0.0f;
        for (const TrackFeature& f : features)
            for (float cand : {s, s + length}) {
                float u = cand - f.start;
                if (u >= 0.0f && u <= f.length) h += FeatureHeight(f.kind, u);
            }
        profile[i] = h;
    }
}

float Track::Wrap(float s) const
{
    s = std::fmod(s, length);
    return s < 0.0f ? s + length : s;
}

float Track::SampleArray(const std::vector<float>& a, float s) const
{
    float f = Wrap(s) / spacing;
    int i0 = (int)f % (int)a.size();
    int i1 = (i0 + 1) % (int)a.size();
    return mu::Lerp(a[i0], a[i1], f - std::floor(f));
}

float Track::Profile(float s) const { return SampleArray(profile, s); }
float Track::Bank(float s) const { return SampleArray(bank, s); }
float Track::Pavement(float s) const { return SampleArray(pavement, s); }
float Track::Asphalt(float s) const { return SampleArray(asphalt, s); }

TrackPoint Track::At(float s) const
{
    float f = Wrap(s) / spacing;
    int n = (int)points.size();
    int i0 = (int)f % n, i1 = (i0 + 1) % n;
    float t = f - std::floor(f);
    const TrackPoint& a = points[i0];
    const TrackPoint& b = points[i1];
    TrackPoint r = a;
    r.x = mu::Lerp(a.x, b.x, t);
    r.z = mu::Lerp(a.z, b.z, t);
    r.tx = mu::Lerp(a.tx, b.tx, t);
    r.tz = mu::Lerp(a.tz, b.tz, t);
    float len = std::sqrt(r.tx * r.tx + r.tz * r.tz);
    r.tx /= len;
    r.tz /= len;
    r.curvature = mu::Lerp(a.curvature, b.curvature, t);
    r.s = Wrap(s);
    return r;
}

float Track::MaxCurvatureAhead(float s, float distance) const
{
    float best = 0.0f;
    for (float d = 0.0f; d <= distance; d += spacing * 2.0f) best = std::max(best, std::fabs(At(s + d).curvature));
    return best;
}

int Track::Nearest(float x, float z, int hint, int window) const
{
    const int n = (int)points.size();
    int best = 0;
    float bestD = 1e30f;
    auto test = [&](int i) {
        float dx = points[i].x - x, dz = points[i].z - z, d = dx * dx + dz * dz;
        if (d < bestD) { bestD = d; best = i; }
    };
    if (hint < 0) {
        for (int i = 0; i < n; ++i) test(i);
    } else {
        for (int k = -window; k <= window; ++k) test((hint + k + n) % n);
    }
    return best;
}
