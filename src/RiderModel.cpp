#include <Jolt/Jolt.h>

#include "RiderModel.h"

#include "MathUtil.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

// Ajustes de la pose del modelo sobre la moto (sus proporciones no son las del piloto generado).
constexpr float kHipsDrop = 0.05f;       // m que baja la cadera: rodillas más dobladas (posición de ataque)
constexpr float kExtraLean = 0.1f;       // rad extra de torso hacia adelante respecto de la pose
constexpr float kForwardLean = 0.35f;    // rad más tirado adelante: pecho sobre el manubrio...
constexpr float kForwardHipsBack = 0.08f;// ...con la cola en el asiento y no sobre el tanque
constexpr float kWristBack = 0.07f;      // m de la muñeca al centro del puño del manubrio
constexpr float kMaxReach = 0.97f;       // brazo como mucho al 97%: nunca del todo estirado
constexpr float kMaxExtraPitch = 0.45f;  // rad que puede sumar el torso para llegar al manubrio...
constexpr float kMaxHipShift = 0.3f;     // ...y después m que se acerca la cadera
constexpr float kThrottleRoll = 0.45f;   // rad que gira la mano derecha sobre el puño con el gas a fondo

Vector3 V(JPH::Vec3 v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
Quaternion Q(JPH::Quat q) { return {q.GetX(), q.GetY(), q.GetZ(), q.GetW()}; }

Quaternion FromTo(Vector3 a, Vector3 b)
{
    return QuaternionFromVector3ToVector3(Vector3Normalize(a), Vector3Normalize(b));
}

// Dos huesos A-B-C de largos l1, l2: dónde queda la articulación del medio para que la punta llegue a
// `target` (o lo más cerca que pueda), doblando hacia el lado de `pole`.
void TwoBoneIK(Vector3 a, Vector3 target, float l1, float l2, Vector3 pole, Vector3& mid, Vector3& tip)
{
    const Vector3 d = Vector3Subtract(target, a);
    const float dist = mu::Clamp(Vector3Length(d), std::fabs(l1 - l2) + 1e-3f, (l1 + l2) * 0.999f);
    const Vector3 dir = Vector3Normalize(d);
    const float along = (l1 * l1 - l2 * l2 + dist * dist) / (2.0f * dist);
    const float h = std::sqrt(std::max(0.0f, l1 * l1 - along * along));
    Vector3 side = Vector3Subtract(pole, a);
    side = Vector3Subtract(side, Vector3Scale(dir, Vector3DotProduct(side, dir)));
    side = Vector3Length(side) > 1e-4f ? Vector3Normalize(side) : Vector3Perpendicular(dir);
    mid = Vector3Add(a, Vector3Add(Vector3Scale(dir, along), Vector3Scale(side, h)));
    tip = Vector3Add(a, Vector3Scale(dir, dist));
}

} // namespace

int RiderModel::Bone(const char* name) const
{
    for (int i = 0; i < model.boneCount; ++i)
        if (std::strcmp(model.bones[i].name, name) == 0) return i;
    return -1;
}

