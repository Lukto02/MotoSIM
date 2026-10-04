// Minimapa del HUD y línea de frenada en el suelo de la pista (ver docs/MENU.md, "Minimapa y línea de frenada").
// Sólo leen el estado del juego: nada de acá toca la física (sin ventana ni se llaman).
#include <Jolt/Jolt.h>

#include "Game.h"

#include "MathUtil.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>

using JPH::Vec3;

namespace {

const Color kMiniAccent = {236, 96, 40, 255};    // el color del juego (kAccent de Game.cpp)
const Color kMiniPanel = {12, 14, 18, 150};
constexpr int kMiniRes = 512;                     // textura del relieve: 512² para todo el terreno
// Desaceleración con la que se planea la frenada de la línea (m/s²). Las motos frenan 0.7-1 g (FISICA.md, MOTOS.md);
// 6 deja margen para reaccionar. La del bot (4, o 2.5 en las calles) es la de su control de freno, que es tosco: en
// la recta del autódromo pintaba de rojo 900 m antes de la horquilla.
constexpr float kLineBrake = 6.0f;

Color Mix(Color a, Color b, float t)
{
    t = mu::Clamp(t, 0.0f, 1.0f);
    return {(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t), (unsigned char)(a.b + (b.b - a.b) * t),
            (unsigned char)(a.a + (b.a - a.a) * t)};
}

// Triángulo en la pantalla en cualquier orden (DrawTriangle de raylib sólo dibuja los que van en sentido antihorario).
void Triangle(Vector2 a, Vector2 b, Vector2 c, Color col)
{
    if ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x) > 0.0f) std::swap(b, c);
    DrawTriangle(a, b, c, col);
}

// Flecha de una moto: punta hacia dir (unitario, en la pantalla), con borde oscuro.
void Arrow(Vector2 p, Vector2 dir, float size, Color fill)
{
    const Vector2 side = {-dir.y, dir.x};
    auto at = [&](float f, float s, float grow) {
        return Vector2{p.x + (dir.x * f + side.x * s) * (size + grow), p.y + (dir.y * f + side.y * s) * (size + grow)};
    };
    for (int pass = 0; pass < 2; ++pass) {
        const float g = pass == 0 ? 2.2f : 0.0f;
        const Color c = pass == 0 ? Color{10, 12, 16, 230} : fill;
        Triangle(at(1.0f, 0.0f, g), at(-0.75f, 0.68f, g), at(-0.4f, 0.0f, g), c);
        Triangle(at(1.0f, 0.0f, g), at(-0.4f, 0.0f, g), at(-0.75f, -0.68f, g), c);
    }
}

}  // namespace

Game::MiniMapMode Game::MiniMapShown() const
{
    if (opt.miniMap == "norte") return MiniMapMode::North;
    if (opt.miniMap == "gira") return MiniMapMode::Heading;
    if (!opt.miniMap.empty() || !PlayerDriving()) return MiniMapMode::Off;   // el bot y las pruebas: capturas limpias
    return miniMapMode;
}

bool Game::BrakeLineShown() const
{
    if (!opt.brakeLine.empty()) return opt.brakeLine != "no";      // si, o toda (la vuelta entera, para las capturas)
    return brakeLine && PlayerDriving();
}

void Game::UnloadMiniMap()
{
    if (miniTex.id > 0) UnloadTexture(miniTex);
    miniTex = {};
}

