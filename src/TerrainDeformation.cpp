#include "PhysicsWorld.h"
#include "Terrain.h"   // Jolt antes que raylib

#include "TerrainDeformation.h"

#include "MathUtil.h"
#include "raymath.h"

#include <algorithm>

void TerrainDeformation::Init(const Terrain& terrain, int res)
{
    resolution = res;
    origin = terrain.OriginX();
    size = terrain.Size();
    marks = LoadRenderTexture(resolution, resolution);
    SetTextureWrap(marks.texture, TEXTURE_WRAP_CLAMP);
    Clear();
}

void TerrainDeformation::Unload()
{
    UnloadRenderTexture(marks);
}

void TerrainDeformation::Clear()
{
    BeginTextureMode(marks);
    ClearBackground(kUntouched);           // gris = suelo sin tocar (más oscuro: surco; más claro: tierra levantada)
    EndTextureMode();
    GenTextureMipmaps(&marks.texture);
    SetTextureFilter(marks.texture, TEXTURE_FILTER_TRILINEAR);
    pending.clear();
    hasLast[0] = hasLast[1] = false;
}

Vector2 TerrainDeformation::ToTexel(float x, float z) const
{
    // En una render texture la fila 0 de BeginTextureMode queda arriba (v = 1): se invierte v.
    float u = (x - origin) / size, v = (z - origin) / size;
    return {u * resolution, (1.0f - v) * resolution};
}

void TerrainDeformation::WheelContact(int wheel, float x, float z, bool grounded, float load, float slip)
{
    if (wheel < 0 || wheel >= kWheelSlots) return;
    if (!grounded) {
        hasLast[wheel] = false;
        return;
    }
    const Vector2 p = ToTexel(x, z);
    if (!hasLast[wheel]) {
        last[wheel] = p;
        hasLast[wheel] = true;
        return;
    }
    const float metersPerTexel = size / resolution;
    if (Vector2Distance(p, last[wheel]) * metersPerTexel < 0.25f) return;   // un trazo cada 25 cm

    // Más carga y más patinaje = surco más ancho, más hondo y más oscuro (tierra removida). A los
    // costados queda un borde de tierra suelta empujada por la rueda.
    const float width = (0.12f + mu::Clamp(slip, 0.0f, 1.5f) * 0.12f) / metersPerTexel;
    const float alpha = mu::Clamp(30.0f + load / 45.0f + slip * 90.0f, 0.0f, 140.0f);
    const float ridge = width + 0.12f / metersPerTexel;
    pending.push_back({last[wheel], p, std::max(ridge, 2.0f), Color{240, 228, 212, (unsigned char)(alpha * 0.4f)}, true});
    pending.push_back({last[wheel], p, std::max(width, 1.2f), Color{92, 70, 54, (unsigned char)alpha}, false});
    last[wheel] = p;
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
}

void TerrainDeformation::DigRut(Terrain& terrain, float x, float z, float spin, float slide)
{
    // Sólo el patinaje fuerte remueve tierra: burnouts, salidas de curva a fondo, derrapes.
    const float amount = digRate * (std::max(0.0f, spin - 1.5f) + 0.5f * std::max(0.0f, slide - 1.5f));
    if (amount > 0.0f) terrain.Dig(x, z, amount, 0.45f, maxDepth);
}

void TerrainDeformation::CommitRuts(Terrain& terrain, PhysicsWorld& world, float dt)
{
    commitTimer += dt;
    if (commitTimer < 0.3f || !terrain.HasPendingDeformation()) return;
    commitTimer = 0.0f;
    terrain.CommitDeformation(world);
}
