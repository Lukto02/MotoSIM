// Moto low poly armada por código. Cada pieza es una sola malla hecha de primitivas (cajas
// redondeadas, cilindros, tubos barridos a lo largo de curvas, piezas torneadas y placas) con el
// color y el brillo en los vértices. Espacio de cada pieza: ver BikeMesh en Render.h.
// Ejes de la moto: +X izquierda, +Y arriba, +Z adelante.
#include "Render.h"
#include "BikeStyleDef.h"
#include "MeshBuilder.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <vector>

namespace {

constexpr float kPi = 3.14159265f, kTau = 6.28318531f;

// ------------------------------------------------------------------------ materiales
struct Mat {
    Color c;
    float gloss;
};
const Mat kPlastic = {{204, 24, 34, 255}, 0.55f};           // plásticos rojos
const Mat kPlasticWhite = {{238, 238, 234, 255}, 0.5f};
const Mat kPlasticBlue = {{30, 70, 170, 255}, 0.55f};
const Mat kPlasticBlack = {{30, 30, 34, 255}, 0.35f};
const Mat kNumber = {{22, 22, 26, 255}, 0.4f};
const Mat kSeat = {{24, 24, 27, 255}, 0.12f};
const Mat kAlu = {{178, 180, 186, 255}, 0.6f};               // chasis y basculante
const Mat kAluBright = {{200, 202, 206, 255}, 0.7f};         // tijas, manubrio, levas
const Mat kSteel = {{188, 190, 194, 255}, 0.85f};            // tornillos, ejes, vástago
const Mat kEngine = {{64, 66, 72, 255}, 0.35f};
const Mat kEngineFin = {{88, 90, 96, 255}, 0.4f};
const Mat kCover = {{150, 150, 146, 255}, 0.5f};             // tapas de magnesio
const Mat kRadiator = {{78, 80, 86, 255}, 0.3f};
const Mat kRubber = {{36, 34, 34, 255}, 0.06f};
const Mat kKnob = {{46, 44, 43, 255}, 0.06f};
const Mat kGrip = {{44, 44, 48, 255}, 0.1f};
const Mat kGold = {{210, 162, 60, 255}, 0.7f};               // barras de la horquilla, corona
const Mat kStanchion = {{56, 56, 62, 255}, 0.85f};           // botellas (DLC)
const Mat kHub = {{42, 42, 46, 255}, 0.55f};                 // masas y amortiguador anodizados
const Mat kRim = {{190, 192, 196, 255}, 0.75f};
const Mat kSpoke = {{206, 206, 210, 255}, 0.6f};
const Mat kDisc = {{176, 176, 182, 255}, 0.7f};
const Mat kCaliper = {{150, 152, 158, 255}, 0.55f};
const Mat kChain = {{70, 68, 66, 255}, 0.6f};
const Mat kSpring = {{236, 196, 36, 255}, 0.6f};

float Clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
float Smooth(float e0, float e1, float x)
{
    const float t = Clamp01((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}
Color Mix(Color a, Color b, float t)
{
    t = Clamp01(t);
    return {(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
            (unsigned char)(a.b + (b.b - a.b) * t), 255};
}

// ------------------------------------------------------------------------ transformaciones
const Quaternion kNoRot = {0.0f, 0.0f, 0.0f, 1.0f};
Quaternion RotX(float deg) { return QuaternionFromAxisAngle({1.0f, 0.0f, 0.0f}, deg * DEG2RAD); }

Matrix Place(Vector3 t, Vector3 s, Quaternion q = kNoRot)
{
    return MatrixMultiply(MatrixMultiply(MatrixScale(s.x, s.y, s.z), QuaternionToMatrix(q)), MatrixTranslate(t.x, t.y, t.z));
}

// Primitiva de eje Y (cilindro de radio 1 o caja de lado 1) estirada de a hasta b; sx y sz escalan
// los otros dos ejes (radio del cilindro o ancho de la caja).
Matrix Between(Vector3 a, Vector3 b, float sx, float sz)
{
    Vector3 d = Vector3Subtract(b, a);
    const float len = Vector3Length(d);
    if (d.y < 0.0f) d = Vector3Negate(d);                     // la pieza es simétrica: evita el giro de 180°
    const Quaternion q = QuaternionFromVector3ToVector3({0.0f, 1.0f, 0.0f}, Vector3Scale(d, 1.0f / std::max(len, 1e-6f)));
    return Place(Vector3Lerp(a, b, 0.5f), {sx, len, sz}, q);
}

// Matriz con las columnas dadas (ejes locales ya escalados) y traslación t.
Matrix Basis(Vector3 x, Vector3 y, Vector3 z, Vector3 t)
{
    Matrix m = MatrixIdentity();
    m.m0 = x.x; m.m1 = x.y; m.m2 = x.z;
    m.m4 = y.x; m.m5 = y.y; m.m6 = y.z;
    m.m8 = z.x; m.m9 = z.y; m.m10 = z.z;
    m.m12 = t.x; m.m13 = t.y; m.m14 = t.z;
    return m;
}

// ------------------------------------------------------------------------ primitivas
// Caja de lado 1 centrada.
MeshBuilder BoxShape()
{
    MeshBuilder b;
    const Vector3 faces[6][2] = {
        {{1, 0, 0}, {0, 1, 0}}, {{-1, 0, 0}, {0, 1, 0}}, {{0, 1, 0}, {0, 0, 1}},
        {{0, -1, 0}, {0, 0, 1}}, {{0, 0, 1}, {1, 0, 0}}, {{0, 0, -1}, {1, 0, 0}},
    };
    for (const auto& f : faces) {
        const Vector3 n = f[0], u = f[1], v = Vector3CrossProduct(n, u), c = Vector3Scale(n, 0.5f);
        auto corner = [&](float su, float sv) { return Vector3Add(c, Vector3Add(Vector3Scale(u, 0.5f * su), Vector3Scale(v, 0.5f * sv))); };
        const unsigned short a = b.Vertex(corner(-1, -1), n), bb = b.Vertex(corner(1, -1), n);
        const unsigned short cc = b.Vertex(corner(1, 1), n), d = b.Vertex(corner(-1, 1), n);
        b.Tri(a, bb, cc);
        b.Tri(a, cc, d);
    }
    b.FixWinding();
    return b;
}

// Cilindro de radio 1, eje Y de -0.5 a 0.5, con tapas.
MeshBuilder CylinderShape(int slices)
{
    MeshBuilder b;
    for (int i = 0; i <= slices; ++i) {
        const float a = kTau * (float)i / (float)slices;
        const Vector3 n = {std::cos(a), 0.0f, std::sin(a)};
        b.Vertex({n.x, 0.5f, n.z}, n);
        b.Vertex({n.x, -0.5f, n.z}, n);
    }
    for (int i = 0; i < slices; ++i) {
        const unsigned short t0 = (unsigned short)(i * 2), b0 = t0 + 1, t1 = t0 + 2, b1 = t0 + 3;
        b.Tri(t0, t1, b1);
        b.Tri(t0, b1, b0);
    }
    for (float y : {0.5f, -0.5f}) {
        const Vector3 n = {0.0f, y > 0.0f ? 1.0f : -1.0f, 0.0f};
        const unsigned short center = b.Vertex({0.0f, y, 0.0f}, n);
        for (int i = 0; i <= slices; ++i) {
            const float a = kTau * (float)i / (float)slices;
            b.Vertex({std::cos(a), y, std::sin(a)}, n);
        }
        for (int i = 0; i < slices; ++i) b.Tri(center, (unsigned short)(center + 1 + i), (unsigned short)(center + 2 + i));
    }
    b.FixWinding();
    return b;
}

// Caja redondeada (superelipsoide) de lado 1: exponente chico = más cuadrada.
MeshBuilder RoundedBoxShape(float e, int rings = 8, int slices = 14)
{
    auto sgnpow = [](float v, float p) { return (v < 0.0f ? -1.0f : 1.0f) * std::pow(std::fabs(v), p); };
    MeshBuilder b;
    const int row = slices + 1;
    for (int r = 0; r <= rings; ++r) {
        const float u = -0.5f * kPi + kPi * (float)r / (float)rings;
        for (int s = 0; s <= slices; ++s) {
            const float v = kTau * (float)s / (float)slices;
            const Vector3 p = {0.5f * sgnpow(std::cos(u), e) * sgnpow(std::sin(v), e), 0.5f * sgnpow(std::sin(u), e),
                               0.5f * sgnpow(std::cos(u), e) * sgnpow(std::cos(v), e)};
            const float m = 2.0f - e;                        // normal exacta: la misma forma con exponente 2 - e
            Vector3 n = Vector3Normalize({sgnpow(std::cos(u), m) * sgnpow(std::sin(v), m), sgnpow(std::sin(u), m),
                                          sgnpow(std::cos(u), m) * sgnpow(std::cos(v), m)});
            if (r == 0 || r == rings) n = {0.0f, r == 0 ? -1.0f : 1.0f, 0.0f};
            b.Vertex(p, n);
        }
    }
    for (int r = 0; r < rings; ++r)
        for (int s = 0; s < slices; ++s) {
            const unsigned short a = (unsigned short)(r * row + s), bb = a + 1;
            const unsigned short c = (unsigned short)((r + 1) * row + s + 1), d = c - 1;
            b.Tri(a, bb, c);
            b.Tri(a, c, d);
        }
    b.FixWinding();
    return b;
}

const MeshBuilder& UnitBox()
{
    static const MeshBuilder m = BoxShape();
    return m;
}
const MeshBuilder& UnitCylinder()
{
    static const MeshBuilder m = CylinderShape(16);
    return m;
}
const MeshBuilder& UnitRoundedBox(float e)
{
    static std::map<float, MeshBuilder> cache;
    auto it = cache.find(e);
    if (it == cache.end()) it = cache.emplace(e, RoundedBoxShape(e)).first;
    return it->second;
}

// ------------------------------------------------------------------------ perfiles 2D
using Profile = std::vector<Vector2>;

Profile Circle(float r, int n)
{
    Profile p;
    for (int i = 0; i < n; ++i) p.push_back({r * std::cos(kTau * i / n), r * std::sin(kTau * i / n)});
    return p;
}

// Superelipse de semiejes w, h: exponente 2 = elipse, más alto = más cuadrada.
Profile Superellipse(float w, float h, float n, int count)
{
    auto sgnpow = [](float v, float p) { return (v < 0.0f ? -1.0f : 1.0f) * std::pow(std::fabs(v), p); };
    Profile p;
    for (int i = 0; i < count; ++i) {
        const float a = kTau * i / count;
        p.push_back({w * sgnpow(std::cos(a), 2.0f / n), h * sgnpow(std::sin(a), 2.0f / n)});
    }
    return p;
}

// Sección de guardabarros: cara de arriba curva de semiancho hw que cae `drop` en los bordes,
// espesor th. Arriba en y = 0.
Profile Arch(float hw, float drop, float th, int n = 10)
{
    Profile p;
    const float A = 1.2f;
    for (int i = 0; i <= n; ++i) {
        const float a = -A + 2.0f * A * i / n;
        p.push_back({hw * std::sin(a) / std::sin(A), drop * (std::cos(a) - 1.0f) / (1.0f - std::cos(A))});
    }
    for (int i = n; i >= 0; --i) p.push_back({p[i].x * 0.97f, p[i].y - th});
    return p;
}

// Normales hacia afuera de un perfil cerrado; en un punto repetido queda una arista viva (cada copia
// toma sólo la normal de su lado).
Profile ProfileNormals(const Profile& p)
{
    const int n = (int)p.size();
    float area = 0.0f;
    for (int i = 0; i < n; ++i) area += p[i].x * p[(i + 1) % n].y - p[(i + 1) % n].x * p[i].y;
    const float sgn = area >= 0.0f ? 1.0f : -1.0f;
    auto segment = [&](int i) {
        const Vector2 d = Vector2Subtract(p[(i + 1) % n], p[i]);
        const float l = Vector2Length(d);
        return l < 1e-7f ? Vector2{0.0f, 0.0f} : Vector2{sgn * d.y / l, -sgn * d.x / l};
    };
    Profile out(n);
    for (int i = 0; i < n; ++i) {
        const Vector2 s = Vector2Add(segment((i + n - 1) % n), segment(i));
        out[i] = Vector2Length(s) < 1e-6f ? Vector2{0.0f, 1.0f} : Vector2Normalize(s);
    }
    return out;
}

// ------------------------------------------------------------------------ generadores
// Perfil cerrado (x, r) girado alrededor del eje X. radial(ang, j) multiplica el radio (dientes,
// pétalos); color(segmento, j).
MeshBuilder Revolve(const Profile& prof, int segs, const std::function<Color(int, int)>& color = {},
                    const std::function<float(float, int)>& radial = {})
{
    MeshBuilder b;
    const int n = (int)prof.size();
    const Profile nr = ProfileNormals(prof);
    for (int s = 0; s <= segs; ++s) {
        const float a = kTau * (float)s / (float)segs, ca = std::cos(a), sa = std::sin(a);
        for (int j = 0; j < n; ++j) {
            const float r = prof[j].y * (radial ? radial(a, j) : 1.0f);
            b.Vertex({prof[j].x, r * ca, r * sa}, {nr[j].x, nr[j].y * ca, nr[j].y * sa}, {0.0f, 0.0f},
                     color ? color(s % segs, j) : WHITE);
        }
    }
    for (int s = 0; s < segs; ++s)
        for (int j = 0; j < n; ++j) {
            const int j2 = (j + 1) % n;
            if (prof[j].x == prof[j2].x && prof[j].y == prof[j2].y) continue;
            const unsigned short a = (unsigned short)(s * n + j), bb = (unsigned short)(s * n + j2);
            const unsigned short c = (unsigned short)((s + 1) * n + j2), d = (unsigned short)((s + 1) * n + j);
            b.Tri(a, bb, c);
            b.Tri(a, c, d);
        }
    b.FixWinding();
    return b;
}

// Sección cerrada barrida a lo largo de una curva (Catmull-Rom por los puntos de control).
// t va de 0 a 1 según el largo recorrido.
struct Sweep {
    std::vector<Vector3> path;
    Profile profile;                                   // x = costado, y = "arriba" de la sección
    std::function<Vector2(float)> scale;               // escala de la sección según t
    std::function<Color(float, int)> color;            // según t y el punto del perfil
    std::vector<float> edges;                          // t donde el color cambia de golpe
    Vector3 up = {0.0f, 1.0f, 0.0f};                   // hacia dónde mira el "arriba" de la sección
    bool transport = false;                            // transporte paralelo (tubos que se doblan en 3D)
    bool caps = true;
    int perSegment = 8;
};

Vector3 CatmullRom(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float u)
{
    const float u2 = u * u, u3 = u2 * u;
    auto f = [&](float a, float b, float c, float d) {
        return 0.5f * (2.0f * b + (c - a) * u + (2.0f * a - 5.0f * b + 4.0f * c - d) * u2 + (3.0f * b - a - 3.0f * c + d) * u3);
    };
    return {f(p0.x, p1.x, p2.x, p3.x), f(p0.y, p1.y, p2.y, p3.y), f(p0.z, p1.z, p2.z, p3.z)};
}

MeshBuilder BuildSweep(const Sweep& s)
{
    // Muestras de la curva y su t (largo acumulado).
    const int n = (int)s.path.size();
    std::vector<Vector3> pts;
    for (int i = 0; i + 1 < n; ++i) {
        const Vector3 p0 = s.path[std::max(i - 1, 0)], p1 = s.path[i], p2 = s.path[i + 1], p3 = s.path[std::min(i + 2, n - 1)];
        for (int k = 0; k < s.perSegment; ++k) pts.push_back(CatmullRom(p0, p1, p2, p3, (float)k / (float)s.perSegment));
    }
    pts.push_back(s.path[n - 1]);
    std::vector<float> len(pts.size(), 0.0f);
    for (size_t i = 1; i < pts.size(); ++i) len[i] = len[i - 1] + Vector3Distance(pts[i], pts[i - 1]);
    const float total = std::max(len.back(), 1e-6f);
    struct Sample { float t; Vector3 p; };
    std::vector<Sample> smp;
    for (size_t i = 0; i < pts.size(); ++i) smp.push_back({len[i] / total, pts[i]});
    for (float e : s.edges)
        for (float t : {e - 1e-3f, e + 1e-3f})
            for (size_t i = 0; i + 1 < pts.size(); ++i) {
                const float t0 = len[i] / total, t1 = len[i + 1] / total;
                if (t >= t0 && t <= t1) {
                    smp.push_back({t, Vector3Lerp(pts[i], pts[i + 1], (t - t0) / std::max(t1 - t0, 1e-6f))});
                    break;
                }
            }
    std::sort(smp.begin(), smp.end(), [](const Sample& a, const Sample& b) { return a.t < b.t; });

    // Marcos: tangente, costado y arriba de la sección.
    const int m = (int)smp.size();
    std::vector<Vector3> T(m), X(m), Y(m);
    for (int i = 0; i < m; ++i) T[i] = Vector3Normalize(Vector3Subtract(smp[std::min(i + 1, m - 1)].p, smp[std::max(i - 1, 0)].p));
    for (int i = 0; i < m; ++i) {
        Vector3 side;
        if (s.transport && i > 0) {
            side = Vector3Subtract(X[i - 1], Vector3Scale(T[i], Vector3DotProduct(X[i - 1], T[i])));
        } else {
            side = Vector3CrossProduct(s.up, T[i]);
            if (Vector3Length(side) < 1e-4f) side = i > 0 ? X[i - 1] : Vector3{1.0f, 0.0f, 0.0f};
        }
        X[i] = Vector3Normalize(side);
        Y[i] = Vector3CrossProduct(T[i], X[i]);
    }

    MeshBuilder b;
    const Profile& prof = s.profile;
    const Profile nr = ProfileNormals(prof);
    const int q = (int)prof.size();
    auto scaleAt = [&](float t) { return s.scale ? s.scale(t) : Vector2{1.0f, 1.0f}; };
    auto colorAt = [&](float t, int j) { return s.color ? s.color(t, j) : WHITE; };
    auto pointAt = [&](int i, int j) {
        const Vector2 sc = scaleAt(smp[i].t);
        return Vector3Add(smp[i].p, Vector3Add(Vector3Scale(X[i], prof[j].x * sc.x), Vector3Scale(Y[i], prof[j].y * sc.y)));
    };
    for (int i = 0; i < m; ++i) {
        const Vector2 sc = scaleAt(smp[i].t);
        for (int j = 0; j < q; ++j) {
            const Vector2 n2 = Vector2Normalize({nr[j].x / std::max(sc.x, 1e-4f), nr[j].y / std::max(sc.y, 1e-4f)});
            b.Vertex(pointAt(i, j), Vector3Add(Vector3Scale(X[i], n2.x), Vector3Scale(Y[i], n2.y)), {0.0f, 0.0f}, colorAt(smp[i].t, j));
        }
    }
    for (int i = 0; i + 1 < m; ++i)
        for (int j = 0; j < q; ++j) {
            const int j2 = (j + 1) % q;
            const unsigned short a = (unsigned short)(i * q + j), bb = (unsigned short)(i * q + j2);
            const unsigned short c = (unsigned short)((i + 1) * q + j2), d = (unsigned short)((i + 1) * q + j);
            b.Tri(a, bb, c);
            b.Tri(a, c, d);
        }
    if (s.caps)
        for (int end = 0; end < 2; ++end) {
            const int i = end == 0 ? 0 : m - 1;
            const Vector3 nn = end == 0 ? Vector3Negate(T[0]) : T[m - 1];
            Vector3 center = {0.0f, 0.0f, 0.0f};
            for (int j = 0; j < q; ++j) center = Vector3Add(center, pointAt(i, j));
            center = Vector3Scale(center, 1.0f / (float)q);
            const unsigned short c = b.Vertex(center, nn, {0.0f, 0.0f}, colorAt(smp[i].t, 0));
            for (int j = 0; j < q; ++j) b.Vertex(pointAt(i, j), nn, {0.0f, 0.0f}, colorAt(smp[i].t, j));
            for (int j = 0; j < q; ++j) b.Tri(c, (unsigned short)(c + 1 + j), (unsigned short)(c + 1 + (j + 1) % q));
        }
    b.FixWinding();
    return b;
}

// Placa de espesor th con contorno convexo en el plano (u, v) = (X, Y): la cara de afuera mira a +Z
// y está en z = offset(u, v) (así se curva), la de adentro th más atrás.
MeshBuilder Panel(const Profile& outline, float th, const std::function<float(float, float)>& offset = {}, int rings = 4)
{
    auto off = [&](float u, float v) { return offset ? offset(u, v) : 0.0f; };
    const int n = (int)outline.size();
    Vector2 c = {0.0f, 0.0f};
    for (const Vector2& p : outline) c = Vector2Add(c, p);
    c = Vector2Scale(c, 1.0f / (float)n);

    MeshBuilder b;
    for (int face = 0; face < 2; ++face) {
        const float dz = face == 0 ? 0.0f : -th;
        auto vert = [&](Vector2 p) {
            const float e = 1e-3f;
            const float du = (off(p.x + e, p.y) - off(p.x - e, p.y)) / (2.0f * e);
            const float dv = (off(p.x, p.y + e) - off(p.x, p.y - e)) / (2.0f * e);
            Vector3 nn = Vector3Normalize({-du, -dv, 1.0f});
            if (face == 1) nn = Vector3Negate(nn);
            return b.Vertex({p.x, p.y, off(p.x, p.y) + dz}, nn);
        };
        const unsigned short center = vert(c);
        for (int k = 1; k <= rings; ++k)
            for (int i = 0; i < n; ++i) vert(Vector2Lerp(c, outline[i], (float)k / (float)rings));
        const unsigned short first = center + 1;
        for (int i = 0; i < n; ++i) b.Tri(center, (unsigned short)(first + i), (unsigned short)(first + (i + 1) % n));
        for (int k = 0; k + 1 < rings; ++k)
            for (int i = 0; i < n; ++i) {
                const unsigned short a = (unsigned short)(first + k * n + i), bb = (unsigned short)(first + k * n + (i + 1) % n);
                const unsigned short cc = (unsigned short)(bb + n), d = (unsigned short)(a + n);
                b.Tri(a, bb, cc);
                b.Tri(a, cc, d);
            }
    }
    const Profile nr = ProfileNormals(outline);
    const unsigned short rim = (unsigned short)b.VertexCount();
    for (int i = 0; i < n; ++i) {
        const Vector3 nn = {nr[i].x, nr[i].y, 0.0f};
        b.Vertex({outline[i].x, outline[i].y, off(outline[i].x, outline[i].y)}, nn);
        b.Vertex({outline[i].x, outline[i].y, off(outline[i].x, outline[i].y) - th}, nn);
    }
    for (int i = 0; i < n; ++i) {
        const unsigned short a = (unsigned short)(rim + 2 * i), a2 = (unsigned short)(rim + 2 * ((i + 1) % n));
        b.Tri(a, a2, (unsigned short)(a2 + 1));
        b.Tri(a, (unsigned short)(a2 + 1), (unsigned short)(a + 1));
    }
    b.FixWinding();
    return b;
}

// Número con segmentos gruesos (tipo display), en el plano XY mirando a +Z: alto 1, centrado.
MeshBuilder Digits(const char* text)
{
    static const unsigned char kSegments[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};   // bit 0 = a ... bit 6 = g
    const float w = 0.58f, t = 0.18f, gap = 0.2f;
    const int count = (int)std::strlen(text);
    const float total = count * w + (count - 1) * gap;
    struct Seg { float x, y, sx, sy; };
    const Seg seg[7] = {
        {0.0f, 0.5f - t / 2, w, t}, {w / 2 - t / 2, 0.25f, t, 0.5f}, {w / 2 - t / 2, -0.25f, t, 0.5f},
        {0.0f, -0.5f + t / 2, w, t}, {-w / 2 + t / 2, -0.25f, t, 0.5f}, {-w / 2 + t / 2, 0.25f, t, 0.5f}, {0.0f, 0.0f, w, t},
    };
    MeshBuilder b;
    for (int k = 0; k < count; ++k) {
        const int d = text[k] - '0';
        if (d < 0 || d > 9) continue;
        const float x0 = -total / 2 + k * (w + gap) + w / 2;
        for (int s = 0; s < 7; ++s)
            if (kSegments[d] >> s & 1) b.Append(UnitBox(), Place({x0 + seg[s].x, seg[s].y, 0.0f}, {seg[s].sx, seg[s].sy, 0.02f}));
    }
    return b;
}

// Atajos para sumar piezas de un material a una malla.
struct Parts {
    MeshBuilder& b;
    void Add(const MeshBuilder& o, const Mat& m, const Matrix& at = MatrixIdentity()) { b.Append(o, at, m.c, m.gloss); }
    void Box(Vector3 c, Vector3 size, const Mat& m, Quaternion q = kNoRot) { Add(UnitBox(), m, Place(c, size, q)); }
    void RBox(Vector3 c, Vector3 size, const Mat& m, Quaternion q = kNoRot, float e = 0.35f) { Add(UnitRoundedBox(e), m, Place(c, size, q)); }
    void Rod(Vector3 a, Vector3 c, float r, const Mat& m) { Add(UnitCylinder(), m, Between(a, c, r, r)); }
    void Bar(Vector3 a, Vector3 c, float wx, float wz, const Mat& m) { Add(UnitBox(), m, Between(a, c, wx, wz)); }
    void DiscX(Vector3 c, float r, float thick, const Mat& m) { Rod({c.x - thick / 2, c.y, c.z}, {c.x + thick / 2, c.y, c.z}, r, m); }
    void Tube(const std::vector<Vector3>& path, float r, const Mat& m, int sides = 10)
    {
        Sweep s;
        s.path = path;
        s.profile = Circle(r, sides);
        s.transport = true;
        Add(BuildSweep(s), m);
    }
};

// ------------------------------------------------------------------------ ruedas
// Cubierta con tacos: carcasa torneada (sección elíptica que entra en la llanta) y tacos en filas
// alternadas, cada uno perpendicular a la carcasa. R = radio exterior (arriba de los tacos).
MeshBuilder BuildTire(float R, float hw, float hh, float rimR, int rows, float knobScale = 1.0f)
{
    MeshBuilder b;
    Parts p{b};
    const float knob = 0.013f * knobScale;
    const float rc = R - knob - hh;
    Profile prof;
    for (int i = 0; i <= 20; ++i) {
        const float ph = (-115.0f + 230.0f * i / 20.0f) * DEG2RAD;
        prof.push_back({hw * std::sin(ph), rc + hh * std::cos(ph)});
    }
    prof.push_back({0.021f, rimR - 0.004f});                  // talones, adentro de la llanta
    prof.push_back({-0.021f, rimR - 0.004f});
    p.Add(Revolve(prof, 72), kRubber);
    for (int row = 0; row < rows; ++row) {
        const float a = kTau * (float)row / (float)rows;
        const Vector3 er = {0.0f, std::cos(a), std::sin(a)}, et = {0.0f, -std::sin(a), std::cos(a)};
        auto knobAt = [&](float phDeg, float sx, float st, float h) {
            const float ph = phDeg * DEG2RAD;
            const Vector3 surf = Vector3Add({hw * std::sin(ph), 0.0f, 0.0f}, Vector3Scale(er, rc + hh * std::cos(ph)));
            const Vector3 nn = Vector3Normalize(Vector3Add({std::sin(ph) / hw, 0.0f, 0.0f}, Vector3Scale(er, std::cos(ph) / hh)));
            const Vector3 center = Vector3Add(surf, Vector3Scale(nn, h * 0.5f - 0.002f));   // 2 mm enterrado
            const Vector3 side = Vector3CrossProduct(nn, et);
            p.Add(UnitBox(), kKnob, Basis(Vector3Scale(side, sx), Vector3Scale(nn, h), Vector3Scale(et, st), center));
        };
        if (row % 2 == 0) {
            knobAt(0.0f, 0.024f, 0.019f, knob + 0.002f * knobScale);
            knobAt(62.0f, 0.017f, 0.017f, 0.011f * knobScale);
            knobAt(-62.0f, 0.017f, 0.017f, 0.011f * knobScale);
        } else {
            knobAt(30.0f, 0.019f, 0.019f, knob);
            knobAt(-30.0f, 0.019f, 0.019f, knob);
        }
    }
    return b;
}

// Disco de freno (x = cara, radios rIn..rOut) con pétalos en el borde y agujeros (manchas oscuras),
// más el soporte oscuro que lo une a la masa.
void AddDisc(Parts& p, float x, float rIn, float rOut, int petals)
{
    const float h = 0.002f, r1 = rIn + 0.35f * (rOut - rIn), r2 = rIn + 0.7f * (rOut - rIn);
    const Profile prof = {{x - h, rIn}, {x - h, r1}, {x - h, r2}, {x - h, rOut}, {x + h, rOut}, {x + h, r2}, {x + h, r1}, {x + h, rIn}};
    const int segs = petals * 8;
    p.Add(Revolve(prof, segs,
                  [](int s, int j) {
                      const bool ring1 = (j == 1 || j == 6) && s % 4 == 0, ring2 = (j == 2 || j == 5) && s % 4 == 2;
                      return (ring1 || ring2) ? Color{40, 40, 44, 255} : WHITE;
                  },
                  [petals](float a, int j) { return (j == 3 || j == 4) ? 1.0f + 0.04f * std::cos(petals * a) : 1.0f; }),
          kDisc);
    const Profile carrier = {{x - h, 0.03f}, {x - h, rIn + 0.008f}, {x + h, rIn + 0.008f}, {x + h, 0.03f}};
    p.Add(Revolve(carrier, 24), kHub);
}

// Llanta en U, masa con bridas, 36 rayos cruzados y disco (atrás, también la corona).
MeshBuilder BuildWheel(bool front)
{
    MeshBuilder b;
    Parts p{b};
    const float rimR = front ? 0.267f : 0.243f;
    const Profile rim = {{-0.023f, rimR + 0.005f}, {-0.023f, rimR - 0.005f}, {-0.014f, rimR - 0.011f}, {-0.011f, rimR - 0.02f},
                         {0.011f, rimR - 0.02f}, {0.014f, rimR - 0.011f}, {0.023f, rimR - 0.005f}, {0.023f, rimR + 0.005f}};
    p.Add(Revolve(rim, 64), kRim);

    const float hx = front ? 0.058f : 0.066f, fx = front ? 0.044f : 0.05f;
    const Profile hub = {{-hx, 0.012f}, {-hx, 0.026f}, {-fx - 0.003f, 0.028f}, {-fx - 0.003f, 0.047f}, {-fx + 0.003f, 0.047f},
                         {-fx + 0.003f, 0.03f}, {fx - 0.003f, 0.03f}, {fx - 0.003f, 0.047f}, {fx + 0.003f, 0.047f},
                         {fx + 0.003f, 0.028f}, {hx, 0.026f}, {hx, 0.012f}};
    p.Add(Revolve(hub, 32), kHub);

    for (int i = 0; i < 36; ++i) {
        const float side = i % 2 == 0 ? 1.0f : -1.0f;
        const float ah = kTau * (float)i / 36.0f;
        const float ar = ah + ((i / 2) % 2 == 0 ? 0.62f : -0.62f);      // cruzados: la mitad adelanta, la mitad atrasa
        const Vector3 atHub = {side * fx, 0.043f * std::cos(ah), 0.043f * std::sin(ah)};
        const Vector3 atRim = {side * 0.006f, (rimR - 0.018f) * std::cos(ar), (rimR - 0.018f) * std::sin(ar)};
        p.Bar(atHub, atRim, 0.0038f, 0.0038f, kSpoke);
    }

    if (front) {
        AddDisc(p, 0.05f, 0.075f, 0.135f, 12);                         // a la izquierda
    } else {
        AddDisc(p, -0.052f, 0.065f, 0.12f, 10);                        // a la derecha
        // Corona (izquierda): 48 dientes y agujeros alivianados.
        const float x = 0.068f, h = 0.002f;
        const Profile ring = {{x - h, 0.05f}, {x - h, 0.08f}, {x - h, 0.112f}, {x + h, 0.112f}, {x + h, 0.08f}, {x + h, 0.05f}};
        p.Add(Revolve(ring, 192,
                      [](int s, int j) { return ((j == 1 || j == 4) && s % 32 < 8) ? Color{50, 40, 20, 255} : WHITE; },
                      [](float a, int j) { return (j == 2 || j == 3) ? 1.0f + 0.035f * std::cos(48.0f * a) : 1.0f; }),
              kGold);
        const Profile carrier = {{x - 0.008f, 0.03f}, {x - 0.008f, 0.052f}, {x, 0.052f}, {x, 0.03f}};
        p.Add(Revolve(carrier, 24), kHub);
    }
    return b;
}

// ------------------------------------------------------------------------ basculante y amortiguador
MeshBuilder BuildSwingarm(const Mat& metal = kAlu)
{
    MeshBuilder b;
    Parts p{b};
    const float L = BikeMesh::kSwingarmLength;
    for (float s : {-1.0f, 1.0f}) {
        Sweep arm;
        arm.path = {{s * 0.085f, 0.0f, 0.02f}, {s * 0.098f, 0.005f, -0.20f}, {s * 0.098f, 0.0f, -(L + 0.04f)}};
        arm.profile = Superellipse(0.017f, 0.035f, 3.5f, 16);
        arm.scale = [](float t) { return Vector2{1.0f, 1.0f - 0.3f * t}; };
        p.Add(BuildSweep(arm), metal);
        p.RBox({s * 0.1f, 0.0f, -L}, {0.036f, 0.034f, 0.055f}, metal, kNoRot, 0.4f);   // tensores de cadena
    }
    p.RBox({0.0f, -0.005f, -0.02f}, {0.17f, 0.045f, 0.04f}, metal, kNoRot, 0.4f);        // travesaño
    p.Rod({-0.125f, 0.0f, -L}, {0.125f, 0.0f, -L}, 0.011f, kSteel);                      // eje trasero
    p.DiscX({-0.128f, 0.0f, -L}, 0.018f, 0.012f, kSteel);

    // Bieleta: donde se toma el amortiguador.
    p.RBox(BikeMesh::kShockLow, {0.05f, 0.045f, 0.05f}, kEngine, kNoRot, 0.4f);
    p.Bar(BikeMesh::kShockLow, {0.0f, -0.10f, -0.01f}, 0.04f, 0.022f, kEngine);

    // Cadena (tramos de arriba y de abajo entre piñón y corona) y su guía.
    const float cx = 0.072f, rf = 0.045f, rr = 0.11f;
    const Vector3 sprocket = {cx, -0.075f, 0.023f};
    p.Bar({cx, sprocket.y + rf, sprocket.z}, {cx, rr, -L}, 0.012f, 0.014f, kChain);
    p.Bar({cx, sprocket.y - rf, sprocket.z}, {cx, -rr, -L}, 0.012f, 0.014f, kChain);
    p.DiscX(sprocket, rf, 0.012f, kHub);
    p.RBox({cx, -0.113f, -0.38f}, {0.03f, 0.045f, 0.12f}, kPlasticBlack, kNoRot, 0.3f);

    // Pinza del freno trasero (derecha) con su soporte.
    p.RBox({-0.07f, -0.06f, -L + 0.03f}, {0.015f, 0.08f, 0.08f}, kAlu);
    p.RBox({-0.052f, -0.095f, -L + 0.055f}, {0.035f, 0.055f, 0.07f}, kCaliper);
    return b;
}

MeshBuilder BuildShockBody()
{
    MeshBuilder b;
    Parts p{b};
    p.Rod({-0.02f, 0.0f, 0.0f}, {0.02f, 0.0f, 0.0f}, 0.014f, kAlu);                                   // ojo
    p.Rod({0.0f, 0.0f, 0.0f}, {0.0f, -BikeMesh::kShockBodyLength, 0.0f}, 0.024f, kHub);                 // cuerpo
    p.Rod({0.0f, -BikeMesh::kSpringTop + 0.01f, 0.0f}, {0.0f, -BikeMesh::kSpringTop, 0.0f}, 0.043f, kGold);   // precarga
    p.RBox({0.04f, -0.018f, 0.0f}, {0.06f, 0.03f, 0.032f}, kHub);                                      // depósito al costado
    p.Rod({0.07f, -0.01f, 0.0f}, {0.07f, -0.12f, 0.0f}, 0.019f, kHub);
    p.Rod({0.07f, -0.12f, 0.0f}, {0.07f, -0.13f, 0.0f}, 0.02f, kGold);
    return b;
}

MeshBuilder BuildShockSpring()
{
    Sweep sp;
    const int turns = 7, per = 10;
    for (int i = 0; i <= turns * per; ++i) {
        const float a = kTau * (float)i / (float)per;
        sp.path.push_back({0.036f * std::cos(a), -BikeMesh::kSpringLength * (float)i / (float)(turns * per), 0.036f * std::sin(a)});
    }
    sp.profile = Circle(0.0075f, 6);
    sp.transport = true;
    sp.perSegment = 2;
    MeshBuilder b;
    Parts{b}.Add(BuildSweep(sp), kSpring);
    return b;
}

MeshBuilder BuildShockLower()
{
    MeshBuilder b;
    Parts p{b};
    p.Rod({-0.02f, 0.0f, 0.0f}, {0.02f, 0.0f, 0.0f}, 0.013f, kAlu);                                   // ojo
    p.RBox({0.0f, 0.015f, 0.0f}, {0.03f, 0.04f, 0.03f}, kAlu);
    p.Rod({0.0f, BikeMesh::kSpringBottom - 0.01f, 0.0f}, {0.0f, BikeMesh::kSpringBottom, 0.0f}, 0.043f, kGold);   // asiento
    p.Rod({0.0f, 0.0f, 0.0f}, {0.0f, 0.30f, 0.0f}, 0.009f, kSteel);                                    // vástago
    return b;
}

// ------------------------------------------------------------------------ horquilla y manubrio
MeshBuilder BuildForkUpper(const BikeLivery& L)
{
    MeshBuilder b;
    Parts p{b};
    p.RBox({0.0f, 0.03f, 0.0f}, {0.27f, 0.035f, 0.10f}, kAluBright, kNoRot, 0.35f);       // tija de arriba
    p.RBox({0.0f, -0.20f, 0.0f}, {0.27f, 0.05f, 0.11f}, kAluBright, kNoRot, 0.35f);       // tija de abajo
    p.Rod({0.0f, 0.045f, 0.0f}, {0.0f, 0.058f, 0.0f}, 0.02f, kSteel);
    for (float s : {-1.0f, 1.0f}) {
        p.Rod({s * 0.09f, 0.062f, 0.0f}, {s * 0.09f, -0.40f, 0.0f}, 0.030f, kGold);          // barras
        p.Rod({s * 0.09f, 0.062f, 0.0f}, {s * 0.09f, 0.075f, 0.0f}, 0.022f, kPlasticBlack);  // tapones
        p.Rod({s * 0.09f, -0.385f, 0.0f}, {s * 0.09f, -0.405f, 0.0f}, 0.032f, kPlasticBlack); // retenes
    }
    // Porta número curvo, delante de las barras, con el número.
    const Profile plate = {{-0.115f, 0.0f}, {0.115f, 0.0f}, {0.128f, -0.055f}, {0.11f, -0.175f},
                           {0.055f, -0.21f}, {-0.055f, -0.21f}, {-0.11f, -0.175f}, {-0.128f, -0.055f}};
    p.Add(Panel(plate, 0.01f, [](float u, float) { return 0.07f + 0.018f * (1.0f - (u / 0.13f) * (u / 0.13f)); }), kPlasticWhite);
    p.Add(Digits(L.number), kNumber, MatrixMultiply(MatrixScale(0.075f, 0.075f, 0.075f), MatrixTranslate(0.0f, -0.09f, 0.092f)));
    return b;
}

MeshBuilder BuildForkLower()
{
    MeshBuilder b;
    Parts p{b};
    for (float s : {-1.0f, 1.0f}) {
        p.Rod({s * 0.09f, 0.05f, 0.0f}, {s * 0.09f, 0.44f, 0.0f}, 0.023f, kStanchion);            // botellas
        p.RBox({s * 0.09f, 0.02f, -0.004f}, {0.05f, 0.10f, 0.06f}, kAluBright, kNoRot, 0.35f);    // pies
        p.RBox({s * 0.093f, 0.165f, 0.012f}, {0.056f, 0.22f, 0.05f}, kPlasticBlack, kNoRot, 0.5f); // protectores
    }
    p.RBox({0.054f, 0.075f, -0.10f}, {0.036f, 0.075f, 0.05f}, kCaliper, kNoRot, 0.35f);          // pinza (izquierda)
    p.RBox({0.075f, 0.05f, -0.05f}, {0.02f, 0.05f, 0.08f}, kAluBright);
    p.Rod({-0.12f, 0.0f, 0.0f}, {0.12f, 0.0f, 0.0f}, 0.011f, kSteel);                            // eje
    p.DiscX({-0.124f, 0.0f, 0.0f}, 0.018f, 0.012f, kSteel);
    return b;
}

MeshBuilder BuildCockpit()
{
    MeshBuilder b;
    Parts p{b};
    Sweep bar;                                                // manubrio con curva, más grueso al medio
    bar.path = {{-0.41f, 0.148f, -0.069f}, {-0.30f, 0.144f, -0.064f}, {-0.15f, 0.14f, -0.06f}, {0.0f, 0.14f, -0.06f},
                {0.15f, 0.14f, -0.06f}, {0.30f, 0.144f, -0.064f}, {0.41f, 0.148f, -0.069f}};
    bar.profile = Circle(0.0125f, 10);
    bar.scale = [](float t) {
        const float k = 0.9f + 0.3f * Smooth(0.3f, 0.7f, 1.0f - std::fabs(2.0f * t - 1.0f));
        return Vector2{k, k};
    };
    p.Add(BuildSweep(bar), kAluBright);
    for (float s : {-1.0f, 1.0f}) {
        p.RBox({s * 0.04f, 0.085f, -0.035f}, {0.035f, 0.08f, 0.04f}, kAluBright, kNoRot, 0.4f);   // torres
        p.RBox({s * 0.04f, 0.14f, -0.06f}, {0.04f, 0.036f, 0.045f}, kAluBright, kNoRot, 0.4f);     // abrazaderas
        p.Rod({s * 0.29f, 0.1455f, -0.066f}, {s * 0.405f, 0.148f, -0.069f}, 0.0175f, kGrip);       // puños
        p.Rod({s * 0.405f, 0.148f, -0.069f}, {s * 0.413f, 0.148f, -0.069f}, 0.0185f, kPlastic);
        p.RBox({s * 0.24f, 0.147f, -0.055f}, {0.03f, 0.032f, 0.036f}, kPlasticBlack, kNoRot, 0.4f); // soportes de leva
        p.Tube({{s * 0.245f, 0.15f, -0.036f}, {s * 0.30f, 0.148f, -0.006f}, {s * 0.37f, 0.142f, 0.0f}}, 0.0055f, kAluBright);
    }
    p.RBox({0.0f, 0.155f, -0.06f}, {0.20f, 0.045f, 0.05f}, kPlasticBlue, kNoRot, 0.55f);         // almohadilla
    p.Rod({-0.272f, 0.146f, -0.066f}, {-0.29f, 0.1455f, -0.066f}, 0.024f, kPlasticBlack);        // acelerador
    p.RBox({-0.21f, 0.168f, -0.05f}, {0.04f, 0.02f, 0.035f}, kPlasticBlack, kNoRot, 0.4f);       // bomba de freno
    return b;
}

// Guardabarros delantero: sigue un arco sobre la rueda (centrado en el eje con la moto en reposo) y
// sube un poco hacia la punta.
MeshBuilder BuildFrontFender(const BikeLivery& L)
{
    MeshBuilder b;
    Parts p{b};
    Sweep f;
    const float cy = -0.505f, cz = 0.256f, R = 0.566f;
    for (int i = 0; i <= 8; ++i) {
        const float a = (-54.0f + 66.0f * (float)i / 8.0f) * DEG2RAD;
        f.path.push_back({0.0f, cy + R * std::cos(a), cz + R * std::sin(a)});
    }
    f.profile = Arch(0.075f, 0.03f, 0.006f);
    f.scale = [](float t) { return Vector2{0.78f + 0.22f * std::sin(kPi * std::min(1.0f, t * 1.2f)), 1.0f}; };
    p.Add(BuildSweep(f), Mat{L.plastic, 0.55f});
    p.RBox({0.0f, 0.012f, 0.0f}, {0.1f, 0.02f, 0.075f}, kPlasticBlack, kNoRot, 0.4f);
    return b;
}

} // namespace

// ------------------------------------------------------------------------ chasis
namespace {
// El de motocross; twoStroke = la dos tiempos: cilindro con tapa chata y bujía, carburador atrás,
// escape de expansión (la "panza" que sale del frente del cilindro y pasa por delante del motor) con
// un silenciador corto y fino, y el asiento del color de la primera franja. Con twoStroke = false sale
// exactamente la de siempre.
Mesh BuildMxBody(Vector3 SH, Vector3 up, const BikeLivery& L, bool twoStroke)
{
    MeshBuilder b;
    Parts p{b};
    const Mat plastic = {L.plastic, 0.55f}, stripeA = {L.stripe1, 0.5f}, stripeB = {L.stripe2, 0.55f};
    auto onAxis = [&](float d) { return Vector3Add(SH, Vector3Scale(up, d)); };   // sobre el eje de dirección

    // Chasis de doble viga: pipa, vigas hasta las placas del basculante, caño delantero que se abre
    // en una cuna bajo el motor, y subchasis bajo el asiento.
    p.Rod(onAxis(-0.02f), onAxis(-0.19f), 0.034f, kAlu);
    for (float s : {-1.0f, 1.0f}) {
        Sweep spar;
        spar.path = {Vector3Add(onAxis(-0.08f), {s * 0.03f, 0.0f, -0.035f}), {s * 0.09f, 0.06f, 0.34f}, {s * 0.105f, 0.01f, 0.12f},
                     {s * 0.105f, -0.04f, -0.04f}, {s * 0.10f, -0.14f, -0.12f}, {s * 0.10f, -0.24f, -0.13f}};
        spar.profile = Superellipse(0.018f, 0.038f, 4.0f, 16);
        spar.scale = [](float t) { return Vector2{1.0f, 1.0f - 0.25f * t}; };
        p.Add(BuildSweep(spar), kAlu);
        p.RBox({s * 0.10f, -0.26f, -0.13f}, {0.032f, 0.13f, 0.11f}, kAlu);
        p.Tube({{s * 0.02f, -0.31f, 0.27f}, {s * 0.06f, -0.41f, 0.21f}, {s * 0.065f, -0.485f, 0.10f}, {s * 0.07f, -0.49f, -0.04f},
                {s * 0.085f, -0.40f, -0.12f}, {s * 0.095f, -0.30f, -0.13f}}, 0.018f, kAlu);
        p.Tube({{s * 0.10f, 0.02f, -0.06f}, {s * 0.085f, 0.055f, -0.30f}, {s * 0.075f, 0.07f, -0.62f}}, 0.014f, kAlu);
        p.Tube({{s * 0.10f, -0.20f, -0.14f}, {s * 0.09f, -0.08f, -0.34f}, {s * 0.075f, 0.055f, -0.58f}}, 0.013f, kAlu);
        p.DiscX({s * 0.127f, -0.27f, -0.13f}, 0.017f, 0.01f, kSteel);                    // tuercas del basculante
    }
    p.Tube({Vector3Add(onAxis(-0.17f), {0.0f, 0.0f, -0.01f}), {0.0f, -0.10f, 0.45f}, {0.0f, -0.24f, 0.33f}, {0.0f, -0.31f, 0.27f}}, 0.024f, kAlu);
    p.Rod({-0.125f, -0.27f, -0.13f}, {0.125f, -0.27f, -0.13f}, 0.012f, kSteel);          // eje del basculante
    p.Rod({-0.09f, 0.035f, -0.17f}, {0.09f, 0.035f, -0.17f}, 0.012f, kAlu);              // travesaño del amortiguador
    p.RBox({0.0f, 0.03f, -0.17f}, {0.05f, 0.03f, 0.045f}, kAlu);

    // Motor: cárter con tapas, cilindro con aletas, tapa de cilindros, cuerpo de aceleración,
    // radiadores y mangueras, cubrecárter, estriberas, pedal de freno y palanca de cambios.
    p.RBox({0.0f, -0.36f, 0.02f}, {0.19f, 0.22f, 0.34f}, kEngine);
    p.DiscX({-0.105f, -0.37f, 0.0f}, 0.09f, 0.03f, kCover);                              // embrague (derecha)
    p.DiscX({-0.121f, -0.37f, 0.0f}, 0.035f, 0.008f, kSteel);
    p.DiscX({0.10f, -0.35f, 0.08f}, 0.07f, 0.034f, kCover);                              // encendido (izquierda)
    p.DiscX({-0.10f, -0.43f, 0.13f}, 0.034f, 0.02f, kCover);                             // bomba de agua
    const Quaternion tilt = RotX(10.0f);
    const Vector3 cylAxis = Vector3RotateByQuaternion({0.0f, 1.0f, 0.0f}, tilt);
    const Vector3 cylBase = {0.0f, -0.185f, 0.10f};
    p.RBox(cylBase, {0.13f, 0.15f, 0.13f}, kEngine, tilt, 0.3f);
    for (int k = 0; k < 4; ++k)
        p.RBox(Vector3Add(cylBase, Vector3Scale(cylAxis, -0.05f + 0.033f * (float)k)), {0.158f, 0.01f, 0.152f}, kEngineFin, tilt, 0.25f);
    if (!twoStroke) {
        p.RBox({0.0f, -0.085f, 0.118f}, {0.15f, 0.075f, 0.15f}, kEngine, tilt, 0.3f);
        p.RBox({0.0f, -0.042f, 0.125f}, {0.12f, 0.035f, 0.12f}, kPlasticBlack, tilt, 0.45f);
        p.Rod({0.0f, -0.10f, 0.03f}, {0.0f, -0.085f, -0.07f}, 0.028f, kCover);
    } else {
        // Tapa de cilindro chata con aletas radiales y la bujía con su pipa arriba; la válvula de
        // escape (tapa a la derecha del cilindro) y el carburador con su fuelle hacia la caja de filtro.
        p.RBox({0.0f, -0.10f, 0.116f}, {0.14f, 0.035f, 0.14f}, kCover, tilt, 0.35f);
        for (int k = 0; k < 3; ++k)
            p.RBox(Vector3Add({0.0f, -0.10f, 0.116f}, Vector3Scale(cylAxis, 0.012f + 0.012f * (float)k)), {0.12f - 0.03f * (float)k, 0.006f, 0.12f - 0.03f * (float)k},
                   kEngineFin, tilt, 0.3f);
        p.Rod(Vector3Add({0.0f, -0.10f, 0.116f}, Vector3Scale(cylAxis, 0.04f)), Vector3Add({0.0f, -0.10f, 0.116f}, Vector3Scale(cylAxis, 0.075f)), 0.012f,
              kPlasticBlack);
        p.RBox({-0.075f, -0.19f, 0.13f}, {0.03f, 0.05f, 0.06f}, kCover, tilt, 0.4f);
        p.Rod({0.0f, -0.20f, 0.03f}, {0.0f, -0.18f, -0.05f}, 0.03f, kSteel);          // carburador
        p.Rod({0.0f, -0.18f, -0.05f}, {0.0f, -0.12f, -0.16f}, 0.026f, kPlasticBlack);  // fuelle
    }
    p.RBox({0.0f, -0.51f, 0.04f}, {0.17f, 0.018f, 0.34f}, kPlasticBlack, kNoRot, 0.25f);
    p.RBox({0.0f, -0.02f, -0.33f}, {0.17f, 0.15f, 0.20f}, kPlasticBlack, kNoRot, 0.4f);   // caja de filtro (bajo el asiento)
    for (float s : {-1.0f, 1.0f}) {
        p.RBox({s * 0.095f, -0.05f, 0.31f}, {0.035f, 0.24f, 0.17f}, kRadiator, RotX(-12.0f), 0.2f);
        p.Tube({{s * 0.07f, -0.14f, 0.25f}, {s * 0.045f, -0.15f, 0.20f}, {s * 0.03f, -0.12f, 0.17f}}, 0.011f, kPlasticBlack);
        p.RBox({s * 0.175f, -0.40f, -0.10f}, {0.085f, 0.022f, 0.05f}, kSteel, kNoRot, 0.3f);
        p.RBox({s * 0.12f, -0.40f, -0.10f}, {0.04f, 0.04f, 0.045f}, kAlu, kNoRot, 0.4f);
    }
    p.Tube({{-0.115f, -0.36f, -0.13f}, {-0.13f, -0.40f, -0.04f}, {-0.14f, -0.42f, 0.05f}}, 0.008f, kSteel);
    p.RBox({-0.145f, -0.42f, 0.062f}, {0.03f, 0.012f, 0.03f}, kSteel);
    p.Tube({{0.105f, -0.31f, -0.03f}, {0.13f, -0.35f, 0.03f}, {0.15f, -0.365f, 0.075f}}, 0.007f, kSteel);
    p.Rod({0.14f, -0.365f, 0.08f}, {0.172f, -0.365f, 0.08f}, 0.008f, kSteel);
    p.RBox({0.10f, -0.30f, -0.05f}, {0.02f, 0.07f, 0.08f}, kPlasticBlack);                // tapa del piñón

    if (twoStroke) {
        // Escape de expansión (derecha): sale del frente del cilindro, baja por delante del motor, se
        // infla en la panza y vuelve subiendo por el costado; el cono de salida entra en el silenciador.
        Sweep chamber;
        chamber.path = {{-0.01f, -0.15f, 0.19f}, {-0.03f, -0.21f, 0.28f}, {-0.07f, -0.31f, 0.31f}, {-0.12f, -0.34f, 0.23f}, {-0.15f, -0.29f, 0.11f},
                        {-0.158f, -0.215f, -0.01f}, {-0.155f, -0.15f, -0.13f}, {-0.152f, -0.11f, -0.26f}};
        chamber.profile = Circle(0.022f, 14);
        chamber.scale = [](float t) {
            const float k = 1.0f + 2.0f * Smooth(0.08f, 0.42f, t) - 2.35f * Smooth(0.6f, 0.92f, t);
            return Vector2{k, k};
        };
        chamber.transport = true;
        chamber.color = [](float t, int) {
            const Color hot = {206, 150, 80, 255}, blue = {84, 70, 130, 255}, steel = {62, 62, 70, 255};   // acero pavonado
            return t < 0.18f ? Mix(hot, blue, t / 0.18f) : Mix(blue, steel, Clamp01((t - 0.18f) / 0.35f));
        };
        p.Add(BuildSweep(chamber), {WHITE, 0.7f});
        Sweep silencer;
        silencer.path = {{-0.152f, -0.11f, -0.26f}, {-0.163f, -0.045f, -0.45f}, {-0.172f, 0.012f, -0.64f}};
        silencer.profile = Superellipse(0.034f, 0.038f, 2.2f, 16);
        silencer.scale = [](float t) {
            const float k = 0.5f + 0.5f * Smooth(0.0f, 0.15f, t);
            return Vector2{k, k};
        };
        silencer.edges = {0.15f, 0.18f, 0.9f, 0.94f};
        silencer.color = [](float t, int) {
            if (t > 0.94f) return Color{30, 30, 34, 255};
            if ((t > 0.15f && t < 0.18f) || (t > 0.9f && t < 0.94f)) return Color{196, 198, 204, 255};   // bandas de aluminio
            return Color{40, 40, 46, 255};                                                                 // carbono
        };
        p.Add(BuildSweep(silencer), {WHITE, 0.8f});
        p.Rod({-0.172f, 0.012f, -0.64f}, {-0.174f, 0.02f, -0.68f}, 0.012f, kSteel);
    } else {
    // Escape (derecha): colector con el tornasolado del calor y silenciador con abrazaderas.
    Sweep header;
    header.path = {{-0.03f, -0.10f, 0.19f}, {-0.06f, -0.16f, 0.26f}, {-0.10f, -0.25f, 0.26f}, {-0.13f, -0.31f, 0.17f},
                   {-0.145f, -0.31f, 0.04f}, {-0.14f, -0.25f, -0.06f}, {-0.135f, -0.17f, -0.15f}, {-0.152f, -0.105f, -0.29f}};
    header.profile = Circle(0.02f, 10);
    header.transport = true;
    header.caps = false;
    header.color = [](float t, int) {
        const Color hot = {206, 150, 80, 255}, blue = {96, 86, 150, 255}, cold = {196, 196, 200, 255};
        return t < 0.3f ? Mix(hot, blue, t / 0.3f) : Mix(blue, cold, (t - 0.3f) / 0.3f);
    };
    p.Add(BuildSweep(header), {WHITE, 0.75f});
    Sweep muffler;
    muffler.path = {{-0.152f, -0.105f, -0.29f}, {-0.166f, -0.04f, -0.50f}, {-0.178f, 0.03f, -0.76f}};
    muffler.profile = Superellipse(0.042f, 0.05f, 2.2f, 16);
    muffler.scale = [](float t) {
        const float k = 0.45f + 0.55f * Smooth(0.0f, 0.12f, t);
        return Vector2{k, k};
    };
    muffler.edges = {0.14f, 0.17f, 0.86f, 0.89f, 0.93f};
    muffler.color = [](float t, int) {
        if (t > 0.93f) return Color{30, 30, 34, 255};
        if ((t > 0.14f && t < 0.17f) || (t > 0.86f && t < 0.89f)) return Color{60, 60, 66, 255};
        return Color{200, 202, 206, 255};
    };
    p.Add(BuildSweep(muffler), {WHITE, 0.7f});
    p.Rod({-0.178f, 0.03f, -0.76f}, {-0.18f, 0.037f, -0.80f}, 0.016f, kSteel);
    }

    // Tanque, asiento y guardabarros trasero.
    Sweep tank;
    tank.path = {{0.0f, 0.115f, 0.46f}, {0.0f, 0.105f, 0.30f}, {0.0f, 0.095f, 0.12f}, {0.0f, 0.09f, -0.02f}};
    tank.profile = Superellipse(0.125f, 0.07f, 3.0f, 24);
    tank.scale = [](float t) { return Vector2{0.72f + 0.28f * Smooth(0.0f, 0.35f, t), 1.0f - 0.1f * t}; };
    p.Add(BuildSweep(tank), plastic);
    p.Rod({0.0f, 0.16f, 0.26f}, {0.0f, 0.185f, 0.26f}, 0.028f, kPlasticBlack);
    Sweep seat;
    seat.path = {{0.0f, 0.128f, 0.06f}, {0.0f, 0.108f, -0.08f}, {0.0f, 0.105f, -0.30f}, {0.0f, 0.11f, -0.52f}, {0.0f, 0.112f, -0.63f}};
    seat.profile = Superellipse(0.115f, 0.045f, 4.0f, 28);
    seat.scale = [](float t) {
        return Vector2{(0.5f + 0.5f * Smooth(0.0f, 0.22f, t)) * (1.0f - 0.15f * Smooth(0.6f, 1.0f, t)), 0.75f + 0.25f * Smooth(0.0f, 0.2f, t)};
    };
    const Profile seatProfile = seat.profile;
    const Color cover = twoStroke ? Mix(L.stripe1, {0, 0, 0, 255}, 0.25f) : kSeat.c;   // la 2T: tapizado de color
    seat.color = [seatProfile, band = plastic.c, cover](float, int j) {   // vivo del color de los plásticos a los costados
        const Vector2 q = seatProfile[j];
        return (q.y < -0.01f && q.y > -0.032f && std::fabs(q.x) > 0.09f) ? band : cover;
    };
    p.Add(BuildSweep(seat), {WHITE, kSeat.gloss});
    Sweep rear;
    rear.path = {{0.0f, 0.075f, -0.36f}, {0.0f, 0.095f, -0.60f}, {0.0f, 0.115f, -0.82f}, {0.0f, 0.13f, -0.97f}};
    rear.profile = Arch(0.10f, 0.025f, 0.007f);
    rear.scale = [](float t) { return Vector2{1.0f - 0.3f * t, 1.0f}; };
    p.Add(BuildSweep(rear), plastic);
    // Patín de la cola: caño de aluminio debajo del guardabarros (lo que raspa en un wheelie pasado).
    for (float s : {-1.0f, 1.0f}) p.Rod({s * 0.055f, 0.07f, -0.82f}, {s * 0.06f, -0.07f, -1.0f}, 0.009f, kAlu);
    p.Rod({-0.07f, -0.07f, -1.005f}, {0.07f, -0.07f, -1.005f}, 0.012f, kAlu);

    // Cachas del radiador con calcos, y laterales blancos con el número (placas en el plano (z, y)).
    const Profile shroud = {{0.06f, 0.15f}, {0.36f, 0.175f}, {0.47f, 0.08f}, {0.44f, -0.08f}, {0.33f, -0.19f}, {0.16f, -0.14f}, {0.05f, 0.03f}};
    auto shroudOff = [](float u, float v) { return 0.125f + 0.05f * Clamp01((0.14f - v) / 0.32f) + 0.02f * Clamp01((u - 0.10f) / 0.35f); };
    auto decalOff = [shroudOff](float u, float v) { return shroudOff(u, v) + 0.0035f; };
    const Profile stripeFront = {{0.21f, 0.158f}, {0.27f, 0.163f}, {0.215f, -0.15f}, {0.16f, -0.12f}};
    const Profile stripeBack = {{0.31f, 0.168f}, {0.35f, 0.171f}, {0.29f, -0.17f}, {0.25f, -0.165f}};
    const Profile sidePanel = {{-0.13f, 0.085f}, {-0.58f, 0.105f}, {-0.63f, 0.06f}, {-0.46f, -0.10f}, {-0.22f, -0.13f}, {-0.11f, -0.03f}};
    auto sideOff = [](float u, float) { const float k = (u + 0.37f) / 0.26f; return 0.112f + 0.006f * Clamp01(1.0f - k * k); };
    for (float s : {-1.0f, 1.0f}) {
        const Matrix toSide = Basis({0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {s, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f});
        p.Add(Panel(shroud, 0.012f, shroudOff), plastic, toSide);
        p.Add(Panel(stripeFront, 0.003f, decalOff, 2), stripeA, toSide);
        p.Add(Panel(stripeBack, 0.003f, decalOff, 2), stripeB, toSide);
        p.Add(Panel(sidePanel, 0.012f, sideOff), kPlasticWhite, toSide);
        // El número se lee derecho de los dos lados: de adelante hacia atrás a la izquierda y al revés
        // a la derecha (girado, no espejado).
        const Vector3 at = {s * (sideOff(-0.37f, 0.0f) + 0.003f), -0.01f, -0.37f};
        p.Add(Digits(L.number), kNumber,
              MatrixMultiply(MatrixMultiply(MatrixScale(0.085f, 0.085f, 0.085f), MatrixRotateY(s * kPi * 0.5f)), MatrixTranslate(at.x, at.y, at.z)));
    }
    return b.Build();
}
} // namespace

Mesh GenBikeBody(Vector3 SH, Vector3 up, const BikeLivery& L) { return BuildMxBody(SH, up, L, false); }
Mesh GenTwoStrokeBody(Vector3 SH, Vector3 up, const BikeLivery& L) { return BuildMxBody(SH, up, L, true); }

const BikeLivery& TwoStrokeLivery(int i)
{
    // Colores de las dos tiempos de los 90: amarilla con azul, blanca con violeta y turquesa, celeste
    // con negro y negra con naranja. La primera franja es también el tapizado del asiento.
    static const BikeLivery kTable[kLiveries] = {
        {{250, 204, 18, 255}, {24, 64, 178, 255}, {238, 238, 234, 255}, "92"},
        {{238, 238, 234, 255}, {118, 46, 168, 255}, {20, 176, 170, 255}, "5"},
        {{24, 150, 214, 255}, {26, 26, 30, 255}, {238, 238, 234, 255}, "17"},
        {{26, 26, 30, 255}, {244, 108, 14, 255}, {238, 238, 234, 255}, "38"},
    };
    return kTable[((i % kLiveries) + kLiveries) % kLiveries];
}

void GenBikeMeshes(Mesh* out)
{
    out[BikeMesh::Swingarm] = BuildSwingarm().Build();
    out[BikeMesh::ShockBody] = BuildShockBody().Build();
    out[BikeMesh::ShockSpring] = BuildShockSpring().Build();
    out[BikeMesh::ShockLower] = BuildShockLower().Build();
    out[BikeMesh::ForkLower] = BuildForkLower().Build();
    out[BikeMesh::Cockpit] = BuildCockpit().Build();
    out[BikeMesh::WheelFront] = BuildWheel(true).Build();
    out[BikeMesh::WheelRear] = BuildWheel(false).Build();
    out[BikeMesh::TireFront] = BuildTire(BikeMesh::kFrontRadius, 0.046f, 0.040f, 0.267f, 42).Build();
    out[BikeMesh::TireRear] = BuildTire(BikeMesh::kRearRadius, 0.058f, 0.044f, 0.243f, 40).Build();
}

const BikeLivery& Livery(int i)
{
    // Un esquema por jugador: plásticos, las dos franjas de las cachas y el número.
    static const BikeLivery kTable[kLiveries] = {
        {{204, 24, 34, 255}, {238, 238, 234, 255}, {30, 70, 170, 255}, "21"},    // rojo
        {{26, 78, 196, 255}, {238, 238, 234, 255}, {246, 196, 20, 255}, "7"},    // azul
        {{36, 150, 62, 255}, {238, 238, 234, 255}, {30, 30, 34, 255}, "44"},     // verde
        {{242, 118, 18, 255}, {30, 30, 34, 255}, {238, 238, 234, 255}, "13"},    // naranja
    };
    return kTable[((i % kLiveries) + kLiveries) % kLiveries];
}

void GenLiveryMeshes(const BikeLivery& L, Mesh* out)
{
    out[0] = BuildFrontFender(L).Build();
    out[1] = BuildForkUpper(L).Build();
}

// ------------------------------------------------------------------------ trilheira
const BikeLivery& TrailLivery(int i)
{
    // Colores de la calle: verde y amarillo, negro con verde flúor, blanco con rojo y azul, amarillo con negro.
    static const BikeLivery kTable[kLiveries] = {
        {{0, 150, 64, 255}, {254, 221, 0, 255}, {0, 39, 118, 255}, "22"},
        {{26, 26, 30, 255}, {120, 230, 40, 255}, {238, 238, 234, 255}, "11"},
        {{238, 238, 234, 255}, {200, 30, 34, 255}, {30, 70, 170, 255}, "77"},
        {{250, 200, 20, 255}, {26, 26, 30, 255}, {30, 70, 170, 255}, "10"},
    };
    return kTable[((i % kLiveries) + kLiveries) % kLiveries];
}

namespace {

const Mat kFrameBlack = {{28, 28, 32, 255}, 0.45f};         // chasis de acero pintado
const Mat kChrome = {{214, 216, 222, 255}, 0.95f};
const Mat kLens = {{250, 246, 226, 255}, 0.95f};             // faro
const Mat kAmber = {{246, 150, 30, 255}, 0.8f};              // guiños
const Mat kTailRed = {{200, 24, 24, 255}, 0.8f};
const Mat kTint = {{40, 44, 52, 255}, 0.9f};                 // parabrisas ahumado

// Horquilla convencional: botellas de aluminio abajo y fuelles de goma (sanfona) sobre las barras.
MeshBuilder BuildTrailForkLower()
{
    MeshBuilder b;
    Parts p{b};
    for (float s : {-1.0f, 1.0f}) {
        p.Rod({s * 0.09f, 0.03f, 0.0f}, {s * 0.09f, 0.30f, 0.0f}, 0.027f, kAluBright);             // botellas
        p.RBox({s * 0.09f, 0.02f, -0.004f}, {0.055f, 0.09f, 0.06f}, kAluBright, kNoRot, 0.35f);     // pies
        for (int k = 0; k < 7; ++k) {                                                                 // fuelle
            const float y = 0.31f + 0.022f * k;
            p.Rod({s * 0.09f, y, 0.0f}, {s * 0.09f, y + 0.012f, 0.0f}, k % 2 ? 0.03f : 0.026f, kRubber);
        }
    }
    p.RBox({0.058f, 0.08f, -0.10f}, {0.036f, 0.075f, 0.05f}, kCaliper, kNoRot, 0.35f);             // pinza (izquierda)
    p.Rod({-0.12f, 0.0f, 0.0f}, {0.12f, 0.0f, 0.0f}, 0.011f, kSteel);                               // eje
    p.DiscX({-0.124f, 0.0f, 0.0f}, 0.018f, 0.012f, kSteel);
    return b;
}

// Manubrio alto con travesaño, cubremanos, puños y levas. Centro del manubrio (0, 0.21, -0.10) desde
// la pipa (ver Bike::RiderPoseLocal), puños centrados a 0.36 m.
MeshBuilder BuildTrailCockpit()
{
    MeshBuilder b;
    Parts p{b};
    const float y = 0.21f, z = -0.10f;
    Sweep bar;
    bar.path = {{-0.43f, y + 0.012f, z - 0.012f}, {-0.30f, y + 0.006f, z - 0.004f}, {-0.16f, y - 0.018f, z + 0.02f}, {0.0f, y - 0.02f, z + 0.025f},
                {0.16f, y - 0.018f, z + 0.02f}, {0.30f, y + 0.006f, z - 0.004f}, {0.43f, y + 0.012f, z - 0.012f}};
    bar.profile = Circle(0.0125f, 10);
    p.Add(BuildSweep(bar), kFrameBlack);
    p.Bar({-0.15f, y + 0.035f, z + 0.015f}, {0.15f, y + 0.035f, z + 0.015f}, 0.014f, 0.014f, kFrameBlack);   // travesaño
    p.RBox({0.0f, y + 0.045f, z + 0.015f}, {0.16f, 0.035f, 0.04f}, kPlastic, kNoRot, 0.5f);              // almohadilla
    for (float s : {-1.0f, 1.0f}) {
        p.Rod({s * 0.045f, 0.03f, -0.035f}, {s * 0.045f, y - 0.02f, z + 0.025f}, 0.016f, kAluBright);   // torres altas
        p.RBox({s * 0.045f, y - 0.02f, z + 0.025f}, {0.04f, 0.036f, 0.045f}, kAluBright, kNoRot, 0.4f);
        p.Rod({s * 0.30f, y + 0.006f, z - 0.004f}, {s * 0.42f, y + 0.012f, z - 0.012f}, 0.0175f, kGrip);  // puños
        p.RBox({s * 0.25f, y + 0.008f, z + 0.005f}, {0.03f, 0.032f, 0.036f}, kPlasticBlack, kNoRot, 0.4f);
        p.Tube({{s * 0.255f, y + 0.012f, z + 0.024f}, {s * 0.31f, y + 0.01f, z + 0.054f}, {s * 0.38f, y + 0.004f, z + 0.06f}}, 0.0055f, kAluBright);
        // Cubremanos: una lámina curva delante del puño.
        Sweep guard;
        guard.path = {{s * 0.26f, y + 0.02f, z + 0.03f}, {s * 0.34f, y + 0.035f, z + 0.075f}, {s * 0.43f, y + 0.02f, z + 0.03f}};
        guard.profile = Superellipse(0.004f, 0.045f, 3.0f, 10);
        guard.transport = true;
        p.Add(BuildSweep(guard), kPlasticBlack);
    }
    p.Rod({-0.272f, y + 0.006f, z - 0.004f}, {-0.29f, y + 0.006f, z - 0.004f}, 0.024f, kPlasticBlack);  // acelerador
    return b;
}

// Tijas, barras y máscara con faro redondo, parabrisas ahumado, guiños y tablero (gira con el
// manubrio). Espacio: origen en la pipa, +Y a lo largo de la horquilla.
MeshBuilder BuildTrailForkUpper(const BikeLivery& L)
{
    MeshBuilder b;
    Parts p{b};
    p.RBox({0.0f, 0.03f, 0.0f}, {0.26f, 0.035f, 0.10f}, kFrameBlack, kNoRot, 0.35f);
    p.RBox({0.0f, -0.20f, 0.0f}, {0.26f, 0.05f, 0.11f}, kFrameBlack, kNoRot, 0.35f);
    for (float s : {-1.0f, 1.0f}) {
        p.Rod({s * 0.09f, 0.062f, 0.0f}, {s * 0.09f, -0.46f, 0.0f}, 0.026f, kChrome);                // barras
        p.Rod({s * 0.09f, 0.062f, 0.0f}, {s * 0.09f, 0.075f, 0.0f}, 0.022f, kPlasticBlack);
    }
    // Máscara: placa curva del color de la moto con el faro redondo en el medio.
    const Profile mask = {{-0.13f, 0.06f}, {0.13f, 0.06f}, {0.155f, -0.04f}, {0.14f, -0.20f}, {0.07f, -0.27f}, {-0.07f, -0.27f}, {-0.14f, -0.20f}, {-0.155f, -0.04f}};
    p.Add(Panel(mask, 0.012f, [](float u, float) { return 0.085f + 0.035f * (1.0f - (u / 0.16f) * (u / 0.16f)); }), Mat{L.plastic, 0.6f});
    const Profile stripe = {{-0.15f, -0.02f}, {0.15f, -0.02f}, {0.148f, -0.05f}, {-0.148f, -0.05f}};
    p.Add(Panel(stripe, 0.003f, [](float u, float) { return 0.089f + 0.035f * (1.0f - (u / 0.16f) * (u / 0.16f)); }, 2), Mat{L.stripe1, 0.6f});
    p.Rod({0.0f, -0.125f, 0.10f}, {0.0f, -0.125f, 0.135f}, 0.068f, kChrome);                           // aro del faro
    p.Rod({0.0f, -0.125f, 0.13f}, {0.0f, -0.125f, 0.138f}, 0.058f, kLens);                             // faro
    const Profile screen = {{-0.11f, 0.06f}, {0.11f, 0.06f}, {0.08f, 0.17f}, {-0.08f, 0.17f}};
    p.Add(Panel(screen, 0.004f, [](float, float v) { return 0.10f - 0.12f * (v - 0.06f); }, 2), kTint);   // parabrisas
    p.RBox({0.0f, 0.02f, 0.03f}, {0.12f, 0.06f, 0.05f}, kPlasticBlack, kNoRot, 0.4f);                  // tablero
    p.RBox({0.0f, 0.03f, 0.056f}, {0.08f, 0.035f, 0.006f}, {{30, 60, 50, 255}, 0.9f}, kNoRot, 0.3f);    // pantalla
    for (float s : {-1.0f, 1.0f}) {                                                                        // guiños
        p.Rod({s * 0.15f, -0.08f, 0.05f}, {s * 0.20f, -0.08f, 0.07f}, 0.006f, kPlasticBlack);
        p.RBox({s * 0.21f, -0.08f, 0.075f}, {0.03f, 0.022f, 0.034f}, kAmber, kNoRot, 0.4f);
    }
    return b;
}

} // namespace

void GenTrailMeshes(Mesh* out)
{
    out[BikeMesh::Swingarm] = BuildSwingarm(kFrameBlack).Build();
    out[BikeMesh::ShockBody] = BuildShockBody().Build();
    out[BikeMesh::ShockSpring] = BuildShockSpring().Build();
    out[BikeMesh::ShockLower] = BuildShockLower().Build();
    out[BikeMesh::ForkLower] = BuildTrailForkLower().Build();
    out[BikeMesh::Cockpit] = BuildTrailCockpit().Build();
    out[BikeMesh::WheelFront] = BuildWheel(true).Build();
    out[BikeMesh::WheelRear] = BuildWheel(false).Build();
    out[BikeMesh::TireFront] = BuildTire(BikeMesh::kFrontRadius, 0.046f, 0.040f, 0.267f, 56, 0.6f).Build();   // mixta: tacos más bajos
    out[BikeMesh::TireRear] = BuildTire(BikeMesh::kRearRadius, 0.062f, 0.046f, 0.243f, 54, 0.6f).Build();
}

void GenTrailLiveryMeshes(const BikeLivery& L, Mesh* out)
{
    out[0] = BuildFrontFender(L).Build();
    out[1] = BuildTrailForkUpper(L).Build();
}

// Chasis de la trilheira: cuna de acero negra, monocilíndrico enfriado por aire (aletas grandes),
// tanque grande con franjas, asiento largo y plano, tapas laterales con "450", parrilla (bagageiro),
// escape esportivo cromado levantado, cubrecárter y la patente del Mercosur bien levantada.
Mesh GenTrailBody(Vector3 SH, Vector3 up, const BikeLivery& L)
{
    MeshBuilder b;
    Parts p{b};
    const Mat plastic = {L.plastic, 0.6f}, stripeA = {L.stripe1, 0.55f}, stripeB = {L.stripe2, 0.55f};
    auto onAxis = [&](float d) { return Vector3Add(SH, Vector3Scale(up, d)); };

    // Chasis: pipa, espina hasta el basculante, caño delantero y cuna doble bajo el motor, subchasis.
    p.Rod(onAxis(-0.02f), onAxis(-0.19f), 0.036f, kFrameBlack);
    p.Tube({Vector3Add(onAxis(-0.06f), {0.0f, 0.0f, -0.03f}), {0.0f, 0.075f, 0.30f}, {0.0f, 0.06f, 0.05f}, {0.0f, -0.02f, -0.10f},
            {0.0f, -0.16f, -0.13f}}, 0.028f, kFrameBlack);
    p.Tube({Vector3Add(onAxis(-0.17f), {0.0f, 0.0f, -0.01f}), {0.0f, -0.12f, 0.43f}, {0.0f, -0.26f, 0.33f}}, 0.026f, kFrameBlack);
    for (float s : {-1.0f, 1.0f}) {
        p.Tube({{0.0f, -0.26f, 0.33f}, {s * 0.06f, -0.40f, 0.24f}, {s * 0.065f, -0.49f, 0.10f}, {s * 0.07f, -0.49f, -0.05f},
                {s * 0.085f, -0.40f, -0.12f}, {s * 0.095f, -0.30f, -0.13f}, {s * 0.08f, -0.10f, -0.12f}}, 0.017f, kFrameBlack);
        p.RBox({s * 0.10f, -0.26f, -0.13f}, {0.03f, 0.13f, 0.10f}, kFrameBlack);
        p.Tube({{s * 0.05f, 0.03f, -0.06f}, {s * 0.08f, 0.06f, -0.30f}, {s * 0.075f, 0.075f, -0.64f}}, 0.013f, kFrameBlack);
        p.Tube({{s * 0.09f, -0.20f, -0.14f}, {s * 0.09f, -0.08f, -0.34f}, {s * 0.075f, 0.06f, -0.60f}}, 0.012f, kFrameBlack);
        p.DiscX({s * 0.125f, -0.27f, -0.13f}, 0.017f, 0.01f, kSteel);
    }
    p.Rod({-0.125f, -0.27f, -0.13f}, {0.125f, -0.27f, -0.13f}, 0.012f, kSteel);
    p.Rod({-0.09f, 0.035f, -0.17f}, {0.09f, 0.035f, -0.17f}, 0.012f, kFrameBlack);
    p.RBox({0.0f, 0.03f, -0.17f}, {0.05f, 0.03f, 0.045f}, kFrameBlack);

    // Motor enfriado por aire: cárter, cilindro inclinado con aletas grandes, tapa con aletas,
    // carburador atrás y radiador de aceite chico adelante.
    p.RBox({0.0f, -0.37f, 0.02f}, {0.20f, 0.22f, 0.34f}, kEngine);
    p.DiscX({-0.108f, -0.37f, 0.0f}, 0.095f, 0.03f, kCover);
    p.DiscX({0.103f, -0.35f, 0.08f}, 0.072f, 0.034f, kCover);
    const Quaternion tilt = RotX(16.0f);
    const Vector3 cylAxis = Vector3RotateByQuaternion({0.0f, 1.0f, 0.0f}, tilt);
    const Vector3 cylBase = {0.0f, -0.19f, 0.10f};
    p.RBox(cylBase, {0.12f, 0.17f, 0.12f}, kEngine, tilt, 0.3f);
    for (int k = 0; k < 7; ++k)
        p.RBox(Vector3Add(cylBase, Vector3Scale(cylAxis, -0.07f + 0.026f * (float)k)), {0.19f, 0.008f, 0.18f}, kEngineFin, tilt, 0.25f);
    const Vector3 head = Vector3Add(cylBase, Vector3Scale(cylAxis, 0.13f));
    p.RBox(head, {0.16f, 0.08f, 0.15f}, kEngine, tilt, 0.3f);
    for (int k = 0; k < 3; ++k) p.RBox(Vector3Add(head, Vector3Scale(cylAxis, -0.02f + 0.025f * (float)k)), {0.2f, 0.007f, 0.19f}, kEngineFin, tilt, 0.25f);
    p.Rod({0.0f, -0.12f, 0.02f}, {0.0f, -0.10f, -0.08f}, 0.03f, kCover);                           // carburador
    p.RBox({0.0f, -0.10f, -0.12f}, {0.06f, 0.06f, 0.06f}, kPlasticBlack, kNoRot, 0.4f);
    p.RBox({0.0f, -0.10f, 0.40f}, {0.16f, 0.09f, 0.03f}, kRadiator, RotX(-10.0f), 0.2f);          // radiador de aceite
    p.RBox({0.0f, -0.52f, 0.05f}, {0.22f, 0.02f, 0.40f}, kAluBright, kNoRot, 0.25f);               // cubrecárter de aluminio
    p.RBox({0.0f, -0.03f, -0.33f}, {0.17f, 0.15f, 0.2f}, kPlasticBlack, kNoRot, 0.4f);             // caja de filtro
    for (float s : {-1.0f, 1.0f}) {
        p.RBox({s * 0.175f, -0.40f, -0.10f}, {0.09f, 0.03f, 0.055f}, kRubber, kNoRot, 0.3f);       // estriberas con goma
        p.RBox({s * 0.12f, -0.40f, -0.10f}, {0.04f, 0.04f, 0.045f}, kAlu, kNoRot, 0.4f);
        p.Rod({s * 0.11f, -0.25f, -0.42f}, {s * 0.20f, -0.26f, -0.44f}, 0.012f, kFrameBlack);      // pedaleras del acompañante
        p.RBox({s * 0.21f, -0.26f, -0.44f}, {0.05f, 0.025f, 0.035f}, kRubber, kNoRot, 0.3f);
    }
    p.Tube({{-0.115f, -0.36f, -0.13f}, {-0.13f, -0.40f, -0.04f}, {-0.14f, -0.42f, 0.05f}}, 0.008f, kSteel);   // freno
    p.Tube({{0.105f, -0.31f, -0.03f}, {0.13f, -0.35f, 0.03f}, {0.15f, -0.365f, 0.075f}}, 0.007f, kSteel);      // cambios
    p.Tube({{0.10f, -0.45f, -0.16f}, {0.13f, -0.47f, -0.28f}, {0.14f, -0.40f, -0.40f}}, 0.009f, kFrameBlack);  // pata (levantada)
    p.RBox({0.075f, -0.03f, -0.36f}, {0.012f, 0.05f, 0.30f}, kPlasticBlack, RotX(8.0f), 0.3f);                // cubrecadena

    // Escape esportivo: colector cromado por la derecha y silenciador corto levantado con ponteira.
    Sweep header;
    header.path = {{-0.03f, -0.10f, 0.21f}, {-0.07f, -0.20f, 0.27f}, {-0.11f, -0.31f, 0.20f}, {-0.135f, -0.36f, 0.06f},
                   {-0.145f, -0.32f, -0.08f}, {-0.155f, -0.20f, -0.22f}, {-0.165f, -0.10f, -0.36f}};
    header.profile = Circle(0.021f, 10);
    header.transport = true;
    header.caps = false;
    header.color = [](float t, int) {
        const Color hot = {212, 160, 90, 255}, blue = {110, 96, 160, 255}, chrome = {214, 216, 222, 255};
        return t < 0.25f ? Mix(hot, blue, t / 0.25f) : Mix(blue, chrome, (t - 0.25f) / 0.25f);
    };
    p.Add(BuildSweep(header), {WHITE, 0.9f});
    Sweep muffler;
    muffler.path = {{-0.165f, -0.10f, -0.36f}, {-0.178f, -0.02f, -0.52f}, {-0.192f, 0.06f, -0.72f}};
    muffler.profile = Circle(0.052f, 18);
    muffler.scale = [](float t) {
        const float k = 0.45f + 0.55f * Smooth(0.0f, 0.15f, t);
        return Vector2{k, k};
    };
    muffler.edges = {0.9f};
    muffler.color = [](float t, int) { return t > 0.9f ? Color{214, 216, 222, 255} : Color{34, 34, 38, 255}; };   // fibra negra, boca cromada
    p.Add(BuildSweep(muffler), {WHITE, 0.8f});
    p.Rod({-0.192f, 0.06f, -0.72f}, {-0.195f, 0.075f, -0.765f}, 0.022f, kChrome);                 // ponteira
    p.Rod({-0.195f, 0.075f, -0.765f}, {-0.196f, 0.078f, -0.772f}, 0.016f, kPlasticBlack);

    // Tanque grande con franjas (colores de la calle) y tapa.
    Sweep tank;
    tank.path = {{0.0f, 0.12f, 0.46f}, {0.0f, 0.115f, 0.30f}, {0.0f, 0.10f, 0.10f}, {0.0f, 0.09f, -0.03f}};
    tank.profile = Superellipse(0.14f, 0.085f, 3.0f, 24);
    tank.scale = [](float t) { return Vector2{0.72f + 0.28f * Smooth(0.0f, 0.35f, t), 1.0f - 0.12f * t}; };
    const Profile tankProfile = tank.profile;
    tank.color = [tankProfile, a = L.stripe1, bb = L.stripe2, base = L.plastic](float t, int j) {
        const Vector2 q = tankProfile[j];
        const float band = q.y + 0.25f * (t - 0.4f);                    // franja en diagonal a los costados
        if (std::fabs(q.x) > 0.06f && band > -0.01f && band < 0.012f) return a;
        if (std::fabs(q.x) > 0.06f && band > 0.012f && band < 0.022f) return bb;
        return base;
    };
    p.Add(BuildSweep(tank), {WHITE, 0.6f});
    p.Rod({0.0f, 0.195f, 0.28f}, {0.0f, 0.215f, 0.28f}, 0.03f, kChrome);

    // Asiento largo y plano (hasta la parrilla), con vivo del color de la moto.
    Sweep seat;
    seat.path = {{0.0f, 0.13f, 0.05f}, {0.0f, 0.112f, -0.10f}, {0.0f, 0.112f, -0.35f}, {0.0f, 0.12f, -0.55f}};
    seat.profile = Superellipse(0.12f, 0.05f, 4.0f, 28);
    seat.scale = [](float t) { return Vector2{0.55f + 0.45f * Smooth(0.0f, 0.2f, t), 0.8f + 0.2f * Smooth(0.0f, 0.2f, t)}; };
    const Profile seatProfile = seat.profile;
    seat.color = [seatProfile, band = L.plastic](float, int j) {
        const Vector2 q = seatProfile[j];
        return (q.y < -0.012f && q.y > -0.034f && std::fabs(q.x) > 0.095f) ? band : kSeat.c;
    };
    p.Add(BuildSweep(seat), {WHITE, kSeat.gloss});

    // Guardabarros trasero, parrilla (bagageiro) de caño, luz de freno, guiños y la patente del
    // Mercosur levantada (blanca con la franja azul arriba).
    Sweep rear;
    rear.path = {{0.0f, 0.08f, -0.40f}, {0.0f, 0.10f, -0.62f}, {0.0f, 0.115f, -0.82f}, {0.0f, 0.12f, -0.93f}};
    rear.profile = Arch(0.105f, 0.03f, 0.007f);
    rear.scale = [](float t) { return Vector2{1.0f - 0.25f * t, 1.0f}; };
    p.Add(BuildSweep(rear), plastic);
    for (float s : {-1.0f, 1.0f}) {
        p.Tube({{s * 0.09f, 0.14f, -0.50f}, {s * 0.11f, 0.17f, -0.60f}, {s * 0.11f, 0.17f, -0.86f}, {s * 0.06f, 0.17f, -0.90f}}, 0.009f, kFrameBlack);
        p.Rod({s * 0.10f, 0.08f, -0.62f}, {s * 0.11f, 0.17f, -0.63f}, 0.008f, kFrameBlack);
        p.Rod({s * 0.06f, 0.12f, -0.90f}, {s * 0.13f, 0.12f, -0.92f}, 0.005f, kPlasticBlack);    // guiños traseros
        p.RBox({s * 0.14f, 0.12f, -0.925f}, {0.025f, 0.02f, 0.03f}, kAmber, kNoRot, 0.4f);
    }
    for (int k = 0; k < 4; ++k) p.Bar({-0.11f, 0.17f, -0.64f - 0.07f * k}, {0.11f, 0.17f, -0.64f - 0.07f * k}, 0.008f, 0.008f, kFrameBlack);
    p.Bar({-0.06f, 0.17f, -0.90f}, {0.06f, 0.17f, -0.90f}, 0.009f, 0.009f, kFrameBlack);
    p.RBox({0.0f, 0.13f, -0.905f}, {0.1f, 0.04f, 0.04f}, kTailRed, kNoRot, 0.4f);
    // Protector de rabeta: caño de acero debajo de la patente, lo que raspa (y saca chispas) en el grau pasado.
    for (float s : {-1.0f, 1.0f}) p.Rod({s * 0.06f, 0.12f, -0.90f}, {s * 0.065f, -0.07f, -1.0f}, 0.009f, kSteel);
    p.Rod({-0.075f, -0.07f, -1.005f}, {0.075f, -0.07f, -1.005f}, 0.012f, kSteel);
    const Quaternion plateTilt = RotX(-38.0f);                                                     // bem empinada
    const Vector3 plate = {0.0f, 0.06f, -0.975f};
    p.Tube({{0.0f, 0.11f, -0.90f}, {0.0f, 0.075f, -0.95f}}, 0.008f, kFrameBlack);
    p.RBox(plate, {0.20f, 0.13f, 0.006f}, {{240, 240, 236, 255}, 0.5f}, plateTilt, 0.1f);
    p.RBox(Vector3Add(plate, Vector3RotateByQuaternion({0.0f, 0.048f, -0.004f}, plateTilt)), {0.20f, 0.032f, 0.005f}, {{0, 51, 153, 255}, 0.5f}, plateTilt, 0.1f);
    for (int k = 0; k < 7; ++k)                                                                     // letras y números
        p.RBox(Vector3Add(plate, Vector3RotateByQuaternion({-0.075f + 0.025f * k, -0.012f, -0.005f}, plateTilt)), {0.014f, 0.05f, 0.004f},
               {{24, 24, 28, 255}, 0.3f}, plateTilt, 0.1f);

    // Tapas laterales con "450" y cachas del tanque.
    const Profile sideCover = {{-0.12f, 0.08f}, {-0.46f, 0.10f}, {-0.50f, 0.05f}, {-0.40f, -0.10f}, {-0.18f, -0.12f}, {-0.10f, -0.02f}};
    auto sideOff = [](float u, float) { const float k = (u + 0.31f) / 0.2f; return 0.118f + 0.006f * Clamp01(1.0f - k * k); };
    const Profile shroud = {{0.08f, 0.14f}, {0.34f, 0.16f}, {0.44f, 0.06f}, {0.40f, -0.06f}, {0.28f, -0.12f}, {0.12f, -0.08f}};
    auto shroudOff = [](float u, float v) { return 0.14f + 0.03f * Clamp01((0.14f - v) / 0.3f) + 0.015f * Clamp01((u - 0.1f) / 0.3f); };
    auto decalOff = [shroudOff](float u, float v) { return shroudOff(u, v) + 0.0035f; };
    const Profile stripe = {{0.16f, 0.148f}, {0.40f, 0.12f}, {0.41f, 0.09f}, {0.17f, 0.118f}};
    for (float s : {-1.0f, 1.0f}) {
        const Matrix toSide = Basis({0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {s, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f});
        p.Add(Panel(sideCover, 0.012f, sideOff), plastic, toSide);
        p.Add(Panel(shroud, 0.012f, shroudOff), plastic, toSide);
        p.Add(Panel(stripe, 0.003f, decalOff, 2), stripeA, toSide);
        const Vector3 at = {s * (sideOff(-0.31f, 0.0f) + 0.003f), 0.0f, -0.31f};
        p.Add(Digits("450"), stripeB,
              MatrixMultiply(MatrixMultiply(MatrixScale(0.07f, 0.07f, 0.07f), MatrixRotateY(s * kPi * 0.5f)), MatrixTranslate(at.x, at.y, at.z)));
    }
    return b.Build();
}

// ==== BEGIN RACE (moto de carreras: su sección, ver RaceStyleDef) ====
// Moto de carreras (MotoGP / superbike): carenado entero con parabrisas, alerones y salidas de aire,
// semimanillares bajos, tanque esculpido, colín alto con asiento individual y números, escape de 4
// cilindros al costado, chasis doble viga de aluminio a la vista, horquilla invertida con dos discos
// grandes y pinzas radiales, basculante con refuerzo arriba y llantas de 17" con slicks anchos.
// Las medidas son las de mods/base/bikes/carrera.ini: lanzamiento 24°, cubiertas de 0.30 / 0.31 m y
// el centro de masa a 0.62 m del piso (el piso queda en y = -0.62). Carenado, tanque y colín son
// láminas sobre superficies paramétricas (Shell): así las franjas y los números se pegan encima.
namespace {

constexpr float kRaceRake = 24.0f * DEG2RAD;                // el de carrera.ini (semimanillares sobre la horquilla)
constexpr float kRaceFrontR = 0.30f, kRaceRearR = 0.31f;    // radios reales de los slicks
constexpr float kRaceArmStretch = 0.611f / BikeMesh::kSwingarmLength;   // basculante real / el de la malla

const Mat kCarbon = {{34, 35, 40, 255}, 0.75f};             // fibra de carbono laqueada
const Mat kRaceWheel = {{26, 26, 30, 255}, 0.6f};           // llantas forjadas negras
const Mat kRaceCaliper = {{200, 158, 58, 255}, 0.65f};      // pinzas radiales doradas
const Mat kRaceFrame = {{126, 130, 138, 255}, 0.55f};       // aluminio cepillado del chasis
const Mat kTitanium = {{118, 114, 118, 255}, 0.75f};
const Mat kRaceScreen = {{46, 54, 66, 255}, 0.97f};         // parabrisas ahumado
const Mat kVent = {{14, 14, 16, 255}, 0.2f};                // salidas de aire (negro mate)
const Mat kSlick = {{40, 39, 40, 255}, 0.08f};
const Mat kWhite = {{242, 242, 238, 255}, 0.5f};

// ------------------------------------------------------------------------ láminas paramétricas
using Surf = std::function<Vector3(float, float)>;

float Wrap01(float t) { return t - std::floor(t); }

// Normal (sin orientar) por diferencias; wrapT: la superficie se cierra en t (vuelta entera). En un
// borde colapsado (punta de un disco) la toma un poco más adentro.
Vector3 RawNormal(const Surf& f, float s, float t, bool wrapT = false)
{
    for (int k = 0; k < 2; ++k) {
        const float ss = k == 0 ? s : std::clamp(s, 0.04f, 0.96f), tt = k == 0 ? t : std::clamp(t, 0.04f, 0.96f);
        const float e = 1e-3f;
        const float sa = std::max(0.0f, ss - e), sb = std::min(1.0f, ss + e);
        const Vector3 dt = wrapT ? Vector3Subtract(f(ss, Wrap01(tt + e)), f(ss, Wrap01(tt - e + 1.0f)))
                                 : Vector3Subtract(f(ss, std::min(1.0f, tt + e)), f(ss, std::max(0.0f, tt - e)));
        const Vector3 n = Vector3CrossProduct(Vector3Subtract(f(sb, tt), f(sa, tt)), dt);
        if (Vector3Length(n) > 1e-12f) return Vector3Normalize(n);
    }
    return {0.0f, 1.0f, 0.0f};
}

// Hacia afuera = lejos del eje z que pasa por (0, axisY): sirve para todo lo que envuelve la moto.
float OutSign(const Surf& f, float axisY, bool wrapT)
{
    float sum = 0.0f;
    for (int i = 0; i <= 8; ++i)
        for (int j = 0; j <= 8; ++j) {
            const Vector3 p = f(i / 8.0f, j / 8.0f);
            sum += Vector3DotProduct(RawNormal(f, i / 8.0f, j / 8.0f, wrapT), {p.x, p.y - axisY, 0.0f});
        }
    return sum >= 0.0f ? 1.0f : -1.0f;
}

// Lámina de espesor th sobre f(s, t) (ns x nt celdas): cara de afuera, cara de adentro th más adentro
// y cantos. color(punto, s, t) pinta los vértices; wrapT = superficie cerrada en t (sin cantos ahí).
MeshBuilder Shell(const Surf& f, int ns, int nt, float th, float axisY,
                  const std::function<Color(Vector3, float, float)>& color = {}, bool wrapT = false)
{
    const float sg = OutSign(f, axisY, wrapT);
    const int cols = nt + 1;
    std::vector<Vector3> P((size_t)(ns + 1) * cols), N(P.size());
    std::vector<Color> C(P.size(), WHITE);
    for (int i = 0; i <= ns; ++i)
        for (int j = 0; j <= nt; ++j) {
            const float s = (float)i / ns, t = (float)j / nt;
            const int k = i * cols + j;
            P[k] = f(s, t);
            N[k] = Vector3Scale(RawNormal(f, s, t, wrapT), sg);
            if (color) C[k] = color(P[k], s, t);
        }
    MeshBuilder b;
    for (int face = 0; face < 2; ++face) {
        const unsigned short base = (unsigned short)b.VertexCount();
        for (int k = 0; k < (int)P.size(); ++k)
            b.Vertex(face == 0 ? P[k] : Vector3Subtract(P[k], Vector3Scale(N[k], th)), face == 0 ? N[k] : Vector3Negate(N[k]), {0.0f, 0.0f}, C[k]);
        for (int i = 0; i < ns; ++i)
            for (int j = 0; j < nt; ++j) {
                const unsigned short a = (unsigned short)(base + i * cols + j), bb = (unsigned short)(a + 1);
                const unsigned short c = (unsigned short)(a + cols + 1), d = (unsigned short)(a + cols);
                b.Tri(a, bb, c);
                b.Tri(a, c, d);
            }
    }
    // Canto de un borde: tira entre las dos caras, con la normal hacia afuera del parche.
    auto rim = [&](int i0, int j0, int di, int dj, int count, int ii, int ij) {
        const unsigned short base = (unsigned short)b.VertexCount();
        for (int k = 0; k <= count; ++k) {
            const int i = i0 + di * k, j = j0 + dj * k, idx = i * cols + j;
            const int in = std::clamp(i + ii, 0, ns) * cols + std::clamp(j + ij, 0, nt);
            Vector3 e = Vector3Subtract(P[idx], P[in]);
            e = Vector3Subtract(e, Vector3Scale(N[idx], Vector3DotProduct(e, N[idx])));
            e = Vector3Length(e) > 1e-7f ? Vector3Normalize(e) : N[idx];
            b.Vertex(P[idx], e, {0.0f, 0.0f}, C[idx]);
            b.Vertex(Vector3Subtract(P[idx], Vector3Scale(N[idx], th)), e, {0.0f, 0.0f}, C[idx]);
        }
        for (int k = 0; k < count; ++k) {
            const unsigned short a = (unsigned short)(base + 2 * k);
            b.Tri(a, (unsigned short)(a + 2), (unsigned short)(a + 3));
            b.Tri(a, (unsigned short)(a + 3), (unsigned short)(a + 1));
        }
    };
    rim(0, 0, 0, 1, nt, 1, 0);
    rim(ns, 0, 0, 1, nt, -1, 0);
    if (!wrapT) {
        rim(0, 0, 1, 0, ns, 0, 1);
        rim(0, nt, 1, 0, ns, 0, -1);
    }
    b.FixWinding();
    return b;
}

// Calco: la superficie g(p) (p en el plano de sus parámetros) levantada `lift` metros hacia afuera,
// recorriendo el cuadrilátero a-b-c-d de ese plano (u de a a b, v de a a d).
using Surf2 = std::function<Vector3(Vector2)>;
Surf Decal(const Surf2& g, float axisY, Vector2 a, Vector2 b, Vector2 c, Vector2 d, float lift)
{
    return [=](float u, float v) {
        const Vector2 p = Vector2Lerp(Vector2Lerp(a, b, u), Vector2Lerp(d, c, u), v);
        const float e = 1e-3f;
        const Vector3 o = g(p);
        Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(g({p.x + e, p.y}), g({p.x - e, p.y})),
                                                         Vector3Subtract(g({p.x, p.y + e}), g({p.x, p.y - e}))));
        if (Vector3DotProduct(n, {o.x, o.y - axisY, 0.0f}) < 0.0f) n = Vector3Negate(n);
        return Vector3Add(o, Vector3Scale(n, lift));
    };
}
// Disco (elipse de radios r) centrado en c del plano de parámetros de g: u = radio, v = vuelta.
Surf DiscDecal(const Surf2& g, float axisY, Vector2 c, Vector2 r, float lift)
{
    const Surf flat = Decal(g, axisY, {c.x - r.x, c.y - r.y}, {c.x + r.x, c.y - r.y}, {c.x + r.x, c.y + r.y}, {c.x - r.x, c.y + r.y}, lift);
    return [=](float u, float v) {
        return flat(0.5f + 0.5f * u * std::cos(kTau * v), 0.5f + 0.5f * u * std::sin(kTau * v));
    };
}

// Tabla (x creciente) con Hermite y tangentes de Catmull-Rom: curvas suaves de medidas.
float Tab(const std::vector<Vector2>& k, float x)
{
    const int n = (int)k.size();
    if (x <= k[0].x) return k[0].y;
    if (x >= k[n - 1].x) return k[n - 1].y;
    int i = 1;
    while (k[i].x < x) ++i;
    const Vector2 p1 = k[i - 1], p2 = k[i], p0 = i >= 2 ? k[i - 2] : p1, p3 = i + 1 < n ? k[i + 1] : p2;
    const float h = p2.x - p1.x, u = (x - p1.x) / h, u2 = u * u, u3 = u2 * u;
    const float m1 = (p2.y - p0.y) / (p2.x - p0.x), m2 = (p3.y - p1.y) / (p3.x - p1.x);
    return (2 * u3 - 3 * u2 + 1) * p1.y + (u3 - 2 * u2 + u) * h * m1 + (-2 * u3 + 3 * u2) * p2.y + (u3 - u2) * h * m2;
}

float SgnPow(float v, float p) { return (v < 0.0f ? -1.0f : 1.0f) * std::pow(std::fabs(v), p); }

// ------------------------------------------------------------------------ carenado
// Trompa (A): arco de semiancho W, centro Yc y semialto H en cada z; phi = 0 arriba, + a la izquierda.
// Va de la punta (z 0.86, la toma de aire) hasta atrás del parabrisas y baja por los costados hasta
// y ~0.09, arriba de la rueda (la rueda gira y sube por debajo sin tocarla).
constexpr float kCowlPhi = 112.0f * DEG2RAD, kCowlE = 0.8f, kNoseZ = 0.86f;
float CowlW(float z) { static const std::vector<Vector2> k = {{0.36f, 0.250f}, {0.48f, 0.242f}, {0.58f, 0.228f}, {0.68f, 0.200f}, {0.76f, 0.160f}, {0.82f, 0.115f}, {0.86f, 0.072f}}; return Tab(k, z); }
float CowlYc(float z) { static const std::vector<Vector2> k = {{0.36f, 0.165f}, {0.46f, 0.160f}, {0.58f, 0.155f}, {0.70f, 0.145f}, {0.80f, 0.130f}, {0.86f, 0.115f}}; return Tab(k, z); }
float CowlH(float z) { static const std::vector<Vector2> k = {{0.36f, 0.150f}, {0.48f, 0.150f}, {0.58f, 0.140f}, {0.68f, 0.122f}, {0.76f, 0.100f}, {0.82f, 0.075f}, {0.86f, 0.050f}}; return Tab(k, z); }
Vector3 CowlAt(Vector2 zp)
{
    const float z = zp.x, phi = zp.y;
    return {CowlW(z) * SgnPow(std::sin(phi), kCowlE), CowlYc(z) + CowlH(z) * SgnPow(std::cos(phi), kCowlE), z};
}
float CowlRearZ(float phi) { return 0.48f - 0.12f * std::pow(std::fabs(phi) / kCowlPhi, 1.5f); }
Vector3 Cowl(float s, float t)
{
    const float phi = (2.0f * t - 1.0f) * kCowlPhi;
    return CowlAt({kNoseZ + (CowlRearZ(phi) - kNoseZ) * std::pow(s, 1.3f), phi});
}
// Altura de la trompa en (z, x): sobre ella se apoya el parabrisas.
float CowlTopY(float z, float x)
{
    const float u = std::min(std::fabs(x) / CowlW(z), 0.999f);
    const float sinPhi = std::pow(u, 1.0f / kCowlE);
    return CowlYc(z) + CowlH(z) * std::pow(std::sqrt(std::max(0.0f, 1.0f - sinPhi * sinPhi)), kCowlE);
}
Vector3 Screen(float s, float t)
{
    const float z = 0.73f + (0.50f - 0.73f) * s, w = 2.0f * t - 1.0f;
    const float x = CowlW(z) * (0.56f + 0.14f * s) * w;
    const float bulge = (0.012f + 0.095f * std::pow(s, 1.35f)) * std::pow(std::max(0.0f, 1.0f - w * w), 0.7f);
    return {x, CowlTopY(z, x) + 0.005f + bulge, z};
}

// Costados y panza (B), uno por lado: en cada altura y el semiancho es W(z)·xs(y) (superelipse:
// abombado a la altura del radiador y cerrado abajo del motor, en y = -0.47, lo justo para inclinar
// ~60° sin tocar). El borde de adelante sigue la rueda y el de atrás baja en diagonal hacia las estriberas
// (queda adelante de la rodilla y la pierna del piloto).
constexpr float kSideTop = 0.235f, kSideHt = 0.30f, kSideHb = 0.47f, kSideNu = 3.0f, kSideNl = 2.4f;
float SideW(float z) { static const std::vector<Vector2> k = {{-0.16f, 0.212f}, {0.0f, 0.232f}, {0.15f, 0.245f}, {0.30f, 0.250f}, {0.45f, 0.246f}, {0.55f, 0.232f}, {0.66f, 0.200f}}; return Tab(k, z); }
float SideXs(float y)
{
    const float q = std::min(y >= 0.0f ? y / kSideHt : -y / kSideHb, 1.0f), n = y >= 0.0f ? kSideNu : kSideNl;
    return std::pow(std::max(0.0f, 1.0f - std::pow(q, n)), 1.0f / n);
}
float SideY(float t)
{
    const float a0 = std::acos(std::pow(kSideTop / kSideHt, kSideNu / 2.0f)), c = std::cos(a0 + (kPi - a0) * t);
    return c >= 0.0f ? kSideHt * std::pow(c, 2.0f / kSideNu) : -kSideHb * std::pow(-c, 2.0f / kSideNl);
}
float SideFrontZ(float y)
{
    const float dy = y + 0.32f, r = 0.33f;
    return dy * dy < r * r ? std::min(0.64f, 0.68f - std::sqrt(r * r - dy * dy)) : 0.64f;
}
float SideRearZ(float y) { static const std::vector<Vector2> k = {{-0.47f, -0.14f}, {-0.32f, -0.10f}, {-0.20f, -0.03f}, {-0.08f, 0.07f}, {0.03f, 0.19f}, {0.10f, 0.27f}, {0.174f, 0.33f}, {0.235f, 0.37f}}; return Tab(k, y); }
Vector3 SideAt(float sg, Vector2 zy) { return {sg * SideW(zy.x) * SideXs(zy.y), zy.y, zy.x}; }
Surf Side(float sg)
{
    return [sg](float s, float t) {
        const float y = SideY(t);
        return SideAt(sg, {SideRearZ(y) + (SideFrontZ(y) - SideRearZ(y)) * s, y});
    };
}

// ------------------------------------------------------------------------ tanque y colín
// Secciones superelípticas cerradas alrededor del eje z: semiancho, arriba y abajo según z.
struct RingDef {
    std::vector<Vector2> w, top, bottom;
    float n;
};
Vector3 RingAt(const RingDef& r, float z, float t)
{
    const float a = kTau * t, top = Tab(r.top, z), bottom = Tab(r.bottom, z);
    return {Tab(r.w, z) * SgnPow(std::sin(a), 2.0f / r.n), 0.5f * (top + bottom) + 0.5f * (top - bottom) * SgnPow(std::cos(a), 2.0f / r.n), z};
}
// x de la cara de costado del anillo a la altura y (para pegar los números del colín).
float RingX(const RingDef& r, float z, float y)
{
    const float top = Tab(r.top, z), bottom = Tab(r.bottom, z), q = std::fabs((y - 0.5f * (top + bottom)) / (0.5f * (top - bottom)));
    return Tab(r.w, z) * std::pow(std::max(0.0f, 1.0f - std::pow(std::min(q, 1.0f), r.n)), 1.0f / r.n);
}
const RingDef& TankDef()
{
    static const RingDef d = {{{-0.07f, 0.110f}, {0.03f, 0.140f}, {0.14f, 0.158f}, {0.26f, 0.155f}, {0.34f, 0.118f}},
                              {{-0.07f, 0.222f}, {0.02f, 0.262f}, {0.12f, 0.295f}, {0.24f, 0.300f}, {0.34f, 0.272f}},
                              {{-0.07f, 0.140f}, {0.05f, 0.120f}, {0.20f, 0.125f}, {0.34f, 0.150f}}, 3.0f};
    return d;
}
const RingDef& TailDef()
{
    static const RingDef d = {{{-0.86f, 0.022f}, {-0.78f, 0.055f}, {-0.66f, 0.088f}, {-0.54f, 0.105f}, {-0.44f, 0.110f}, {-0.30f, 0.108f}, {-0.10f, 0.105f}},
                              {{-0.86f, 0.272f}, {-0.78f, 0.292f}, {-0.66f, 0.302f}, {-0.56f, 0.296f}, {-0.48f, 0.262f}, {-0.43f, 0.212f}, {-0.30f, 0.188f}, {-0.10f, 0.185f}},
                              {{-0.86f, 0.240f}, {-0.76f, 0.205f}, {-0.62f, 0.160f}, {-0.48f, 0.118f}, {-0.30f, 0.095f}, {-0.10f, 0.110f}}, 4.0f};
    return d;
}
// El anillo de z0 a z1; las puntas se cierran achicando la sección (tip0/tip1 = largo del redondeo).
Surf Ring(const RingDef& r, float z0, float z1, float tip0, float tip1)
{
    return [&r, z0, z1, tip0, tip1](float s, float t) {
        const float z = z0 + (z1 - z0) * s, len = std::fabs(z1 - z0);
        auto round = [](float d, float tip) {
            return tip > 0.0f ? std::sqrt(std::max(0.0f, 1.0f - std::pow(std::max(0.0f, 1.0f - d / tip), 2.0f))) : 1.0f;
        };
        const float k = 0.12f + 0.88f * std::min(round(s * len, tip0), round((1.0f - s) * len, tip1));
        const Vector3 q = RingAt(r, z, t);
        const float yc = 0.5f * (Tab(r.top, z) + Tab(r.bottom, z));
        return Vector3{q.x * k, yc + (q.y - yc) * k, z};
    };
}

// Número con su orientación: centro, "derecha" de la lectura, "arriba" y la normal (hacia afuera).
void AddNumber(Parts& p, const char* text, const Mat& m, Vector3 at, Vector3 right, Vector3 up, float h)
{
    const Vector3 n = Vector3Normalize(Vector3CrossProduct(right, up));
    p.Add(Digits(text), m, Basis(Vector3Scale(Vector3Normalize(right), h), Vector3Scale(Vector3Normalize(up), h), Vector3Scale(n, h), at));
}

// ------------------------------------------------------------------------ ruedas
// Llanta forjada de 5 rayos partidos, masa, y (adelante) dos discos flotantes de 330 mm o (atrás) disco
// chico a la derecha y corona a la izquierda. Se arma en medidas reales y se escala al radio nominal
// de la malla (Bike::Draw la vuelve a escalar al radio de la física).
void AddRaceDisc(Parts& p, float x, float rIn, float rOut, int holes)
{
    const float h = 0.0025f, r1 = rIn + 0.3f * (rOut - rIn), r2 = rIn + 0.7f * (rOut - rIn);
    const Profile prof = {{x - h, rIn}, {x - h, r1}, {x - h, r2}, {x - h, rOut}, {x + h, rOut}, {x + h, r2}, {x + h, r1}, {x + h, rIn}};
    p.Add(Revolve(prof, holes * 4, [](int s, int j) {
              const bool hole = ((j == 1 || j == 6) && s % 4 == 0) || ((j == 2 || j == 5) && s % 4 == 2);
              return hole ? Color{36, 36, 40, 255} : WHITE;
          }),
          kDisc);
    // Portadiscos con botones flotantes.
    const float cx = x + (x > 0.0f ? -0.004f : 0.004f);
    for (int i = 0; i < 6; ++i) {
        const float a = kTau * (float)i / 6.0f;
        p.Bar({cx, 0.055f * std::cos(a), 0.055f * std::sin(a)}, {cx, (rIn + 0.004f) * std::cos(a + 0.2f), (rIn + 0.004f) * std::sin(a + 0.2f)}, 0.006f, 0.016f, kHub);
    }
    for (int i = 0; i < 10; ++i) {
        const float a = kTau * (float)i / 10.0f;
        p.DiscX({x, (rIn + 0.002f) * std::cos(a), (rIn + 0.002f) * std::sin(a)}, 0.0065f, 0.009f, kSteel);
    }
}

MeshBuilder BuildRaceWheel(bool front)
{
    MeshBuilder real;
    Parts p{real};
    const float hw = front ? 0.046f : 0.078f;
    const Profile rim = {{-hw, 0.228f}, {-hw, 0.219f}, {-hw + 0.007f, 0.214f}, {-hw * 0.45f, 0.205f},
                         {hw * 0.45f, 0.205f}, {hw - 0.007f, 0.214f}, {hw, 0.219f}, {hw, 0.228f}};
    p.Add(Revolve(rim, 72, [](int, int j) { return (j == 0 || j == 7) ? Color{200, 30, 36, 255} : WHITE; }), kRaceWheel);   // filete rojo en el borde
    const float hx = front ? 0.062f : 0.085f;
    const Profile hub = {{-hx, 0.014f}, {-hx, 0.034f}, {-hx * 0.6f, 0.05f}, {hx * 0.6f, 0.05f}, {hx, 0.034f}, {hx, 0.014f}};
    p.Add(Revolve(hub, 32), kRaceWheel);
    for (int i = 0; i < 5; ++i) {                                    // rayos partidos: dos brazos por rayo
        const float a = kTau * (float)i / 5.0f;
        for (float d : {-0.11f, 0.11f}) {
            const Vector3 atHub = {0.0f, 0.048f * std::cos(a), 0.048f * std::sin(a)};
            const Vector3 atRim = {0.0f, 0.207f * std::cos(a + d), 0.207f * std::sin(a + d)};
            p.Bar(atHub, atRim, front ? 0.016f : 0.022f, 0.015f, kRaceWheel);
        }
    }
    if (front) {
        AddRaceDisc(p, 0.078f, 0.117f, 0.165f, 24);
        AddRaceDisc(p, -0.078f, 0.117f, 0.165f, 24);
    } else {
        AddRaceDisc(p, -0.078f, 0.078f, 0.110f, 18);                  // a la derecha
        const float x = 0.105f, h = 0.0025f;                         // corona de 42 dientes (izquierda)
        const Profile ring = {{x - h, 0.058f}, {x - h, 0.082f}, {x - h, 0.108f}, {x + h, 0.108f}, {x + h, 0.082f}, {x + h, 0.058f}};
        p.Add(Revolve(ring, 168, [](int s, int j) { return ((j == 1 || j == 4) && s % 28 < 12) ? Color{60, 44, 20, 255} : WHITE; },
                      [](float a, int j) { return (j == 2 || j == 3) ? 1.0f + 0.03f * std::cos(42.0f * a) : 1.0f; }),
              kGold);
        const Profile carrier = {{x - 0.02f, 0.03f}, {x - 0.02f, 0.06f}, {x, 0.06f}, {x, 0.03f}};
        p.Add(Revolve(carrier, 24), kRaceWheel);
    }
    MeshBuilder b;
    const float k = (front ? BikeMesh::kFrontRadius / kRaceFrontR : BikeMesh::kRearRadius / kRaceRearR);
    b.Append(real, MatrixScale(k, k, k));
    return b;
}

// Slick: banda de rodamiento redonda de radio rc (el "roundness" de la física: el contacto al inclinar
// cae sobre ella), flancos que se abren sobre la llanta y una línea de color del compuesto.
MeshBuilder BuildSlick(bool front)
{
    const float R = front ? kRaceFrontR : kRaceRearR, rc = front ? 0.06f : 0.095f;
    const float maxDeg = front ? 84.0f : 70.0f, hwMax = front ? 0.061f : 0.096f, bead = front ? 0.047f : 0.079f;
    Profile prof;
    const int tread = 22;
    for (int i = 0; i <= tread; ++i) {
        const float ph = (-maxDeg + 2.0f * maxDeg * i / tread) * DEG2RAD;
        prof.push_back({rc * std::sin(ph), R - rc + rc * std::cos(ph)});
    }
    const float yEdge = R - rc + rc * std::cos(maxDeg * DEG2RAD);
    const Profile side = {{hwMax, yEdge - 0.012f}, {hwMax - 0.002f, 0.232f}, {bead + 0.004f, 0.222f}, {bead - 0.004f, 0.214f}};
    for (const Vector2& q : side) prof.push_back(q);
    for (int i = (int)side.size() - 1; i >= 0; --i) prof.insert(prof.begin(), {-side[i].x, side[i].y});
    const int first = (int)side.size(), last = first + tread;
    MeshBuilder real;
    Parts p{real};
    p.Add(Revolve(prof, 72, [first, last](int, int j) {
              if (j >= first && j <= last) return Color{58, 56, 56, 255};                       // banda gastada, más clara
              if (j == first - 1 || j == last + 1) return Color{226, 196, 40, 255};            // línea del compuesto
              return WHITE;
          }),
          kSlick);
    MeshBuilder b;
    const float k = (front ? BikeMesh::kFrontRadius : BikeMesh::kRearRadius) / R;
    b.Append(real, MatrixScale(k, k, k));
    return b;
}

// ------------------------------------------------------------------------ basculante
// Brazos altos con refuerzo arriba (ala de gaviota), travesaño, bieleta, cadena a la izquierda, pinza
// de abajo a la derecha y guardabarros de carbono sobre la rueda. Espacio del basculante (eje trasero
// en z = -kSwingarmLength; Bike::Draw lo estira ~9% en z hasta el eje real).
MeshBuilder BuildRaceSwingarm()
{
    MeshBuilder b;
    Parts p{b};
    const float L = BikeMesh::kSwingarmLength;
    for (float s : {-1.0f, 1.0f}) {
        Sweep arm;
        arm.path = {{s * 0.10f, 0.0f, 0.03f}, {s * 0.125f, 0.0f, -0.10f}, {s * 0.135f, 0.0f, -0.30f}, {s * 0.135f, 0.0f, -(L + 0.035f)}};
        arm.profile = Superellipse(0.0175f, 0.052f, 4.0f, 18);
        arm.scale = [](float t) { return Vector2{1.0f, 1.0f - 0.45f * t}; };
        p.Add(BuildSweep(arm), kRaceFrame);
        Sweep brace;                                                  // refuerzo de arriba
        brace.path = {{s * 0.118f, 0.04f, -0.02f}, {s * 0.13f, 0.095f, -0.16f}, {s * 0.135f, 0.07f, -0.30f}, {s * 0.135f, 0.02f, -0.42f}};
        brace.profile = Superellipse(0.013f, 0.02f, 3.0f, 12);
        brace.transport = true;
        p.Add(BuildSweep(brace), kRaceFrame);
        p.Bar({s * 0.13f, 0.02f, -0.12f}, {s * 0.13f, 0.09f, -0.16f}, 0.02f, 0.015f, kRaceFrame);
        p.Bar({s * 0.135f, 0.02f, -0.26f}, {s * 0.135f, 0.078f, -0.28f}, 0.02f, 0.015f, kRaceFrame);
        p.RBox({s * 0.135f, 0.0f, -L}, {0.038f, 0.04f, 0.07f}, kAluBright, kNoRot, 0.4f);     // tensores
        p.DiscX({s * 0.156f, 0.0f, -L}, 0.017f, 0.01f, kSteel);
    }
    p.RBox({0.0f, 0.005f, -0.05f}, {0.22f, 0.06f, 0.05f}, kRaceFrame, kNoRot, 0.4f);      // travesaño
    p.Rod({-0.152f, 0.0f, -L}, {0.152f, 0.0f, -L}, 0.011f, kSteel);                       // eje
    p.RBox(BikeMesh::kShockLow, {0.05f, 0.04f, 0.05f}, kEngine, kNoRot, 0.4f);           // bieleta
    p.Bar(BikeMesh::kShockLow, {0.0f, -0.035f, -0.04f}, 0.04f, 0.02f, kEngine);

    // Cadena a la izquierda (piñón adelante del eje del basculante, corona de 0.105 m).
    const float cx = 0.105f, rf = 0.036f, rr = 0.105f;
    const Vector3 sprocket = {cx, -0.02f, 0.075f};
    p.Bar({cx, sprocket.y + rf, sprocket.z}, {cx, rr, -L}, 0.012f, 0.012f, kChain);
    p.Bar({cx, sprocket.y - rf, sprocket.z}, {cx, -rr, -L}, 0.012f, 0.012f, kChain);
    p.DiscX(sprocket, rf, 0.012f, kHub);
    p.RBox({cx + 0.004f, 0.05f, -0.18f}, {0.02f, 0.012f, 0.20f}, kCarbon, RotX(-9.0f), 0.3f);   // cubrecadena

    // Pinza trasera (derecha, debajo del disco) con su soporte en el eje.
    p.RBox({-0.078f, -0.084f, -L + 0.028f}, {0.034f, 0.05f, 0.075f}, kRaceCaliper, kNoRot, 0.35f);
    p.Bar({-0.10f, 0.0f, -L}, {-0.10f, -0.07f, -L + 0.05f}, 0.012f, 0.03f, kAluBright);

    // Guardabarros trasero de carbono (hugger): arco sobre la parte de adelante de la rueda.
    Sweep hug;
    for (int i = 0; i <= 8; ++i) {
        const float a = (18.0f + 84.0f * i / 8.0f) * DEG2RAD, r = 0.338f;
        hug.path.push_back({0.0f, r * std::sin(a), -L + r * std::cos(a) / kRaceArmStretch});
    }
    hug.profile = Arch(0.108f, 0.025f, 0.005f);
    p.Add(BuildSweep(hug), kCarbon);
    for (float s : {-1.0f, 1.0f}) p.Bar({s * 0.12f, 0.02f, -0.31f}, {s * 0.105f, 0.20f, -0.40f}, 0.008f, 0.02f, kCarbon);
    return b;
}

// ------------------------------------------------------------------------ horquilla invertida
// Abajo (en el eje): barras finas negras, pies con las pinzas radiales doradas atrás de cada pierna,
// eje, cañerías de freno y guardabarros de carbono (sube y baja con la rueda). Medidas reales.
MeshBuilder BuildRaceForkLower()
{
    MeshBuilder b;
    Parts p{b};
    const float legX = 0.105f;
    for (float s : {-1.0f, 1.0f}) {
        p.Rod({s * legX, 0.04f, 0.0f}, {s * legX, 0.34f, 0.0f}, 0.0245f, kStanchion);           // barras (DLC)
        p.RBox({s * legX, 0.035f, -0.006f}, {0.056f, 0.13f, 0.072f}, kRaceFrame, kNoRot, 0.35f); // pies
        p.RBox({s * legX, 0.10f, -0.004f}, {0.058f, 0.02f, 0.06f}, kHub, kNoRot, 0.35f);
        // Pinza radial: atrás y arriba del eje, abrazando el disco (x = ±0.078, pastillas a r = 0.141).
        const float ang = 122.0f * DEG2RAD, r = 0.141f;
        const Vector3 c = {s * 0.078f, r * std::sin(ang), r * std::cos(ang)};
        const Quaternion q = RotX(-(122.0f - 90.0f));
        p.RBox(c, {0.052f, 0.042f, 0.104f}, kRaceCaliper, q, 0.35f);
        p.RBox(Vector3Add(c, {s * 0.018f, 0.0f, 0.0f}), {0.018f, 0.05f, 0.09f}, kRaceCaliper, q, 0.4f);
        p.Bar(Vector3Add(c, {s * 0.02f, 0.0f, 0.0f}), {s * legX, 0.05f, -0.03f}, 0.02f, 0.03f, kRaceCaliper);   // montaje radial al pie
        p.Tube({Vector3Add(c, {s * 0.02f, 0.025f, 0.02f}), {s * (legX + 0.02f), 0.16f, -0.035f}, {s * (legX + 0.022f), 0.30f, -0.03f}}, 0.0045f, kPlasticBlack);
    }
    p.Rod({-0.128f, 0.0f, 0.0f}, {0.128f, 0.0f, 0.0f}, 0.011f, kSteel);
    p.DiscX({-0.132f, 0.0f, 0.0f}, 0.019f, 0.01f, kSteel);
    p.DiscX({0.132f, 0.0f, 0.0f}, 0.015f, 0.01f, kSteel);
    // Guardabarros: arco a 0.325 m del eje, de adelante (15° sobre la horizontal) hasta pasar arriba.
    Sweep f;
    for (int i = 0; i <= 10; ++i) {
        const float bikeDeg = 12.0f + 108.0f * (float)i / 10.0f, a = bikeDeg * DEG2RAD - kRaceRake, r = 0.326f;
        f.path.push_back({0.0f, r * std::sin(a), r * std::cos(a)});
    }
    f.profile = Arch(0.068f, 0.028f, 0.005f);
    f.scale = [](float t) { return Vector2{0.82f + 0.18f * std::sin(kPi * std::min(1.0f, t * 1.15f)), 1.0f}; };
    p.Add(BuildSweep(f), kCarbon);
    for (float s : {-1.0f, 1.0f}) p.Bar({s * 0.066f, 0.19f, 0.25f}, {s * (legX - 0.02f), 0.12f, 0.025f}, 0.006f, 0.03f, kCarbon);
    return b;
}

// Arriba (desde la pipa, +Y a lo largo de la horquilla): tijas, botellas doradas, tapones con el
// color del esquema y las abrazaderas de los semimanillares debajo de la tija de arriba.
MeshBuilder BuildRaceForkUpper(const BikeLivery& L)
{
    MeshBuilder b;
    Parts p{b};
    p.RBox({0.0f, 0.03f, 0.0f}, {0.29f, 0.03f, 0.10f}, kRaceFrame, kNoRot, 0.3f);
    p.RBox({0.0f, -0.21f, 0.0f}, {0.30f, 0.045f, 0.11f}, kRaceFrame, kNoRot, 0.3f);
    p.Rod({0.0f, 0.045f, 0.0f}, {0.0f, 0.056f, 0.0f}, 0.018f, kSteel);
    for (float s : {-1.0f, 1.0f}) {
        p.Rod({s * 0.105f, 0.07f, 0.0f}, {s * 0.105f, -0.40f, 0.0f}, 0.029f, kGold);                // botellas
        p.Rod({s * 0.105f, -0.39f, 0.0f}, {s * 0.105f, -0.405f, 0.0f}, 0.031f, kHub);
        p.Rod({s * 0.105f, 0.07f, 0.0f}, {s * 0.105f, 0.076f, 0.0f}, 0.024f, Mat{L.stripe1, 0.6f});   // aro del color del esquema
        p.Rod({s * 0.105f, 0.076f, 0.0f}, {s * 0.105f, 0.086f, 0.0f}, 0.021f, kHub);                  // tapones
        p.Rod({s * 0.105f, 0.086f, 0.0f}, {s * 0.105f, 0.092f, 0.0f}, 0.011f, kAluBright);
        p.Rod({s * 0.105f, -0.028f, 0.0f}, {s * 0.105f, -0.072f, 0.0f}, 0.036f, kAluBright);        // abrazaderas
        p.RBox({s * 0.105f, -0.05f, -0.036f}, {0.018f, 0.04f, 0.02f}, kAluBright, kNoRot, 0.3f);
    }
    return b;
}

// Semimanillares, puños, levas (freno a la derecha con su protector y bomba radial, embrague a la
// izquierda) y comandos. Ejes de la moto desde la pipa; la horquilla con el lanzamiento de carrera.ini.
// Centro de los puños: (±0.293, -0.071, -0.022) desde la pipa (RaceStyleDef).
MeshBuilder BuildRaceCockpit()
{
    MeshBuilder b;
    Parts p{b};
    const Vector3 forkUp = {0.0f, std::cos(kRaceRake), -std::sin(kRaceRake)};
    const Vector3 dir = Vector3Normalize({1.0f, -0.16f, -0.26f});   // afuera, abajo y atrás
    for (float s : {-1.0f, 1.0f}) {
        const Vector3 clamp = Vector3Add({s * 0.105f, 0.0f, 0.0f}, Vector3Scale(forkUp, -0.05f));
        const Vector3 d = {s * dir.x, dir.y, dir.z};
        const Vector3 start = Vector3Add(clamp, {s * 0.033f, -0.001f, -0.002f});
        auto along = [&](float l) { return Vector3Add(start, Vector3Scale(d, l)); };
        p.Rod(start, along(0.225f), 0.011f, kAluBright);                                       // tubo
        p.Rod(along(0.095f), along(0.222f), 0.0175f, kGrip);                                  // puño
        p.Rod(along(0.222f), along(0.238f), 0.019f, Mat{{200, 30, 36, 255}, 0.6f});           // contrapeso
        p.RBox(along(0.07f), {0.035f, 0.032f, 0.04f}, kPlasticBlack, kNoRot, 0.4f);          // comando
        // Leva: sale del soporte adelante del puño y corre paralela a él.
        const Vector3 perch = Vector3Add(along(0.085f), {0.0f, 0.004f, 0.03f});
        p.RBox(perch, {0.03f, 0.02f, 0.03f}, kAluBright, kNoRot, 0.4f);
        p.Tube({perch, Vector3Add(along(0.14f), {0.0f, 0.0f, 0.052f}), Vector3Add(along(0.2f), {0.0f, -0.004f, 0.048f})}, 0.005f, kAluBright);
        if (s < 0.0f) {                                                                        // derecha: acelerador, bomba y protector
            p.Rod(along(0.085f), along(0.10f), 0.023f, kPlasticBlack);
            p.Rod(Vector3Add(perch, {0.0f, 0.012f, 0.01f}), Vector3Add(perch, {0.03f, 0.018f, 0.075f}), 0.011f, kPlasticBlack);
            p.Rod(Vector3Add(perch, {0.02f, 0.03f, 0.05f}), Vector3Add(perch, {0.02f, 0.065f, 0.05f}), 0.012f, Mat{{220, 220, 214, 255}, 0.4f});
            p.Tube({along(0.225f), Vector3Add(along(0.235f), {0.0f, 0.0f, 0.04f}), Vector3Add(along(0.225f), {0.0f, 0.0f, 0.075f})}, 0.006f, kCarbon);
        }
    }
    return b;
}

// Guardabarros "de la tija": en la de carreras el guardabarros va en las botellas, así que acá sólo
// queda el repartidor de las cañerías de freno debajo de la tija de abajo.
MeshBuilder BuildRaceFenderParts(const BikeLivery& L)
{
    MeshBuilder b;
    Parts p{b};
    p.RBox({0.0f, -0.01f, 0.07f}, {0.04f, 0.022f, 0.025f}, kPlasticBlack, kNoRot, 0.4f);
    p.RBox({0.0f, 0.0f, 0.06f}, {0.07f, 0.008f, 0.02f}, Mat{L.stripe2, 0.5f}, kNoRot, 0.3f);
    for (float s : {-1.0f, 1.0f}) p.Tube({{s * 0.012f, -0.012f, 0.075f}, {s * 0.06f, -0.03f, 0.07f}, {s * 0.08f, -0.07f, 0.06f}}, 0.0045f, kPlasticBlack);
    return b;
}

} // namespace

void GenRaceMeshes(Mesh* out)
{
    out[BikeMesh::Swingarm] = BuildRaceSwingarm().Build();
    out[BikeMesh::ShockBody] = BuildShockBody().Build();
    out[BikeMesh::ShockSpring] = BuildShockSpring().Build();
    out[BikeMesh::ShockLower] = BuildShockLower().Build();
    out[BikeMesh::ForkLower] = BuildRaceForkLower().Build();
    out[BikeMesh::Cockpit] = BuildRaceCockpit().Build();
    out[BikeMesh::WheelFront] = BuildRaceWheel(true).Build();
    out[BikeMesh::WheelRear] = BuildRaceWheel(false).Build();
    out[BikeMesh::TireFront] = BuildSlick(true).Build();
    out[BikeMesh::TireRear] = BuildSlick(false).Build();
}

void GenRaceLiveryMeshes(const BikeLivery& livery, Mesh* out)
{
    out[0] = BuildRaceFenderParts(livery).Build();
    out[1] = BuildRaceForkUpper(livery).Build();
}

// Chasis de la de carreras: doble viga de aluminio, 4 en línea con el radiador curvo, escape 4-2-1 al
// costado derecho, tanque, asiento y colín con los números, carenado con alerones y parabrisas,
// tablero, estriberas altas y atrasadas.
Mesh GenRaceBody(Vector3 SH, Vector3 up, const BikeLivery& L)
{
    MeshBuilder b;
    Parts p{b};
    const Mat plastic = {L.plastic, 0.62f}, stripeA = {L.stripe1, 0.6f}, stripeB = {L.stripe2, 0.6f};
    auto onAxis = [&](float d) { return Vector3Add(SH, Vector3Scale(up, d)); };

    // ------------------------------------------------------------------ chasis
    p.Rod(onAxis(0.0f), onAxis(-0.215f), 0.037f, kRaceFrame);                         // pipa
    for (float s : {-1.0f, 1.0f}) {
        Sweep spar;
        spar.path = {Vector3Add(onAxis(-0.07f), {s * 0.04f, 0.0f, -0.035f}), {s * 0.112f, SH.y - 0.075f, SH.z - 0.15f}, {s * 0.150f, 0.10f, 0.12f},
                     {s * 0.150f, 0.03f, -0.02f}, {s * 0.140f, -0.05f, -0.10f}, {s * 0.137f, -0.13f, -0.135f}};
        spar.profile = Superellipse(0.02f, 0.048f, 4.0f, 18);
        spar.scale = [](float t) { return Vector2{1.0f, 1.0f - 0.3f * Smooth(0.5f, 1.0f, t)}; };
        p.Add(BuildSweep(spar), kRaceFrame);
        p.RBox({s * 0.137f, -0.26f, -0.135f}, {0.034f, 0.24f, 0.12f}, kRaceFrame, kNoRot, 0.3f);   // placas del basculante
        p.DiscX({s * 0.158f, -0.27f, -0.13f}, 0.02f, 0.012f, kSteel);
        p.Tube({{s * 0.13f, 0.06f, -0.08f}, {s * 0.10f, 0.10f, -0.30f}, {s * 0.075f, 0.13f, -0.56f}}, 0.012f, kFrameBlack);   // subchasis
    }
    p.Rod({-0.14f, -0.27f, -0.13f}, {0.14f, -0.27f, -0.13f}, 0.012f, kSteel);                 // eje del basculante
    p.Rod({-0.13f, 0.035f, -0.17f}, {0.13f, 0.035f, -0.17f}, 0.013f, kRaceFrame);            // travesaño del amortiguador
    p.RBox({0.0f, 0.03f, -0.17f}, {0.05f, 0.03f, 0.045f}, kRaceFrame);

    // ------------------------------------------------------------------ motor y radiador
    p.RBox({0.0f, -0.34f, 0.03f}, {0.30f, 0.20f, 0.34f}, kEngine);                           // cárter
    const Quaternion tilt = RotX(30.0f);
    const Vector3 cylAxis = Vector3RotateByQuaternion({0.0f, 1.0f, 0.0f}, tilt);
    const Vector3 block = {0.0f, -0.20f, 0.17f};
    p.RBox(block, {0.34f, 0.17f, 0.13f}, kEngine, tilt, 0.3f);
    p.RBox(Vector3Add(block, Vector3Scale(cylAxis, 0.12f)), {0.33f, 0.07f, 0.14f}, kEngine, tilt, 0.3f);
    p.RBox(Vector3Add(block, Vector3Scale(cylAxis, 0.165f)), {0.30f, 0.04f, 0.12f}, kPlasticBlack, tilt, 0.45f);   // tapa de válvulas
    p.DiscX({-0.155f, -0.33f, -0.02f}, 0.085f, 0.03f, kCover);                              // embrague
    p.DiscX({0.155f, -0.31f, 0.10f}, 0.07f, 0.03f, kCover);                                 // alternador
    p.RBox({0.0f, -0.10f, 0.33f}, {0.42f, 0.31f, 0.05f}, kRadiator, RotX(13.0f), 0.2f);     // radiador
    p.RBox({0.0f, 0.06f, -0.02f}, {0.20f, 0.12f, 0.24f}, kPlasticBlack, kNoRot, 0.4f);      // caja de aire (bajo el tanque)
    p.RBox({0.0f, 0.075f, -0.40f}, {0.15f, 0.11f, 0.22f}, kPlasticBlack, RotX(-12.0f), 0.4f); // batería y electrónica (bajo el colín)

    // ------------------------------------------------------------------ escape 4-2-1 (derecha)
    for (int k = 0; k < 4; ++k) {                                                             // colectores bajo el motor
        const float x = -0.105f + 0.07f * k;
        Sweep h;
        h.path = {{x, -0.06f, 0.255f}, {x * 0.9f, -0.20f, 0.285f}, {x * 0.7f, -0.36f, 0.245f}, {x * 0.35f, -0.44f, 0.13f}, {-0.01f, -0.445f, 0.02f}};
        h.profile = Circle(0.019f, 10);
        h.transport = true;
        h.caps = false;
        h.color = [](float t, int) { return Mix({176, 120, 64, 255}, {84, 74, 118, 255}, t * 1.6f); };
        p.Add(BuildSweep(h), {WHITE, 0.7f});
    }
    Sweep link;                                                                               // por abajo y atrás del talón, afuera del basculante
    link.path = {{-0.01f, -0.445f, 0.03f}, {-0.05f, -0.44f, -0.12f}, {-0.13f, -0.40f, -0.28f}, {-0.19f, -0.33f, -0.38f},
                 {-0.185f, -0.22f, -0.43f}, {-0.165f, -0.12f, -0.46f}};
    link.profile = Circle(0.029f, 12);
    link.transport = true;
    link.color = [](float t, int) { return Mix({96, 84, 124, 255}, {132, 128, 132, 255}, t); };
    p.Add(BuildSweep(link), {WHITE, 0.75f});
    Sweep can;                                                                                // silenciador corto bajo el colín
    can.path = {{-0.165f, -0.12f, -0.46f}, {-0.174f, -0.04f, -0.58f}, {-0.18f, 0.04f, -0.69f}};
    can.profile = Superellipse(0.046f, 0.056f, 2.6f, 18);
    can.scale = [](float t) { const float k = 0.62f + 0.38f * Smooth(0.0f, 0.2f, t); return Vector2{k, k}; };
    can.edges = {0.12f, 0.9f};
    can.color = [](float t, int) {
        if (t > 0.9f) return Color{112, 108, 114, 255};                                       // boca de titanio
        return t < 0.12f ? Color{110, 96, 140, 255} : Color{44, 45, 50, 255};                 // carbono
    };
    p.Add(BuildSweep(can), {WHITE, 0.75f});
    p.Rod({-0.18f, 0.04f, -0.69f}, {-0.1805f, 0.046f, -0.699f}, 0.024f, {{96, 92, 98, 255}, 0.7f});
    p.Rod({-0.1805f, 0.046f, -0.699f}, {-0.181f, 0.048f, -0.702f}, 0.017f, kPlasticBlack);

    // ------------------------------------------------------------------ estriberas y comandos
    for (float s : {-1.0f, 1.0f}) {
        const Matrix toSide = Basis({0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {s, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f});
        const Profile plate = {{-0.13f, -0.17f}, {-0.30f, -0.195f}, {-0.335f, -0.26f}, {-0.23f, -0.305f}, {-0.12f, -0.25f}};
        p.Add(Panel(plate, 0.008f, [](float, float) { return 0.128f; }, 2), kCarbon, toSide);        // taloneras
        p.Rod({s * 0.125f, -0.24f, -0.23f}, {s * 0.205f, -0.24f, -0.23f}, 0.011f, kAluBright);      // estribera
        p.Rod({s * 0.19f, -0.24f, -0.23f}, {s * 0.215f, -0.24f, -0.23f}, 0.013f, kPlasticBlack);
        p.Bar({s * 0.13f, -0.20f, -0.14f}, {s * 0.13f, -0.24f, -0.23f}, 0.012f, 0.03f, kAluBright);  // soporte
        const float px = s * 0.145f;                                                                  // freno (der.) / cambios (izq.)
        p.Tube({{px, -0.232f, -0.215f}, {px, -0.25f, -0.15f}, {s * 0.16f, -0.262f, -0.09f}}, 0.006f, kAluBright);
        p.RBox({s * 0.165f, -0.262f, -0.085f}, {0.03f, 0.012f, 0.022f}, kAluBright, kNoRot, 0.3f);
    }

    // ------------------------------------------------------------------ tanque, asiento y colín
    const RingDef& tank = TankDef();
    const float tank0 = 0.34f, tank1 = -0.07f;
    p.Add(Shell(Ring(tank, tank0, tank1, 0.05f, 0.04f), 22, 48, 0.006f, 0.2f, {}, true), plastic);
    {   // franja al medio del tanque (calco sobre la cara de arriba)
        const Surf2 top = [&tank](Vector2 zx) {
            const float t = Tab(tank.top, zx.x), bo = Tab(tank.bottom, zx.x), w = Tab(tank.w, zx.x);
            const float q = std::min(std::fabs(zx.y) / w, 1.0f);
            return Vector3{zx.y, 0.5f * (t + bo) + 0.5f * (t - bo) * std::pow(std::max(0.0f, 1.0f - std::pow(q, tank.n)), 1.0f / tank.n), zx.x};
        };
        p.Add(Shell(Decal(top, 0.2f, {0.325f, -0.02f}, {-0.055f, -0.02f}, {-0.055f, 0.02f}, {0.325f, 0.02f}, 0.002f), 16, 2, 0.003f, 0.2f), stripeA);
    }
    for (float s : {-1.0f, 1.0f}) {                                                           // rodilleras de goma
        const Surf2 tg = [&tank, s](Vector2 zy) { return Vector3{s * RingX(tank, zy.x, zy.y), zy.y, zy.x}; };
        p.Add(Shell(Decal(tg, 0.2f, {0.12f, 0.155f}, {-0.03f, 0.165f}, {-0.03f, 0.205f}, {0.12f, 0.235f}, 0.002f), 6, 4, 0.004f, 0.2f), kRubber);
    }
    Sweep seat;
    seat.path = {{0.0f, 0.19f, -0.06f}, {0.0f, 0.19f, -0.20f}, {0.0f, 0.19f, -0.33f}, {0.0f, 0.195f, -0.44f}};
    seat.profile = Superellipse(0.105f, 0.019f, 4.0f, 24);
    seat.scale = [](float t) { return Vector2{0.72f + 0.28f * Smooth(0.0f, 0.3f, t), 1.0f}; };
    p.Add(BuildSweep(seat), {{44, 44, 48, 255}, 0.1f});
    const RingDef& tail = TailDef();
    const float tail0 = -0.10f, tail1 = -0.86f;
    p.Add(Shell(Ring(tail, tail0, tail1, 0.0f, 0.03f), 30, 40, 0.006f, 0.18f,
                [&](Vector3 q, float, float) {
                    const float band = q.y - (0.14f + 0.12f * Clamp01((-q.z - 0.45f) / 0.4f));        // franja que sube hacia la punta
                    if (std::fabs(q.x) > 0.05f && band > -0.012f && band < 0.012f) return L.stripe2;
                    return L.plastic;
                },
                true),
          {WHITE, 0.62f});
    p.RBox({0.0f, 0.256f, -0.857f}, {0.03f, 0.018f, 0.01f}, kTailRed, kNoRot, 0.3f);         // luz de lluvia
    for (float s : {-1.0f, 1.0f}) {                                                           // números del colín
        const float z = -0.63f, y = 0.232f, h = 0.062f;
        float x = 0.0f;                                                                       // afuera de toda la zona del número
        for (float dz = -0.05f; dz <= 0.051f; dz += 0.025f)
            for (float dy = -0.5f * h; dy <= 0.5f * h + 1e-4f; dy += 0.25f * h) x = std::max(x, RingX(tail, z + dz, y + dy));
        AddNumber(p, L.number, stripeA, {s * (x + 0.003f), y, z}, {0.0f, 0.0f, -s}, {0.0f, 1.0f, 0.0f}, h);
    }

    // ------------------------------------------------------------------ carenado
    const float cowlAxis = 0.15f;
    p.Add(Shell(Cowl, 26, 36, 0.006f, cowlAxis), plastic);
    p.Add(Shell(Screen, 12, 20, 0.004f, cowlAxis), kRaceScreen);
    p.RBox({0.0f, 0.114f, 0.848f}, {0.135f, 0.078f, 0.05f}, kVent, kNoRot, 0.6f);            // toma de aire de la punta
    // Número de adelante: disco blanco sobre la punta.
    const Surf2 cowl2 = CowlAt;
    p.Add(Shell(DiscDecal(cowl2, cowlAxis, {0.795f, 0.0f}, {0.036f, 0.62f}, 0.003f), 4, 32, 0.003f, cowlAxis, {}, true), kWhite);
    {
        const Vector3 c = CowlAt({0.795f, 0.0f}), upDir = Vector3Normalize(Vector3Subtract(CowlAt({0.785f, 0.0f}), CowlAt({0.805f, 0.0f})));
        AddNumber(p, L.number, {kNumber.c, 0.4f}, Vector3Add(c, Vector3Scale(Vector3Normalize(Vector3CrossProduct({1.0f, 0.0f, 0.0f}, upDir)), 0.0075f)),
                  {1.0f, 0.0f, 0.0f}, upDir, 0.052f);
    }
    for (float s : {-1.0f, 1.0f}) {
        const Surf side = Side(s);
        p.Add(Shell(side, 28, 30, 0.006f, 0.0f,
                    [&](Vector3 q, float, float) { return q.y < -0.29f ? kCarbon.c : L.plastic; }),   // panza de carbono
              {WHITE, 0.62f});
        const Surf2 side2 = [s](Vector2 zy) { return SideAt(s, zy); };
        // Franja en diagonal (color 1) y un filete (color 2) debajo.
        p.Add(Shell(Decal(side2, 0.0f, {0.60f, 0.105f}, {0.05f, -0.15f}, {0.07f, -0.105f}, {0.62f, 0.15f}, 0.003f), 24, 4, 0.003f, 0.0f), stripeA);
        p.Add(Shell(Decal(side2, 0.0f, {0.58f, 0.085f}, {0.03f, -0.18f}, {0.04f, -0.165f}, {0.59f, 0.097f}, 0.003f), 24, 2, 0.003f, 0.0f), stripeB);
        for (int k = 0; k < 3; ++k) {                                                          // salidas de aire
            const float y0 = 0.035f + 0.032f * k, z0 = 0.30f + 0.018f * k;
            p.Add(Shell(Decal(side2, 0.0f, {z0 + 0.10f, y0}, {z0, y0 - 0.004f}, {z0, y0 + 0.012f}, {z0 + 0.10f, y0 + 0.016f}, 0.002f), 6, 2, 0.003f, 0.0f), kVent);
        }
        // Alerón: plano en flecha con su placa de punta, a los costados de la trompa.
        const float wz = 0.665f, root = CowlW(wz) * 0.96f;
        const Matrix toWing = Basis({s, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.148f, 0.0f});
        const Profile wing = {{root - 0.01f, wz + 0.055f}, {root + 0.075f, wz + 0.015f}, {root + 0.078f, wz - 0.03f}, {root - 0.01f, wz - 0.045f}};
        p.Add(Panel(wing, 0.007f, [root](float u, float) { return -0.12f * (u - root); }, 2), plastic, toWing);   // baja un poco hacia la punta
        p.RBox({s * (root + 0.078f), 0.128f, wz - 0.008f}, {0.006f, 0.05f, 0.07f}, kCarbon, kNoRot, 0.25f);
    }

    // ------------------------------------------------------------------ tablero
    // Tablero: pantalla inclinada hacia el piloto, colgada de la trompa atrás del parabrisas.
    const Quaternion dashTilt = RotX(35.0f);
    const Vector3 dash = {0.0f, 0.33f, 0.495f};
    p.Bar({0.0f, 0.30f, 0.53f}, dash, 0.03f, 0.012f, kPlasticBlack);
    p.RBox(dash, {0.15f, 0.09f, 0.025f}, kPlasticBlack, dashTilt, 0.3f);
    p.RBox(Vector3Add(dash, Vector3RotateByQuaternion({0.0f, 0.004f, -0.013f}, dashTilt)), {0.12f, 0.066f, 0.004f}, {{36, 70, 64, 255}, 0.95f}, dashTilt, 0.2f);
    return b.Build();
}

const BikeLivery& RaceLivery(int i)
{
    // Esquemas de equipo: plásticos, color 1 (franjas y números del colín), color 2 (filetes) y el número.
    static const BikeLivery kTable[kLiveries] = {
        {{196, 18, 30, 255}, {244, 244, 240, 255}, {24, 24, 28, 255}, "27"},     // rojo, blanco y negro
        {{18, 50, 146, 255}, {20, 186, 236, 255}, {240, 240, 236, 255}, "33"},   // azul con celeste
        {{26, 26, 30, 255}, {140, 226, 34, 255}, {236, 236, 232, 255}, "72"},    // negro con verde flúor
        {{244, 108, 14, 255}, {26, 26, 30, 255}, {238, 238, 234, 255}, "8"},     // naranja con negro
    };
    return kTable[((i % kLiveries) + kLiveries) % kLiveries];
}

const BikeStyleDef& RaceStyleDef()
{
    static const BikeStyleDef d = {
        "race", GenRaceMeshes, GenRaceLiveryMeshes, GenRaceBody, RaceLivery,
        JPH::Vec3(0.0f, -0.077f, -0.016f), 0.293f,                   // semimanillares bajos y adelante
        JPH::Vec3(0.17f, -0.24f, -0.23f),                            // estriberas altas y atrasadas
        JPH::Vec3(0.0f, 0.265f, -0.27f), JPH::Vec3(0.0f, 0.59f, 0.09f), JPH::Vec3(0.21f, 0.07f, 0.10f),
        0.05f, 0.12f, 0.0f, 1.0f,                                    // agachado sobre el tanque a alta velocidad
        JPH::Vec3(-0.181f, 0.048f, -0.703f), JPH::Vec3(-0.02f, 0.5f, -0.7f).Normalized(), true,
        1.0f,                                                        // colgado con la rodilla al piso en las curvas
    };
    return d;
}
// ==== END RACE ====

// ==== BEGIN TRIAL (moto de trial: su sección, ver TrialStyleDef) ====
// Moto de trial de competición (diseño propio): chasis perimetral de aluminio pulido a la vista y
// bajo, cubrecárter enorme, una tapa chiquita sobre el tanque en vez de tanque de verdad, casi sin
// asiento, manubrio alto y ancho con travesaño, radiador chico con sus cachas, guardabarros mínimos,
// ruedas de 21" y 18" con cubiertas de trial (tacos cuadrados, bajos y blandos) y un dos tiempos con
// la panza del escape por abajo y un silenciador chico. Las medidas van con la geometría de
// mods/base/bikes/trial.ini: horquilla a 23°, 1.32 m entre ejes y el piso ~0.79 m debajo del centro de
// masa con la moto en reposo.
const BikeLivery& TrialLivery(int i)
{
    // Colores vivos de trial: rojo, amarillo, azul y verde lima; el número va en la máscara.
    static const BikeLivery kTable[kLiveries] = {
        {{214, 26, 36, 255}, {245, 245, 240, 255}, {26, 26, 30, 255}, "3"},
        {{250, 198, 16, 255}, {26, 26, 30, 255}, {214, 26, 36, 255}, "8"},
        {{18, 84, 200, 255}, {245, 245, 240, 255}, {250, 198, 16, 255}, "15"},
        {{122, 200, 32, 255}, {26, 26, 30, 255}, {245, 245, 240, 255}, "27"},
    };
    return kTable[((i % kLiveries) + kLiveries) % kLiveries];
}

namespace {

const Mat kTrialAlu = {{186, 190, 198, 255}, 0.8f};        // aluminio pulido: chasis, basculante, cubrecárter
const Mat kTrialBlack = {{28, 28, 32, 255}, 0.5f};         // anodizado negro: tijas, manubrio, botellas
const Mat kTrialCase = {{62, 64, 70, 255}, 0.4f};          // cárter y cilindro
const Mat kTrialWhite = {{240, 240, 236, 255}, 0.5f};      // guardabarros delantero y campo del número
constexpr float kTrialRakeDeg = 23.0f;                     // horquilla de trial.ini (la máscara la da por hecho)

// Cubierta de trial: carcasa balón (los talones entran en la llanta a ±bead) y tacos cuadrados bajos
// en filas alternadas, como ladrillos, cada uno perpendicular a la carcasa. R = radio exterior.
MeshBuilder BuildTrialTire(float R, float hw, float hh, float rimR, float bead, int rows)
{
    MeshBuilder b;
    Parts p{b};
    const float block = 0.0075f;                             // goma blanda: tacos de 7.5 mm
    const float rc = R - block - hh;
    Profile prof;
    for (int i = 0; i <= 20; ++i) {
        const float ph = (-112.0f + 224.0f * i / 20.0f) * DEG2RAD;
        prof.push_back({hw * std::sin(ph), rc + hh * std::cos(ph)});
    }
    prof.push_back({bead, rimR - 0.004f});
    prof.push_back({-bead, rimR - 0.004f});
    p.Add(Revolve(prof, 72), kRubber);
    const float pitch = kTau * (rc + hh) / (float)rows;      // paso entre filas sobre la banda
    for (int row = 0; row < rows; ++row) {
        const float a = kTau * (float)row / (float)rows;
        const Vector3 er = {0.0f, std::cos(a), std::sin(a)}, et = {0.0f, -std::sin(a), std::cos(a)};
        auto blockAt = [&](float phDeg, float sx, float h) {
            const float ph = phDeg * DEG2RAD;
            const Vector3 surf = Vector3Add({hw * std::sin(ph), 0.0f, 0.0f}, Vector3Scale(er, rc + hh * std::cos(ph)));
            const Vector3 nn = Vector3Normalize(Vector3Add({std::sin(ph) / hw, 0.0f, 0.0f}, Vector3Scale(er, std::cos(ph) / hh)));
            const Vector3 center = Vector3Add(surf, Vector3Scale(nn, h * 0.5f - 0.002f));   // 2 mm enterrado
            const Vector3 side = Vector3CrossProduct(nn, et);
            p.Add(UnitBox(), kKnob, Basis(Vector3Scale(side, sx), Vector3Scale(nn, h), Vector3Scale(et, pitch * 0.64f), center));
        };
        const float w = hw * 0.36f;                          // ancho de cada taco
        if (row % 2 == 0) {
            for (float ph : {0.0f, 30.0f, -30.0f, 60.0f, -60.0f}) blockAt(ph, w, block + 0.002f);
            for (float ph : {88.0f, -88.0f}) blockAt(ph, w * 0.8f, block);
        } else {
            for (float ph : {15.0f, -15.0f, 45.0f, -45.0f}) blockAt(ph, w, block + 0.002f);
            for (float ph : {74.0f, -74.0f}) blockAt(ph, w * 0.9f, block + 0.001f);
        }
    }
    return b;
}

// Rueda de trial: llanta anodizada negra con los flancos pulidos (21" adelante, 18" atrás), masa
// chica, 32 rayos cruzados, disco ondulado grande adelante (izquierda) y chico atrás (derecha), y la
// corona grande de trial (42 dientes) a la izquierda.
MeshBuilder BuildTrialWheel(bool front)
{
    MeshBuilder b;
    Parts p{b};
    const float rimR = front ? 0.270f : 0.232f, rw = front ? 0.021f : 0.028f;
    const Profile rim = {{-rw, rimR + 0.005f}, {-rw, rimR - 0.005f}, {-rw + 0.009f, rimR - 0.011f}, {-rw + 0.012f, rimR - 0.02f},
                         {rw - 0.012f, rimR - 0.02f}, {rw - 0.009f, rimR - 0.011f}, {rw, rimR - 0.005f}, {rw, rimR + 0.005f}};
    p.Add(Revolve(rim, 64, [](int, int j) { return (j == 0 || j == 7) ? Color{190, 192, 198, 255} : Color{34, 34, 38, 255}; }), {WHITE, 0.65f});

    const float hx = front ? 0.05f : 0.062f, fx = front ? 0.034f : 0.044f;
    const Profile hub = {{-hx, 0.011f}, {-hx, 0.024f}, {-fx - 0.003f, 0.026f}, {-fx - 0.003f, 0.042f}, {-fx + 0.003f, 0.042f},
                         {-fx + 0.003f, 0.027f}, {fx - 0.003f, 0.027f}, {fx - 0.003f, 0.042f}, {fx + 0.003f, 0.042f},
                         {fx + 0.003f, 0.026f}, {hx, 0.024f}, {hx, 0.011f}};
    p.Add(Revolve(hub, 32), kAluBright);
    for (int i = 0; i < 32; ++i) {
        const float side = i % 2 == 0 ? 1.0f : -1.0f;
        const float ah = kTau * (float)i / 32.0f;
        const float ar = ah + ((i / 2) % 2 == 0 ? 0.6f : -0.6f);
        const Vector3 atHub = {side * fx, 0.038f * std::cos(ah), 0.038f * std::sin(ah)};
        const Vector3 atRim = {side * 0.005f, (rimR - 0.018f) * std::cos(ar), (rimR - 0.018f) * std::sin(ar)};
        p.Bar(atHub, atRim, 0.0036f, 0.0036f, kSpoke);
    }
    if (front) {
        AddDisc(p, 0.045f, 0.066f, 0.102f, 12);
    } else {
        AddDisc(p, -0.05f, 0.05f, 0.078f, 10);
        const float x = 0.062f, h = 0.002f;                  // corona de 42 dientes con agujeros
        const Profile ring = {{x - h, 0.05f}, {x - h, 0.078f}, {x - h, 0.106f}, {x + h, 0.106f}, {x + h, 0.078f}, {x + h, 0.05f}};
        p.Add(Revolve(ring, 168,
                      [](int s, int j) { return ((j == 1 || j == 4) && s % 28 < 9) ? Color{40, 40, 44, 255} : WHITE; },
                      [](float a, int j) { return (j == 2 || j == 3) ? 1.0f + 0.03f * std::cos(42.0f * a) : 1.0f; }),
              kAluBright);
        const Profile carrier = {{x - 0.008f, 0.03f}, {x - 0.008f, 0.052f}, {x, 0.052f}, {x, 0.03f}};
        p.Add(Revolve(carrier, 24), kHub);
    }
    return b;
}

// Basculante de aluminio fundido (brazos altos adelante que se afinan hacia el eje), levas de cadena
// tipo caracol, la bieleta del amortiguador, cadena con el tensor de rodillo abajo y la pinza trasera.
MeshBuilder BuildTrialSwingarm()
{
    MeshBuilder b;
    Parts p{b};
    const float L = BikeMesh::kSwingarmLength;
    for (float s : {-1.0f, 1.0f}) {
        Sweep arm;
        arm.path = {{s * 0.076f, 0.0f, 0.02f}, {s * 0.082f, 0.006f, -0.22f}, {s * 0.087f, 0.0f, -(L + 0.035f)}};
        arm.profile = Superellipse(0.014f, 0.034f, 3.5f, 16);
        arm.scale = [](float t) { return Vector2{1.0f, 1.0f - 0.38f * t}; };
        p.Add(BuildSweep(arm), kTrialAlu);
        p.DiscX({s * 0.103f, 0.0f, -L}, 0.024f, 0.008f, kAluBright);                      // levas caracol
    }
    p.RBox({0.0f, -0.004f, -0.03f}, {0.15f, 0.042f, 0.04f}, kTrialAlu, kNoRot, 0.4f);        // travesaño
    p.RBox({0.0f, 0.006f, -0.11f}, {0.155f, 0.026f, 0.03f}, kTrialAlu, kNoRot, 0.4f);
    p.Rod({-0.115f, 0.0f, -L}, {0.115f, 0.0f, -L}, 0.0105f, kSteel);                       // eje trasero
    p.DiscX({-0.118f, 0.0f, -L}, 0.017f, 0.012f, kSteel);
    p.RBox(BikeMesh::kShockLow, {0.05f, 0.045f, 0.05f}, kTrialBlack, kNoRot, 0.4f);           // bieleta
    p.Bar(BikeMesh::kShockLow, {0.0f, -0.10f, -0.01f}, 0.04f, 0.022f, kTrialBlack);

    // Cadena del piñón chico (9 dientes) a la corona grande, con el tensor de rodillo en el tramo de abajo.
    const float cx = 0.064f, rf = 0.025f, rr = 0.105f;
    const Vector3 sprocket = {cx, -0.035f, 0.07f};
    p.Bar({cx, sprocket.y + rf, sprocket.z}, {cx, rr, -L}, 0.011f, 0.013f, kChain);
    p.Bar({cx, sprocket.y - rf, sprocket.z}, {cx, -rr, -L}, 0.011f, 0.013f, kChain);
    p.DiscX(sprocket, rf, 0.011f, kHub);
    const Vector3 roller = {cx, -0.083f, -0.13f};
    p.DiscX(roller, 0.019f, 0.016f, kPlasticBlack);
    p.Bar({cx + 0.012f, -0.02f, -0.07f}, Vector3Add(roller, {0.012f, 0.0f, 0.0f}), 0.008f, 0.014f, kTrialAlu);
    p.RBox({cx, 0.042f, -0.14f}, {0.024f, 0.012f, 0.14f}, kPlasticBlack, kNoRot, 0.3f);       // patín de cadena

    // Pinza del freno trasero (derecha) con su soporte.
    p.RBox({-0.066f, -0.055f, -L + 0.035f}, {0.012f, 0.07f, 0.07f}, kTrialAlu);
    p.RBox({-0.05f, -0.07f, -L + 0.065f}, {0.03f, 0.045f, 0.055f}, kGold);
    return b;
}

// Resorte cromado del amortiguador: más vueltas y de alambre más fino que el de motocross.
MeshBuilder BuildTrialShockSpring()
{
    Sweep sp;
    const int turns = 8, per = 10;
    for (int i = 0; i <= turns * per; ++i) {
        const float a = kTau * (float)i / (float)per;
        sp.path.push_back({0.034f * std::cos(a), -BikeMesh::kSpringLength * (float)i / (float)(turns * per), 0.034f * std::sin(a)});
    }
    sp.profile = Circle(0.0065f, 6);
    sp.transport = true;
    sp.perSegment = 2;
    MeshBuilder b;
    Parts{b}.Add(BuildSweep(sp), kChrome);
    return b;
}

// Horquilla convencional: botellas negras, pinza dorada de cuatro pistones (izquierda) y el
// guardabarros chico atornillado a las botellas: sube y baja con la rueda y va pegado a la cubierta.
// Espacio: origen en el eje delantero, +Y a lo largo de la horquilla.
MeshBuilder BuildTrialForkLower()
{
    MeshBuilder b;
    Parts p{b};
    for (float s : {-1.0f, 1.0f}) {
        p.Rod({s * 0.08f, 0.035f, 0.0f}, {s * 0.08f, 0.305f, 0.0f}, 0.025f, kTrialBlack);          // botellas
        p.Rod({s * 0.08f, 0.30f, 0.0f}, {s * 0.08f, 0.318f, 0.0f}, 0.022f, kRubber);               // guardapolvos
        p.RBox({s * 0.08f, 0.018f, -0.004f}, {0.046f, 0.075f, 0.056f}, kTrialBlack, kNoRot, 0.35f);   // pies
        p.Bar({s * 0.07f, 0.29f, 0.012f}, {s * 0.036f, 0.37f, 0.05f}, 0.012f, 0.02f, kTrialBlack);  // soportes del guardabarros
    }
    p.RBox({0.048f, 0.05f, -0.07f}, {0.034f, 0.05f, 0.064f}, kGold, RotX(-38.0f), 0.35f);           // pinza
    p.RBox({0.07f, 0.075f, -0.04f}, {0.02f, 0.05f, 0.06f}, kTrialBlack);
    p.Rod({-0.11f, 0.0f, 0.0f}, {0.11f, 0.0f, 0.0f}, 0.0105f, kSteel);                              // eje
    p.DiscX({-0.112f, 0.0f, 0.0f}, 0.016f, 0.01f, kSteel);
    Sweep f;                                                  // arco sobre la cubierta, a 3 cm del taco
    for (int i = 0; i <= 8; ++i) {
        const float a = (-22.0f + 88.0f * (float)i / 8.0f) * DEG2RAD;
        f.path.push_back({0.0f, 0.382f * std::cos(a), 0.382f * std::sin(a)});
    }
    f.profile = Arch(0.05f, 0.02f, 0.005f);
    f.scale = [](float t) { return Vector2{0.7f + 0.3f * std::sin(kPi * std::min(1.0f, t * 1.1f)), 1.0f}; };
    p.Add(BuildSweep(f), kTrialWhite);
    return b;
}

// Tijas negras, barras cromadas finas y tapones dorados. Espacio: origen en la pipa, +Y a lo largo de
// la horquilla. (El esquema de colores va en la máscara.)
MeshBuilder BuildTrialForkUpper(const BikeLivery&)
{
    MeshBuilder b;
    Parts p{b};
    p.RBox({0.0f, 0.03f, 0.0f}, {0.225f, 0.035f, 0.09f}, kTrialBlack, kNoRot, 0.35f);           // tija de arriba
    p.RBox({0.0f, -0.20f, 0.0f}, {0.225f, 0.045f, 0.10f}, kTrialBlack, kNoRot, 0.35f);          // tija de abajo
    p.Rod({0.0f, 0.045f, 0.0f}, {0.0f, 0.056f, 0.0f}, 0.017f, kSteel);
    for (float s : {-1.0f, 1.0f}) {
        p.Rod({s * 0.08f, 0.06f, 0.0f}, {s * 0.08f, -0.49f, 0.0f}, 0.019f, kChrome);               // barras
        p.Rod({s * 0.08f, 0.06f, 0.0f}, {s * 0.08f, 0.074f, 0.0f}, 0.017f, kGold);                 // tapones
    }
    return b;
}

// Máscara delantera (va en el lugar del guardabarros: tija de abajo, ejes de la moto): placa del
// color de la moto delante de las barras con el campo blanco del número y el faro LED chico. Se arma
// en el espacio de la horquilla (lanzamiento kTrialRakeDeg) y se inclina con ella.
MeshBuilder BuildTrialMask(const BikeLivery& L)
{
    MeshBuilder f;
    Parts p{f};
    const Profile mask = {{-0.085f, 0.265f}, {0.085f, 0.265f}, {0.10f, 0.215f}, {0.094f, 0.12f}, {0.06f, 0.065f},
                          {-0.06f, 0.065f}, {-0.094f, 0.12f}, {-0.10f, 0.215f}};
    auto curve = [](float u, float) { return 0.068f + 0.018f * (1.0f - (u / 0.1f) * (u / 0.1f)); };
    p.Add(Panel(mask, 0.007f, curve), Mat{L.plastic, 0.55f});
    const Profile field = {{-0.07f, 0.255f}, {0.07f, 0.255f}, {0.078f, 0.19f}, {0.07f, 0.16f}, {-0.07f, 0.16f}, {-0.078f, 0.19f}};
    p.Add(Panel(field, 0.002f, [curve](float u, float v) { return curve(u, v) + 0.0025f; }, 2), kTrialWhite);
    p.Add(Digits(L.number), kNumber, MatrixMultiply(MatrixScale(0.07f, 0.07f, 0.07f), MatrixTranslate(0.0f, 0.207f, 0.089f)));
    p.RBox({0.0f, 0.11f, 0.086f}, {0.075f, 0.03f, 0.012f}, kTrialBlack, kNoRot, 0.3f);           // faro LED
    p.RBox({0.0f, 0.11f, 0.091f}, {0.062f, 0.018f, 0.006f}, kLens, kNoRot, 0.3f);
    for (float s : {-1.0f, 1.0f}) p.Bar({s * 0.07f, 0.23f, 0.02f}, {s * 0.07f, 0.23f, 0.068f}, 0.008f, 0.012f, kTrialBlack);   // soportes
    // Del espacio de la horquilla (origen a kFenderDrop bajo la pipa) a los ejes de la moto.
    MeshBuilder b;
    Parts{b}.Add(f, {WHITE, -1.0f}, MatrixRotateX(-kTrialRakeDeg * DEG2RAD));
    return b;
}

// Manubrio de trial: alto (mucha subida desde las tijas), ancho (0.82 m) y plano en las puntas, con
// travesaño y su almohadilla, puños, levas cortas de dos dedos con sus bombas (freno a la derecha,
// embrague hidráulico a la izquierda) y el corta corriente con su cordón rojo. Origen en la pipa, ejes
// de la moto: el centro de los puños queda a (±0.352, 0.1706, -0.061), lo que pide TrialStyleDef.
MeshBuilder BuildTrialCockpit()
{
    MeshBuilder b;
    Parts p{b};
    const Vector3 up = {0.0f, std::cos(kTrialRakeDeg * DEG2RAD), -std::sin(kTrialRakeDeg * DEG2RAD)};
    Sweep bar;
    bar.path = {{-0.412f, 0.172f, -0.063f}, {-0.352f, 0.1706f, -0.061f}, {-0.27f, 0.167f, -0.058f}, {-0.205f, 0.153f, -0.052f},
                {-0.155f, 0.12f, -0.045f}, {-0.095f, 0.082f, -0.037f}, {0.0f, 0.077f, -0.035f}, {0.095f, 0.082f, -0.037f},
                {0.155f, 0.12f, -0.045f}, {0.205f, 0.153f, -0.052f}, {0.27f, 0.167f, -0.058f}, {0.352f, 0.1706f, -0.061f},
                {0.412f, 0.172f, -0.063f}};
    bar.profile = Circle(0.0115f, 10);
    bar.scale = [](float t) {
        const float k = 0.95f + 0.3f * Smooth(0.25f, 0.4f, 1.0f - std::fabs(2.0f * t - 1.0f));   // 28 mm al medio, 22 en las puntas
        return Vector2{k, k};
    };
    p.Add(BuildSweep(bar), kTrialBlack);
    p.Bar({-0.172f, 0.142f, -0.048f}, {0.172f, 0.142f, -0.048f}, 0.013f, 0.013f, kTrialBlack);      // travesaño
    p.RBox({0.0f, 0.146f, -0.048f}, {0.19f, 0.034f, 0.038f}, kPlasticBlack, kNoRot, 0.5f);          // almohadilla
    p.RBox({0.0f, 0.1635f, -0.048f}, {0.12f, 0.004f, 0.02f}, kTrialWhite, kNoRot, 0.3f);
    for (float s : {-1.0f, 1.0f}) {
        p.Rod(Vector3Add(Vector3Scale(up, 0.045f), {s * 0.04f, 0.0f, 0.0f}), {s * 0.04f, 0.07f, -0.034f}, 0.013f, kAluBright);  // elevadores
        p.RBox({s * 0.04f, 0.077f, -0.035f}, {0.036f, 0.032f, 0.042f}, kAluBright, kNoRot, 0.4f);   // abrazaderas
        p.Rod({s * 0.30f, 0.1692f, -0.0598f}, {s * 0.405f, 0.1718f, -0.0628f}, 0.0175f, kGrip);    // puños
        p.Rod({s * 0.405f, 0.1718f, -0.0628f}, {s * 0.413f, 0.172f, -0.063f}, 0.0185f, kPlasticBlack);
        p.RBox({s * 0.235f, 0.162f, -0.05f}, {0.032f, 0.03f, 0.034f}, kAluBright, kNoRot, 0.4f);    // bombas
        p.RBox({s * 0.228f, 0.185f, -0.055f}, {0.03f, 0.018f, 0.03f}, kPlasticBlack, kNoRot, 0.4f);
        p.Tube({{s * 0.245f, 0.166f, -0.033f}, {s * 0.285f, 0.166f, -0.008f}, {s * 0.33f, 0.162f, -0.002f}}, 0.0055f, kAluBright);   // levas cortas
    }
    p.Rod({-0.272f, 0.1675f, -0.0585f}, {-0.29f, 0.1685f, -0.0592f}, 0.022f, kPlasticBlack);          // acelerador
    p.RBox({0.265f, 0.168f, -0.058f}, {0.02f, 0.034f, 0.034f}, {{200, 30, 30, 255}, 0.5f}, kNoRot, 0.4f);   // corta corriente
    p.Tube({{0.265f, 0.15f, -0.06f}, {0.25f, 0.11f, -0.07f}, {0.21f, 0.10f, -0.09f}}, 0.003f, {{200, 30, 30, 255}, 0.3f});
    return b;
}

} // namespace

void GenTrialMeshes(Mesh* out)
{
    out[BikeMesh::Swingarm] = BuildTrialSwingarm().Build();
    out[BikeMesh::ShockBody] = BuildShockBody().Build();
    out[BikeMesh::ShockSpring] = BuildTrialShockSpring().Build();
    out[BikeMesh::ShockLower] = BuildShockLower().Build();
    out[BikeMesh::ForkLower] = BuildTrialForkLower().Build();
    out[BikeMesh::Cockpit] = BuildTrialCockpit().Build();
    out[BikeMesh::WheelFront] = BuildTrialWheel(true).Build();
    out[BikeMesh::WheelRear] = BuildTrialWheel(false).Build();
    out[BikeMesh::TireFront] = BuildTrialTire(BikeMesh::kFrontRadius, 0.036f, 0.048f, 0.270f, 0.019f, 60).Build();   // 2.75-21
    out[BikeMesh::TireRear] = BuildTrialTire(BikeMesh::kRearRadius, 0.052f, 0.059f, 0.232f, 0.026f, 64).Build();     // 4.00R18, ancha
}

void GenTrialLiveryMeshes(const BikeLivery& L, Mesh* out)
{
    out[0] = BuildTrialMask(L).Build();                      // en el lugar del guardabarros (tija de abajo)
    out[1] = BuildTrialForkUpper(L).Build();
}

// Chasis de la de trial: vigas de aluminio que bajan derecho de la pipa a las placas del basculante,
// caños delanteros que abrazan el radiador y apoyan en el cubrecárter, subchasis fino con la manija
// de la cola; dos tiempos angosto, tapa chiquita del tanque, casi sin asiento, laterales con "300".
Mesh GenTrialBody(Vector3 SH, Vector3 up, const BikeLivery& L)
{
    MeshBuilder b;
    Parts p{b};
    const Mat plastic = {L.plastic, 0.55f}, stripeA = {L.stripe1, 0.5f}, stripeB = {L.stripe2, 0.55f};
    auto onAxis = [&](float d) { return Vector3Add(SH, Vector3Scale(up, d)); };   // sobre el eje de dirección

    // --- Chasis perimetral de aluminio pulido.
    p.Rod(onAxis(0.02f), onAxis(-0.19f), 0.031f, kTrialAlu);                                        // pipa
    for (float s : {-1.0f, 1.0f}) {
        Sweep spar;                                                                                   // vigas
        spar.path = {Vector3Add(onAxis(-0.07f), {s * 0.028f, 0.0f, -0.03f}), {s * 0.066f, 0.075f, 0.33f}, {s * 0.086f, 0.012f, 0.18f},
                     {s * 0.094f, -0.075f, 0.03f}, {s * 0.098f, -0.16f, -0.085f}, {s * 0.10f, -0.215f, -0.125f}};
        spar.profile = Superellipse(0.015f, 0.032f, 4.0f, 16);
        spar.scale = [](float t) { return Vector2{1.0f, 1.0f - 0.2f * t}; };
        p.Add(BuildSweep(spar), kTrialAlu);
        p.RBox({s * 0.10f, -0.27f, -0.13f}, {0.03f, 0.15f, 0.11f}, kTrialAlu);                      // placas del basculante
        p.DiscX({s * 0.117f, -0.27f, -0.13f}, 0.016f, 0.01f, kSteel);
        p.Tube({Vector3Add(onAxis(-0.165f), {s * 0.018f, 0.0f, -0.005f}), {s * 0.06f, -0.045f, 0.37f}, {s * 0.093f, -0.14f, 0.285f},
                {s * 0.09f, -0.29f, 0.25f}, {s * 0.07f, -0.40f, 0.22f}}, 0.014f, kTrialAlu);         // caños delanteros
        p.Tube({{s * 0.094f, -0.07f, 0.02f}, {s * 0.082f, 0.015f, -0.25f}, {s * 0.06f, 0.035f, -0.56f}}, 0.011f, kTrialAlu);   // subchasis
        p.Tube({{s * 0.10f, -0.20f, -0.14f}, {s * 0.09f, -0.10f, -0.33f}, {s * 0.06f, 0.03f, -0.55f}}, 0.0095f, kTrialAlu);
        // Manija de la cola (para levantar la moto) que hace de patín en un wheelie pasado.
        p.Tube({{s * 0.06f, 0.035f, -0.60f}, {s * 0.068f, 0.0f, -0.80f}, {s * 0.058f, -0.055f, -0.95f}}, 0.009f, kTrialAlu);
    }
    p.Rod({-0.068f, -0.058f, -0.962f}, {0.068f, -0.058f, -0.962f}, 0.011f, kTrialAlu);
    p.Rod({-0.12f, -0.27f, -0.13f}, {0.12f, -0.27f, -0.13f}, 0.011f, kSteel);                       // eje del basculante
    p.Rod({-0.085f, 0.035f, -0.17f}, {0.085f, 0.035f, -0.17f}, 0.011f, kTrialAlu);                  // travesaño del amortiguador
    p.RBox({0.0f, 0.03f, -0.17f}, {0.05f, 0.03f, 0.045f}, kTrialAlu);

    // --- Cubrecárter enorme de aluminio: del frente del motor hasta abajo del basculante.
    Sweep plate;
    plate.path = {{0.0f, -0.35f, 0.29f}, {0.0f, -0.425f, 0.24f}, {0.0f, -0.445f, 0.13f}, {0.0f, -0.447f, -0.02f},
                  {0.0f, -0.437f, -0.12f}, {0.0f, -0.39f, -0.19f}};
    plate.profile = Superellipse(0.092f, 0.004f, 8.0f, 28);
    p.Add(BuildSweep(plate), kTrialAlu);
    for (float z : {0.2f, 0.02f, -0.13f})
        for (float s : {-1.0f, 1.0f}) p.Rod({s * 0.07f, -0.44f, z}, {s * 0.07f, -0.452f, z}, 0.008f, kSteel);   // tornillos

    // --- Motor dos tiempos enfriado por agua: cárter angosto con las tapas, cilindro casi vertical, tapa
    // lisa con la pipa de bujía, carburador atrás y bomba de agua (izquierda).
    p.RBox({0.0f, -0.335f, 0.05f}, {0.155f, 0.19f, 0.30f}, kTrialCase);
    p.DiscX({-0.083f, -0.34f, 0.03f}, 0.078f, 0.022f, kCover);                                      // embrague (derecha)
    p.DiscX({-0.095f, -0.34f, 0.03f}, 0.03f, 0.006f, kSteel);
    p.DiscX({0.083f, -0.32f, 0.09f}, 0.058f, 0.022f, kCover);                                       // encendido (izquierda)
    p.DiscX({0.083f, -0.405f, 0.18f}, 0.026f, 0.02f, kCover);                                       // bomba de agua
    p.RBox({0.085f, -0.29f, -0.065f}, {0.02f, 0.06f, 0.07f}, kPlasticBlack);                        // tapa del piñón
    const Quaternion tilt = RotX(12.0f);
    const Vector3 cylAxis = Vector3RotateByQuaternion({0.0f, 1.0f, 0.0f}, tilt);
    const Vector3 cylBase = {0.0f, -0.20f, 0.12f};
    p.RBox(cylBase, {0.11f, 0.13f, 0.115f}, kTrialCase, tilt, 0.3f);
    const Vector3 head = Vector3Add(cylBase, Vector3Scale(cylAxis, 0.09f));
    p.RBox(head, {0.125f, 0.045f, 0.125f}, kCover, tilt, 0.3f);
    for (int k = 0; k < 3; ++k) p.RBox(Vector3Add(head, Vector3Scale(cylAxis, -0.012f + 0.012f * (float)k)), {0.132f, 0.004f, 0.132f}, kEngineFin, tilt, 0.25f);
    p.Rod(Vector3Add(head, Vector3Scale(cylAxis, 0.02f)), Vector3Add(head, Vector3Scale(cylAxis, 0.05f)), 0.011f, kPlasticBlack);
    p.Rod({0.0f, -0.195f, 0.06f}, {0.0f, -0.18f, -0.03f}, 0.025f, kCover);                          // carburador
    p.RBox({0.0f, -0.225f, 0.015f}, {0.04f, 0.04f, 0.04f}, kCover);
    p.Rod({0.0f, -0.18f, -0.03f}, {0.0f, -0.12f, -0.07f}, 0.028f, kRubber);
    p.RBox({0.0f, -0.055f, -0.04f}, {0.14f, 0.11f, 0.19f}, kPlasticBlack, kNoRot, 0.4f);           // caja de filtro

    // --- Radiador chico con ventilador, tapa arriba, mangueras y sus cachas del color de la moto.
    const Quaternion radTilt = RotX(22.0f);
    const Vector3 radC = {0.0f, -0.10f, 0.30f}, radN = Vector3RotateByQuaternion({0.0f, 0.0f, 1.0f}, radTilt);
    p.RBox(radC, {0.15f, 0.15f, 0.03f}, kRadiator, radTilt, 0.2f);
    p.Rod(Vector3Add(radC, Vector3Scale(radN, -0.016f)), Vector3Add(radC, Vector3Scale(radN, -0.034f)), 0.055f, kPlasticBlack);
    p.RBox(Vector3Add(radC, Vector3RotateByQuaternion({0.0f, 0.082f, 0.0f}, radTilt)), {0.16f, 0.018f, 0.04f}, kPlasticBlack, radTilt, 0.3f);
    p.Tube({Vector3Add(head, Vector3Scale(cylAxis, 0.01f)), {0.02f, -0.055f, 0.23f}, {0.03f, -0.035f, 0.28f}}, 0.009f, kPlasticBlack);
    p.Tube({{0.05f, -0.165f, 0.27f}, {0.07f, -0.29f, 0.225f}, {0.085f, -0.39f, 0.19f}}, 0.009f, kPlasticBlack);
    const Profile shroud = {{0.23f, 0.0f}, {0.335f, 0.012f}, {0.365f, -0.065f}, {0.33f, -0.17f}, {0.255f, -0.18f}, {0.215f, -0.09f}};
    auto shroudOff = [](float u, float) { return 0.108f + 0.012f * Clamp01((u - 0.2f) / 0.18f); };
    const Profile shroudStripe = {{0.295f, 0.007f}, {0.325f, 0.01f}, {0.285f, -0.176f}, {0.258f, -0.178f}};

    // --- Escape (derecha): sale adelante del cilindro, baja y da la vuelta por abajo del motor (la panza
    // de expansión del dos tiempos, con el tornasolado del calor), sube detrás de la estribera y termina
    // en un silenciador chico de aluminio al costado de la rueda.
    Sweep header;
    header.path = {{0.0f, -0.185f, 0.18f}, {-0.004f, -0.235f, 0.245f}, {-0.035f, -0.31f, 0.262f}, {-0.08f, -0.375f, 0.215f},
                   {-0.108f, -0.395f, 0.12f}, {-0.115f, -0.395f, 0.01f}, {-0.13f, -0.37f, -0.07f}, {-0.14f, -0.29f, -0.17f},
                   {-0.135f, -0.17f, -0.28f}, {-0.13f, -0.095f, -0.35f}};
    header.profile = Circle(0.021f, 12);
    header.scale = [](float t) {
        const float k = 0.85f + 0.85f * Smooth(0.18f, 0.42f, t) * (1.0f - Smooth(0.62f, 0.9f, t));
        return Vector2{k, k};
    };
    header.transport = true;
    header.caps = false;
    header.color = [](float t, int) {
        const Color hot = {206, 150, 80, 255}, blue = {96, 86, 150, 255}, cold = {150, 150, 154, 255};
        return t < 0.2f ? Mix(hot, blue, t / 0.2f) : Mix(blue, cold, (t - 0.2f) / 0.25f);
    };
    p.Add(BuildSweep(header), {WHITE, 0.7f});
    Sweep muffler;
    muffler.path = {{-0.13f, -0.095f, -0.35f}, {-0.133f, -0.065f, -0.45f}, {-0.135f, -0.035f, -0.56f}};
    muffler.profile = Circle(0.036f, 16);
    muffler.scale = [](float t) {
        const float k = 0.6f + 0.4f * Smooth(0.0f, 0.2f, t);
        return Vector2{k, k};
    };
    muffler.edges = {0.9f};
    muffler.color = [](float t, int) { return t > 0.9f ? Color{40, 40, 44, 255} : Color{208, 210, 214, 255}; };
    p.Add(BuildSweep(muffler), {WHITE, 0.75f});
    p.Rod({-0.135f, -0.035f, -0.56f}, {-0.136f, -0.03f, -0.585f}, 0.011f, kSteel);                  // boca
    p.Bar({-0.12f, -0.07f, -0.44f}, {-0.075f, 0.02f, -0.45f}, 0.012f, 0.02f, kTrialAlu);            // soporte

    // --- Estriberas anchas de trial (dientes arriba), pedal de freno, cambios y patada plegada.
    for (float s : {-1.0f, 1.0f}) {
        p.RBox({s * 0.165f, -0.47f, -0.08f}, {0.085f, 0.018f, 0.062f}, kSteel, kNoRot, 0.25f);
        for (int k = 0; k < 4; ++k) p.Box({s * (0.132f + 0.022f * (float)k), -0.459f, -0.08f}, {0.004f, 0.006f, 0.06f}, kSteel);
        p.Bar({s * 0.10f, -0.35f, -0.11f}, {s * 0.125f, -0.465f, -0.085f}, 0.02f, 0.03f, kTrialAlu);
    }
    p.Tube({{-0.11f, -0.35f, -0.12f}, {-0.125f, -0.425f, -0.03f}, {-0.135f, -0.44f, 0.04f}}, 0.007f, kSteel);   // freno
    p.RBox({-0.14f, -0.443f, 0.05f}, {0.03f, 0.01f, 0.025f}, kSteel);
    p.Tube({{0.09f, -0.29f, -0.02f}, {0.12f, -0.38f, 0.02f}, {0.14f, -0.43f, 0.055f}}, 0.006f, kSteel);          // cambios
    p.Rod({0.13f, -0.43f, 0.06f}, {0.16f, -0.43f, 0.06f}, 0.007f, kSteel);
    p.Tube({{-0.092f, -0.30f, -0.03f}, {-0.112f, -0.26f, -0.10f}, {-0.118f, -0.22f, -0.165f}}, 0.008f, kSteel);  // patada

    // --- Tapa chiquita del tanque entre las vigas, con su tapón y una franja; casi sin asiento
    // (una almohadilla), guardabarros trasero mínimo con el LED y los laterales con "300".
    Sweep tank;
    tank.path = {{0.0f, 0.105f, 0.405f}, {0.0f, 0.085f, 0.28f}, {0.0f, 0.058f, 0.15f}, {0.0f, 0.045f, 0.06f}};
    tank.profile = Superellipse(0.07f, 0.04f, 3.0f, 24);
    tank.scale = [](float t) { return Vector2{0.75f + 0.25f * Smooth(0.0f, 0.4f, t), 1.0f - 0.15f * t}; };
    const Profile tankProfile = tank.profile;
    tank.color = [tankProfile, a = L.stripe1, base = L.plastic](float t, int j) {
        const Vector2 q = tankProfile[j];
        const float band = q.y + 0.12f * (t - 0.45f);                                   // franja en diagonal a los costados
        return (std::fabs(q.x) > 0.035f && band > -0.012f && band < 0.004f) ? a : base;
    };
    p.Add(BuildSweep(tank), {WHITE, 0.55f});
    p.Rod({0.0f, 0.125f, 0.33f}, {0.0f, 0.142f, 0.33f}, 0.02f, kPlasticBlack);
    Sweep rear;                  // cola: desde la caja de filtro, ancha sobre los laterales, se afina y sube al final
    rear.path = {{0.0f, 0.058f, -0.07f}, {0.0f, 0.058f, -0.25f}, {0.0f, 0.064f, -0.45f}, {0.0f, 0.08f, -0.62f}, {0.0f, 0.108f, -0.76f},
                 {0.0f, 0.15f, -0.87f}};
    rear.profile = Arch(0.07f, 0.016f, 0.005f);
    rear.scale = [](float t) { return Vector2{1.4f - 0.8f * t, 1.0f}; };
    p.Add(BuildSweep(rear), plastic);
    p.RBox({0.0f, 0.072f, -0.36f}, {0.09f, 0.02f, 0.15f}, kSeat, kNoRot, 0.5f);                   // almohadilla
    p.RBox({0.0f, 0.136f, -0.862f}, {0.045f, 0.018f, 0.024f}, kTailRed, RotX(-35.0f), 0.4f);      // LED de la cola
    const Profile sidePanel = {{-0.06f, 0.045f}, {-0.40f, 0.062f}, {-0.45f, 0.03f}, {-0.37f, -0.085f}, {-0.19f, -0.13f}, {-0.07f, -0.10f}};
    auto sideOff = [](float u, float) { const float k = (u + 0.25f) / 0.22f; return 0.102f + 0.007f * Clamp01(1.0f - k * k); };
    auto decalOff = [sideOff](float u, float v) { return sideOff(u, v) + 0.0035f; };
    const Profile sideStripe = {{-0.11f, 0.048f}, {-0.38f, 0.06f}, {-0.40f, 0.037f}, {-0.13f, 0.024f}};
    for (float s : {-1.0f, 1.0f}) {
        const Matrix toSide = Basis({0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {s, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f});
        p.Add(Panel(sidePanel, 0.01f, sideOff), plastic, toSide);
        p.Add(Panel(sideStripe, 0.003f, decalOff, 2), stripeA, toSide);
        p.Add(Panel(shroud, 0.008f, shroudOff), plastic, toSide);
        p.Add(Panel(shroudStripe, 0.003f, [shroudOff](float u, float v) { return shroudOff(u, v) + 0.003f; }, 2), stripeB, toSide);
        // "300" derecho de los dos lados (girado, no espejado), como en la trilheira.
        const Vector3 at = {s * (sideOff(-0.25f, 0.0f) + 0.004f), -0.045f, -0.25f};
        p.Add(Digits("300"), stripeB,
              MatrixMultiply(MatrixMultiply(MatrixScale(0.055f, 0.055f, 0.055f), MatrixRotateY(s * kPi * 0.5f)), MatrixTranslate(at.x, at.y, at.z)));
    }
    return b.Build();
}

const BikeStyleDef& TrialStyleDef()
{
    // Piloto SIEMPRE de pie: la pose de pie va en hips/shoulders/knee (cadera sobre las estriberas,
    // rodillas dobladas contra las vigas, torso ~35° adelante y brazos doblados con los codos afuera).
    // Al correr el peso la cadera viaja mucho más que los hombros (las manos siguen en el manubrio):
    // adelante queda la cadera debajo del pecho, atrás la cola afuera y los brazos estirados.
    // stand queda en 0 a propósito: en Bike::RiderPoseLocal stand = 1 lleva al piloto a la pose del
    // wheelie pasado (hecha para la moto casi vertical), que con la moto derecha lo deja hecho un bollo
    // sobre el tanque; esa mezcla sigue pasando sola cuando la moto se para en la cola.
    static const BikeStyleDef d = {
        "trial", GenTrialMeshes, GenTrialLiveryMeshes, GenTrialBody, TrialLivery,
        JPH::Vec3(0.0f, 0.165f, -0.055f), 0.352f,             // manubrio alto y ancho (BuildTrialCockpit)
        JPH::Vec3(0.16f, -0.47f, -0.08f),                      // estriberas bajas
        JPH::Vec3(0.0f, 0.36f, -0.15f), JPH::Vec3(0.0f, 0.745f, 0.13f), JPH::Vec3(0.18f, -0.02f, 0.10f),
        0.15f, 0.06f, 0.0f, 0.0f,
        JPH::Vec3(-0.136f, -0.03f, -0.585f), JPH::Vec3(-0.02f, 0.25f, -0.97f).Normalized(), false,
    };
    return d;
}
// ==== END TRIAL ====