bool RiderModel::Load(const std::vector<std::string>& paths)
{
    for (const std::string& path : paths) {
        if (!FileExists(path.c_str())) continue;
        model = LoadModel(path.c_str());
        break;
    }
    if (model.meshCount < 1 || model.boneCount < 1 || !model.meshes[0].boneIds || !model.meshes[0].animVertices) {
        if (model.meshCount > 0) UnloadModel(model);
        model = Model{};
        return false;
    }

    hips = Bone("Hips");
    spine = Bone("Spine02");
    spineChain[0] = spine;                   // columna de abajo hacia arriba (las que falten, -1)
    spineChain[1] = Bone("Spine01");
    spineChain[2] = Bone("Spine");
    neck = Bone("neck");
    head = Bone("Head");
    const char* sides[2] = {"Left", "Right"};                  // [0] = izquierda (+X), como RiderPose
    for (int i = 0; i < 2; ++i) {
        auto bone = [&](const char* suffix) { return Bone((std::string(sides[i]) + suffix).c_str()); };
        arm[i] = bone("Arm");
        foreArm[i] = bone("ForeArm");
        hand[i] = bone("Hand");
        upLeg[i] = bone("UpLeg");
        leg[i] = bone("Leg");
        foot[i] = bone("Foot");
        toe[i] = bone("ToeBase");
    }
    for (int b : {hips, spine, neck, head, arm[0], arm[1], foreArm[0], foreArm[1], hand[0], hand[1], upLeg[0], upLeg[1],
                  leg[0], leg[1], foot[0], foot[1], toe[0], toe[1]}) {
        if (b < 0) {
            TraceLog(LOG_WARNING, "piloto: el modelo no tiene los huesos esperados, se usa el generado");
            UnloadModel(model);
            model = Model{};
            return false;
        }
    }

    const int n = model.boneCount;
    bindPos.resize(n);
    parent.resize(n);
    for (int i = 0; i < n; ++i) {
        bindPos[i] = model.bindPose[i].translation;
        parent[i] = model.bones[i].parent;
    }
    // Padres antes que hijos.
    std::vector<int> depth(n, 0);
    for (int i = 0; i < n; ++i)
        for (int p = parent[i]; p >= 0; p = parent[p]) ++depth[i];
    order.resize(n);
    for (int i = 0; i < n; ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return depth[a] < depth[b]; });

    // raylib aplica a los vértices la escala del nodo del esqueleto (0.01 en exportaciones de
    // Blender en centímetros) pero no a la pose de los huesos: se deshace para que coincidan.
    Mesh& m = model.meshes[0];
    float maxY = 0.0f;
    for (int v = 0; v < m.vertexCount; ++v) maxY = std::max(maxY, m.vertices[v * 3 + 1]);
    const float scaleFix = (maxY < 0.2f * bindPos[hips].y && model.bindPose[hips].scale.y > 0.0f) ? 1.0f / model.bindPose[hips].scale.y : 1.0f;
    bindVerts.resize(m.vertexCount);
    bindNormals.resize(m.vertexCount);
    for (int v = 0; v < m.vertexCount; ++v) {
        bindVerts[v] = Vector3Scale({m.vertices[v * 3], m.vertices[v * 3 + 1], m.vertices[v * 3 + 2]}, scaleFix);
        bindNormals[v] = m.normals ? Vector3Normalize({m.normals[v * 3], m.normals[v * 3 + 1], m.normals[v * 3 + 2]}) : Vector3{0.0f, 1.0f, 0.0f};
    }

    // Parte del ragdoll de cada hueso (los que no están en la lista siguen a su padre).
    partOf.assign(n, -1);
    auto assign = [&](int b, int part) { if (b >= 0) partOf[b] = part; };
    assign(hips, RiderPart::Pelvis);
    assign(spine, RiderPart::Torso);
    assign(neck, RiderPart::Head);           // el cuello va con la cabeza: el ragdoll la articula en su base
    assign(head, RiderPart::Head);
    for (int i = 0; i < 2; ++i) {
        assign(arm[i], RiderPart::UpperArm + i);
        assign(foreArm[i], RiderPart::Forearm + i);
        assign(upLeg[i], RiderPart::Thigh + i);
        assign(leg[i], RiderPart::Shin + i);
    }
    for (int b : order)
        if (partOf[b] < 0) partOf[b] = parent[b] >= 0 ? partOf[parent[b]] : RiderPart::Pelvis;

    texture = model.materials[model.meshMaterial[0]].maps[MATERIAL_MAP_DIFFUSE].texture;
    GenTextureMipmaps(&texture);
    SetTextureFilter(texture, TEXTURE_FILTER_TRILINEAR);

    // Pesos propios (se les suman los dedos virtuales).
    realBones = n;
    skinBone.assign(m.boneIds, m.boneIds + m.vertexCount * 4);
    skinWeight.assign(m.boneWeights, m.boneWeights + m.vertexCount * 4);
    curlAngle.assign(n, 0.0f);
    curlAxis.assign(n, Vector3{0.0f, 0.0f, 0.0f});
    for (int i = 0; i < 2; ++i) BuildGrip(i);

    pos = bindPos;
    delta.assign(bindPos.size(), QuaternionIdentity());
    loaded = true;
    TraceLog(LOG_INFO, "piloto: modelo con %d huesos (+%d de dedos) y %d vértices", n, (int)bindPos.size() - n, m.vertexCount);
    return true;
}

