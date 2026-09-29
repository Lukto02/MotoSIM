#include "PhysicsWorld.h"
#include "Terrain.h"   // Jolt antes que raylib

#include "TerrainDeformation.h"

#include "MathUtil.h"
#include "Render.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>

void TerrainDeformation::Init(const Terrain& t, int res)
{
    terrain = &t;
    resolution = res;
    origin = t.OriginX();
    size = t.Size();
    marks = LoadRenderTexture(resolution, resolution);
    SetTextureWrap(marks.texture, TEXTURE_WRAP_CLAMP);

    // Goma: la malla entera se arma una vez (índices fijos) y se sube como dinámica; después sólo se
    // actualizan los cuadriláteros nuevos. Se dibujan los usados (Renderer::DrawSkidMarks).
    skid = Mesh{};
    skid.vertexCount = kSkidQuads * 4;
    skid.triangleCount = kSkidQuads * 2;
    skid.vertices = (float*)MemAlloc(skid.vertexCount * 3 * sizeof(float));
    skid.normals = (float*)MemAlloc(skid.vertexCount * 3 * sizeof(float));
    skid.texcoords = (float*)MemAlloc(skid.vertexCount * 2 * sizeof(float));
    skid.colors = (unsigned char*)MemAlloc(skid.vertexCount * 4);
    skid.indices = (unsigned short*)MemAlloc(skid.triangleCount * 3 * sizeof(unsigned short));
    for (int q = 0; q < kSkidQuads; ++q) {
        const unsigned short v = (unsigned short)(q * 4);
        const unsigned short idx[6] = {v, (unsigned short)(v + 1), (unsigned short)(v + 2), v, (unsigned short)(v + 2), (unsigned short)(v + 3)};
        std::copy(idx, idx + 6, skid.indices + q * 6);
    }
    UploadMesh(&skid, true);
    Clear();
}

void TerrainDeformation::Unload()
{
    UnloadRenderTexture(marks);
    marks = RenderTexture2D{};
    UnloadMesh(skid);
    skid = Mesh{};
}

void TerrainDeformation::Clear()
{
    BeginTextureMode(marks);
    ClearBackground(kUntouched);           // gris = suelo sin tocar (más oscuro: surco; más claro: tierra levantada)
    EndTextureMode();
    GenTextureMipmaps(&marks.texture);
    SetTextureFilter(marks.texture, TEXTURE_FILTER_TRILINEAR);
    pending.clear();
    ResetWheelHistory();
    commitTimer = 0.0f;
    for (float& d : dwell) d = 0.0f;
    for (bool& h : hasLast) h = false;
    for (SkidEnd& e : skidEnd) e = SkidEnd{};
    skidNext = skidUsed = 0;
    dirtyLo = kSkidQuads;
    dirtyHi = -1;
}

Vector2 TerrainDeformation::ToTexel(float x, float z) const
{
    // En una render texture la fila 0 de BeginTextureMode queda arriba (v = 1): se invierte v.
    float u = (x - origin) / size, v = (z - origin) / size;
    return {u * resolution, (1.0f - v) * resolution};
}

void TerrainDeformation::WheelContact(int wheel, float x, float z, bool grounded, float load, float slip, float slide, Vector2 heading,
                                      bool onTerrain, float dt, float edge)
{
    if (wheel < 0 || wheel >= kWheelSlots) return;
    // En el asfalto no hay surcos de tierra: la goma (si resbala). Entre los dos, en el borde del pavimento,
    // los surcos se van apagando.
    const float paved = terrain ? terrain->PavedAmount(x, z) : 0.0f;
    if (skid.vaoId > 0) Skid(wheel, x, z, grounded && onTerrain, load, slide, heading, paved, dt, edge);

    if (!grounded || !onTerrain || !visualRuts || paved > 0.6f) {
        hasLast[wheel] = false;
        dwell[wheel] = 0.0f;
        return;
    }
    const Vector2 p = ToTexel(x, z);
    if (!hasLast[wheel]) {
        last[wheel] = p;
        hasLast[wheel] = true;
        return;
    }
    const float metersPerTexel = size / resolution;
    const float traveled = Vector2Distance(p, last[wheel]) * metersPerTexel;
    if (traveled > 3.0f) { last[wheel] = p; dwell[wheel] = 0.0f; return; }
    dwell[wheel] += dt;
    const bool stationary = traveled < 0.12f;
    if (stationary && (slide < 1.5f || dwell[wheel] < 0.10f)) return;
    dwell[wheel] = 0.0f;   // un trazo cada 25 cm

    const float dirt = 1.0f - mu::Smoothstep(0.2f, 0.6f, paved);          // 1 en la tierra y el pasto (como siempre)
    if (dirt > 0.0f) {
        // Más carga y más patinaje = surco más ancho, más hondo y más oscuro (tierra removida). A los
        // costados queda un borde de tierra suelta empujada por la rueda.
        const float width = (0.12f + mu::Clamp(slip, 0.0f, 1.5f) * 0.12f) / metersPerTexel;
        const float alpha = mu::Clamp(30.0f + load / 45.0f + slip * 90.0f, 0.0f, 140.0f) * dirt;
        const float ridge = width + 0.12f / metersPerTexel;
        const Vector2 a = stationary ? Vector2Add(p, Vector2Scale(Vector2{heading.x, -heading.y}, -0.11f / metersPerTexel)) : last[wheel];
        const Vector2 b = stationary ? Vector2Add(p, Vector2Scale(Vector2{heading.x, -heading.y}, 0.11f / metersPerTexel)) : p;
        pending.push_back({a, b, std::max(ridge, 2.0f), Color{240, 228, 212, (unsigned char)(alpha * 0.4f)}, true});
        pending.push_back({a, b, std::max(width, 1.2f), Color{92, 70, 54, (unsigned char)alpha}, false});
    }
    last[wheel] = p;
    if (pending.size() > 8192) pending.erase(pending.begin(), pending.begin() + 4096);
}

