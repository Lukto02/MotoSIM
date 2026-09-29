#include "Terrain.h"   // Jolt antes que raylib

#include "Particles.h"
#include "Render.h"

#include "MathUtil.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>

void Particles::Init(size_t maxParticles)
{
    capacity = maxParticles;
    items.reserve(capacity);
    clodTransforms.reserve(capacity);
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
    if (kind == DIRT) {
        items.back().angle = (pos.x+pos.z)*11.7f;
        items.back().spin = 3.0f + std::fmod(Vector3Length(vel)*1.7f,13.0f);
    }
}

void Particles::Update(float dt, const Terrain& terrain)
{
    for (Item& p : items) {
        p.life -= dt;
        if (p.kind == DIRT) {
            // Subpasos cortos: los terrones rápidos no atraviesan lomas ni surcos.
            const int steps = std::clamp((int)std::ceil(dt / 0.016f),1,12);
            const float h = dt/steps;
            for (int step=0; step<steps; ++step) {
                if (p.settled) { p.pos.y=terrain.Height(p.pos.x,p.pos.z)+p.size*0.18f; break; }
                p.vel.y -= 9.81f*h;
                p.vel = Vector3Scale(p.vel,std::exp(-0.55f*h));
                p.pos = Vector3Add(p.pos,Vector3Scale(p.vel,h));
                p.angle += p.spin*h;
                const float ground = terrain.Height(p.pos.x,p.pos.z)+p.size*0.18f;
                if (p.pos.y < ground) {
                    p.pos.y = ground;
                    const auto jn=terrain.Normal(p.pos.x,p.pos.z);
                    const Vector3 n={jn.GetX(),jn.GetY(),jn.GetZ()};
                    const float into=Vector3DotProduct(p.vel,n);
                    if (into < 0.0f) p.vel=Vector3Scale(Vector3Subtract(p.vel,Vector3Scale(n,1.25f*into)),0.42f);
                    p.spin *= 0.35f;
                    if (Vector3Length(p.vel)<1.2f) { p.settled=true; p.life=std::min(p.life,0.65f); }
                }
            }
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
            if (p.kind == DUST) p.ground = terrain.Height(p.pos.x, p.pos.z);
        }
    }
    items.erase(std::remove_if(items.begin(), items.end(), [](const Item& p) { return p.life <= 0.0f; }), items.end());
}

void Particles::Draw(Renderer& renderer, const Camera3D& cam, const Matrix* bikeFrames, int frameCount) const
{
    // Terrones con volumen, luz y sombras recibidas; un solo draw instanciado.
    clodTransforms.clear();
    for (const Item& p : items) {
        if (p.kind != DIRT) continue;
        const float size=p.size*0.5f*mu::Smoothstep(0.0f,0.25f,p.life);
        if (size < 0.0005f) continue;
        Matrix m=MatrixMultiply(MatrixMultiply(MatrixScale(size,size*0.65f,size*0.85f),
            MatrixRotateXYZ({p.angle,p.angle*0.7f,p.angle*0.3f})),MatrixTranslate(p.pos.x,p.pos.y,p.pos.z));
        m.m3=p.color.r/255.0f; m.m7=p.color.g/255.0f; m.m11=p.color.b/255.0f;
        clodTransforms.push_back(m);
    }
    renderer.DrawSoilChunks(clodTransforms.data(),(int)clodTransforms.size());
    // Polvo: transparente, sin escribir profundidad para que las bocanadas no se recorten entre sí.
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    for (const Item& p : items) {
        if (p.kind != DUST) continue;
        float t = p.life / p.maxLife;                                // 1 -> 0
        float fade = mu::Smoothstep(0.0f, 0.35f, t) * mu::Smoothstep(1.0f, 0.85f, t);
        Color c = p.color;
        c.a = (unsigned char)(p.color.a * fade);
        // El billboard es un cuadrado vertical de lado `size`: con el centro pegado al suelo, el terreno lo corta por la
        // mitad y queda un borde recto (un "rectángulo oscuro" en la tierra). Se sube el centro hasta que el borde de
        // abajo, que es transparente en el degradé radial, quede apenas bajo el suelo (a 0.45·size, ahí alfa < 10%).
        Vector3 at = p.pos;
        at.y = std::max(at.y, p.ground + 0.45f * p.size);
        DrawBillboard(cam, dustTex, at, p.size, c);
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