// Arma los dedos de una mano. Mide la mano en reposo en sus propios ejes (s a lo largo desde la
// muñeca, t a lo ancho hacia el pulgar, u hacia la palma): nudillos al 65% del largo, el pulgar es
// lo que sobresale a un costado de la palma, y la flexión de cada falange se calcula para que el
// dedo envuelva un puño del radio del manubrio apoyado en la palma, 2 cm antes de los nudillos
// (como en un agarre de fuerza: así el pulgar llega a cerrarlo por el otro lado).
void RiderModel::BuildGrip(int side)
{
    constexpr float kBarRadius = 0.0175f;    // puño de goma del manubrio (BikeMeshes: Cockpit)
    const Mesh& m = model.meshes[0];
    const int hb = hand[side];
    const Vector3 W = bindPos[hb];
    auto handWeight = [&](int v) {
        float w = 0.0f;
        for (int k = 0; k < 4; ++k)
            if (skinBone[v * 4 + k] == hb) w += skinWeight[v * 4 + k];
        return w;
    };

    // Eje a lo largo: de la muñeca al centro de las puntas de los dedos (el 10% más lejano).
    const Vector3 fore = Vector3Normalize(Vector3Subtract(W, bindPos[foreArm[side]]));
    std::vector<std::pair<float, int>> byS;
    for (int v = 0; v < m.vertexCount; ++v)
        if (handWeight(v) >= 0.5f) byS.push_back({Vector3DotProduct(Vector3Subtract(bindVerts[v], W), fore), v});
    if (byS.size() < 100) return;
    std::sort(byS.begin(), byS.end());
    Vector3 tip = {0.0f, 0.0f, 0.0f};
    const int tipCount = (int)byS.size() / 10;
    for (int i = (int)byS.size() - tipCount; i < (int)byS.size(); ++i) tip = Vector3Add(tip, bindVerts[byS[i].second]);
    tip = Vector3Scale(tip, 1.0f / (float)tipCount);
    const Vector3 A = Vector3Normalize(Vector3Subtract(tip, W));

    // A lo ancho: la dirección de más extensión perpendicular a A (análisis de componentes principales).
    const Vector3 e1 = Vector3Normalize(Vector3Perpendicular(A)), e2 = Vector3CrossProduct(A, e1);
    float mx = 0.0f, my = 0.0f, cxx = 0.0f, cxy = 0.0f, cyy = 0.0f;
    for (const auto& sv : byS) {
        const Vector3 d = Vector3Subtract(bindVerts[sv.second], W);
        mx += Vector3DotProduct(d, e1);
        my += Vector3DotProduct(d, e2);
    }
    mx /= (float)byS.size();
    my /= (float)byS.size();
    for (const auto& sv : byS) {
        const Vector3 d = Vector3Subtract(bindVerts[sv.second], W);
        const float a = Vector3DotProduct(d, e1) - mx, b = Vector3DotProduct(d, e2) - my;
        cxx += a * a;
        cxy += a * b;
        cyy += b * b;
    }
    const float ang = 0.5f * std::atan2(2.0f * cxy, cxx - cyy);
    Vector3 K = Vector3Add(Vector3Scale(e1, std::cos(ang)), Vector3Scale(e2, std::sin(ang)));
    Vector3 P = Vector3CrossProduct(A, K);
    if (P.x * W.x > 0.0f) P = Vector3Negate(P);                     // la palma mira al centro del cuerpo
    K = Vector3CrossProduct(P, A);

    struct HV { int v; float s, t, u; };
    std::vector<HV> hv;
    float sMax = 0.0f;
    for (const auto& sv : byS) {
        const Vector3 d = Vector3Subtract(bindVerts[sv.second], W);
        hv.push_back({sv.second, Vector3DotProduct(d, A), Vector3DotProduct(d, K), Vector3DotProduct(d, P)});
        sMax = std::max(sMax, hv.back().s);
    }
    const float sK = 0.65f * sMax;                                   // nudillos

    // Pulgar: el costado donde la mano, entre la muñeca y los nudillos, sobresale más que la palma
    // cerca de la muñeca. Se da vuelta K para que quede en +t.
    auto range = [&](float s0, float s1, float& lo, float& hi) {
        lo = 1e9f;
        hi = -1e9f;
        for (const HV& h : hv)
            if (h.s >= s0 && h.s <= s1) {
                lo = std::min(lo, h.t);
                hi = std::max(hi, h.t);
            }
    };
    float wLo, wHi, mLo, mHi;
    range(0.08f * sMax, 0.3f * sMax, wLo, wHi);
    range(0.35f * sMax, 0.62f * sMax, mLo, mHi);
    if (wLo - mLo > mHi - wHi) {
        K = Vector3Negate(K);
        for (HV& h : hv) h.t = -h.t;
        std::swap(wLo, wHi);
        wLo = -wLo;
        wHi = -wHi;
    }
    const float tEdge = wHi;                                         // borde de la palma del lado del pulgar

    auto smooth = [](float e0, float e1, float x) {
        const float k = mu::Clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
        return k * k * (3.0f - 2.0f * k);
    };
    auto thumbness = [&](const HV& h) {
        return smooth(tEdge, tEdge + 0.008f, h.t) * (1.0f - smooth(sK - 0.005f, sK + 0.008f, h.s)) * smooth(0.25f * sMax, 0.35f * sMax, h.s);
    };
    // Promedio de u (el centro del dedo o del pulgar) cerca de una altura s.
    auto centerU = [&](float s, bool thumb) {
        float sum = 0.0f;
        int count = 0;
        for (const HV& h : hv)
            if (std::fabs(h.s - s) < 0.004f && ((thumbness(h) > 0.5f) == thumb) && (thumb || h.t < tEdge)) {
                sum += h.u;
                ++count;
            }
        return count > 0 ? sum / (float)count : 0.0f;
    };

    // Dedos: articulaciones al 0, 42 y 72% del largo del dedo; el ancho que ocupan los cuatro.
    const float fs[4] = {sK, sK + 0.42f * (sMax - sK), sK + 0.72f * (sMax - sK), sMax - 0.004f};
    float tLo = 1e9f, tHi = -1e9f, uPalm = -1e9f, uLo = 1e9f, uHi = -1e9f;
    int thumbCount = 0;
    float thumbS0 = 1e9f, thumbS1 = -1e9f;
    for (const HV& h : hv) {
        if (thumbness(h) > 0.5f) {
            ++thumbCount;
            thumbS0 = std::min(thumbS0, h.s);
            thumbS1 = std::max(thumbS1, h.s);
            continue;
        }
        if (h.s > sK + 0.01f) {
            tLo = std::min(tLo, h.t);
            tHi = std::max(tHi, h.t);
        }
        if (h.s > sK - 0.02f && h.s < sK && h.t < tEdge) uPalm = std::max(uPalm, h.u);
        if (std::fabs(h.s - fs[1]) < 0.003f) {
            uLo = std::min(uLo, h.u);
            uHi = std::max(uHi, h.u);
        }
    }
    if (thumbCount < 10 || tHi <= tLo) return;
    const float halfFinger = std::min(0.012f, 0.5f * (uHi - uLo));

    // Flexión: cada falange gira (hacia la palma) hasta que la articulación siguiente queda a la
    // distancia del puño; la última, hasta que la punta llega.
    const Vector2 channel = {sK - 0.02f, uPalm + kBarRadius - 0.002f};
    const float wrap = kBarRadius + halfFinger;
    Vector2 J[4];
    for (int j = 0; j < 4; ++j) J[j] = {fs[j], centerU(fs[j], false)};
    auto rotate2 = [](Vector2 p, Vector2 c, float a) {             // +a lleva +s hacia +u
        const Vector2 d = Vector2Subtract(p, c);
        return Vector2Add(c, {d.x * std::cos(a) - d.y * std::sin(a), d.x * std::sin(a) + d.y * std::cos(a)});
    };
    float fingerCurl[3];
    for (int j = 0; j < 3; ++j) {
        float best = 0.0f, bestDist = 1e9f;
        for (int deg = 0; deg <= 110; ++deg) {
            const float a = mu::Rad((float)deg);
            const float dist = Vector2Distance(rotate2(J[j + 1], J[j], a), channel);
            if (dist < bestDist) {
                bestDist = dist;
                best = a;
            }
            if (dist <= wrap) break;
        }
        fingerCurl[j] = best + (j > 0 ? mu::Rad(8.0f) : 0.0f);         // un poco más: aprieta la goma
        for (int k = j + 1; k < 4; ++k) J[k] = rotate2(J[k], J[j], best);
    }

    // Huesos virtuales: pivotes sobre la línea media de los dedos y del pulgar.
    const float tMid = 0.5f * (tLo + tHi);
    auto point = [&](float s, float t, float u) {
        return Vector3Add(W, Vector3Add(Vector3Scale(A, s), Vector3Add(Vector3Scale(K, t), Vector3Scale(P, u))));
    };
    const Vector3 axis = Vector3Normalize(Vector3CrossProduct(A, P));   // girar +: de "a lo largo" hacia la palma
    auto addBone = [&](int par, Vector3 at, float curl) {
        const int b = (int)bindPos.size();
        bindPos.push_back(at);
        parent.push_back(par);
        partOf.push_back(partOf[par]);
        order.push_back(b);
        curlAngle.push_back(curl);
        curlAxis.push_back(axis);
        return b;
    };
    int finger[3];
    int par = hb;
    for (int j = 0; j < 3; ++j) par = finger[j] = addBone(par, point(fs[j], tMid, centerU(fs[j], false)), fingerCurl[j]);
    // Pulgar: sale hacia afuera de la palma rodeando el puño del lado de la muñeca y se dobla de
    // vuelta sobre él, hasta tocar los dedos.
    const float sT[2] = {0.3f * sMax, 0.5f * (thumbS0 + thumbS1)};
    const int thumb0 = addBone(hb, point(sT[0], tEdge, centerU(sT[0] + 0.01f, true)), mu::Rad(35.0f));
    const int thumb1 = addBone(thumb0, point(sT[1], tEdge + 0.02f, centerU(sT[1], true)), mu::Rad(-20.0f));

    // Pesos: palma -> falanges con transiciones suaves en cada articulación.
    for (const HV& h : hv) {
        if (handWeight(h.v) < 0.98f) continue;                        // la muñeca queda como estaba
        const float tf = thumbness(h);
        const float ff = (1.0f - tf) * smooth(sK - 0.006f, sK + 0.006f, h.s);
        const float a2 = smooth(fs[1] - 0.004f, fs[1] + 0.004f, h.s), a3 = smooth(fs[2] - 0.004f, fs[2] + 0.004f, h.s);
        const float aT = smooth(sT[1] - 0.005f, sT[1] + 0.005f, h.s);
        std::pair<float, int> w[6] = {{1.0f - ff - tf, hb}, {ff * (1.0f - a2), finger[0]}, {ff * (a2 - a3), finger[1]},
                                      {ff * a3, finger[2]}, {tf * (1.0f - aT), thumb0}, {tf * aT, thumb1}};
        std::sort(w, w + 6, [](const auto& a, const auto& b) { return a.first > b.first; });
        float total = 0.0f;
        for (int k = 0; k < 4; ++k) total += std::max(w[k].first, 0.0f);
        for (int k = 0; k < 4; ++k) {
            skinBone[h.v * 4 + k] = w[k].second;
            skinWeight[h.v * 4 + k] = std::max(w[k].first, 0.0f) / std::max(total, 1e-6f);
        }
    }

    Grip& g = grip[side];
    g.across = K;
    g.along = A;
    g.palm = P;
    g.channel = point(channel.x, tMid, channel.y);
    g.valid = true;
    TraceLog(LOG_INFO, "piloto: mano %s con dedos (flexión %.0f/%.0f/%.0f°)", side == 0 ? "izquierda" : "derecha",
             mu::Deg(fingerCurl[0]), mu::Deg(fingerCurl[1]), mu::Deg(fingerCurl[2]));
}