// Relieve del terreno visto desde arriba (sombreado con una luz del noroeste en la pantalla, exagerado para que se lean
// las lomas suaves) y lo que es pista o calle más claro (TrackMask: en la favela salen también las calles que cruzan).
// Se arma una vez por mapa; dibujarlo es un cuadrilátero con textura.
void Game::BuildMiniMap()
{
    UnloadMiniMap();
    miniTrack.clear();
    miniYawSet = false;
    lineIndex = -1;

    const float x0 = terrain.OriginX(), z0 = terrain.OriginZ(), size = terrain.Size();
    float hMin = 1e9f, hMax = -1e9f;
    std::vector<float> heights((size_t)kMiniRes * kMiniRes);
    // Píxel (u, v): u crece hacia -x y v hacia -z (con el norte, +z, arriba, +x queda a la izquierda: así se ve desde arriba).
    auto world = [&](int u, int v, float& x, float& z) {
        x = x0 + size - ((float)u + 0.5f) / (float)kMiniRes * size;
        z = z0 + size - ((float)v + 0.5f) / (float)kMiniRes * size;
    };
    for (int v = 0; v < kMiniRes; ++v)
        for (int u = 0; u < kMiniRes; ++u) {
            float x, z;
            world(u, v, x, z);
            const float h = terrain.Height(x, z);
            heights[(size_t)v * kMiniRes + u] = h;
            hMin = std::min(hMin, h);
            hMax = std::max(hMax, h);
        }
    const float texel = size / (float)kMiniRes;
    const bool streets = terrain.Streets();
    Image img = GenImageColor(kMiniRes, kMiniRes, BLANK);
    Color* px = (Color*)img.data;
    for (int v = 0; v < kMiniRes; ++v)
        for (int u = 0; u < kMiniRes; ++u) {
            float x, z;
            world(u, v, x, z);
            auto h = [&](int a, int b) { return heights[(size_t)std::clamp(b, 0, kMiniRes - 1) * kMiniRes + std::clamp(a, 0, kMiniRes - 1)]; };
            // Pendiente en la pantalla (u a la derecha, v hacia abajo), exagerada x3; luz desde arriba a la izquierda.
            const float du = (h(u + 1, v) - h(u - 1, v)) / (2.0f * texel) * 3.0f, dv = (h(u, v + 1) - h(u, v - 1)) / (2.0f * texel) * 3.0f;
            const float inv = 1.0f / std::sqrt(du * du + dv * dv + 1.0f);
            const float light = (du * 0.55f + dv * 0.55f + 0.63f) * inv;           // (-0.55, -0.55, 0.63) · (-du, -dv, 1)
            const float shade = mu::Clamp(0.45f + 0.75f * light, 0.35f, 1.25f);
            const float height01 = hMax - hMin > 0.5f ? (heights[(size_t)v * kMiniRes + u] - hMin) / (hMax - hMin) : 0.5f;
            const float m = mu::Clamp(terrain.TrackMask(x, z), 0.0f, 1.0f);
            const Color ground = {56, 64, 60, 255}, road = streets ? Color{118, 120, 126, 255} : Color{132, 112, 88, 255};
            Color c = Mix(ground, road, m);
            const float k = shade * (0.85f + 0.3f * height01);
            c = {(unsigned char)std::min(255.0f, c.r * k), (unsigned char)std::min(255.0f, c.g * k), (unsigned char)std::min(255.0f, c.b * k), 255};
            px[v * kMiniRes + u] = c;
        }
    miniTex = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&miniTex);
    SetTextureFilter(miniTex, TEXTURE_FILTER_TRILINEAR);

    // El trazado, cada ~2 m, y lo que se encuadra con el norte arriba: la pista entera, o el terreno en un mapa libre.
    float bx0 = 1e9f, bx1 = -1e9f, bz0 = 1e9f, bz1 = -1e9f;
    if (!FreeRide() && !track.Points().empty()) {
        // Un punto cada 2 m en las curvas y cada 30 m en las rectas (cuando el rumbo giró 3.5° desde el último): pocas líneas
        // que dibujar cada cuadro (el autódromo queda en unos cientos).
        const int every = std::max(1, (int)std::lround(2.0f / track.Spacing()));
        const auto& pts = track.Points();
        size_t kept = 0;
        miniTrack.push_back({pts[0].x, pts[0].z});
        for (size_t i = every; i < pts.size(); i += every) {
            const float turn = std::fabs(std::remainder(std::atan2(pts[i].tx, pts[i].tz) - std::atan2(pts[kept].tx, pts[kept].tz), 2.0f * PI));
            if (turn < 0.06f && pts[i].s - pts[kept].s < 30.0f) continue;
            miniTrack.push_back({pts[i].x, pts[i].z});
            kept = i;
        }
        miniTrack.push_back({pts[0].x, pts[0].z});                  // cerrada
        for (const Vector2& p : miniTrack) {
            bx0 = std::min(bx0, p.x);
            bx1 = std::max(bx1, p.x);
            bz0 = std::min(bz0, p.y);
            bz1 = std::max(bz1, p.y);
        }
        const float margin = track.HalfWidth() + 8.0f;
        bx0 -= margin, bx1 += margin, bz0 -= margin, bz1 += margin;
    } else {
        bx0 = x0, bx1 = x0 + size, bz0 = z0, bz1 = z0 + size;
    }
    miniCx = 0.5f * (bx0 + bx1);
    miniCz = 0.5f * (bz0 + bz1);
    miniExtent = std::max(bx1 - bx0, bz1 - bz0);
}