// ------------------------------------------------------------------------------------ goma
TerrainDeformation::SkidEnd TerrainDeformation::MakeEnd(const SkidPoint& p, Vector2 side, float along) const
{
    SkidEnd e;
    e.active = true;
    e.x = p.x;
    e.z = p.z;
    e.side = side;
    e.hasEdge = true;
    // A la altura del terreno en cada borde (sigue el peralte) y 4 mm arriba: junto con el corrimiento hacia la
    // cámara del shader, no se pisa con el asfalto (ver kSkidVS).
    const float lx = p.x - side.x * p.halfWidth, lz = p.z - side.y * p.halfWidth;
    const float rx = p.x + side.x * p.halfWidth, rz = p.z + side.y * p.halfWidth;
    e.left = {lx, terrain->Height(lx, lz) + 0.004f, lz};
    e.right = {rx, terrain->Height(rx, rz) + 0.004f, rz};
    e.halfWidth = p.halfWidth;
    e.along = along;
    e.dark = p.dark;
    return e;
}

void TerrainDeformation::AddSkidQuad(const SkidEnd& a, const SkidEnd& b)
{
    const int q = skidNext;
    skidNext = (skidNext + 1) % kSkidQuads;
    skidUsed = std::min(skidUsed + 1, kSkidQuads);
    dirtyLo = std::min(dirtyLo, q);
    dirtyHi = std::max(dirtyHi, q);

    // Vértices: borde izquierdo y derecho de a, derecho e izquierdo de b. La "normal" lleva el corrimiento
    // desde el centro de la tira: el shader la ensancha a lo lejos (ver kSkidVS).
    const Vector3 ca = Vector3Scale(Vector3Add(a.left, a.right), 0.5f), cb = Vector3Scale(Vector3Add(b.left, b.right), 0.5f);
    const Vector3 pos[4] = {a.left, a.right, b.right, b.left};
    const Vector3 ctr[4] = {ca, ca, cb, cb};
    const float side[4] = {-1.0f, 1.0f, 1.0f, -1.0f};
    const float along[4] = {a.along, a.along, b.along, b.along};
    const unsigned char dark[4] = {a.dark, a.dark, b.dark, b.dark};
    for (int i = 0; i < 4; ++i) {
        const int v = q * 4 + i;
        skid.vertices[v * 3 + 0] = pos[i].x;
        skid.vertices[v * 3 + 1] = pos[i].y;
        skid.vertices[v * 3 + 2] = pos[i].z;
        skid.normals[v * 3 + 0] = pos[i].x - ctr[i].x;
        skid.normals[v * 3 + 1] = pos[i].y - ctr[i].y;
        skid.normals[v * 3 + 2] = pos[i].z - ctr[i].z;
        skid.texcoords[v * 2 + 0] = side[i];
        skid.texcoords[v * 2 + 1] = along[i];
        skid.colors[v * 4 + 0] = skid.colors[v * 4 + 1] = skid.colors[v * 4 + 2] = 0;
        skid.colors[v * 4 + 3] = dark[i];
    }
}