// Giro de la mano (respecto del reposo) para agarrar el manubrio: el ancho de la mano a lo largo
// del manubrio con el pulgar hacia adentro, y la mano en la dirección del antebrazo (muñeca derecha).
Quaternion RiderModel::GripRotation(int side, Vector3 barInward, Vector3 forearmDir) const
{
    const Grip& g = grip[side];
    const Vector3 D = Vector3Normalize(barInward);
    Vector3 A = Vector3Subtract(forearmDir, Vector3Scale(D, Vector3DotProduct(forearmDir, D)));
    A = Vector3Length(A) > 1e-4f ? Vector3Normalize(A) : Vector3Perpendicular(D);
    Vector3 P = Vector3CrossProduct(D, A);
    if (Vector3DotProduct(Vector3CrossProduct(g.across, g.along), g.palm) < 0.0f) P = Vector3Negate(P);   // misma mano (sin espejar)
    auto frame = [](Vector3 x, Vector3 y, Vector3 z) {
        Matrix mm = MatrixIdentity();
        mm.m0 = x.x; mm.m1 = x.y; mm.m2 = x.z;
        mm.m4 = y.x; mm.m5 = y.y; mm.m6 = y.z;
        mm.m8 = z.x; mm.m9 = z.y; mm.m10 = z.z;
        return mm;
    };
    // R = mundo * reposo^T: lleva los ejes de la mano en reposo a los pedidos.
    const Matrix R = MatrixMultiply(MatrixTranspose(frame(g.across, g.along, g.palm)), frame(D, A, P));
    return QuaternionNormalize(QuaternionFromMatrix(R));
}