// Arriba a la derecha (a pedido; los pilotos en red van debajo, la vuelta y los tiempos arriba a la izquierda y el
// cronómetro al centro). Con el norte arriba, la pista entera; girando con la moto, una ventana alrededor de ella con la
// moto un poco abajo del centro (se ve más lo que viene).
void Game::DrawMiniMap(float ui)
{
    const MiniMapMode mode = MiniMapShown();
    if (mode == MiniMapMode::Off || miniTex.id == 0) return;
    const float W = (float)GetScreenWidth();
    const float S = kMiniMapSize * ui;
    const Rectangle panel = {W - 18.0f * ui - S, 18.0f * ui, S, S};
    const Vector2 mid = {panel.x + S * 0.5f, panel.y + S * 0.5f};

    // De quién: la moto, o el piloto si se cayó (la flecha sigue con el último rumbo de la moto).
    const Vec3 at = ragdoll.Active() ? ragdoll.Position(alpha) : bike.RenderPosition(alpha);
    const Vec3 fwd3 = bike.RenderRotation(alpha) * Vec3::sAxisZ();
    float yaw = std::atan2(fwd3.GetX(), fwd3.GetZ());
    if (!miniYawSet) miniYaw = yaw, miniYawSet = true;
    // Suavizado (0.12 s): la moto cabecea y serpentea un poco, y el mapa entero girando con eso tiembla.
    float diff = std::remainder(yaw - miniYaw, 2.0f * PI);
    miniYaw = std::remainder(miniYaw + diff * (1.0f - std::exp(-GetFrameTime() / 0.12f)), 2.0f * PI);

    // Ejes de la pantalla en el mundo (x, z): derecha y arriba. Con el norte arriba, +z arriba y +x a la izquierda.
    float rx = -1.0f, rz = 0.0f, fx = 0.0f, fz = 1.0f, cx = miniCx, cz = miniCz, scale = S * 0.9f / std::max(miniExtent, 40.0f);
    Vector2 c = mid;
    if (mode == MiniMapMode::Heading) {
        fx = std::sin(miniYaw), fz = std::cos(miniYaw);
        rx = -fz, rz = fx;
        cx = at.GetX(), cz = at.GetZ();
        const float radius = mu::Clamp(miniExtent * 0.3f, 60.0f, 200.0f);   // m del centro al borde
        scale = S * 0.5f / radius;
        c.y = panel.y + S * 0.62f;
    }
    auto P = [&](float x, float z) {
        const float dx = x - cx, dz = z - cz;
        return Vector2{c.x + (dx * rx + dz * rz) * scale, c.y - (dx * fx + dz * fz) * scale};
    };
    auto D = [&](float x, float z) {                  // una dirección del mundo en la pantalla (unitaria)
        Vector2 d = {x * rx + z * rz, -(x * fx + z * fz)};
        const float l = std::sqrt(d.x * d.x + d.y * d.y);
        return l > 1e-5f ? Vector2{d.x / l, d.y / l} : Vector2{0.0f, -1.0f};
    };

    DrawRectangleRec(panel, kMiniPanel);
    rlDrawRenderBatchActive();
    BeginScissorMode((int)panel.x, (int)panel.y, (int)S, (int)S);
    // El relieve: un cuadrilátero con las esquinas del terreno donde caen (gira y escala con lo demás).
    {
        const float x0 = terrain.OriginX(), z0 = terrain.OriginZ(), x1 = x0 + terrain.Size(), z1 = z0 + terrain.Size();
        const Vector2 tl = P(x1, z1), bl = P(x1, z0), br = P(x0, z0), tr = P(x0, z1);
        rlSetTexture(miniTex.id);
        rlBegin(RL_QUADS);
        rlColor4ub(255, 255, 255, 215);
        rlNormal3f(0.0f, 0.0f, 1.0f);
        rlTexCoord2f(0.0f, 0.0f);
        rlVertex2f(tl.x, tl.y);
        rlTexCoord2f(0.0f, 1.0f);
        rlVertex2f(bl.x, bl.y);
        rlTexCoord2f(1.0f, 1.0f);
        rlVertex2f(br.x, br.y);
        rlTexCoord2f(1.0f, 0.0f);
        rlVertex2f(tr.x, tr.y);
        rlEnd();
        rlSetTexture(0);
    }
    // El trazado: borde oscuro y la línea clara, del ancho de la pista (con un mínimo para que se lea).
    if (miniTrack.size() > 1) {
        const float w = std::max(3.0f * ui, 2.0f * track.HalfWidth() * scale * 0.7f);
        std::vector<Vector2> screen(miniTrack.size());
        for (size_t i = 0; i < miniTrack.size(); ++i) screen[i] = P(miniTrack[i].x, miniTrack[i].y);
        // Una tira con las juntas en inglete (la bisectriz de las dos normales): sin muescas en las curvas y sin solaparse
        // (el borde es translúcido). Con DrawLineEx y un círculo en cada junta costaba ~1.5 ms por cuadro en el autódromo.
        const size_t count = screen.size() - 1;                // el último repite el primero (cerrada)
        std::vector<Vector2> miter(count);
        for (size_t i = 0; i < count; ++i) {
            const Vector2 a = screen[(i + count - 1) % count], b = screen[i], c = screen[(i + 1) % count];
            auto normal = [](Vector2 from, Vector2 to) {
                const float dx = to.x - from.x, dy = to.y - from.y, l = std::max(std::sqrt(dx * dx + dy * dy), 1e-4f);
                return Vector2{-dy / l, dx / l};
            };
            const Vector2 n0 = normal(a, b), n1 = normal(b, c);
            Vector2 m = {n0.x + n1.x, n0.y + n1.y};
            const float ml = std::sqrt(m.x * m.x + m.y * m.y);
            m = ml > 1e-4f ? Vector2{m.x / ml, m.y / ml} : n1;
            const float k = 1.0f / std::max(m.x * n1.x + m.y * n1.y, 0.35f);
            miter[i] = {m.x * k, m.y * k};
        }
        rlDrawRenderBatchActive();
        rlDisableBackfaceCulling();
        for (int pass = 0; pass < 2; ++pass) {
            const float half = (pass == 0 ? w + 2.5f * ui : w) * 0.5f;
            const Color col = pass == 0 ? Color{10, 12, 16, 200} : Color{226, 228, 232, 235};
            rlBegin(RL_TRIANGLES);
            rlColor4ub(col.r, col.g, col.b, col.a);
            for (size_t i = 0; i < count; ++i) {
                const size_t j = (i + 1) % count;
                const Vector2 al = {screen[i].x + miter[i].x * half, screen[i].y + miter[i].y * half};
                const Vector2 ar = {screen[i].x - miter[i].x * half, screen[i].y - miter[i].y * half};
                const Vector2 bl = {screen[j].x + miter[j].x * half, screen[j].y + miter[j].y * half};
                const Vector2 br = {screen[j].x - miter[j].x * half, screen[j].y - miter[j].y * half};
                rlVertex2f(al.x, al.y), rlVertex2f(ar.x, ar.y), rlVertex2f(br.x, br.y);
                rlVertex2f(al.x, al.y), rlVertex2f(br.x, br.y), rlVertex2f(bl.x, bl.y);
            }
            rlEnd();
        }
        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
        // Largada: una raya del color del juego cruzando la pista.
        const TrackPoint sp = track.At(track.startLine);
        const Vector2 a = P(sp.x, sp.z), n = D(-sp.tz, sp.tx);
        const float half = w * 0.5f + 3.0f * ui;
        DrawLineEx({a.x - n.x * half, a.y - n.y * half}, {a.x + n.x * half, a.y + n.y * half}, 2.5f * ui, kMiniAccent);
    }
    // Los demás (en red): una flecha más chica del color de su moto; si quedan afuera, contra el borde.
    const float inset = 8.0f * ui;
    auto clampIn = [&](Vector2 p) {
        return Vector2{mu::Clamp(p.x, panel.x + inset, panel.x + S - inset), mu::Clamp(p.y, panel.y + inset, panel.y + S - inset)};
    };
    for (int id = 0; id < Multiplayer::kMaxPlayers; ++id) {
        const Multiplayer::Remote& r = mp.Remotes()[id];
        if (!r.active || !r.hasState) continue;
        const Vec3 p = r.DisplayPos(), f = r.DisplayRot() * Vec3::sAxisZ();
        Arrow(clampIn(P(p.GetX(), p.GetZ())), D(f.GetX(), f.GetZ()), 8.5f * ui, Livery(id).plastic);
    }
    Arrow(clampIn(P(at.GetX(), at.GetZ())), D(std::sin(miniYaw), std::cos(miniYaw)), 11.0f * ui, kMiniAccent);
    rlDrawRenderBatchActive();
    EndScissorMode();
    DrawRectangle((int)panel.x, (int)panel.y, (int)(44.0f * ui), (int)(2.0f * ui), kMiniAccent);
    if (mode == MiniMapMode::Heading) {                   // dónde queda el norte
        const Vector2 n = D(0.0f, 1.0f);
        const float rr = S * 0.5f - 11.0f * ui;
        const Vector2 np = {mid.x + n.x * rr, mid.y + n.y * rr};
        const char* label = "N";
        const float fs = 14.0f * ui;
        const Vector2 m = MeasureTextEx(font, label, fs, 0.0f);
        DrawCircleV(np, 8.5f * ui, Color{10, 12, 16, 190});
        DrawTextEx(font, label, {np.x - m.x * 0.5f, np.y - m.y * 0.5f}, fs, 0.0f, Color{236, 238, 240, 230});
    }
}