void TerrainDeformation::Skid(int wheel, float x, float z, bool on, float load, float slide, Vector2 heading, float paved, float dt, float edge)
{
    SkidEnd& e = skidEnd[wheel];
    // Cuánto marca. Rodando no hay resbale (la cubierta no desliza mientras le alcanza el agarre: slide ~0;
    // acelerando a fondo en la recta, < 1 m/s; frenando con la S la de carreras, 0.6). Desde 1.5 m/s marca,
    // más cuanto más resbala y más carga lleva: al límite (patinaje del pico de agarre, 3-5 m/s) apenas,
    // trabada o derrapando a más de ~30 km/h de resbale, del todo. Con poco peso (la trasera frenando fuerte), clara.
    const float slideK = mu::Smoothstep(1.5f, 9.0f, slide);
    const float loadK = mu::Clamp(0.3f + 0.7f * load / 1300.0f, 0.0f, 1.25f);
    // Frenando al límite sin trabar (edge, la moto propia): tenue, hasta un tercio de una trabada, y sólo con peso
    // (la trasera casi en el aire de una frenada fuerte usa mucho agarre relativo pero no apoya: no marca).
    const float edgeK = 0.33f * edge * mu::Clamp((load - 300.0f) / 1000.0f, 0.0f, 1.2f);
    const float k = std::min(1.0f, std::max(slideK * loadK, edgeK)) * mu::Smoothstep(0.35f, 0.75f, paved);
    constexpr float kStep = 0.12f;                 // un borde cada 12 cm (la marca llega casi hasta la rueda)
    const float dx = x - e.x, dz = z - e.z, d = std::sqrt(dx * dx + dz * dz);
    // Patinando sin avanzar (burnout, o el golpe de gas de una largada): una mancha del largo de la huella, a lo
    // largo de la moto.
    auto spot = [&](float halfWidth, unsigned char dark) {
        const float hl = std::sqrt(heading.x * heading.x + heading.y * heading.y);
        if (hl < 1e-3f) return;
        const Vector2 f = {heading.x / hl, heading.y / hl}, side = {f.y, -f.x};
        AddSkidQuad(MakeEnd({x - f.x * 0.11f, z - f.y * 0.11f, halfWidth, dark}, side, 0.0f),
                    MakeEnd({x + f.x * 0.11f, z + f.y * 0.11f, halfWidth, dark}, side, 0.22f));
    };
    if (!on || k < 0.03f) {
        // Deja de marcar: si venía marcando, la tira termina acá apagándose (sin el último tramo se corta de golpe).
        if (e.active && on && d > 0.05f && d < 3.0f) {
            const Vector2 dir = {dx / d, dz / d};
            Vector2 side = {dir.y, -dir.x};
            if (e.hasEdge && side.x * e.side.x + side.y * e.side.y < 0.0f) side = {-side.x, -side.y};
            const SkidEnd from = e.hasEdge ? e : MakeEnd({e.x, e.z, e.halfWidth, e.dark}, side, e.along);
            AddSkidQuad(from, MakeEnd({x, z, e.halfWidth, 0}, side, e.along + d));
        } else if (e.active && on && !e.hasEdge && e.dwell > 0.03f) {
            spot(e.halfWidth, e.dark);             // un golpe corto sin llegar a avanzar un tramo
        }
        e = SkidEnd{};
        return;
    }

    // Ancho de la huella de la cubierta (trasera más ancha): crece un poco con la carga y con el resbale.
    const bool rear = (wheel & 1) != 0;
    const float halfWidth = (rear ? 0.075f : 0.06f) * (0.85f + 0.15f * slideK) * mu::Clamp(0.75f + 0.25f * std::sqrt(load / 1300.0f), 0.8f, 1.2f);
    const unsigned char dark = (unsigned char)(k * 185.0f);      // cada pasada oscurece hasta el 73%
    if (!e.active || d > 3.0f) {                   // empieza (o saltó: reapareció, un remoto corregido)
        e = SkidEnd{};
        e.active = true;
        e.x = x;
        e.z = z;
        e.halfWidth = halfWidth;
        e.dark = (unsigned char)(dark / 2);
        return;
    }
    if (d < kStep) {
        e.dwell += dt;                             // sin avanzar: cada 0.12 s una mancha (se van sumando)
        if (e.dwell >= 0.12f && slideK > 0.5f) {
            e.dwell = 0.0f;
            spot(halfWidth, (unsigned char)(dark / 2));
        }
        return;
    }
    // Un tramo nuevo: el borde perpendicular a hacia dónde se movió la goma (derrapando, no es hacia donde
    // mira la moto). Si da la vuelta, los bordes no se cruzan.
    const Vector2 dir = {dx / d, dz / d};
    Vector2 side = {dir.y, -dir.x};
    if (e.hasEdge && side.x * e.side.x + side.y * e.side.y < 0.0f) side = {-side.x, -side.y};
    const SkidEnd from = e.hasEdge ? e : MakeEnd({e.x, e.z, e.halfWidth, e.dark}, side, e.along);
    const SkidEnd to = MakeEnd({x, z, halfWidth, dark}, side, e.along + d);
    AddSkidQuad(from, to);
    e = to;
}