void RiderModel::CurlFingers()
{
    for (int b = realBones; b < (int)bindPos.size(); ++b) {
        const int pa = parent[b];
        pos[b] = Vector3Add(pos[pa], Vector3RotateByQuaternion(Vector3Subtract(bindPos[b], bindPos[pa]), delta[pa]));
        delta[b] = QuaternionMultiply(QuaternionFromAxisAngle(Vector3RotateByQuaternion(curlAxis[b], delta[pa]), curlAngle[b] * gripAmount), delta[pa]);
    }
}

void RiderModel::Unload()
{
    if (loaded) UnloadModel(model);          // también libera la textura del material
    model = Model{};
    loaded = false;
}

void RiderModel::PoseOnBike(const RiderPose& p)
{
    // Las manos no se sueltan del manubrio: si con la inclinación de la pose no llegan (sobre todo con
    // el piloto tirado atrás), el torso se inclina más hacia adelante, lo justo para que lleguen.
    // Primero se inclina el torso (hasta un límite razonable); si todavía no llega, la cadera se acerca
    // al manubrio: tirado atrás queda la cola atrás y los brazos estirados, no acostado sobre el tanque.
    gripAmount = 1.0f;
    throttleS += (p.throttle - throttleS) * (1.0f - std::exp(-14.0f * GetFrameTime()));
    if (SolveBody(p, 0.0f, 0.0f) <= kMaxReach) return;
    auto search = [&](float hiValue, auto solve) {          // el valor mínimo con el que las manos llegan
        float lo = 0.0f, hi = hiValue;
        for (int k = 0; k < 10; ++k) {
            const float mid = 0.5f * (lo + hi);
            if (solve(mid) > kMaxReach) lo = mid;
            else hi = mid;
        }
        solve(hi);
    };
    if (SolveBody(p, kMaxExtraPitch, 0.0f) <= kMaxReach) {
        search(kMaxExtraPitch, [&](float pitch) { return SolveBody(p, pitch, 0.0f); });
    } else {
        search(kMaxHipShift, [&](float shift) { return SolveBody(p, kMaxExtraPitch, shift); });
    }
}