// ---------------------------------------------------------------------------------------------------- línea ideal
// La trayectoria de carrera de cada mapa con trazado: abrirse antes de la curva, tocar el vértice por adentro y salir
// abierto, sin pasar los bordes. Es el método K1999 (Rémi Coulom, el bot de TORCS): cada punto se corre de costado para
// que su curvatura quede en el promedio de las de sus vecinos (la curvatura cambia de a poco y queda lo más abierta
// posible), primero con puntos cada 128 m y después cada vez más juntos hasta 2 m. En las curvas con peralte (la pista
// de motocross) la línea va por la franja de afuera donde el peralte bajo la moto pasa los 11° (MAPAS.md: ahí se dobla a
// fondo): el vértice queda en el berm y no en el plano de adentro.
float Game::RacingSample(const std::vector<float>& a, float s) const
{
    if (a.empty()) return 0.0f;
    const int m = (int)a.size();
    const float f = track.Wrap(s) / track.Spacing();
    const int i0 = (int)f % m, i1 = (i0 + 1) % m;
    return mu::Lerp(a[(size_t)i0], a[(size_t)i1], f - std::floor(f));
}

void Game::BuildRacingLine()
{
    rlOffset.clear();
    rlCurv.clear();
    const std::vector<TrackPoint>& pts = track.Points();
    if (!current.racingLine || FreeRide() || pts.size() < 64) return;   // sólo los circuitos (racing_line en el .json del mapa)
    const int m = (int)pts.size();
    const int k = std::max(1, (int)std::lround(2.0f / track.Spacing()));   // se optimiza con un punto cada ~2 m
    const int count = m / k;
    const double hw = track.HalfWidth();
    // lane: 0 = c + hw·n, 1 = c − hw·n, con n = (−tz, tx) (en el plano x-z, a la izquierda yendo hacia t; es el `lat` de
    // Terrain). El corrimiento desde el centro es hw·(1 − 2·lane).
    std::vector<double> lx(count), lz(count), rx(count), rz(count), px(count), pz(count), lane(count, 0.5), lo(count), hi(count);
    const double margin = std::min(1.1, hw * 0.35);             // del borde al centro de la moto
    for (int j = 0; j < count; ++j) {
        const TrackPoint& p = pts[(size_t)j * k];
        lx[j] = p.x - p.tz * hw, lz[j] = p.z + p.tx * hw;
        rx[j] = p.x + p.tz * hw, rz[j] = p.z - p.tx * hw;
        lo[j] = margin / (2.0 * hw);
        hi[j] = 1.0 - lo[j];
        // Peralte (Track::Bank, + = berm del lado de lane 1, el borde de adentro es el de lane 0: t = lane): de costado es bank·t² con t de 0 en el borde de adentro a 1
        // en el de afuera; la pendiente pasa los 11° desde t = tan(11°)·hw/bank. La línea va de ahí para afuera (con un
        // poco de margen), entrando de a poco entre 0.6 y 1.4 m de peralte.
        const double bank = std::fabs(track.Bank(p.s));
        if (bank > 0.3) {
            const double t11 = std::tan(11.0 * PI / 180.0) * hw / bank;
            const double need = std::min(t11 + 0.08, 0.8) * mu::Smoothstep(0.6f, 1.4f, (float)bank);
            if (track.Bank(p.s) > 0.0f) lo[j] = std::max(lo[j], std::min(hi[j], need));
            else hi[j] = std::min(hi[j], std::max(lo[j], 1.0 - need));
        }
    }
    auto place = [&](int j) {
        px[j] = lx[j] + lane[j] * (rx[j] - lx[j]);
        pz[j] = lz[j] + lane[j] * (rz[j] - lz[j]);
    };
    for (int j = 0; j < count; ++j) place(j);
    // 1/radio con signo del círculo por tres puntos (+ = hacia lane 0).
    auto inverseRadius = [](double ax, double az, double bx, double bz, double cx, double cz) {
        const double x1 = cx - bx, y1 = cz - bz, x2 = ax - bx, y2 = az - bz, x3 = cx - ax, y3 = cz - az;
        const double nnn = std::sqrt((x1 * x1 + y1 * y1) * (x2 * x2 + y2 * y2) * (x3 * x3 + y3 * y3));
        return nnn > 1e-12 ? 2.0 * (x1 * y2 - x2 * y1) / nnn : 0.0;
    };
    // Corre el punto i (entre prev y next) para que su curvatura sea target, dentro de los bordes.
    auto adjust = [&](int prev, int i, int next, double target, double security) {
        // Primero en la recta de prev a next; después un paso de Newton hacia la curvatura pedida.
        const double ax = px[next] - px[prev], az = pz[next] - pz[prev];
        const double den = az * (rx[i] - lx[i]) - ax * (rz[i] - lz[i]);
        if (std::fabs(den) > 1e-9) lane[i] = mu::Clamp((float)((-az * (lx[i] - px[prev]) + ax * (lz[i] - pz[prev])) / den), -0.2f, 1.2f);
        place(i);
        const double dLane = 1e-4;
        const double d = inverseRadius(px[prev], pz[prev], px[i] + dLane * (rx[i] - lx[i]), pz[i] + dLane * (rz[i] - lz[i]), px[next], pz[next]) -
                         inverseRadius(px[prev], pz[prev], px[i], pz[i], px[next], pz[next]);
        if (std::fabs(d) > 1e-12) lane[i] += dLane / d * (target - inverseRadius(px[prev], pz[prev], px[i], pz[i], px[next], pz[next]));
        const double sec = std::min(security / (2.0 * hw), 0.25);
        const double a = std::min(lo[i] + sec, 0.5), b = std::max(hi[i] - sec, 0.5);
        lane[i] = std::clamp(lane[i], a, std::max(a, b));
        place(i);
    };
    std::vector<int> idx;
    auto smooth = [&](int step) {
        const int c = (int)idx.size();
        if (c < 5) return;
        for (int q = 0; q < c; ++q) {
            const int pp = idx[(q + c - 2) % c], p = idx[(q + c - 1) % c], i = idx[q], n = idx[(q + 1) % c], nn = idx[(q + 2) % c];
            const double ri0 = inverseRadius(px[pp], pz[pp], px[p], pz[p], px[i], pz[i]);
            const double ri1 = inverseRadius(px[i], pz[i], px[n], pz[n], px[nn], pz[nn]);
            const double lPrev = std::hypot(px[i] - px[p], pz[i] - pz[p]), lNext = std::hypot(px[i] - px[n], pz[i] - pz[n]);
            const double target = (lNext * ri0 + lPrev * ri1) / std::max(lNext + lPrev, 1e-6);
            adjust(p, i, n, target, lPrev * lNext / 800.0);
        }
        (void)step;
    };
    for (int step = 128; (step /= 2) > 0;) {
        idx.clear();
        for (int j = 0; j < count; j += step) idx.push_back(j);
        for (int it = 100 * (int)std::sqrt((double)step); --it >= 0;) smooth(step);
        // Los puntos del medio, en línea recta entre los ya ubicados (la pasada siguiente los acomoda).
        for (size_t q = 0; q < idx.size(); ++q) {
            const int a = idx[q], b = q + 1 < idx.size() ? idx[q + 1] : count;
            for (int j = a + 1; j < b; ++j) {
                lane[j] = mu::Lerp((float)lane[a], (float)lane[b % count], (float)(j - a) / (float)(b - a));
                place(j);
            }
        }
    }
    // Curvatura de la línea (cuerda de ±4 m, y un promedio de ±4 m: con 2 m sale ruidosa) y todo a cada punto del trazado.
    std::vector<double> curv(count), avg(count);
    for (int j = 0; j < count; ++j) {
        const int a = (j + count - 2) % count, b = (j + 2) % count;
        curv[j] = inverseRadius(px[a], pz[a], px[j], pz[j], px[b], pz[b]);
    }
    for (int j = 0; j < count; ++j) {
        double sum = 0.0;
        for (int d = -2; d <= 2; ++d) sum += curv[(j + d + count) % count];
        avg[j] = sum / 5.0;
    }
    rlOffset.resize((size_t)m);
    rlCurv.resize((size_t)m);
    for (int i = 0; i < m; ++i) {
        int a = i / k, b = a + 1;
        float t = (float)(i - a * k) / (float)k;
        if (a >= count - 1) {                        // el último tramo, hasta la largada (puede ser más largo que k)
            a = count - 1, b = 0;
            t = (float)(i - a * k) / (float)(m - a * k);
        }
        rlOffset[(size_t)i] = (float)(hw * (1.0 - 2.0 * mu::Lerp((float)lane[a], (float)lane[b % count], t)));
        rlCurv[(size_t)i] = mu::Lerp((float)avg[a], (float)avg[b % count], t);
    }
}