void TerrainDeformation::DrawSkids(Renderer& r) const
{
    if (skidUsed > 0 && skid.vaoId > 0) r.DrawSkidMarks(skid, skidUsed);
}

void TerrainDeformation::Flush()
{
    if (!pending.empty()) {
        BeginTextureMode(marks);
        for (int pass = 0; pass < 2; ++pass)           // primero los bordes, encima los surcos
            for (const Stroke& s : pending)
                if (s.ridge == (pass == 0)) DrawLineEx(s.a, s.b, s.width, s.color);
        EndTextureMode();
        pending.clear();
        mipsStale = true;
    }
    // Sin mipmaps las huellas finas titilan a lo lejos, pero regenerarlos en una textura de 4096
    // cada frame cuesta: alcanza con hacerlo unas pocas veces por segundo.
    if (mipsStale && GetTime() - lastMips > 0.15) {
        GenTextureMipmaps(&marks.texture);
        mipsStale = false;
        lastMips = GetTime();
    }
    // Goma: sube sólo los cuadriláteros nuevos (un rango; si el anillo dio la vuelta, de la punta al final).
    if (dirtyHi >= dirtyLo && skid.vaoId > 0) {
        const int v0 = dirtyLo * 4, nv = (dirtyHi - dirtyLo + 1) * 4;
        UpdateMeshBuffer(skid, 0, skid.vertices + v0 * 3, nv * 3 * (int)sizeof(float), v0 * 3 * (int)sizeof(float));
        UpdateMeshBuffer(skid, 1, skid.texcoords + v0 * 2, nv * 2 * (int)sizeof(float), v0 * 2 * (int)sizeof(float));
        UpdateMeshBuffer(skid, 2, skid.normals + v0 * 3, nv * 3 * (int)sizeof(float), v0 * 3 * (int)sizeof(float));
        UpdateMeshBuffer(skid, 3, skid.colors + v0 * 4, nv * 4, v0 * 4);
        dirtyLo = kSkidQuads;
        dirtyHi = -1;
    }
}

void TerrainDeformation::DigRut(Terrain& terrain, float x, float z, float spin, float slide)
{
    // Sólo el patinaje fuerte remueve tierra: burnouts, salidas de curva a fondo, derrapes.
    const float amount = digRate * (std::max(0.0f, spin - 1.5f) + 0.5f * std::max(0.0f, slide - 1.5f));
    if (amount > 0.0f) terrain.Dig(x, z, amount, 0.45f, maxDepth);
}

void TerrainDeformation::CommitRuts(Terrain& terrain, PhysicsWorld& world, float dt)
{
    terrain.StepRide(dt);              // el suelo de las ruedas se hunde de a poco (Terrain::StepRide)
    commitTimer += dt;
    if (commitTimer < 0.06f || !terrain.HasPendingDeformation()) return;
    commitTimer = 0.0f;
    terrain.CommitDeformation(world);
}

void TerrainDeformation::ResetWheelHistory()
{
    for (bool& active : hasRutLast) active = false;
}

void TerrainDeformation::PressWheel(Terrain& terrain, int wheel, Vector2 at, Vector2 heading, bool contact,
                                    float load, float spin, float slide, float softness, float depth, float dt)
{
    if (wheel < 0 || wheel >= 2) return;
    if (!contact) { hasRutLast[wheel] = false; return; }
    const float distance = hasRutLast[wheel] ? Vector2Distance(at, rutLast[wheel]) : 0.0f;
    // Compactación por rodadura: proporcional a distancia, sin exigir resbale de la goma.
    // Reaparecer no cuenta como atravesar la distancia entre las dos posiciones.
    const float rollingSpeed=dt>0 && distance<=3.0f ? distance/dt : 0.0f;
    if (!hasRutLast[wheel] || distance > 3.0f) rutLast[wheel] = at;
    const int steps = std::clamp((int)std::ceil((distance <= 3.0f ? distance : 0.0f) / std::max(0.025f,terrain.SoilCell()*0.5f)),1,64);
    for (int i=1; i<=steps; ++i) {
        const Vector2 p = Vector2Lerp(rutLast[wheel],at,(float)i/steps);
        terrain.PressSoil(p.x,p.y,heading.x,heading.y,load,std::hypot(spin,slide),softness,depth,dt/steps,rollingSpeed);
    }
    rutLast[wheel] = at;
    hasRutLast[wheel] = true;
}