float RiderModel::SolveBody(const RiderPose& p, float extraPitch, float hipShift)
{
    // Cada hueso: si no tiene un ajuste propio, sigue a su padre (cinemática directa).
    const float forward = std::max(0.0f, p.lean);
    const Vector3 hipsTarget = Vector3Add(V(p.hips), {0.0f, -kHipsDrop + 0.3f * hipShift, hipShift - kForwardHipsBack * forward});
    const Vector3 torsoBind = Vector3Subtract(bindPos[neck], bindPos[hips]);
    Vector3 torsoTarget = Vector3Subtract(V(p.shoulders), V(p.hips));
    const float pitch = kExtraLean + kForwardLean * forward + extraPitch;
    torsoTarget = Vector3RotateByAxisAngle(torsoTarget, {1.0f, 0.0f, 0.0f}, pitch);   // + adelante
    float reach = 0.0f;                                     // distancia hombro-manubrio / largo del brazo
    const Quaternion torsoTurn = FromTo(torsoBind, torsoTarget);
    // La inclinación la toma sobre todo la pelvis (la bisagra está en las articulaciones de la cadera,
    // como en la posición de ataque) y un poco cada vértebra: espalda casi recta, sin quiebre en la
    // cintura. Girando sólo la columna la panza se inflaba y la espalda baja se doblaba.
    const Quaternion pelvisTurn = QuaternionSlerp(QuaternionIdentity(), torsoTurn, 0.75f);
    const Quaternion spineStep = QuaternionSlerp(QuaternionIdentity(), torsoTurn, 0.25f / 3.0f);

    Vector3 elbow[2], wrist[2], knee[2], ankle[2];
    Quaternion handRot[2] = {QuaternionIdentity(), QuaternionIdentity()};
    for (int b : order) {
        const int pa = parent[b];
        const Quaternion inherited = pa >= 0 ? delta[pa] : QuaternionIdentity();
        pos[b] = pa >= 0 ? Vector3Add(pos[pa], Vector3RotateByQuaternion(Vector3Subtract(bindPos[b], bindPos[pa]), inherited)) : bindPos[b];
        delta[b] = inherited;
        if (curlAngle[b] != 0.0f) {                                 // falange virtual: se dobla sobre su padre
            delta[b] = QuaternionMultiply(QuaternionFromAxisAngle(Vector3RotateByQuaternion(curlAxis[b], inherited), curlAngle[b] * gripAmount), inherited);
            continue;
        }

        // Gira el hueso para que su hijo (en reposo en childBind) quede apuntando a `target`.
        auto aim = [&](int child, Vector3 target) {
            const Vector3 now = Vector3RotateByQuaternion(Vector3Subtract(bindPos[child], bindPos[b]), delta[b]);
            delta[b] = QuaternionMultiply(FromTo(now, Vector3Subtract(target, pos[b])), delta[b]);
        };

        if (b == hips) {
            pos[b] = hipsTarget;
            delta[b] = pelvisTurn;
        } else if (b == spineChain[0] || b == spineChain[1] || b == spineChain[2]) {
            delta[b] = QuaternionMultiply(spineStep, delta[b]);
        } else if (b == head) {
            // Mirando la pista: la cara (+Z en reposo) hacia adelante y un poco abajo.
            const Vector3 face = Vector3RotateByQuaternion({0.0f, 0.0f, 1.0f}, delta[b]);
            delta[b] = QuaternionMultiply(FromTo(face, {0.0f, -0.2f, 1.0f}), delta[b]);
        }
        for (int i = 0; i < 2; ++i) {
            const float side = i == 0 ? 1.0f : -1.0f;               // +X = izquierda
            if (b == arm[i]) {
                const Vector3 gripAt = V(p.grip[i]);
                const float l1 = Vector3Distance(bindPos[foreArm[i]], bindPos[b]), l2 = Vector3Distance(bindPos[hand[i]], bindPos[foreArm[i]]);
                const Vector3 pole = Vector3Add(pos[b], {side * 1.0f, 0.1f, -0.3f});   // codos hacia afuera y arriba (posición de ataque)
                Vector3 target = Vector3Add(gripAt, Vector3Scale(Vector3Normalize(Vector3Subtract(pos[b], gripAt)), kWristBack));
                if (grip[i].valid) {
                    // La mano agarra el puño: su orientación sale del manubrio y del antebrazo, y de ahí dónde
                    // tiene que quedar la muñeca. Dos pasadas: el antebrazo depende de dónde quede la muñeca.
                    const Vector3 inward = Vector3Scale(Vector3Normalize(V(p.grip[0] - p.grip[1])), -side);   // hacia el pulgar
                    Vector3 fore = Vector3Normalize(Vector3Subtract(gripAt, pos[b]));
                    // La derecha gira el acelerador: la mano rueda sobre el puño, el dorso hacia atrás.
                    const Quaternion twist = QuaternionFromAxisAngle(inward, i == 1 ? -kThrottleRoll * throttleS : 0.0f);
                    for (int pass = 0; pass < 2; ++pass) {
                        handRot[i] = QuaternionMultiply(twist, GripRotation(i, inward, fore));
                        target = Vector3Subtract(gripAt, Vector3RotateByQuaternion(Vector3Subtract(grip[i].channel, bindPos[hand[i]]), handRot[i]));
                        TwoBoneIK(pos[b], target, l1, l2, pole, elbow[i], wrist[i]);
                        fore = Vector3Normalize(Vector3Subtract(wrist[i], elbow[i]));
                    }
                } else {
                    TwoBoneIK(pos[b], target, l1, l2, pole, elbow[i], wrist[i]);
                }
                reach = std::max(reach, Vector3Distance(pos[b], target) / (l1 + l2));
                aim(foreArm[i], elbow[i]);
            } else if (b == foreArm[i]) {
                aim(hand[i], wrist[i]);
            } else if (b == hand[i] && grip[i].valid) {
                delta[b] = handRot[i];
            } else if (b == upLeg[i]) {
                const float out = p.legOut[i];                              // pata afuera en la curva
                // El pie apoya en la estribera; afuera, el tobillo va donde lo pide la pose.
                const Vector3 target = Vector3Add(V(p.ankle[i]), Vector3Lerp({0.0f, 0.04f, -0.03f}, {0.0f, 0.0f, 0.0f}, out));
                const float l1 = Vector3Distance(bindPos[leg[i]], bindPos[b]), l2 = Vector3Distance(bindPos[foot[i]], bindPos[leg[i]]);
                // Rodillas adelante y apenas abiertas; con la pata afuera, adelante, arriba y afuera.
                const Vector3 pole = Vector3Lerp({side * 0.25f, 0.0f, 1.0f}, {side * 0.5f, 0.6f, 0.8f}, out);
                TwoBoneIK(pos[b], target, l1, l2, Vector3Add(pos[b], pole), knee[i], ankle[i]);
                aim(leg[i], knee[i]);
            } else if (b == leg[i]) {
                aim(foot[i], ankle[i]);
            } else if (b == foot[i]) {
                // Pie hacia adelante, casi plano; estirado afuera, con la punta para arriba.
                aim(toe[i], Vector3Add(pos[b], Vector3Lerp({0.0f, -0.05f, 0.15f}, {side * 0.03f, 0.08f, 0.15f}, p.legOut[i])));
            }
        }
    }
    return reach;
}

