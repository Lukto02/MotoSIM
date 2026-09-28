#include "Terrain.h"   // Jolt antes que raylib

#include "Particles.h"

#include "MathUtil.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>

void Particles::Init(size_t maxParticles)
{
    capacity = maxParticles;
    items.reserve(capacity);
    // Texturas generadas: un disco para la tierra y una bocanada suave para el polvo.
    Image disc = GenImageGradientRadial(32, 32, 0.55f, WHITE, BLANK);
    Image puff = GenImageGradientRadial(64, 64, 0.0f, WHITE, BLANK);
    dirtTex = LoadTextureFromImage(disc);
    dustTex = LoadTextureFromImage(puff);
    SetTextureFilter(dirtTex, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(dustTex, TEXTURE_FILTER_BILINEAR);
    UnloadImage(disc);
    UnloadImage(puff);
}

void Particles::Unload()
{
    UnloadTexture(dirtTex);
    UnloadTexture(dustTex);
    items.clear();
}

void Particles::Emit(Kind kind, Vector3 pos, Vector3 vel, float size, float life, Color color, int owner)
{
    if (items.size() >= capacity) return;
    items.push_back({pos, vel, life, life, size, color, kind, (unsigned char)owner});
}

void Particles::Update(float dt, const Terrain& terrain)
{
    for (Item& p : items) {
        p.life -= dt;
        if (p.kind == DIRT) {
            p.vel.y -= 9.81f * dt;
            p.vel = Vector3Scale(p.vel, 1.0f - 0.4f * dt);            // algo de arrastre
            p.pos = Vector3Add(p.pos, Vector3Scale(p.vel, dt));
            if (p.pos.y < terrain.Height(p.pos.x, p.pos.z)) p.life = 0.0f;
        } else if (p.kind == SPARK) {
            p.vel.y -= 9.81f * dt;
            p.vel = Vector3Scale(p.vel, 1.0f - 0.8f * dt);
            p.pos = Vector3Add(p.pos, Vector3Scale(p.vel, dt));
            const float ground = terrain.Height(p.pos.x, p.pos.z);
            if (p.pos.y < ground) {                                  // rebota y pierde velocidad
                p.pos.y = ground;
                p.vel = {p.vel.x * 0.55f, -p.vel.y * 0.35f, p.vel.z * 0.55f};
            }
        } else if (p.kind == FLAME) {                                // en el espacio de la moto
            p.vel = Vector3Scale(p.vel, std::max(0.0f, 1.0f - 7.0f * dt));   // la llamarada se frena en el aire
            p.pos = Vector3Add(p.pos, Vector3Scale(p.vel, dt));
            p.size += dt * 1.4f;
        } else {
            p.vel = Vector3Scale(p.vel, 1.0f - 1.5f * dt);            // el polvo frena rápido
            p.vel.y += 0.25f * dt;                                   // y sube un poco
            p.pos = Vector3Add(p.pos, Vector3Scale(p.vel, dt));
            p.size += dt * 1.8f;                                     // crece
        }
    }
    items.erase(std::remove_if(items.begin(), items.end(), [](const Item& p) { return p.life <= 0.0f; }), items.end());
}

void Particles::Draw(const Camera3D& cam, const Matrix* bikeFrames, int frameCount) const
{
    // Tierra: opaca, con profundidad.
    for (const Item& p : items) {
        if (p.kind != DIRT) continue;
        DrawBillboard(cam, dirtTex, p.pos, p.size, p.color);
    }
    // Polvo: transparente, sin escribir profundidad para que las bocanadas no se recorten entre sí.
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    for (const Item& p : items) {
        if (p.kind != DUST) continue;
        float t = p.life / p.maxLife;                                // 1 -> 0
        float fade = mu::Smoothstep(0.0f, 0.35f, t) * mu::Smoothstep(1.0f, 0.85f, t);
        Color c = p.color;
        c.a = (unsigned char)(p.color.a * fade);
        DrawBillboard(cam, dustTex, p.pos, p.size, c);
    }
    // Fuego: aditivo (se suma a lo que hay atrás, como la luz), del blanco amarillento al rojo.
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ADDITIVE);
    for (const Item& p : items) {
        if (p.kind != FLAME || p.owner >= frameCount) continue;
        const float t = p.life / p.maxLife;                         // 1 -> 0
        const float heat = t * t;
        const Color c = {(unsigned char)(p.color.r * (0.55f + 0.45f * t)), (unsigned char)(p.color.g * (0.25f + 0.75f * heat)),
                         (unsigned char)(p.color.b * heat), (unsigned char)(p.color.a * mu::Smoothstep(0.0f, 0.5f, t))};
        DrawBillboard(cam, dustTex, Vector3Transform(p.pos, bikeFrames[p.owner]), p.size, c);
    }
    // Chispas: una estela en la dirección en que vuelan (lo que recorren en ~25 ms) y un punto que brilla.
    for (const Item& p : items) {
        if (p.kind != SPARK) continue;
        const float t = p.life / p.maxLife;                         // 1 -> 0
        const Color c = {255, (unsigned char)(120 + 125 * t), (unsigned char)(40 + 170 * t * t), (unsigned char)(255 * mu::Smoothstep(0.0f, 0.3f, t))};
        DrawLine3D(p.pos, Vector3Subtract(p.pos, Vector3Scale(p.vel, 0.04f)), c);
        DrawBillboard(cam, dustTex, p.pos, p.size * (0.5f + 0.5f * t), c);
    }
    rlDrawRenderBatchActive();
    EndBlendMode();
    rlEnableDepthMask();
}