// Línea de frenada sobre la línea ideal, unos metros adelante, apoyada en el suelo: verde si la velocidad de ahora alcanza
// para todo lo que viene y amarilla, naranja y roja donde, yendo así, ya habría que estar frenando. Es la cuenta del bot
// (BotInput) sobre la línea ideal: en cada punto la velocidad de la curva v = sqrt(lateral / curvatura) con la aceleración
// lateral del mapa (bot_lateral), y la más alta con la que se llega frenando a todas las de adelante, sqrt(v² + 2·a·d).
void Game::DrawBrakeLine()
{
    if (!BrakeLineShown() || !showHud || FreeRide() || menu != Menu::None || rlOffset.empty()) return;   // F2 y --nohud también la esconden
    const bool whole = opt.brakeLine == "toda";      // pruebas: la vuelta entera, sin desvanecer (capturas desde arriba)
    if (!whole && (bike.crashed || !bike.RiderOnBike())) return;
    const Vec3 pos = bike.RenderPosition(alpha);
    lineIndex = track.Nearest(pos.GetX(), pos.GetZ(), lineIndex, 80);
    const TrackPoint& here = track.Points()[lineIndex];
    const float hw = track.HalfWidth();
    // Lejos de la pista no tiene sentido (se va apagando de 2 a 10 m afuera del borde).
    const float off = std::hypot(pos.GetX() - here.x, pos.GetZ() - here.z);
    const float offFade = whole ? 1.0f : 1.0f - mu::Smoothstep(hw + 2.0f, hw + 10.0f, off);
    if (offFade <= 0.0f) return;
    // Para dónde se va: con la velocidad, o parado hacia donde mira la moto (en contramano la línea va para el otro lado).
    const Vec3 vel = bike.Velocity(), fwd = bike.Rotation() * Vec3::sAxisZ();
    const Vec3 heading = Vec3(vel.GetX(), 0.0f, vel.GetZ()).Length() > 2.0f ? vel : fwd;
    const float dir = heading.GetX() * here.tx + heading.GetZ() * here.tz >= 0.0f ? 1.0f : -1.0f;
    const float v = Vec3(vel.GetX(), 0.0f, vel.GetZ()).Length();

    const float lateral = current.botLateral > 0.0f ? current.botLateral : opt.botLateralAccel;
    const float visible = whole ? track.Length() : mu::Clamp(25.0f + v * 3.0f, 35.0f, 180.0f);   // ~3 s adelante
    const float step = whole ? 1.0f : 0.5f;
    const float total = visible + v * v / (2.0f * kLineBrake) + 30.0f;           // y hasta donde llega la frenada
    const int n = (int)std::ceil(total / step) + 1;
    struct Sample { float x, z, tx, tz, vmax; };
    static std::vector<Sample> line;
    line.resize((size_t)n);
    for (int i = 0; i < n; ++i) {
        const float s = here.s + dir * step * (float)i;
        const TrackPoint p = track.At(s);
        const float o = RacingSample(rlOffset, s);
        line[(size_t)i] = {p.x - p.tz * o, p.z + p.tx * o, p.tx, p.tz, std::sqrt(lateral / std::max(std::fabs(RacingSample(rlCurv, s)), 1e-3f))};
    }
    for (int i = n - 2; i >= 0; --i) {               // de atrás para adelante: lo que permite frenar para las que siguen
        Sample& a = line[(size_t)i];
        const Sample& b = line[(size_t)i + 1];
        const float d = std::hypot(b.x - a.x, b.z - a.z);
        a.vmax = std::min(a.vmax, std::sqrt(b.vmax * b.vmax + 2.0f * kLineBrake * d));
    }

    const Color green = {40, 225, 95, 255}, yellow = {255, 214, 40, 255}, red = {240, 44, 32, 255};
    const float width = mu::Clamp(hw * 0.08f, 0.32f, 0.55f) * (whole ? 2.5f : 1.0f), feather = 0.14f;
    const Vector3 cam = camera.cam.position;
    // Vértice apoyado en el terreno: 2 cm arriba y corrido hacia la cámara a lo largo de la mirada (mismo píxel, otra
    // profundidad), como las marcas de goma (RENDER.md): sin pisarse con el suelo a ninguna distancia.
    auto ground = [&](float x, float y, float z) {
        Vector3 p = {x, y + 0.02f, z};
        const Vector3 d = Vector3Subtract(p, cam);
        const float dist = std::max(Vector3Length(d), 1e-3f);
        return Vector3Subtract(p, Vector3Scale(d, (0.01f + 0.0012f * dist) / dist));
    };
    struct Edge { Vector3 p[4]; Color c[4]; };
    auto vertex = [&](const Edge& e, int q) {
        rlColor4ub(e.c[q].r, e.c[q].g, e.c[q].b, e.c[q].a);
        rlVertex3f(e.p[q].x, e.p[q].y, e.p[q].z);
    };
    auto edgeAt = [&](int i) {
        const Sample& p = line[(size_t)i];
        const float d = step * (float)i;
        // Hasta el 95% de la velocidad que permite, verde; al 100%, amarilla; pasado el 108%, roja.
        const float t = mu::Clamp((v / std::max(p.vmax, 0.1f) - 0.95f) / 0.13f, 0.0f, 1.0f);
        Color col = t < 0.5f ? Mix(green, yellow, t * 2.0f) : Mix(yellow, red, t * 2.0f - 1.0f);
        const float a = whole ? 0.9f : 0.9f * offFade * mu::Smoothstep(1.5f, 6.0f, d) * (1.0f - mu::Smoothstep(visible * 0.55f, visible, d));
        col.a = (unsigned char)(255.0f * a);
        Edge e;
        const float lat[4] = {-width * 0.5f - feather, -width * 0.5f, width * 0.5f, width * 0.5f + feather};
        // El terreno se lee en los dos bordes del medio; los de afuera (transparentes) siguen la misma pendiente.
        const float y1 = terrain.Height(p.x - p.tz * lat[1], p.z + p.tx * lat[1]), y2 = terrain.Height(p.x - p.tz * lat[2], p.z + p.tx * lat[2]);
        const float ys[4] = {y1 + (y1 - y2) * feather / width, y1, y2, y2 + (y2 - y1) * feather / width};
        for (int q = 0; q < 4; ++q) {
            e.p[q] = ground(p.x - p.tz * lat[q], ys[q], p.z + p.tx * lat[q]);
            e.c[q] = (q == 0 || q == 3) ? Color{col.r, col.g, col.b, 0} : col;
        }
        return e;
    };

    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    rlBegin(RL_TRIANGLES);
    const int last = std::min(n - 1, (int)std::ceil(visible / step));
    Edge prev = edgeAt(whole ? 0 : 2);
    for (int i = whole ? 1 : 3; i <= last; ++i) {
        const Edge e = edgeAt(i);
        for (int q = 0; q < 3; ++q) {
            vertex(prev, q), vertex(e, q), vertex(e, q + 1);
            vertex(prev, q), vertex(e, q + 1), vertex(prev, q + 1);
        }
        prev = e;
    }
    rlEnd();
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
}