RiderPose RiderModel::JointPose(const RiderPose& p)
{
    PoseOnBike(p);
    RiderPose q = p;
    auto at = [&](int b) { return JPH::Vec3(pos[b].x, pos[b].y, pos[b].z); };
    // Las cuentas del ragdoll (Rider.cpp): la pelvis 2 cm arriba de hips, el cuello 8 cm arriba y 4 cm
    // adelante de shoulders y la cabeza (una esfera) centrada en head.
    q.hips = at(hips) - JPH::Vec3(0.0f, 0.02f, 0.0f);
    q.shoulders = at(neck) - JPH::Vec3(0.0f, 0.08f, 0.04f);
    const Vector3 headUp = Vector3RotateByQuaternion({0.0f, 0.10f, 0.02f}, delta[head]);
    q.head = at(head) + JPH::Vec3(headUp.x, headUp.y, headUp.z);
    for (int i = 0; i < 2; ++i) {
        q.hip[i] = at(upLeg[i]);
        q.knee[i] = at(leg[i]);
        q.ankle[i] = at(foot[i]);
        q.shoulder[i] = at(arm[i]);
        q.elbow[i] = at(foreArm[i]);
    }
    return q;
}

void RiderModel::BindToRagdoll(const RiderPose& p, const RiderRagdoll& ragdoll)
{
    JPH::Mat44 locals[RiderPart::Count];
    for (int i = 0; i < RiderPart::Count; ++i) locals[i] = ragdoll.PartSpawnLocal(i);
    BindToParts(p, locals);
}

void RiderModel::BindToParts(const RiderPose& p, const JPH::Mat44* partLocal)
{
    PoseOnBike(p);
    const int n = (int)pos.size();
    relPos.resize(n);
    relDelta.resize(n);
    for (int b = 0; b < n; ++b) {
        const JPH::Mat44& part = partLocal[partOf[b]];
        const Quaternion partRot = QuaternionInvert(Q(part.GetQuaternion()));
        relPos[b] = Vector3RotateByQuaternion(Vector3Subtract(pos[b], V(part.GetTranslation())), partRot);
        relDelta[b] = QuaternionMultiply(partRot, delta[b]);
    }
}

void RiderModel::PoseFromRagdoll(const RiderRagdoll& ragdoll, float alpha)
{
    JPH::Mat44 parts[RiderPart::Count];
    for (int i = 0; i < RiderPart::Count; ++i) parts[i] = ragdoll.PartTransform(i, alpha);
    PoseFromParts(parts);
}

void RiderModel::PoseFromParts(const JPH::Mat44* partWorld)
{
    if (relPos.size() != pos.size()) return;
    for (int b = 0; b < realBones; ++b) {
        const JPH::Mat44& part = partWorld[partOf[b]];
        const Quaternion rot = Q(part.GetQuaternion());
        pos[b] = Vector3Add(V(part.GetTranslation()), Vector3RotateByQuaternion(relPos[b], rot));
        delta[b] = QuaternionMultiply(rot, relDelta[b]);
    }
    // Al salir despedido suelta el manubrio: los dedos se abren en un cuarto de segundo.
    gripAmount = std::max(0.0f, gripAmount - GetFrameTime() / 0.25f);
    CurlFingers();
}

void RiderModel::Skin()
{
    // Cada vértice: suma pesada de sus huesos, v' = pos + giro * (v - posReposo).
    const int n = (int)pos.size();
    std::vector<Matrix> xf(n), rot(n);
    for (int b = 0; b < n; ++b) {
        rot[b] = QuaternionToMatrix(delta[b]);
        xf[b] = MatrixMultiply(MatrixMultiply(MatrixTranslate(-bindPos[b].x, -bindPos[b].y, -bindPos[b].z), rot[b]),
                               MatrixTranslate(pos[b].x, pos[b].y, pos[b].z));
    }
    Mesh& m = model.meshes[0];
    for (int v = 0; v < m.vertexCount; ++v) {
        Vector3 p = {0.0f, 0.0f, 0.0f}, nrm = {0.0f, 0.0f, 0.0f};
        for (int k = 0; k < 4; ++k) {
            const float w = skinWeight[v * 4 + k];
            if (w <= 0.0f) continue;
            const int b = skinBone[v * 4 + k];
            p = Vector3Add(p, Vector3Scale(Vector3Transform(bindVerts[v], xf[b]), w));
            nrm = Vector3Add(nrm, Vector3Scale(Vector3Transform(bindNormals[v], rot[b]), w));
        }
        nrm = Vector3Normalize(nrm);
        std::memcpy(&m.animVertices[v * 3], &p, sizeof(Vector3));
        std::memcpy(&m.animNormals[v * 3], &nrm, sizeof(Vector3));
    }
    UpdateMeshBuffer(m, 0, m.animVertices, m.vertexCount * 3 * (int)sizeof(float), 0);
    UpdateMeshBuffer(m, 2, m.animNormals, m.vertexCount * 3 * (int)sizeof(float), 0);
}

void RiderModel::Draw(Renderer& r, const Matrix& world)
{
    r.SetGloss(0.2f);
    r.DrawMeshTextured(model.meshes[0], world, texture);
}
