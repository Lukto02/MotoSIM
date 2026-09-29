#include "Render.h"
#include "BikeStyleDef.h"
#include "GpuPreset.h"
#include "MeshBuilder.h"

#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <type_traits>
#include <vector>

namespace {

// ------------------------------------------------------------------------------------ shaders
// Todo se ilumina en espacio lineal y se pasa a pantalla con un tone mapping filmico (ACES), así
// el sol puede ser mucho más intenso que el ambiente sin quemar los colores.
const char* kVersion = "#version 330\n";

const char* kCommon = R"(
uniform vec3 sunDir;         // hacia el sol
uniform vec3 sunColor;       // colores lineales
uniform vec3 skyZenith;
uniform vec3 skyHorizon;
uniform vec3 groundBounce;
uniform float fogDensity;
uniform vec3 camPos;
uniform float exposure;
uniform float ambientStrength;

vec3 ToLinear(vec3 c) { return pow(c, vec3(2.2)); }

// Cielo en una dirección: degradé cenit-horizonte, bruma bajo el horizonte, halo y disco del sol.
vec3 SkyColor(vec3 d, float sunDisc)
{
    vec3 col = mix(skyHorizon, skyZenith, pow(clamp(d.y, 0.0, 1.0), 0.5));
    col = mix(col, skyHorizon * 0.75 + groundBounce * 0.6, smoothstep(0.02, -0.25, d.y));
    float s = max(dot(d, sunDir), 0.0);
    col += sunColor * (0.05 * pow(s, 4.0) + 0.18 * pow(s, 32.0) + 0.5 * pow(s, 400.0));
    col += sunColor * sunDisc * 10.0 * smoothstep(0.99955, 0.99975, s);
    return col;
}

// Niebla del color del cielo en la dirección de la mirada (perspectiva aérea).
vec3 ApplyFog(vec3 col, vec3 p)
{
    vec3 d = p - camPos;
    float dist = length(d);
    float f = 1.0 - exp(-(dist * fogDensity) * (dist * fogDensity));
    return mix(col, SkyColor(d / max(dist, 1e-4), 0.0), clamp(f, 0.0, 1.0));
}

vec3 Tonemap(vec3 c)
{
    c *= exposure;
    c = (c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14);
    return pow(clamp(c, 0.0, 1.0), vec3(1.0 / 2.2));
}

// Luz de ambiente hemisférica: rebote del suelo abajo, cielo arriba (algo desaturado, para que la sombra no
// salga azul) y un relleno de lado desde el lado opuesto al sol (las paredes en sombra no quedan negras).
vec3 Ambient(vec3 n)
{
    vec3 sky = skyZenith * 0.8 + skyHorizon * 0.35;
    sky = mix(sky, vec3(dot(sky, vec3(1.0 / 3.0))), 0.30);
    float fill = max(dot(n, -normalize(vec3(sunDir.x, 0.0, sunDir.z) + vec3(1e-5, 0.0, 0.0))), 0.0) * (1.0 - abs(n.y));
    return ambientStrength * (1.25 * mix(groundBounce, sky, 0.5 + 0.5 * n.y)
                              + 0.30 * fill * (skyHorizon * 0.5 + groundBounce * 0.5));
}
)";

// Sombra del sol en dos cascadas ortográficas, cada una en su textura: la cercana (todo lo que se mueve, el
// detalle fino) y la lejana (sólo el mundo quieto, en caché; ver Game::Draw). Comparación por hardware
// (sampler2DShadow, cada lectura ya es un 2x2 bilineal) con una grilla de 5 o 9 lecturas separadas 1 texel por
// la suavidad. El sesgo va en metros del mundo (el texel de cada cascada): un sesgo en profundidad normalizada
// o con piso separaba la sombra del pie de las cosas, y crecía con el radio.
// Plan B (SHADOW_MANUAL, si no hay glTexParameteri para pedir la comparación): sampler2D y comparación a mano.
const char* kShadow = R"(
#ifdef SHADOW_MANUAL
uniform sampler2D shadowMap;
uniform sampler2D shadowMap1;
#define SHADOW_SAMPLER sampler2D
float ShadowRef(sampler2D map, vec3 c, float invRes)
{
    vec2 st = c.xy / invRes - 0.5;
    vec2 b = floor(st), f = st - b;
    vec2 o = (b + 0.5) * invRes;
    float s00 = c.z <= texture(map, o).r ? 1.0 : 0.0;
    float s10 = c.z <= texture(map, o + vec2(invRes, 0.0)).r ? 1.0 : 0.0;
    float s01 = c.z <= texture(map, o + vec2(0.0, invRes)).r ? 1.0 : 0.0;
    float s11 = c.z <= texture(map, o + vec2(invRes)).r ? 1.0 : 0.0;
    return mix(mix(s00, s10, f.x), mix(s01, s11, f.x), f.y);
}
#else
uniform sampler2DShadow shadowMap;
uniform sampler2DShadow shadowMap1;
#define SHADOW_SAMPLER sampler2DShadow
float ShadowRef(sampler2DShadow map, vec3 c, float invRes) { return texture(map, c); }
#endif
uniform mat4 lightVP;
uniform mat4 lightVP1;
uniform vec2 shadowWorldTexel;   // metros por texel de cada cascada
uniform vec2 shadowInvRes;       // 1 / lado de cada mapa
uniform float shadowFarOn;       // 1 con cascada lejana
uniform float shadowTaps;        // lecturas del filtro: 5 o 9
uniform float shadowStrength;
uniform float shadowSoftness;
const float kShadowDepth = 349.0;   // largo del volumen de la luz (planos 1 a 350 m)

float ShadowTaps(SHADOW_SAMPLER map, vec3 c, float invRes)
{
    float r = invRes * shadowSoftness;
#ifndef SHADOW_MANUAL
    if (shadowTaps > 7.0) {
        float s = 0.0;
        for (int j = -1; j <= 1; ++j)
            for (int i = -1; i <= 1; ++i) s += ShadowRef(map, vec3(c.xy + vec2(i, j) * r, c.z), invRes);
        return s * (1.0 / 9.0);
    }
#endif
    float s = ShadowRef(map, c, invRes) * 2.0;
    s += ShadowRef(map, vec3(c.xy + vec2(r, 0.0), c.z), invRes) + ShadowRef(map, vec3(c.xy - vec2(r, 0.0), c.z), invRes);
    s += ShadowRef(map, vec3(c.xy + vec2(0.0, r), c.z), invRes) + ShadowRef(map, vec3(c.xy - vec2(0.0, r), c.z), invRes);
    return s * (1.0 / 6.0);
}

// Visibilidad del sol en una cascada. inside: cuánto pesa (1 adentro, cae a 0 en el borde del mapa: fadeW es la
// fracción del lado del mapa que dura el fundido).
float CascadeVis(SHADOW_SAMPLER map, mat4 vp, float wt, float invRes, float fadeW, vec3 p, vec3 n, float ndl, out float inside)
{
    float sinT = sqrt(max(0.0, 1.0 - ndl * ndl));
    vec3 pp = p + n * wt * (0.35 + sinT);                   // corrimiento por la normal, en texels del mundo
    vec3 c = (vp * vec4(pp, 1.0)).xyz * 0.5 + 0.5;
    vec2 e2 = min(c.xy, 1.0 - c.xy);
    float edge = min(e2.x, e2.y);
    inside = smoothstep(0.0, fadeW, edge);
    if (edge <= 0.0 || c.z >= 1.0 || c.z <= 0.0) { inside = 0.0; return 1.0; }
    float tanT = min(sinT / max(ndl, 0.05), 4.0);
    c.z -= wt * (0.5 + 0.5 * tanT) / kShadowDepth;            // sesgo de profundidad, en metros del mundo
    return ShadowTaps(map, c, invRes);
}

float Shadow(vec3 p, vec3 n, float ndl)
{
    if (shadowStrength <= 0.0) return 1.0;
    bool farOn = shadowFarOn > 0.5;
    float in0, in1 = 0.0;
    // Cercana: se funde a la lejana en el 8% del borde del mapa (o a luz plena en el 15%, si no hay lejana).
    float v = CascadeVis(shadowMap, lightVP, shadowWorldTexel.x, shadowInvRes.x, farOn ? 0.08 : 0.15, p, n, ndl, in0);
    if (in0 < 1.0) {
        float v1 = 1.0;
        if (farOn) {                                            // lejana: a luz plena en el 25% de su borde
            v1 = CascadeVis(shadowMap1, lightVP1, shadowWorldTexel.y, shadowInvRes.y, 0.25, p, n, ndl, in1);
            v1 = mix(1.0, v1, in1);
        }
        v = mix(v1, v, in0);
    }
    return mix(1.0, v, shadowStrength);
}
)";

const char* kLitVS = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec2 vertexTexCoord2;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec3 fragPos;
out vec3 fragNormal;
out vec4 fragColor;
out vec2 fragTexCoord;
out vec2 fragSoil;
void main()
{
    fragSoil = vertexTexCoord2;
    fragPos = vec3(matModel * vec4(vertexPosition, 1.0));
    fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));
    fragColor = vertexColor;
    fragTexCoord = vertexTexCoord;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

const char* kDebrisVS = R"(#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
in mat4 instanceTransform;
uniform mat4 mvp;
out vec3 fragPos;
out vec3 fragNormal;
out vec4 fragColor;
out vec2 fragTexCoord;
void main()
{
    mat4 m=instanceTransform;
    fragColor=vec4(m[0][3],m[1][3],m[2][3],1.0);
    m[0][3]=0.0; m[1][3]=0.0; m[2][3]=0.0;
    vec4 wp=m*vec4(vertexPosition,1.0);
    fragPos=wp.xyz;
    fragNormal=normalize(transpose(inverse(mat3(m)))*vertexNormal);
    fragTexCoord=vec2(0.5);
    gl_Position=mvp*wp;
}
)";

const char* kLitFS = R"(
in vec3 fragPos;
in vec3 fragNormal;
in vec4 fragColor;
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform float gloss;
uniform int riderSurface;
out vec4 finalColor;

float Hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
float Noise2(vec2 p)
{
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(Hash12(i), Hash12(i + vec2(1.0, 0.0)), f.x), mix(Hash12(i + vec2(0.0, 1.0)), Hash12(i + vec2(1.0, 1.0)), f.x), f.y);
}

void main()
{
    vec4 base = texture(texture0, fragTexCoord) * colDiffuse * fragColor;
    if (riderSurface != 0) {
        // La textura original define los paneles, pero su luz pintada no debe duplicar la del sol.
        vec3 t = texture(texture0, fragTexCoord).rgb;
        vec3 rest = fragColor.rgb * 2.0 - vec3(1.0, 0.0, 1.0);
        float ax = abs(rest.x);
        float jacket = smoothstep(0.94, 0.965, rest.y);
        float sidePanel = smoothstep(0.145, 0.165, ax) * (1.0-smoothstep(0.23,0.25,ax));
        float sleeve = smoothstep(0.23,0.26,ax);
        float red = jacket * (1.0-sidePanel) * mix(1.0,smoothstep(1.14,1.17,rest.y),sleeve);
        vec3 suit = mix(vec3(0.12,0.135,0.16),vec3(0.48,0.105,0.12),red);
        float piping = (1.0-smoothstep(0.003,0.008,abs(ax-0.15))) * jacket * (1.0-sleeve);
        float band = (1.0-smoothstep(0.013,0.019,abs(rest.y-1.20))) * sleeve;
        suit = mix(suit,vec3(0.73,0.75,0.74),max(piping,band));
        // Casco, piel y guantes mantienen su atlas; la ropa tiene paneles diseñados en reposo.
        float skin = smoothstep(1.39,1.43,rest.y);
        suit = mix(suit,t,skin);
        // Trama muy fina: desaparece por derivadas antes de volverse ruido a distancia.
        vec2 weaveUV = fragTexCoord * 1400.0;
        float visible = 1.0 - smoothstep(0.25, 0.8, max(fwidth(weaveUV.x), fwidth(weaveUV.y)));
        float weave = sin(weaveUV.x * 6.28318) * sin(weaveUV.y * 6.28318);
        base = vec4(suit * (1.0 + 0.025 * weave * visible * (1.0 - skin)), 1.0);
    }
    vec3 albedo = ToLinear(base.rgb);
    vec3 n = normalize(fragNormal);
    vec3 v = normalize(camPos - fragPos);

    // Muros de la favela (v de la UV): 3 = ladrillo cerámico a la vista, 4 = revoque con manchas. El
    // dibujo sale de la posición en el mundo sobre la pared y de lejos se desvanece (no titila).
    float mode = fragTexCoord.y;
    if (mode > 2.5) {
        vec2 t2 = normalize(vec2(-n.z, n.x) + vec2(1e-5, 0.0));
        vec2 w = abs(n.y) < 0.6 ? vec2(dot(fragPos.xz, t2), fragPos.y) : fragPos.xz;
        if (mode < 3.5) {
            vec2 cell = vec2(0.30, 0.20);                               // bloque cerámico con junta de 1.5 cm
            float row = floor(w.y / cell.y);
            vec2 q = vec2(w.x / cell.x + 0.5 * mod(row, 2.0), w.y / cell.y);
            vec2 f = fract(q), fw = max(fwidth(q), vec2(1e-4));
            vec2 jw = vec2(0.015) / cell;
            vec2 j = smoothstep(jw - fw, jw + fw, f) * smoothstep(jw - fw, jw + fw, 1.0 - f);
            float var = 0.8 + 0.34 * Hash12(floor(q));
            vec3 pattern = mix(vec3(0.26, 0.25, 0.23), albedo * var, j.x * j.y);
            albedo = mix(pattern, albedo * 0.82, smoothstep(0.2, 0.5, max(fw.x, fw.y)));
        } else {
            float n1 = Noise2(w * vec2(1.3, 0.8)), n2 = Noise2(w * 7.0);
            float streak = Noise2(vec2(w.x * 3.0, w.y * 0.35));
            albedo *= (0.84 + 0.2 * n1) * (0.93 + 0.1 * n2) * (1.0 - 0.2 * smoothstep(0.55, 0.85, streak));
        }
    }
    float ndl = max(dot(n, sunDir), 0.0);
    float sh = Shadow(fragPos, n, ndl);
    vec3 col = albedo * (Ambient(n) + sunColor * ndl * sh);

    // Especular (Blinn-Phong) + reflejo del cielo en los bordes (Fresnel): plásticos y metal. Las
    // piezas armadas de varios materiales traen su brillo en la UV (v = 2, ver MeshBuilder::Append).
    float g = fragTexCoord.y > 1.5 ? fragTexCoord.x : gloss;
    float fres = pow(1.0 - max(dot(n, v), 0.0), 5.0);
    float shin = mix(8.0, 160.0, g);
    float spec = pow(max(dot(n, normalize(sunDir + v)), 0.0), shin) * (shin + 8.0) / 60.0;
    col += sunColor * (sh * ndl * spec * g * (0.3 + 0.7 * fres));
    col += SkyColor(reflect(-v, n), 0.0) * (g * (0.04 + 0.5 * fres));
    finalColor = vec4(Tonemap(ApplyFog(col, fragPos)), base.a);
}
)";

// Terreno: tierra (con piedras) y pasto como texturas de mundo, mezcladas por altura según la
// máscara de pista (alfa del color de vértice); relieve por derivadas de pantalla desde la altura
// de esas texturas y de las huellas.
const char* kTerrainFS = R"(
in vec3 fragPos;
in vec3 fragNormal;
in vec4 fragColor;
in vec2 fragTexCoord;
in vec2 fragSoil;
uniform sampler2D texture0;   // huellas (UV del terreno)
uniform sampler2D texture1;   // ruido gris de detalle
uniform sampler2D dirtMap;    // rgb color, a altura
uniform sampler2D grassMap;
uniform float soilDetail;
uniform float soilWetness;
uniform float pavedTint;      // favela: el color del vértice tiñe también el pavimento (asfalto / cemento)
uniform float sheenOn;        // 1 con ground_textures circuit o street (hay asfalto); 0 en tierra y arena
out vec4 finalColor;

// Normal perturbada por una altura escalar sin tangentes (Mikkelsen, "bump mapping unparametrized
// surfaces"): usa cómo cambian la posición y la altura entre píxeles vecinos.
vec3 PerturbNormal(vec3 p, vec3 n, float h)
{
    vec3 dpdx = dFdx(p), dpdy = dFdy(p);
    float dhdx = dFdx(h), dhdy = dFdy(h);
    vec3 r1 = cross(dpdy, n), r2 = cross(n, dpdx);
    float det = dot(dpdx, r1);
    vec3 grad = sign(det) * (dhdx * r1 + dhdy * r2);
    vec3 b = normalize(abs(det) * n - grad);
    return normalize(n + (b - n) * min(1.0, 0.7 / max(length(b - n), 1e-4)));   // tope: nunca da vuelta la cara
}

void main()
{
    vec2 w = fragPos.xz;
    float dist = length(fragPos - camPos);
    vec3 n0 = normalize(fragNormal);

    vec4 dirt = texture(dirtMap, w * 0.42);
    vec4 dirtFar = texture(dirtMap, w * 0.071 + vec2(0.31, 0.77));      // segunda escala: sin repetición visible
    dirt.rgb = mix(dirt.rgb, dirtFar.rgb, 0.4);
    vec4 grass = texture(grassMap, w * 0.65);
    float coarse = texture(texture1, w * 0.045 + vec2(0.37, 0.61)).r;
    float fine = texture(texture1, w * 0.5).r;

    // Mezcla por altura: en el borde de la pista asoman piedras entre el pasto y matas en la tierra.
    float wd = clamp(fragColor.a + (coarse - 0.5) * 0.4, 0.0, 1.0);
    float hd = dirt.a + wd * 1.3, hg = grass.a + (1.0 - wd) * 1.3;
    float m = max(hd, hg) - 0.2;
    float bd = max(hd - m, 0.0), bg = max(hg - m, 0.0);
    float t = bd / (bd + bg + 1e-4);

    vec3 dirtCol = ToLinear(dirt.rgb) * mix(1.0, 0.86, smoothstep(0.95, 0.72, n0.y));   // pendientes: tierra removida
    dirtCol *= mix(vec3(1.0), ToLinear(fragColor.rgb) * 2.0, pavedTint);
    vec3 grassCol = ToLinear(fragColor.rgb) * ToLinear(grass.rgb) * 2.0;
    vec3 albedo = mix(grassCol, dirtCol, t) * (0.82 + 0.36 * coarse) * (0.9 + 0.2 * fine);
    // Lomas del borde: el suelo se proyecta por (x, z) y en una pared se estira, así que sus manchas no valen; la pared
    // lisa de lejos parecía de cartón. Las manchas de macro-variación (más claras, más secas) se toman a lo largo de la
    // curva de nivel y a lo alto (sin estirar), sólo sobre pasto empinado.
    float wall = (1.0 - smoothstep(0.55, 0.95, n0.y)) * (1.0 - t);
    if (wall > 0.0) {
        vec2 tang = normalize(vec2(-n0.z, n0.x) + vec2(1e-5, 0.0));
        vec2 sw = vec2(dot(w, tang), fragPos.y);
        float patch1 = texture(texture1, sw * 0.055 + vec2(0.13, 0.41)).r;
        float patch2 = texture(texture1, sw * 0.16 + vec2(0.71, 0.29)).r;
        albedo *= mix(vec3(1.0), (0.84 + 0.32 * patch1) * mix(vec3(1.0), vec3(1.13, 1.05, 0.78), smoothstep(0.4, 0.7, patch2)), wall);
    }

    // Huellas: gris neutro = suelo sin tocar. Más oscuro = surco (tierra compactada, hundida);
    // más claro = tierra suelta que la rueda empujó a los costados (levantada).
    vec3 marks = texture(texture0, fragTexCoord).rgb * (1.0 / 0.784);
    float relief = dot(marks, vec3(0.3333)) - 1.0;
    // El barro físico usa su altura de 5 cm: el atlas global no limita el detalle ni pinta cuadrados.
    float physical = clamp(fragSoil.y,0.0,1.0);
    relief = mix(relief,clamp(fragSoil.x * 5.0,-0.7,0.4),physical);
    albedo *= mix(pow(marks,vec3(1.6)),vec3(1.0),physical);
    float disturbed = physical * smoothstep(0.001,0.012,abs(fragSoil.x));
    vec3 mud = ToLinear(vec3(0.31,0.235,0.16)) * (0.8+0.4*fine);
    albedo = mix(albedo,mud,disturbed * (0.45+0.45*soilWetness));

    // Altura para el relieve con un mip más borroso: la derivada de detalles de un píxel es ruido.
    float dirtH = texture(dirtMap, w * 0.42, 1.5).a, grassH = texture(grassMap, w * 0.65, 1.5).a;
    float height = (mix(grassH * 0.008, dirtH * 0.012, t) + relief * 0.085 * (1.0-physical)) * soilDetail;
    float bumpFade = 1.0 - smoothstep(8.0, 30.0, dist);                 // lejos el relieve sólo titilaría
    vec3 n = PerturbNormal(fragPos, n0, height * bumpFade);

    float ndl = max(dot(n, sunDir), 0.0);
    float sh = Shadow(fragPos, n0, max(dot(n0, sunDir), 0.0));
    float churn = clamp(-relief * 2.0, 0.0, 1.0);
    float wet = soilWetness * churn;
    albedo *= 1.0 - wet * 0.23;
    vec3 col = albedo * (Ambient(n) * (1.0 - churn * 0.16) + sunColor * ndl * sh);
    // Taludes: la luz de ambiente de una pared empinada pierde el cielo de arriba y sale más oscura que el llano
    // del lado; se le suma el ambiente de cara al cielo para que no se vean como una franja apagada.
    col += albedo * 0.5 * (1.0 - smoothstep(0.55, 0.95, n0.y)) * Ambient(vec3(0.0, 1.0, 0.0));
    vec3 view = normalize(camPos - fragPos);
    float spec = pow(max(dot(n, normalize(sunDir + view)), 0.0), 48.0);
    col += sunColor * spec * wet * 0.22 * sh * ndl;
    float fresnel = pow(1.0-max(dot(n,view),0.0),5.0);
    col += SkyColor(reflect(-view,n),0.0) * wet * (0.025+0.12*fresnel);
    // Brillo rasante del asfalto (sólo circuito y calle): reflejo Fresnel del horizonte, más fuerte en lo oscuro.
    float sheen = pow(1.0 - max(dot(n0, view), 0.0), 5.0) * smoothstep(0.3, 0.7, t) * pavedTint
                * (1.0 - 0.8 * smoothstep(0.1, 0.35, dot(albedo, vec3(1.0 / 3.0))));
    col += skyHorizon * sheen * 0.5 * sheenOn;
    finalColor = vec4(Tonemap(ApplyFog(col, fragPos)), 1.0);
}
)";

const char* kSkyFS = R"(
in vec3 fragPos;
uniform float time;
out vec4 finalColor;

float Hash21(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}
float VNoise(vec2 p)
{
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(Hash21(i), Hash21(i + vec2(1.0, 0.0)), f.x), mix(Hash21(i + vec2(0.0, 1.0)), Hash21(i + vec2(1.0, 1.0)), f.x), f.y);
}
float Fbm(vec2 p)
{
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 5; ++i) {
        s += a * VNoise(p);
        p = p * 2.03 + vec2(17.1, -9.7);
        a *= 0.5;
    }
    return s;
}

void main()
{
    vec3 d = normalize(fragPos - camPos);
    vec3 col = SkyColor(d, 1.0);
    // Nubes: una capa plana proyectada sobre el cielo, que se mueve despacio con el viento.
    if (d.y > 0.0) {
        vec2 uv = d.xz / (d.y + 0.15) * 1.4 + vec2(time * 0.006, time * 0.003);
        float n = Fbm(uv);
        float cover = smoothstep(0.5, 0.72, n) * smoothstep(0.0, 0.2, d.y);
        float s = max(dot(d, sunDir), 0.0);
        vec3 lit = skyHorizon * 1.35 + sunColor * (0.22 + 0.5 * pow(s, 8.0));
        vec3 cloud = lit * mix(1.0, 0.62, smoothstep(0.6, 0.95, n));   // lo más espeso, más gris
        col = mix(col, cloud, cover * 0.92);
    }
    finalColor = vec4(Tonemap(col), 1.0);
}
)";

// Pasto 3D: matas instanciadas (tres planos cruzados). La fila de abajo de la matriz de cada
// instancia (que en una transformación afín no se usa) trae el tono del pasto en ese lugar.
const char* kGrassVS = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in mat4 instanceTransform;
uniform mat4 mvp;
uniform float time;
uniform sampler2D texture1;    // huellas: donde pasó una rueda el pasto queda aplastado
uniform vec2 terrainOrigin;
uniform float terrainSize;
uniform float grassDistance;
uniform float grassDensity;    // 0..1 (graphics.grassDensity)
uniform float grassWind;
uniform float grassDisplacement;
uniform vec3 grassCamera;
uniform vec4 grassContacts[10];
out vec3 fragPos;
out vec2 fragTexCoord;
out vec3 fragTint;
void main()
{
    mat4 m = instanceTransform;
    fragTint = vec3(m[0][3], m[1][3], m[2][3]);
    float lodHash = m[3][3];                                   // hash de raleo de la mata (Terrain::GrassTufts)
    m[0][3] = 0.0; m[1][3] = 0.0; m[2][3] = 0.0; m[3][3] = 1.0;
    vec3 root = m[3].xyz;
    vec3 marks = textureLod(texture1, (root.xz - terrainOrigin) / terrainSize, 0.0).rgb * (1.0 / 0.784);
    float squash = clamp((1.0 - dot(marks, vec3(0.3333))) * 3.0, 0.0, 0.85);
    squash *= min(grassDisplacement, 1.0);
    float camDist = length(root.xz-grassCamera.xz);
    float distanceFade = 1.0 - smoothstep(grassDistance * 0.72, grassDistance, camDist);
    // Raleo sin saltos: la fracción de matas que quedan baja con el cuadrado de la distancia (y con la densidad); las
    // que sobran se achican hasta desaparecer con la distancia de este cuadro (el hash de cada una viaja en la matriz) y
    // las que quedan crecen para que la cobertura no se vea a puntitos.
    float keep = min(1.0, 900.0 / max(camDist * camDist, 1.0)) * grassDensity;
    float lodK = smoothstep(lodHash, lodHash + 0.15, keep);
    float grow = min(2.0, inversesqrt(max(keep, 0.05)));
    vec4 wp = m * vec4(vertexPosition.x, vertexPosition.y * (1.0 - squash), vertexPosition.z, 1.0);
    distanceFade *= lodK;
    wp.xyz = root + (wp.xyz-root)*distanceFade*grow;
    float top = 1.0 - vertexTexCoord.y;                        // 0 en la base, 1 en la punta
    wp.xz += vec2(sin(time * 1.7 + root.x * 0.6 + root.z * 0.4), cos(time * 1.3 + root.z * 0.5)) * (0.07 * top * top * grassWind * (1.0-squash) * distanceFade);
    vec2 push = vec2(0.0);
    float bend = 0.0;
    for (int i=0; i<10; ++i) {
        if (grassContacts[i].w <= 0.0) continue;
        vec2 offset = root.xz-grassContacts[i].xz;
        float influence = 1.0-smoothstep(0.12,grassContacts[i].w,length(offset));
        influence *= 1.0-smoothstep(0.3,1.0,abs(root.y-grassContacts[i].y));
        push += offset/max(length(offset),0.1) * influence;
        bend = max(bend,influence);
    }
    wp.xz += push * (0.32 * top*top * grassDisplacement * distanceFade);
    wp.y -= min(wp.y-root.y, 0.24 * bend*top*top * grassDisplacement * distanceFade);
    fragPos = wp.xyz;
    fragTexCoord = vertexTexCoord;
    gl_Position = mvp * wp;
}
)";

const char* kGrassFS = R"(
in vec3 fragPos;
in vec2 fragTexCoord;
in vec3 fragTint;
uniform sampler2D texture0;
out vec4 finalColor;
void main()
{
    vec4 tex = texture(texture0, fragTexCoord);
    if (tex.a < 0.4) discard;
    vec3 albedo = ToLinear(fragTint) * ToLinear(tex.rgb) * 2.0;
    vec3 n = vec3(0.0, 1.0, 0.0);                  // se ilumina como el suelo: sin costuras con el terreno
    float ndl = max(dot(n, sunDir), 0.0);
    float sh = Shadow(fragPos, n, ndl);
    float ao = mix(0.8, 1.0, 1.0 - fragTexCoord.y);   // la base de la mata, entre hojas, recibe menos cielo
    // A la sombra la mata recibe rebote de las hojas de al lado y del suelo (el terreno en sombra tiene su relleno de
    // taludes): sin esto, la base oscura de la hoja (textura) queda casi negra al lado del suelo en sombra.
    vec3 col = albedo * (Ambient(n) * ao * (1.0 + 0.5 * (1.0 - sh)) + sunColor * ndl * sh);
    finalColor = vec4(Tonemap(ApplyFog(col, fragPos)), 1.0);
}
)";

// Marcas de goma en el asfalto (TerrainDeformation): tiras apoyadas en el terreno que oscurecen lo que ya
// está dibujado (mezcla multiplicativa: sirve igual al sol y a la sombra). Cada vértice trae su borde en la
// posición y en la normal el corrimiento desde el centro de la tira; en el color, cuánto oscurece.
const char* kSkidVS = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
uniform vec3 camPos;
uniform float pixelAngle;      // rad que mide un píxel de la escena
out vec3 fragPos;
out float fragSide;
out float fragDark;
void main()
{
    vec3 off = vertexNormal;
    vec3 center = vertexPosition - off;
    vec3 d = center - camPos;
    float dist = max(length(d), 1e-3);
    vec3 v = d / dist;
    // A lo lejos (o vista de costado, rasante) una tira de 12-18 cm mide menos de un píxel y se rasteriza a los
    // saltos: rayas que titilan. Se ensancha hasta ~1.5 píxeles y se aclara en la misma proporción: en promedio
    // oscurece lo mismo (lo que harían los mipmaps en una textura).
    float px = length(off - dot(off, v) * v) / (dist * pixelAngle);
    float grow = clamp(0.75 / max(px, 1e-4), 1.0, 16.0);
    vec3 p = center + off * grow;
    // Hacia la cámara a lo largo de la mirada: cae en el mismo píxel, sólo cambia la profundidad. Con los 4 mm
    // de altura sobre el terreno, no se pisa con el asfalto a ninguna distancia (y queda sobre las líneas pintadas).
    p -= v * (0.005 + 0.001 * dist);
    fragPos = p;
    fragSide = vertexTexCoord.x;
    fragDark = vertexColor.a / grow;
    gl_Position = mvp * vec4(p, 1.0);
}
)";

const char* kSkidFS = R"(
in vec3 fragPos;
in float fragSide;
in float fragDark;
out vec4 finalColor;

float Hash11(float x) { return fract(sin(x * 127.1) * 43758.5453); }

void main()
{
    // Bordes suaves, el centro un poco más marcado y vetas a lo largo (la goma se pega despareja). Las vetas se
    // apagan cuando la tira mide pocos píxeles: si no, titilan.
    float s = abs(fragSide);
    float a = fragDark * (1.0 - smoothstep(0.6, 1.0, s)) * (0.85 + 0.15 * (1.0 - s * s));
    float streak = mix(0.7, 1.0, Hash11(floor(fragSide * 4.0) + 13.0));
    a *= mix(streak, 0.85, clamp(fwidth(fragSide) * 2.5, 0.0, 1.0));
    // Con la niebla se va borrando (se oscurece lo que ya tiene niebla encima).
    float dist = length(fragPos - camPos);
    a *= exp(-(dist * fogDensity) * (dist * fogDensity));
    finalColor = vec4(0.0, 0.0, 0.0, a);
}
)";

// Pasada de sombras: sólo profundidad (el framebuffer no tiene color).
const char* kDepthVS = R"(#version 330
in vec3 vertexPosition;
uniform mat4 mvp;
void main() { gl_Position = mvp * vec4(vertexPosition, 1.0); }
)";
const char* kDepthFS = R"(#version 330
out vec4 finalColor;
void main() { finalColor = vec4(1.0); }
)";

// Post-proceso sobre la imagen ya tonemapeada.
const char* kPostFS = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec2 resolution;
uniform vec2 focus;            // uv del punto nítido
uniform float speedBlur;
uniform float aberration;
uniform float vignette;
uniform float time;
uniform float grain;
uniform vec4 grade;            // exposición, saturación, contraste, nitidez (el estilo de color; ver Renderer::DrawPost)
uniform int fxaaEnabled;
out vec4 finalColor;

// FXAA (versión compacta de la de Timothy Lottes): suaviza bordes siguiendo el gradiente de luma. `diag` devuelve
// el promedio de las cuatro diagonales (sin suavizar), que usa la nitidez.
vec3 Fxaa(vec2 uv, out vec3 diag)
{
    vec2 px = 1.0 / resolution;
    vec3 nw = texture(texture0, uv + vec2(-1.0, -1.0) * px).rgb;
    vec3 ne = texture(texture0, uv + vec2(1.0, -1.0) * px).rgb;
    vec3 sw = texture(texture0, uv + vec2(-1.0, 1.0) * px).rgb;
    vec3 se = texture(texture0, uv + vec2(1.0, 1.0) * px).rgb;
    diag = 0.25 * (nw + ne + sw + se);
    vec3 m = texture(texture0, uv).rgb;
    const vec3 L = vec3(0.299, 0.587, 0.114);
    float lNW = dot(nw, L), lNE = dot(ne, L), lSW = dot(sw, L), lSE = dot(se, L), lM = dot(m, L);
    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), (lNW + lSW) - (lNE + lSE));
    float reduce = max((lNW + lNE + lSW + lSE) * (0.25 / 8.0), 1.0 / 128.0);
    dir = clamp(dir / (min(abs(dir.x), abs(dir.y)) + reduce), vec2(-8.0), vec2(8.0)) * px;
    vec3 a = 0.5 * (texture(texture0, uv + dir * (1.0 / 3.0 - 0.5)).rgb + texture(texture0, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 b = a * 0.5 + 0.25 * (texture(texture0, uv - dir * 0.5).rgb + texture(texture0, uv + dir * 0.5).rgb);
    float lB = dot(b, L);
    return (lB < lMin || lB > lMax) ? a : b;
}

float AmountAt(vec2 uv, float aspect)
{
    return speedBlur * smoothstep(0.12, 0.75, length((uv - focus) * vec2(aspect, 1.0)));
}

// FXAA + motion blur radial en un punto: el centro (la moto) queda nítido, los bordes se estiran hacia afuera como
// el suelo que pasa al costado de la cámara. Todo pasa por acá, también los canales de la aberración: mezclar una
// muestra cruda con la ya suavizada cancelaría el FXAA (escalones rosa y verde) aunque la aberración valga 0.
vec3 Scene(vec2 uv, float aspect, out vec3 diag)
{
    vec3 col;
    if (fxaaEnabled != 0) {
        col = Fxaa(uv, diag);
    } else {
        vec2 px = 1.0 / resolution;
        col = texture(texture0, uv).rgb;
        diag = 0.25 * (texture(texture0, uv + vec2(-1.0, -1.0) * px).rgb + texture(texture0, uv + vec2(1.0, -1.0) * px).rgb
                     + texture(texture0, uv + vec2(-1.0, 1.0) * px).rgb + texture(texture0, uv + vec2(1.0, 1.0) * px).rgb);
    }
    vec2 d = uv - focus;
    float r = length(d * vec2(aspect, 1.0));
    float amount = AmountAt(uv, aspect);
    if (amount > 0.0005) {
        vec3 acc = col;
        float wsum = 1.0;
        for (int i = 1; i <= 12; ++i) {
            float t = float(i) / 12.0;
            float w = 1.0 - 0.6 * t;
            acc += texture(texture0, uv - d * (amount * t / max(r, 0.05))).rgb * w;
            wsum += w;
        }
        col = acc / wsum;
    }
    return col;
}

float Hash2(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

void main()
{
    vec2 uv = fragTexCoord;
    float aspect = resolution.x / resolution.y;
    vec3 diag;
    vec3 col = Scene(uv, aspect, diag);

    // Aberración cromática: rojo y azul se separan un poco hacia los bordes (sólo si la hay: costo triple).
    vec2 c = uv - 0.5;
    float e = aberration * dot(c, c) * 4.0;
    if (e > 0.0) {
        vec3 dr, db;
        col.r = Scene(uv + c * e, aspect, dr).r;
        col.b = Scene(uv - c * e, aspect, db).b;
    }

    // Estilo de color (grade.x exposición, .y saturación, .z contraste, .w nitidez), sobre la imagen ya tonemapeada:
    // 1. nitidez: la diferencia con el promedio de las diagonales, acotada (no dibuja halos); con el motion blur se
    //    apaga (no hay nada que afilar); 2. exposición; 3. saturación; 4. contraste (curva S suave).
    float sharp = grade.w * (1.0 - clamp(AmountAt(uv, aspect) / 0.004, 0.0, 1.0));
    col += clamp(col - diag, vec3(-0.06), vec3(0.06)) * sharp;
    col *= grade.x;
    col = mix(vec3(dot(col, vec3(0.2126, 0.7152, 0.0722))), col, grade.y);
    col = clamp(col, 0.0, 1.0);
    col = mix(col, col * col * (3.0 - 2.0 * col), grade.z);

    // Viñeta y grano de película.
    col *= 1.0 - vignette * smoothstep(0.45, 1.25, length(c * vec2(aspect, 1.0) * 1.6));
    float g = fract(sin(dot(uv * resolution + time * 61.7, vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
    col += g * grain;
    // Dither triangular de ±1 nivel al final: sin él, el degradé del cielo se corta en bandas de un nivel.
    col += (Hash2(gl_FragCoord.xy) + Hash2(gl_FragCoord.xy + 7.0) - 1.0) / 255.0;
    finalColor = vec4(col, 1.0);
}
)";

std::string Fragment(const char* body, bool shadows, bool manualShadows = false)
{
    return std::string(kVersion) + (manualShadows ? "#define SHADOW_MANUAL 1\n" : "") + kCommon + (shadows ? kShadow : "") + body;
}

// ------------------------------------------------------------------------ texturas generadas
// Ruido de valor tileable: la retícula de `period` celdas se repite cada `size` píxeles.
float TileNoise(int x, int y, int size, int period, unsigned seed)
{
    auto hash = [&](int ix, int iy) {
        unsigned h = (unsigned)(((ix % period) + period) % period) * 374761393u + (unsigned)(((iy % period) + period) % period) * 668265263u + seed * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return (float)((h ^ (h >> 16)) & 0xffff) / 65535.0f;
    };
    const float cell = (float)size / (float)period;
    float fx = (float)x / cell, fy = (float)y / cell;
    int ix = (int)fx, iy = (int)fy;
    float tx = fx - ix, ty = fy - iy;
    tx = tx * tx * (3.0f - 2.0f * tx);
    ty = ty * ty * (3.0f - 2.0f * ty);
    float a = hash(ix, iy) + (hash(ix + 1, iy) - hash(ix, iy)) * tx;
    float b = hash(ix, iy + 1) + (hash(ix + 1, iy + 1) - hash(ix, iy + 1)) * tx;
    return a + (b - a) * ty;
}

float TileFbm(int x, int y, int size, unsigned seed)
{
    return TileNoise(x, y, size, 8, seed) * 0.45f + TileNoise(x, y, size, 32, seed + 1) * 0.35f + TileNoise(x, y, size, 128, seed + 2) * 0.20f;
}

struct Rng {
    unsigned s;
    float Next()
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return (float)(s & 0xffffff) / (float)0xffffff;
    }
};

float Smooth01(float e0, float e1, float x)
{
    float t = (x - e0) / (e1 - e0);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

unsigned char U8(float v) { return (unsigned char)(v < 0.0f ? 0.0f : (v > 1.0f ? 255.0f : v * 255.0f)); }

Texture2D UploadTileable(Image img)
{
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&tex);
    SetTextureFilter(tex, TEXTURE_FILTER_TRILINEAR);
    SetTextureWrap(tex, TEXTURE_WRAP_REPEAT);
    return tex;                                             // la anisotropía (el suelo se ve de costado) la pone Renderer::UpdateGraphics
}

Texture2D GenDetailTexture()
{
    const int n = 256;
    Image img = GenImageColor(n, n, WHITE);
    Color* px = (Color*)img.data;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            unsigned char c = U8(TileFbm(x, y, n, 1));
            px[y * n + x] = {c, c, c, 255};
        }
    return UploadTileable(img);
}

// Tierra: suelo con variación y piedritas redondeadas. RGB = color, A = altura (relieve y mezcla).
Texture2D GenDirtTexture()
{
    const int n = 512;
    std::vector<float> height(n * n), tone(n * n), stone(n * n, 0.0f), stoneTone(n * n, 0.0f);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const float v = TileFbm(x, y, n, 21);
            height[y * n + x] = v * 0.4f;
            tone[y * n + x] = v;
        }
    Rng rng{0x9e3779b9u};
    for (int k = 0; k < 650; ++k) {
        const float cx = rng.Next() * n, cy = rng.Next() * n;
        const float r = 0.9f + rng.Next() * rng.Next() * 4.0f;      // casi todas chicas, alguna más grande
        const float shade = rng.Next();
        const int R = (int)std::ceil(r) + 1;
        for (int dy = -R; dy <= R; ++dy)
            for (int dx = -R; dx <= R; ++dx) {
                const int px = (int)cx + dx, py = (int)cy + dy;
                const float ddx = (float)px + 0.5f - cx, ddy = (float)py + 0.5f - cy;
                const float d = std::sqrt(ddx * ddx + ddy * ddy) / r;
                if (d >= 1.0f) continue;
                const int i = ((py % n + n) % n) * n + ((px % n + n) % n);
                const float dome = 1.0f - d * d;
                height[i] = std::fmax(height[i], 0.3f + 0.45f * dome * dome);   // canto redondeado, sin escalón
                const float cover = Smooth01(1.0f, 0.6f, d);
                if (cover > stone[i]) {
                    stone[i] = cover;
                    stoneTone[i] = shade * (1.0f - 0.35f * d);   // borde de la piedra más oscuro
                }
            }
    }
    Image img = GenImageColor(n, n, WHITE);
    Color* px = (Color*)img.data;
    for (int i = 0; i < n * n; ++i) {
        const float t = tone[i];
        const float soilR = 0.38f + 0.14f * t, soilG = 0.285f + 0.105f * t, soilB = 0.215f + 0.075f * t;
        const float s = 0.40f + 0.16f * stoneTone[i];
        const float st = stone[i] * 0.8f;
        px[i] = {U8(soilR + (s * 1.0f - soilR) * st), U8(soilG + (s * 0.94f - soilG) * st), U8(soilB + (s * 0.86f - soilB) * st),
                 U8(height[i])};
    }
    return UploadTileable(img);
}

// Pasto: brillo neutro (el color lo pone el vértice) con matas y hojas sueltas. A = altura.
Texture2D GenGrassTexture()
{
    const int n = 512;
    std::vector<float> lum(n * n), height(n * n), dry(n * n, 0.0f);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const float v = TileFbm(x, y, n, 41);
            lum[y * n + x] = 0.52f + 0.22f * v;
            height[y * n + x] = v * 0.3f;
        }
    Rng rng{0x2545f491u};
    for (int k = 0; k < 14000; ++k) {
        float x = rng.Next() * n, y = rng.Next() * n;
        const float ang = rng.Next() * 6.2831853f;
        const float len = 3.0f + rng.Next() * 9.0f;
        const float bright = 0.62f + 0.4f * rng.Next();
        const float isDry = rng.Next() < 0.12f ? 1.0f : 0.0f;
        const float sx = std::cos(ang), sy = std::sin(ang);
        for (float s = 0.0f; s < len; s += 0.7f, x += sx * 0.7f, y += sy * 0.7f) {
            const int i = (((int)y % n + n) % n) * n + (((int)x % n + n) % n);
            const float t = s / len;                                   // 0 base -> 1 punta
            lum[i] = bright * (0.8f + 0.25f * t);
            height[i] = std::fmax(height[i], 0.45f + 0.55f * (1.0f - t * 0.4f));
            dry[i] = std::fmax(dry[i] * 0.5f, isDry * t);
        }
    }
    Image img = GenImageColor(n, n, WHITE);
    Color* px = (Color*)img.data;
    for (int i = 0; i < n * n; ++i) {
        const float l = lum[i], d = dry[i];
        px[i] = {U8(l * (0.92f + 0.25f * d)), U8(l), U8(l * (0.82f - 0.25f * d)), U8(height[i])};
    }
    return UploadTileable(img);
}

// Favela, pavimento: gris claro neutro (el vértice le da el tono de asfalto o cemento) con agregado,
// grietas, parches y manchas de aceite. A = altura (las grietas y los parches bajan).
Texture2D GenPavedTexture()
{
    const int n = 512;
    std::vector<float> lum(n * n), height(n * n);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const float v = TileFbm(x, y, n, 61), g = TileNoise(x, y, n, 256, 62);
            lum[y * n + x] = 0.66f + 0.14f * v + 0.12f * (g - 0.5f);
            height[y * n + x] = 0.5f + 0.2f * g;
        }
    Rng rng{0x51A5u};
    for (int k = 0; k < 9; ++k) {                                  // parches rectangulares
        const int x0 = (int)(rng.Next() * n), y0 = (int)(rng.Next() * n), w = 40 + (int)(rng.Next() * 120), h = 30 + (int)(rng.Next() * 90);
        const float tone = 0.85f + 0.25f * rng.Next();
        for (int y = y0; y < y0 + h; ++y)
            for (int x = x0; x < x0 + w; ++x) {
                const int i = (y % n) * n + (x % n);
                lum[i] *= tone;
                height[i] -= 0.04f;
            }
    }
    for (int k = 0; k < 26; ++k) {                                 // grietas: caminatas al azar
        float x = rng.Next() * n, y = rng.Next() * n, a = rng.Next() * 6.2831853f;
        const int len = 40 + (int)(rng.Next() * 160);
        for (int s = 0; s < len; ++s) {
            a += (rng.Next() - 0.5f) * 0.6f;
            x += std::cos(a);
            y += std::sin(a);
            const int i = (((int)y % n + n) % n) * n + (((int)x % n + n) % n);
            lum[i] *= 0.55f;
            height[i] = 0.2f;
        }
    }
    for (int k = 0; k < 14; ++k) {                                 // manchas de aceite
        const float cx = rng.Next() * n, cy = rng.Next() * n, r = 6.0f + rng.Next() * 16.0f;
        for (int dy = -(int)r; dy <= (int)r; ++dy)
            for (int dx = -(int)r; dx <= (int)r; ++dx) {
                const float d = std::sqrt((float)(dx * dx + dy * dy)) / r;
                if (d >= 1.0f) continue;
                const int i = ((((int)cy + dy) % n + n) % n) * n + ((((int)cx + dx) % n + n) % n);
                lum[i] *= 1.0f - 0.3f * (1.0f - d);
            }
    }
    Image img = GenImageColor(n, n, WHITE);
    Color* px = (Color*)img.data;
    for (int i = 0; i < n * n; ++i) px[i] = {U8(lum[i]), U8(lum[i] * 0.99f), U8(lum[i] * 0.97f), U8(height[i])};
    return UploadTileable(img);
}

// Favela, tierra del morro: neutra clara con terrones y piedritas (el vértice la tiñe de rojo).
Texture2D GenSoilTexture()
{
    const int n = 512;
    std::vector<float> lum(n * n), height(n * n);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const float v = TileFbm(x, y, n, 71);
            lum[y * n + x] = 0.58f + 0.2f * v;
            height[y * n + x] = v * 0.45f;
        }
    Rng rng{0x7E77Au};
    for (int k = 0; k < 900; ++k) {
        const float cx = rng.Next() * n, cy = rng.Next() * n, r = 0.8f + rng.Next() * rng.Next() * 3.5f, tone = 0.7f + 0.6f * rng.Next();
        for (int dy = -(int)r - 1; dy <= (int)r + 1; ++dy)
            for (int dx = -(int)r - 1; dx <= (int)r + 1; ++dx) {
                const float d = std::sqrt((float)(dx * dx + dy * dy)) / r;
                if (d >= 1.0f) continue;
                const int i = ((((int)cy + dy) % n + n) % n) * n + ((((int)cx + dx) % n + n) % n);
                lum[i] = lum[i] + (tone * 0.7f - lum[i]) * (1.0f - d * d);
                height[i] = std::fmax(height[i], 0.5f + 0.3f * (1.0f - d * d));
            }
    }
    Image img = GenImageColor(n, n, WHITE);
    Color* px = (Color*)img.data;
    for (int i = 0; i < n * n; ++i) px[i] = {U8(lum[i]), U8(lum[i] * 0.97f), U8(lum[i] * 0.93f), U8(height[i])};
    return UploadTileable(img);
}

// Arena: neutra clara (el vértice le da el tono) con grano fino y ondas de viento (ripples) que
// serpentean; A = altura (las ondas dan el relieve de cerca). Las crestas de las ondas van a lo largo
// de x de la textura (el suelo la usa con x = este, así que corren de este a oeste).
Texture2D GenSandTexture()
{
    const int n = 512;
    std::vector<float> lum(n * n), height(n * n);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const float warp = TileFbm(x, y, n, 81);                 // 0..1, tileable
            const float big = TileFbm(x, y, n, 82);
            // 16 ondas por lado de la textura, corridas por el warp (las crestas se curvan y se cortan).
            const float ph = ((float)y / (float)n * 16.0f + warp * 3.0f) * 6.2831853f;
            const float ripple = 0.5f + 0.5f * std::sin(ph + 0.6f * std::sin(ph));   // cresta aguda, valle ancho
            const float grain = TileNoise(x, y, n, 256, 83);
            lum[y * n + x] = 0.70f + 0.07f * (big - 0.5f) + 0.035f * (ripple - 0.5f) + 0.06f * (grain - 0.5f);
            height[y * n + x] = 0.25f + 0.5f * ripple * (0.6f + 0.4f * big) + 0.08f * grain;
        }
    Image img = GenImageColor(n, n, WHITE);
    Color* px = (Color*)img.data;
    for (int i = 0; i < n * n; ++i) px[i] = {U8(lum[i]), U8(lum[i] * 0.985f), U8(lum[i] * 0.965f), U8(height[i])};
    return UploadTileable(img);
}

// Hojas de pasto para las matas 3D: fondo transparente (con color de pasto para que el filtrado
// no oscurezca los bordes), hojas finas que se afinan hacia la punta. La base queda abajo.
Texture2D GenBladeTexture()
{
    const int n = 128;
    Image img = GenImageColor(n, n, Color{110, 120, 90, 0});
    Color* px = (Color*)img.data;
    Rng rng{0x68e31da4u};
    for (int k = 0; k < 18; ++k) {
        const float x0 = 14.0f + rng.Next() * (n - 28.0f);
        const float h = n * (0.5f + 0.48f * rng.Next());
        const float lean = (rng.Next() - 0.5f) * 0.7f;
        const float w0 = 1.8f + rng.Next() * 2.4f;
        const float tone = 0.8f + 0.35f * rng.Next();
        for (int y = 0; y < (int)h; ++y) {
            const float t = (float)y / h;                          // 0 base -> 1 punta
            const float cx = x0 + lean * (float)y * (0.4f + t);
            const float w = w0 * (1.0f - t) + 0.3f;
            for (int x = (int)(cx - w - 1.0f); x <= (int)(cx + w + 1.0f); ++x) {
                if (x < 0 || x >= n) continue;
                const float cover = Smooth01(w + 0.6f, w - 0.6f, std::fabs((float)x + 0.5f - cx));
                if (cover <= 0.0f) continue;
                Color& c = px[(n - 1 - y) * n + x];
                const float l = (0.42f + 0.52f * t) * tone;
                c = {U8(l * 0.95f), U8(l), U8(l * 0.78f), (unsigned char)std::max((int)c.a, (int)(cover * 255.0f))};
            }
        }
    }
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&tex);
    SetTextureFilter(tex, TEXTURE_FILTER_TRILINEAR);
    SetTextureWrap(tex, TEXTURE_WRAP_CLAMP);
    return tex;
}

constexpr float kTau = 6.28318530718f;

// Mata de pasto: tres planos verticales cruzados (se dibuja sin culling). La base en y = 0.
Mesh GenTuftMesh()
{
    MeshBuilder b;
    const float w = 0.28f, h = 0.42f;
    const Vector3 up = {0.0f, 1.0f, 0.0f};
    for (int q = 0; q < 3; ++q) {
        const float a = (float)q * kTau / 6.0f;
        const float dx = std::cos(a) * w, dz = std::sin(a) * w;
        unsigned short v0 = b.Vertex({-dx, 0.0f, -dz}, up, {0.0f, 1.0f});
        unsigned short v1 = b.Vertex({dx, 0.0f, dz}, up, {1.0f, 1.0f});
        unsigned short v2 = b.Vertex({dx, h, dz}, up, {1.0f, 0.0f});
        unsigned short v3 = b.Vertex({-dx, h, -dz}, up, {0.0f, 0.0f});
        b.Tri(v0, v1, v2);
        b.Tri(v0, v2, v3);
    }
    return b.Build();
}

// Casco de motocross: esfera deformada (más larga atrás) con la mentonera en punta hacia adelante y
// abajo, el hueco de las antiparras con lente y marco, la correa alrededor y la base abierta tapada
// con un disco oscuro. Colores en los vértices (se dibuja con color blanco). Espacio local: +Z
// adelante, +Y arriba, origen en el centro de la cabeza.
Mesh GenHelmetMesh()
{
    const int rings = 40, slices = 64;
    const float thMax = 0.8f * 3.14159265f;                  // la base queda abierta (el cuello)
    const Color shell = {240, 240, 236, 255}, accent = {226, 74, 32, 255}, dark = {32, 32, 36, 255};
    const Color lens = {30, 36, 48, 255}, frame = {236, 96, 40, 255}, strap = {22, 22, 26, 255};
    const int row = slices + 1;
    std::vector<Vector3> p((rings + 1) * row);
    std::vector<Color> c(p.size());
    for (int r = 0; r <= rings; ++r) {
        const float th = thMax * (float)r / (float)rings;
        for (int s = 0; s <= slices; ++s) {
            const float ph = kTau * (float)s / (float)slices;                // costura atrás
            const Vector3 d = {std::sin(th) * std::sin(ph), std::cos(th), -std::sin(th) * std::cos(ph)};
            const float ax = std::fabs(d.x);
            Vector3 q = {d.x * 0.124f, d.y * 0.138f, d.z * (d.z > 0.0f ? 0.148f : 0.162f)};
            const float chin = Smooth01(0.2f, 0.8f, d.z) * Smooth01(0.1f, -0.5f, d.y) * (1.0f - Smooth01(0.35f, 0.85f, ax));
            q.z += chin * 0.095f;                                            // mentonera larga
            q.y -= chin * 0.03f;
            const float eye = Smooth01(0.5f, 0.72f, d.z) * Smooth01(-0.22f, -0.05f, d.y) * Smooth01(0.42f, 0.22f, d.y) * Smooth01(0.78f, 0.6f, ax);
            q = Vector3Add(q, Vector3Scale(d, eye * 0.012f));                 // las antiparras sobresalen
            p[r * row + s] = q;

            Color col = shell;
            if (ax < 0.1f && d.y > 0.2f) col = accent;                         // franja central
            const float diag = d.y + 0.5f * d.z;
            if (ax > 0.55f && diag > -0.18f && diag < 0.02f) col = accent;     // gráfica lateral
            if (d.y < -0.6f && chin < 0.3f) col = dark;                        // ribete inferior
            if (chin > 0.6f) col = (ax < 0.18f && d.y > -0.55f) ? dark : accent;   // ventilación de la mentonera
            if (d.z < 0.55f && std::fabs(d.y) < 0.075f) col = strap;           // correa de las antiparras
            if (eye > 0.6f) col = lens;
            else if (eye > 0.2f) col = frame;
            c[r * row + s] = col;
        }
    }
    MeshBuilder b;
    for (int r = 0; r <= rings; ++r)
        for (int s = 0; s <= slices; ++s) {
            // Normal por diferencias entre vecinos de la grilla (en el polo, hacia arriba).
            const Vector3 du = Vector3Subtract(p[r * row + (s + 1) % row], p[r * row + (s + slices - 1) % row]);
            const Vector3 dv = Vector3Subtract(p[std::min(r + 1, rings) * row + s], p[std::max(r - 1, 0) * row + s]);
            Vector3 n = Vector3Normalize(Vector3CrossProduct(dv, du));
            if (r == 0 || Vector3Length(n) < 0.5f) n = {0.0f, 1.0f, 0.0f};
            if (Vector3DotProduct(n, p[r * row + s]) < 0.0f) n = Vector3Negate(n);
            b.Vertex(p[r * row + s], n, {0.0f, 0.0f}, c[r * row + s]);
        }
    for (int r = 0; r < rings; ++r)
        for (int s = 0; s < slices; ++s) {
            unsigned short a = (unsigned short)(r * row + s), bb = a + 1;
            unsigned short cc = (unsigned short)((r + 1) * row + s + 1), d = cc - 1;
            b.Tri(a, bb, cc);
            b.Tri(a, cc, d);
        }
    // Tapa de la base (el interior del casco, oscuro).
    Vector3 center = {0.0f, 0.0f, 0.0f};
    for (int s = 0; s < slices; ++s) center = Vector3Add(center, p[rings * row + s]);
    center = Vector3Scale(center, 1.0f / (float)slices);
    const unsigned short mid = b.Vertex(center, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}, dark);
    for (int s = 0; s < slices; ++s) {
        const unsigned short a = b.Vertex(p[rings * row + s], {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}, dark);
        const unsigned short n1 = b.Vertex(p[rings * row + s + 1], {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}, dark);
        b.Tri(mid, a, n1);
    }
    return b.Build();
}

// Visera del casco: placa curva sobre las antiparras, inclinada hacia arriba, con cara de arriba y de
// abajo. Blanca: toma el color con que se dibuja.
Mesh GenHelmetPeakMesh()
{
    MeshBuilder b;
    const int n = 18;
    for (int face = 0; face < 2; ++face) {
        const float off = face == 0 ? 0.003f : -0.003f;
        const Vector3 nrm = Vector3Normalize({0.0f, face == 0 ? 1.0f : -1.0f, face == 0 ? -0.35f : 0.35f});
        const unsigned short first = (unsigned short)(b.pos.size() / 3);
        for (int i = 0; i <= n; ++i) {
            const float a = -1.1f + 2.2f * (float)i / (float)n;
            b.Vertex({std::sin(a) * 0.102f, 0.07f + off, std::cos(a) * 0.123f}, nrm);          // borde contra el casco
            b.Vertex({std::sin(a) * 0.112f, 0.108f + off, std::cos(a) * 0.215f}, nrm);         // borde de adelante
        }
        for (int i = 0; i < n; ++i) {
            const unsigned short in0 = (unsigned short)(first + i * 2), out0 = in0 + 1, in1 = in0 + 2, out1 = in0 + 3;
            if (face == 0) {
                b.Tri(in0, out0, out1);
                b.Tri(in0, out1, in1);
            } else {
                b.Tri(in0, out1, out0);
                b.Tri(in0, in1, out1);
            }
        }
    }
    return b.Build();
}

// ------------------------------------------------------------------------ cuerpo del piloto
// Equipo del piloto: remera, pantalón, botas, guantes (el casco usa los mismos tonos).
const Color kJersey = {36, 92, 196, 255}, kJerseyDark = {20, 46, 112, 255}, kKitWhite = {236, 236, 232, 255};
const Color kKitOrange = {226, 74, 32, 255}, kPants = {44, 44, 52, 255}, kPantsPanel = {74, 76, 88, 255};
const Color kBoot = {228, 228, 224, 255}, kBootBlack = {30, 30, 34, 255}, kBuckle = {172, 174, 180, 255};
const Color kGlove = {26, 26, 30, 255}, kKnuckle = {72, 74, 82, 255}, kBrace = {58, 60, 68, 255};

// Pieza "torneada" a lo largo de Y (-0.5..0.5) con sección elíptica de semiejes width(y) en X y
// depth(y) en Z (0.5 = el grosor con que se escala), puntas redondeadas y colores por vértice. En
// `edges` el color (y el radio, si cambia) salta de golpe: ahí se duplica el anillo.
struct Lathe {
    std::function<float(float)> width, depth;
    std::function<Color(float y, float ang)> color;          // ang: 0 = +Z (adelante), pi/2 = +X
    std::vector<float> edges;
    float capLow = 0.1f, capHigh = 0.1f;                    // largo de la punta redondeada en y = -0.5 / 0.5
    int rings = 16, slices = 16;
};

Mesh GenLathe(const Lathe& L)
{
    auto roundness = [&](float y) {
        if (L.capHigh > 0.0f && y > 0.5f - L.capHigh) {
            const float t = (y - (0.5f - L.capHigh)) / L.capHigh;
            return std::sqrt(std::max(0.0f, 1.0f - t * t));
        }
        if (L.capLow > 0.0f && y < -0.5f + L.capLow) {
            const float t = (-0.5f + L.capLow - y) / L.capLow;
            return std::sqrt(std::max(0.0f, 1.0f - t * t));
        }
        return 1.0f;
    };
    auto surface = [&](float y, float ang) {
        const float f = roundness(y);
        return Vector3{std::sin(ang) * L.width(y) * f, y, std::cos(ang) * L.depth(y) * f};
    };
    // Anillos: parejos en el cuerpo, más juntos hacia las puntas y duplicados en los bordes de color.
    std::vector<float> ys;
    for (int i = 0; i <= L.rings; ++i) ys.push_back(-0.5f + (float)i / (float)L.rings);
    for (int k = 1; k < 5; ++k) {
        const float s = std::sin((float)k / 5.0f * 1.5708f);
        if (L.capHigh > 0.0f) ys.push_back(0.5f - L.capHigh * (1.0f - s));
        if (L.capLow > 0.0f) ys.push_back(-0.5f + L.capLow * (1.0f - s));
    }
    for (float e : L.edges) {
        ys.push_back(e - 1e-3f);
        ys.push_back(e + 1e-3f);
    }
    std::sort(ys.begin(), ys.end());

    MeshBuilder b;
    const int S = L.slices, row = S + 1;
    for (float y : ys)
        for (int s = 0; s <= S; ++s) {
            const float ang = kTau * (float)s / (float)S, e = 1e-3f;
            Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(surface(y, ang + e), surface(y, ang - e)),
                                                             Vector3Subtract(surface(y + e, ang), surface(y - e, ang))));
            if (roundness(y) < 1e-3f || Vector3Length(n) < 0.5f) n = {0.0f, y > 0.0f ? 1.0f : -1.0f, 0.0f};   // punta
            b.Vertex(surface(y, ang), n, {0.0f, 0.0f}, L.color(y, ang));
        }
    for (int r = 0; r + 1 < (int)ys.size(); ++r)
        for (int s = 0; s < S; ++s) {
            unsigned short a = (unsigned short)(r * row + s), bb = a + 1;
            unsigned short c = (unsigned short)((r + 1) * row + s + 1), d = c - 1;
            b.Tri(a, bb, c);
            b.Tri(a, c, d);
        }
    return b.Build();
}

// Caja redondeada (superelipsoide) de lado 1: exponente chico = más cuadrada. Color según la
// posición en la caja (-0.5..0.5).
Mesh GenRoundedBox(float e, const std::function<Color(Vector3)>& color, int rings = 14, int slices = 20)
{
    auto sgnpow = [](float v, float p) { return (v < 0.0f ? -1.0f : 1.0f) * std::pow(std::fabs(v), p); };
    MeshBuilder b;
    const int row = slices + 1;
    for (int r = 0; r <= rings; ++r) {
        const float u = -1.5708f + 3.14159f * (float)r / (float)rings;          // abajo -> arriba
        for (int s = 0; s <= slices; ++s) {
            const float v = kTau * (float)s / (float)slices;                   // 0 = +Z, pi/2 = +X
            const Vector3 p = {0.5f * sgnpow(std::cos(u), e) * sgnpow(std::sin(v), e), 0.5f * sgnpow(std::sin(u), e),
                               0.5f * sgnpow(std::cos(u), e) * sgnpow(std::cos(v), e)};
            // Normal exacta del superelipsoide: la misma forma con exponente 2 - e.
            const float m = 2.0f - e;
            Vector3 n = Vector3Normalize({sgnpow(std::cos(u), m) * sgnpow(std::sin(v), m), sgnpow(std::sin(u), m),
                                          sgnpow(std::cos(u), m) * sgnpow(std::cos(v), m)});
            if (r == 0 || r == rings) n = {0.0f, r == 0 ? -1.0f : 1.0f, 0.0f};
            b.Vertex(p, n, {0.0f, 0.0f}, color(p));
        }
    }
    for (int r = 0; r < rings; ++r)
        for (int s = 0; s < slices; ++s) {
            unsigned short a = (unsigned short)(r * row + s), bb = a + 1;
            unsigned short c = (unsigned short)((r + 1) * row + s + 1), d = c - 1;
            b.Tri(a, bb, c);
            b.Tri(a, c, d);
        }
    return b.Build();
}

void GenRiderMeshes(Mesh* out)
{
    // Torso: cintura angosta, pecho y hombros anchos. Franjas en el pecho, paneles laterales, cuello
    // y el panel blanco del número en la espalda.
    Lathe torso;
    torso.width = [](float y) { return 0.42f + 0.08f * Smooth01(-0.3f, 0.3f, y); };
    torso.depth = [](float y) { return 0.43f + 0.07f * Smooth01(-0.4f, 0.15f, y); };
    torso.capLow = 0.06f;
    torso.capHigh = 0.16f;
    torso.slices = 24;
    torso.edges = {-0.42f, -0.26f, 0.0f, 0.08f, 0.16f, 0.22f, 0.40f};
    torso.color = [](float y, float ang) {
        if (y > 0.40f || y < -0.42f) return kJerseyDark;                        // cuello y ruedo
        if (y > 0.08f && y < 0.16f) return kKitWhite;                           // franjas del pecho
        if (y > 0.16f && y < 0.22f) return kKitOrange;
        if (std::cos(ang) < -0.55f && y > -0.26f && y < 0.0f) return kKitWhite; // número, atrás
        if (std::fabs(std::sin(ang)) > 0.88f) return kJerseyDark;               // paneles laterales
        return kJersey;
    };
    out[RiderMesh::Torso] = GenLathe(torso);

    // Cadera: pantalón con cinturón y hebilla adelante.
    out[RiderMesh::Pelvis] = GenRoundedBox(0.55f, [](Vector3 p) {
        if (p.y > 0.22f) return (p.z > 0.3f && std::fabs(p.x) < 0.1f) ? kBuckle : kBootBlack;
        return std::fabs(p.x) > 0.38f ? kPantsPanel : kPants;
    });

    // Cuello con collarín (neck brace) negro y naranja: tapa el espacio entre hombros y casco.
    Lathe neck;
    neck.width = neck.depth = [](float) { return 0.5f; };
    neck.capLow = neck.capHigh = 0.2f;
    neck.edges = {-0.12f, 0.08f};
    neck.color = [](float y, float) { return (y > -0.12f && y < 0.08f) ? kKitOrange : kBootBlack; };
    out[RiderMesh::Neck] = GenLathe(neck);

    // Muslo (cadera -> rodilla): se afina hacia la rodilla; rodillera con franja naranja.
    Lathe thigh;
    thigh.width = thigh.depth = [](float y) {
        const float r = 0.5f - 0.1f * (y + 0.5f) + 0.03f * std::sin(3.1416f * (y + 0.5f));
        return y > 0.3f ? r + 0.04f : r;
    };
    thigh.capLow = 0.16f;
    thigh.capHigh = 0.12f;
    thigh.edges = {0.3f, 0.38f, 0.43f};
    thigh.color = [](float y, float ang) {
        if (y > 0.38f && y < 0.43f) return kKitOrange;
        if (y > 0.3f) return kBrace;
        return std::fabs(std::sin(ang)) > 0.9f ? kPantsPanel : kPants;           // panel lateral
    };
    out[RiderMesh::Thigh] = GenLathe(thigh);

    // Canilla (rodilla -> tobillo): abajo de la rodillera, la caña de la bota de motocross, gruesa,
    // con tres correas y sus hebillas.
    Lathe shin;
    shin.width = shin.depth = [](float y) { return y < -0.3f ? 0.44f : 0.5f - 0.05f * Smooth01(0.1f, 0.5f, y); };
    shin.capLow = 0.12f;
    shin.capHigh = 0.06f;
    shin.edges = {-0.3f, -0.22f, -0.04f, 0.04f, 0.16f, 0.24f, 0.34f, 0.42f};
    shin.color = [](float y, float ang) {
        if (y < -0.3f) return kBrace;
        if (y < -0.22f) return kBootBlack;                                       // borde de la bota
        const bool strap = (y > -0.04f && y < 0.04f) || (y > 0.16f && y < 0.24f) || (y > 0.34f && y < 0.42f);
        if (strap) return std::fabs(std::sin(ang)) > 0.82f ? kBuckle : kBootBlack;
        return kBoot;
    };
    out[RiderMesh::Shin] = GenLathe(shin);

    // Pie de la bota: suela y puntera negras, franja naranja al costado.
    out[RiderMesh::Foot] = GenRoundedBox(0.4f, [](Vector3 p) {
        if (p.y < -0.26f || (p.z > 0.32f && p.y < 0.1f)) return kBootBlack;
        if (p.y > -0.08f && p.y < 0.02f && std::fabs(p.x) > 0.3f) return kKitOrange;
        return kBoot;
    });

    // Brazo (hombro -> codo): manga con franjas y codera.
    Lathe upper;
    upper.width = upper.depth = [](float y) { return 0.5f - 0.07f * (y + 0.5f); };
    upper.capLow = 0.22f;
    upper.capHigh = 0.12f;
    upper.edges = {-0.04f, 0.04f, 0.1f, 0.36f};
    upper.color = [](float y, float) {
        if (y > 0.36f) return kJerseyDark;
        if (y > -0.04f && y < 0.04f) return kKitWhite;
        if (y > 0.04f && y < 0.1f) return kKitOrange;
        return kJersey;
    };
    out[RiderMesh::UpperArm] = GenLathe(upper);

    // Antebrazo (codo -> manubrio): se afina a la muñeca, puño de la manga y guante.
    Lathe fore;
    fore.width = fore.depth = [](float y) { return y > 0.28f ? 0.42f : 0.5f - 0.12f * Smooth01(-0.4f, 0.28f, y); };
    fore.capLow = 0.12f;
    fore.capHigh = 0.08f;
    fore.edges = {0.2f, 0.28f};
    fore.color = [](float y, float) { return y > 0.28f ? kGlove : (y > 0.2f ? kJerseyDark : kJersey); };
    out[RiderMesh::Forearm] = GenLathe(fore);

    // Puño con guante: nudillos grises arriba.
    out[RiderMesh::Fist] = GenRoundedBox(0.5f, [](Vector3 p) { return (p.y > 0.15f && p.z > -0.15f) ? kKnuckle : kGlove; });

    out[RiderMesh::Helmet] = GenHelmetMesh();
    out[RiderMesh::Peak] = GenHelmetPeakMesh();
}

Vector3 Linear(Color c, float scale = 1.0f)
{
    return {std::pow(c.r / 255.0f, 2.2f) * scale, std::pow(c.g / 255.0f, 2.2f) * scale, std::pow(c.b / 255.0f, 2.2f) * scale};
}

} // namespace

// ------------------------------------------------------------------------------------ profundidad de 24 bits
// raylib pide la profundidad sin tamaño (GL_DEPTH_COMPONENT: "que elija el driver"), y un driver puede dar
// 16 bits: con el plano cercano en 0.08 m, a 100 m el escalón sería de 2 m y todo lo de lejos parpadearía.
// Se pide GL_DEPTH_COMPONENT24 con las funciones de OpenGL 3.x core que rlgl no expone, sacadas de GLFW
// (la plataforma de escritorio de raylib, en Windows y en Mac). Sin ellas, queda lo de raylib.
extern "C" {
typedef void (*GLFWglproc)(void);
GLFWglproc glfwGetProcAddress(const char* procname);
}

namespace {

#if defined(_WIN32) && !defined(_WIN64)
#define MOTOSIM_GLAPI __stdcall
#else
#define MOTOSIM_GLAPI
#endif

struct DepthGL {
    enum : unsigned {
        kTexture2D = 0x0DE1, kUnsignedInt = 0x1405, kDepthComponent = 0x1902, kDepthComponent24 = 0x81A6,
        kFramebuffer = 0x8D40, kRenderbuffer = 0x8D41, kDepthAttachment = 0x8D00, kAttachmentDepthSize = 0x8216,
        kTextureDepthSize = 0x884A,
    };
    void(MOTOSIM_GLAPI* genRenderbuffers)(int, unsigned*) = nullptr;
    void(MOTOSIM_GLAPI* bindRenderbuffer)(unsigned, unsigned) = nullptr;
    void(MOTOSIM_GLAPI* renderbufferStorage)(unsigned, unsigned, int, int) = nullptr;
    void(MOTOSIM_GLAPI* texImage2D)(unsigned, int, int, int, int, int, unsigned, unsigned, const void*) = nullptr;
    void(MOTOSIM_GLAPI* getAttachmentParam)(unsigned, unsigned, unsigned, int*) = nullptr;
    void(MOTOSIM_GLAPI* getTexLevelParam)(unsigned, int, unsigned, int*) = nullptr;
    void(MOTOSIM_GLAPI* texParameteri)(unsigned, unsigned, int) = nullptr;
    void(MOTOSIM_GLAPI* getTexParameteri)(unsigned, unsigned, int*) = nullptr;
    bool ok = false;

    static const DepthGL& Get()
    {
        static DepthGL gl = [] {
            DepthGL g;
            auto load = [](auto& fn, const char* name) { fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(glfwGetProcAddress(name)); };
            load(g.genRenderbuffers, "glGenRenderbuffers");
            load(g.bindRenderbuffer, "glBindRenderbuffer");
            load(g.renderbufferStorage, "glRenderbufferStorage");
            load(g.texImage2D, "glTexImage2D");
            load(g.getAttachmentParam, "glGetFramebufferAttachmentParameteriv");
            load(g.getTexLevelParam, "glGetTexLevelParameteriv");
            load(g.texParameteri, "glTexParameteri");
            load(g.getTexParameteri, "glGetTexParameteriv");
            g.ok = g.genRenderbuffers && g.bindRenderbuffer && g.renderbufferStorage && g.texImage2D && g.getAttachmentParam && g.getTexLevelParam;
            if (!g.ok) TraceLog(LOG_WARNING, "RENDER: sin las funciones de OpenGL para pedir 24 bits de profundidad; elige el driver");
            return g;
        }();
        return gl;
    }
};

// Como LoadRenderTexture (color RGBA8 + renderbuffer de profundidad), pero con la profundidad de 24 bits.
// UnloadRenderTexture la libera igual (rlUnloadFramebuffer borra el renderbuffer que tenga enganchado).
RenderTexture2D LoadSceneTarget(int w, int h)
{
    const DepthGL& gl = DepthGL::Get();
    RenderTexture2D t{};
    t.id = rlLoadFramebuffer();
    if (t.id == 0) return t;
    rlEnableFramebuffer(t.id);
    t.texture = {rlLoadTexture(nullptr, w, h, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1), w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    unsigned depth = 0;
    if (gl.ok) {
        gl.genRenderbuffers(1, &depth);
        gl.bindRenderbuffer(DepthGL::kRenderbuffer, depth);
        gl.renderbufferStorage(DepthGL::kRenderbuffer, DepthGL::kDepthComponent24, w, h);
        gl.bindRenderbuffer(DepthGL::kRenderbuffer, 0);
    } else {
        depth = rlLoadTextureDepth(w, h, true);
    }
    t.depth = {depth, w, h, 1, 19};
    rlFramebufferAttach(t.id, t.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    rlFramebufferAttach(t.id, depth, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_RENDERBUFFER, 0);
    if (!rlFramebufferComplete(t.id)) TraceLog(LOG_WARNING, "RENDER: la textura de la escena quedó incompleta");
    int bits = 0;
    rlEnableFramebuffer(t.id);                    // rlFramebufferAttach y rlFramebufferComplete la sueltan
    if (gl.ok) gl.getAttachmentParam(DepthGL::kFramebuffer, DepthGL::kDepthAttachment, DepthGL::kAttachmentDepthSize, &bits);
    TraceLog(LOG_INFO, "RENDER: escena de %d x %d, profundidad de %d bits", w, h, bits);
    rlDisableFramebuffer();
    return t;
}

// La textura de profundidad del mapa de sombras (la de rlLoadTextureDepth, sin tamaño) rehecha con 24 bits.
void MakeDepth24(unsigned tex, int w, int h)
{
    const DepthGL& gl = DepthGL::Get();
    if (!gl.ok) return;
    rlEnableTexture(tex);
    int before = 0, after = 0;
    gl.getTexLevelParam(DepthGL::kTexture2D, 0, DepthGL::kTextureDepthSize, &before);
    gl.texImage2D(DepthGL::kTexture2D, 0, (int)DepthGL::kDepthComponent24, w, h, 0, DepthGL::kDepthComponent, DepthGL::kUnsignedInt, nullptr);
    gl.getTexLevelParam(DepthGL::kTexture2D, 0, DepthGL::kTextureDepthSize, &after);
    rlDisableTexture();
    TraceLog(LOG_INFO, "RENDER: mapa de sombras de %d bits (el driver había dado %d)", after, before);
}

// Sombras con comparación por hardware (sampler2DShadow): hace falta glTexParameteri para pedirla en la textura
// de profundidad. Sin él, el shader de sombras se arma en su plan B (comparación a mano, ver kShadow).
bool ShadowHardware()
{
    const DepthGL& gl = DepthGL::Get();
    const bool ok = gl.ok && gl.texParameteri && gl.getTexParameteri;
    static bool said = false;
    if (!ok && !said) {
        said = true;
        TraceLog(LOG_WARNING, "RENDER: sin glTexParameteri para la comparación de sombras por hardware; filtro manual");
    }
    return ok;
}

// La textura de profundidad del mapa de sombras con comparación por hardware y filtro bilineal (cada lectura de
// sampler2DShadow promedia 2x2 texels ya comparados) y sin repetir en los bordes. Sin glTexParameteri (plan B),
// queda como la deja raylib: nearest, y el shader compara a mano.
void MakeShadowSampler(unsigned tex)
{
    if (!ShadowHardware()) return;
    const DepthGL& gl = DepthGL::Get();
    rlEnableTexture(tex);
    gl.texParameteri(DepthGL::kTexture2D, 0x884C, 0x884E);   // GL_TEXTURE_COMPARE_MODE = GL_COMPARE_REF_TO_TEXTURE
    gl.texParameteri(DepthGL::kTexture2D, 0x884D, 0x0203);   // GL_TEXTURE_COMPARE_FUNC = GL_LEQUAL
    gl.texParameteri(DepthGL::kTexture2D, 0x2800, 0x2601);   // GL_TEXTURE_MAG_FILTER = GL_LINEAR
    gl.texParameteri(DepthGL::kTexture2D, 0x2801, 0x2601);   // GL_TEXTURE_MIN_FILTER = GL_LINEAR
    gl.texParameteri(DepthGL::kTexture2D, 0x2802, 0x812F);   // GL_TEXTURE_WRAP_S = GL_CLAMP_TO_EDGE
    gl.texParameteri(DepthGL::kTexture2D, 0x2803, 0x812F);   // GL_TEXTURE_WRAP_T
    int mode = 0;
    gl.getTexParameteri(DepthGL::kTexture2D, 0x884C, &mode);
    rlDisableTexture();
    if (mode != 0x884E) TraceLog(LOG_WARNING, "RENDER: el driver no tomó la comparación de sombras por hardware (modo 0x%X)", mode);
}

// Un mapa de sombras: framebuffer sólo con profundidad de 24 bits. Id 0 si no se pudo armar.
RenderTexture2D MakeShadowTarget(int resolution)
{
    RenderTexture2D next{};
    next.id = rlLoadFramebuffer();
    next.texture.width = next.texture.height = resolution;
    rlEnableFramebuffer(next.id);
    next.depth.id = rlLoadTextureDepth(resolution, resolution, false);
    MakeDepth24(next.depth.id, resolution, resolution);
    MakeShadowSampler(next.depth.id);
    next.depth.width = next.depth.height = resolution;
    next.depth.mipmaps = 1;
    next.depth.format = PIXELFORMAT_UNCOMPRESSED_R32;
    rlFramebufferAttach(next.id, next.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
    const bool complete = rlFramebufferComplete(next.id);
    rlDisableFramebuffer();
    if (!complete) {
        rlUnloadFramebuffer(next.id);
        return RenderTexture2D{};
    }
    return next;
}

} // namespace

Mesh GenBoxMesh()
{
    MeshBuilder b;
    // Cada cara: normal n, tangente u, v = n x u  => vértices en orden antihorario vistos desde fuera.
    const Vector3 faces[6][2] = {
        {{1, 0, 0}, {0, 1, 0}}, {{-1, 0, 0}, {0, 1, 0}}, {{0, 1, 0}, {0, 0, 1}},
        {{0, -1, 0}, {0, 0, 1}}, {{0, 0, 1}, {1, 0, 0}}, {{0, 0, -1}, {1, 0, 0}},
    };
    for (const auto& f : faces) {
        Vector3 n = f[0], u = f[1], v = Vector3CrossProduct(n, u);
        Vector3 c = Vector3Scale(n, 0.5f);
        auto corner = [&](float su, float sv) {
            return Vector3Add(c, Vector3Add(Vector3Scale(u, 0.5f * su), Vector3Scale(v, 0.5f * sv)));
        };
        unsigned short a = b.Vertex(corner(-1, -1), n, {0, 0});
        unsigned short bb = b.Vertex(corner(1, -1), n, {1, 0});
        unsigned short cc = b.Vertex(corner(1, 1), n, {1, 1});
        unsigned short d = b.Vertex(corner(-1, 1), n, {0, 1});
        b.Tri(a, bb, cc);
        b.Tri(a, cc, d);
    }
    return b.Build();
}

Mesh GenCylinderMesh(int slices)
{
    MeshBuilder b;
    for (int i = 0; i <= slices; ++i) {
        float a = kTau * (float)i / (float)slices;
        Vector3 n = {std::cos(a), 0.0f, std::sin(a)};
        b.Vertex({n.x, 0.5f, n.z}, n);
        b.Vertex({n.x, -0.5f, n.z}, n);
    }
    for (int i = 0; i < slices; ++i) {
        unsigned short t0 = (unsigned short)(i * 2), b0 = t0 + 1, t1 = t0 + 2, b1 = t0 + 3;
        b.Tri(t0, t1, b1);
        b.Tri(t0, b1, b0);
    }
    for (int cap = 0; cap < 2; ++cap) {
        float y = cap == 0 ? 0.5f : -0.5f;
        Vector3 n = {0.0f, cap == 0 ? 1.0f : -1.0f, 0.0f};
        unsigned short center = b.Vertex({0.0f, y, 0.0f}, n);
        unsigned short first = 0;
        for (int i = 0; i <= slices; ++i) {
            float a = kTau * (float)i / (float)slices;
            unsigned short v = b.Vertex({std::cos(a), y, std::sin(a)}, n);
            if (i == 0) first = v;
        }
        for (int i = 0; i < slices; ++i) {
            unsigned short p0 = (unsigned short)(first + i), p1 = (unsigned short)(first + i + 1);
            if (cap == 0) b.Tri(center, p1, p0);
            else b.Tri(center, p0, p1);
        }
    }
    return b.Build();
}

Mesh GenSphereMesh(int rings, int slices)
{
    MeshBuilder b;
    for (int r = 0; r <= rings; ++r) {
        float th = 3.14159265f * (float)r / (float)rings;
        for (int s = 0; s <= slices; ++s) {
            float ph = kTau * (float)s / (float)slices;
            Vector3 n = {std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph)};
            b.Vertex(n, n);
        }
    }
    const int row = slices + 1;
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < slices; ++s) {
            unsigned short a = (unsigned short)(r * row + s), bb = a + 1;
            unsigned short c = (unsigned short)((r + 1) * row + s + 1), d = c - 1;
            b.Tri(a, bb, c);
            b.Tri(a, c, d);
        }
    }
    return b.Build();
}

Mesh GenTireMesh(float majorRadius, float minorRadius, int segments, int sides)
{
    // Toro con eje X (el eje de la rueda en espacio local de la moto).
    MeshBuilder b;
    for (int i = 0; i <= segments; ++i) {
        float u = kTau * (float)i / (float)segments;
        Vector3 e = {0.0f, std::cos(u), std::sin(u)};
        for (int j = 0; j <= sides; ++j) {
            float v = kTau * (float)j / (float)sides;
            Vector3 n = Vector3Add(Vector3Scale(e, std::cos(v)), Vector3{std::sin(v), 0.0f, 0.0f});
            Vector3 p = Vector3Add(Vector3Scale(e, majorRadius), Vector3Scale(n, minorRadius));
            b.Vertex(p, n);
        }
    }
    const int row = sides + 1;
    for (int i = 0; i < segments; ++i) {
        for (int j = 0; j < sides; ++j) {
            unsigned short a = (unsigned short)(i * row + j);
            unsigned short bb = (unsigned short)((i + 1) * row + j);
            unsigned short c = (unsigned short)((i + 1) * row + j + 1);
            unsigned short d = (unsigned short)(i * row + j + 1);
            b.Tri(a, bb, c);
            b.Tri(a, c, d);
        }
    }
    return b.Build();
}

// ------------------------------------------------------------------------------------ Renderer
void Renderer::FindLocs(Shader& sh, Locs& l)
{
    l.sunDir = GetShaderLocation(sh, "sunDir");
    l.sunColor = GetShaderLocation(sh, "sunColor");
    l.skyZenith = GetShaderLocation(sh, "skyZenith");
    l.skyHorizon = GetShaderLocation(sh, "skyHorizon");
    l.groundBounce = GetShaderLocation(sh, "groundBounce");
    l.fogDensity = GetShaderLocation(sh, "fogDensity");
    l.camPos = GetShaderLocation(sh, "camPos");
    l.exposure = GetShaderLocation(sh, "exposure");
    l.lightVP = GetShaderLocation(sh, "lightVP");
    l.lightVP1 = GetShaderLocation(sh, "lightVP1");
    l.shadowInvRes = GetShaderLocation(sh, "shadowInvRes");
    l.shadowFarOn = GetShaderLocation(sh, "shadowFarOn");
    l.shadowTaps = GetShaderLocation(sh, "shadowTaps");
    l.ambientStrength = GetShaderLocation(sh, "ambientStrength");
    l.shadowStrength = GetShaderLocation(sh, "shadowStrength");
    l.shadowSoftness = GetShaderLocation(sh, "shadowSoftness");
    l.shadowWorldTexel = GetShaderLocation(sh, "shadowWorldTexel");
}

void Renderer::ApplyAniso(Texture2D tex) const
{
    // rlTextureParameters vuelve la anisotropía a 1 en CADA llamada (también con el wrap o el filtro): tiene que ser
    // lo último que se le pide a la textura.
    if (tex.id != 0) rlTextureParameters(tex.id, RL_TEXTURE_FILTER_ANISOTROPIC, std::clamp(graphics.aniso, 1, 16));
}

void Renderer::UpdateGraphics()
{
    // Filtro anisotrópico de las texturas del suelo (los atlas de Circuit y Favela lo reaplican al ver otra revisión).
    graphics.aniso = std::clamp(graphics.aniso, 1, 16);
    if (graphics.aniso != anisoApplied && detail.id != 0) {
        for (const Texture2D& t : {detail, dirtTex, grassTex, pavedTex, soilTex, sandTex}) ApplyAniso(t);
        anisoApplied = graphics.aniso;
        ++anisoRevision;
    }
    // El radio de la lejana o su resolución cambian lo que hay dibujado en ella: Game::Draw la rehace.
    if (graphics.shadowFar != shadowFarRadius) {
        shadowFarRadius = graphics.shadowFar;
        ++shadowFarRevision;
    }
    if (graphics.shadowQuality == 0) return;
    const int resolution = graphics.shadowQuality == 3 ? 4096 : graphics.shadowQuality == 1 ? 1024 : 2048;
    const int farResolution = std::min(resolution, 3072);
    const bool wantFar = graphics.shadowFar > 0;
    if (shadowMap.id && resolution == shadowResolution && wantFar == (shadowMap1.id != 0)) return;

    // Se construye primero el reemplazo: si falla, seguimos con la textura anterior.
    const bool nearChanged = !shadowMap.id || resolution != shadowResolution;
    if (nearChanged) {
        RenderTexture2D next = MakeShadowTarget(resolution);
        if (!next.id) {
            TraceLog(LOG_WARNING, "sombras: no se pudo crear el mapa de %d", resolution);
            graphics.shadowQuality = !shadowMap.id ? 0 : shadowResolution == 4096 ? 3 : shadowResolution == 1024 ? 1 : 2;
            return;
        }
        if (shadowMap.id) rlUnloadFramebuffer(shadowMap.id);
        shadowMap = next;
        shadowResolution = resolution;
    }
    if (!wantFar || nearChanged) {                                // la lejana se rehace con la cercana (o se apaga)
        if (shadowMap1.id) rlUnloadFramebuffer(shadowMap1.id);
        shadowMap1 = RenderTexture2D{};
        shadowResolution1 = 0;
    }
    if (wantFar && !shadowMap1.id) {
        shadowMap1 = MakeShadowTarget(farResolution);
        if (shadowMap1.id) {
            shadowResolution1 = farResolution;
            ++shadowFarRevision;
        } else {
            TraceLog(LOG_WARNING, "sombras: no se pudo crear el mapa lejano de %d; queda sólo la cercana", farResolution);
            graphics.shadowFar = shadowFarRadius = 0;
        }
    }
    // Un slot de textura por cascada en los cuatro materiales que reciben sombras (occlusion y emission). Sin
    // lejana, el segundo slot lleva la misma textura (el sampler tiene que apuntar a algo válido).
    for (Material* m : {&material, &terrainMaterial, &grassMaterial, &debrisMaterial})
        if (m->maps) {
            m->maps[MATERIAL_MAP_OCCLUSION].texture = shadowMap.depth;
            m->maps[MATERIAL_MAP_EMISSION].texture = shadowMap1.id ? shadowMap1.depth : shadowMap.depth;
        }
}

// ------------------------------------------------------------------------------------ presets de calidad
namespace {
// Lo que fija cada preset: sólo campos de calidad (los que cuestan rendimiento). El estilo (sol, ambiente, exposición,
// niebla, intensidad y suavidad de sombra, viñeta, grano, aberración, viento, estilo de color) queda del jugador, y la
// física del suelo (soilMode, soilSoftness, soilDepth), la pantalla completa y el sacudón nunca entran.
struct QualityPreset {
    int shadowQuality, shadowNear, shadowFar, shadowTaps;   // sombras: resolución cercana (1 1024, 2 2048, 3 4096), radios (m), lecturas
    int grassDistance, grassDensity, propsRadius, detailRadius;
    int aniso, renderScale, soilDetail, soilParticles, grassDisplacement;
    bool fxaa;
};
//                                       sombras                 pasto           props  gente  aniso  escala  relieve  partíc.  desplaz.  fxaa
const QualityPreset kQualityPresets[Renderer::kPresets] = {
    /* Bajo  */ {1, 20, 48, 5,      25, 60, 250, 260,   4, 75, 60, 50, 100, true},
    /* Medio */ {2, 24, 80, 5,      45, 85, 350, 300,   8, 100, 100, 80, 100, true},
    /* Alto  */ {2, 24, 96, 9,      70, 100, 500, 350,  16, 100, 100, 100, 100, true},
    /* Ultra */ {3, 28, 140, 9,     120, 100, 0, 450,   16, 100, 100, 100, 100, true},
};
} // namespace

void Renderer::ApplyPreset(Graphics& g, int preset)
{
    const QualityPreset& p = kQualityPresets[std::clamp(preset, 0, kPresets - 1)];
    g.shadowQuality = p.shadowQuality;
    g.shadowNear = p.shadowNear;
    g.shadowFar = p.shadowFar;
    g.shadowTaps = p.shadowTaps;
    g.grassDistance = p.grassDistance;
    g.grassDensity = p.grassDensity;
    g.propsRadius = p.propsRadius;
    g.detailRadius = p.detailRadius;
    g.aniso = p.aniso;
    g.renderScale = p.renderScale;
    g.soilDetail = p.soilDetail;
    g.soilParticles = p.soilParticles;
    g.grassDisplacement = p.grassDisplacement;
    g.fxaa = p.fxaa;
    g.preset = g.customBase = std::clamp(preset, 0, kPresets - 1);
}

int Renderer::MatchPreset(const Graphics& g)
{
    for (int i = 0; i < kPresets; ++i) {
        const QualityPreset& p = kQualityPresets[i];
        if (g.shadowQuality == p.shadowQuality && g.shadowNear == p.shadowNear && g.shadowFar == p.shadowFar && g.shadowTaps == p.shadowTaps &&
            g.grassDistance == p.grassDistance && g.grassDensity == p.grassDensity && g.propsRadius == p.propsRadius &&
            g.detailRadius == p.detailRadius && g.aniso == p.aniso && g.renderScale == p.renderScale && g.soilDetail == p.soilDetail &&
            g.soilParticles == p.soilParticles && g.grassDisplacement == p.grassDisplacement && g.fxaa == p.fxaa)
            return i;
    }
    return -1;
}

void Renderer::ResetStyle(Graphics& g)
{
    const Graphics defaults{};
    g.sunlight = defaults.sunlight;
    g.ambient = defaults.ambient;
    g.brightness = defaults.brightness;
    g.fog = defaults.fog;
    g.shadowStrength = defaults.shadowStrength;
    g.shadowSoftness = defaults.shadowSoftness;
    g.vignette = defaults.vignette;
    g.grain = defaults.grain;
    g.aberration = defaults.aberration;
    g.grassWind = defaults.grassWind;
    g.colorStyle = defaults.colorStyle;
}

const char* Renderer::PresetName(int preset)
{
    static const char* names[kPresets] = {"Bajo", "Medio", "Alto", "Ultra"};
    return preset >= 0 && preset < kPresets ? names[preset] : "Personalizado";
}

const char* Renderer::PresetKey(int preset)
{
    static const char* keys[kPresets] = {"bajo", "medio", "alto", "ultra"};
    return preset >= 0 && preset < kPresets ? keys[preset] : "personalizado";
}

int Renderer::PresetFromKey(const std::string& key)
{
    for (int i = 0; i < kPresets; ++i)
        if (key == PresetKey(i)) return i;
    return -1;
}

std::string Renderer::GpuDescription(int* detectedPreset)
{
    // glGetString(GL_VENDOR 0x1F00 / GL_RENDERER 0x1F01) por glfwGetProcAddress, como el resto de las funciones que rlgl no expone.
    using GetString = const unsigned char*(MOTOSIM_GLAPI*)(unsigned);
    const GetString getString = reinterpret_cast<GetString>(glfwGetProcAddress("glGetString"));
    std::string vendor, renderer;
    if (getString) {
        if (const unsigned char* v = getString(0x1F00)) vendor = reinterpret_cast<const char*>(v);
        if (const unsigned char* r = getString(0x1F01)) renderer = reinterpret_cast<const char*>(r);
    }
    const int preset = gpupreset::ForGpu(vendor, renderer);   // Ultra no sale nunca sola; sin datos, Medio
    if (detectedPreset) *detectedPreset = preset;
    TraceLog(LOG_INFO, "RENDER: placa \"%s\" (%s): preset %s", renderer.c_str(), vendor.c_str(), PresetName(preset));
    return renderer.empty() ? vendor : renderer;
}

void Renderer::Init()
{
    sunDir = Vector3Normalize({-0.55f, 0.62f, -0.45f});      // ~41° de altura: sombras largas y relieve
    // Intensidades pensadas para el tone mapping: el cielo se escala para que salga del color elegido.
    sunColor = {2.35f, 2.15f, 1.85f};
    skyZenith = Linear({78, 128, 196, 255}, 0.72f);
    skyHorizon = Linear({186, 206, 224, 255}, 0.7f);
    groundBounce = Linear({92, 80, 64, 255}, 0.8f);

    UpdateGraphics();

    const bool manualShadows = !ShadowHardware();    // plan B: sin comparación por hardware
    lit = LoadShaderFromMemory(kLitVS, Fragment(kLitFS, true, manualShadows).c_str());
    terrainShader = LoadShaderFromMemory(kLitVS, Fragment(kTerrainFS, true, manualShadows).c_str());
    skyShader = LoadShaderFromMemory(kLitVS, Fragment(kSkyFS, false).c_str());
    depthShader = LoadShaderFromMemory(kDepthVS, kDepthFS);
    grassShader = LoadShaderFromMemory(kGrassVS, Fragment(kGrassFS, true, manualShadows).c_str());
    skidShader = LoadShaderFromMemory(kSkidVS, Fragment(kSkidFS, false).c_str());
    debrisShader = LoadShaderFromMemory(kDebrisVS, Fragment(kLitFS, true, manualShadows).c_str());
    FindLocs(debrisShader, debrisLocs);
    FindLocs(lit, litLocs);
    FindLocs(terrainShader, terrainLocs);
    FindLocs(skyShader, skyLocs);
    FindLocs(grassShader, grassLocs);
    FindLocs(skidShader, skidLocs);
    skidPixelLoc = GetShaderLocation(skidShader, "pixelAngle");
    glossLoc = GetShaderLocation(lit, "gloss");
    skyTimeLoc = GetShaderLocation(skyShader, "time");
    grassTimeLoc = GetShaderLocation(grassShader, "time");
    grassOriginLoc = GetShaderLocation(grassShader, "terrainOrigin");
    grassSizeLoc = GetShaderLocation(grassShader, "terrainSize");
    grassDistanceLoc = GetShaderLocation(grassShader, "grassDistance");
    grassDensityLoc = GetShaderLocation(grassShader, "grassDensity");
    grassWindLoc = GetShaderLocation(grassShader, "grassWind");
    grassDisplacementLoc = GetShaderLocation(grassShader, "grassDisplacement");
    grassCameraLoc = GetShaderLocation(grassShader, "grassCamera");
    grassContactsLoc = GetShaderLocation(grassShader, "grassContacts");
    soilDetailLoc = GetShaderLocation(terrainShader, "soilDetail");
    soilWetnessLoc = GetShaderLocation(terrainShader, "soilWetness");

    // El mapa de sombras va en el slot de "occlusion" del material; raylib lo enlaza en cada DrawMesh.
    lit.locs[SHADER_LOC_MAP_OCCLUSION] = GetShaderLocation(lit, "shadowMap");
    lit.locs[SHADER_LOC_MAP_EMISSION] = GetShaderLocation(lit, "shadowMap1");
    material = LoadMaterialDefault();
    material.shader = lit;
    material.maps[MATERIAL_MAP_OCCLUSION].texture = shadowMap.depth;
    material.maps[MATERIAL_MAP_EMISSION].texture = shadowMap1.id ? shadowMap1.depth : shadowMap.depth;

    detail = GenDetailTexture();
    dirtTex = GenDirtTexture();
    grassTex = GenGrassTexture();
    pavedTex = GenPavedTexture();
    soilTex = GenSoilTexture();
    sandTex = GenSandTexture();
    UpdateGraphics();                                        // anisotropía de las texturas recién hechas
    pavedTintLoc = GetShaderLocation(terrainShader, "pavedTint");
    sheenOnLoc = GetShaderLocation(terrainShader, "sheenOn");
    terrainShader.locs[SHADER_LOC_MAP_NORMAL] = GetShaderLocation(terrainShader, "dirtMap");
    terrainShader.locs[SHADER_LOC_MAP_ROUGHNESS] = GetShaderLocation(terrainShader, "grassMap");
    terrainShader.locs[SHADER_LOC_MAP_OCCLUSION] = GetShaderLocation(terrainShader, "shadowMap");
    terrainShader.locs[SHADER_LOC_MAP_EMISSION] = GetShaderLocation(terrainShader, "shadowMap1");
    terrainMaterial = LoadMaterialDefault();
    terrainMaterial.shader = terrainShader;
    terrainMaterial.maps[MATERIAL_MAP_SPECULAR].texture = detail;    // -> texture1
    terrainMaterial.maps[MATERIAL_MAP_NORMAL].texture = dirtTex;
    terrainMaterial.maps[MATERIAL_MAP_ROUGHNESS].texture = grassTex;
    terrainMaterial.maps[MATERIAL_MAP_OCCLUSION].texture = shadowMap.depth;
    terrainMaterial.maps[MATERIAL_MAP_EMISSION].texture = shadowMap1.id ? shadowMap1.depth : shadowMap.depth;

    skyMaterial = LoadMaterialDefault();
    skyMaterial.shader = skyShader;
    depthMaterial = LoadMaterialDefault();
    depthMaterial.shader = depthShader;
    skidMaterial = LoadMaterialDefault();
    skidMaterial.shader = skidShader;

    postShader = LoadShaderFromMemory(nullptr, kPostFS);
    postResLoc = GetShaderLocation(postShader, "resolution");
    postFocusLoc = GetShaderLocation(postShader, "focus");
    postBlurLoc = GetShaderLocation(postShader, "speedBlur");
    postAberrationLoc = GetShaderLocation(postShader, "aberration");
    postVignetteLoc = GetShaderLocation(postShader, "vignette");
    postTimeLoc = GetShaderLocation(postShader, "time");
    postGrainLoc = GetShaderLocation(postShader, "grain");
    postFxaaLoc = GetShaderLocation(postShader, "fxaaEnabled");
    postGradeLoc = GetShaderLocation(postShader, "grade");
    riderSurfaceLoc = GetShaderLocation(lit, "riderSurface");

    // Pasto: la matriz de cada instancia llega por atributo; texture1 = huellas (se asigna al dibujar).
    bladeTex = GenBladeTexture();
    grassShader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocationAttrib(grassShader, "instanceTransform");
    grassShader.locs[SHADER_LOC_MAP_OCCLUSION] = GetShaderLocation(grassShader, "shadowMap");
    grassShader.locs[SHADER_LOC_MAP_EMISSION] = GetShaderLocation(grassShader, "shadowMap1");
    grassMaterial = LoadMaterialDefault();
    grassMaterial.shader = grassShader;
    grassMaterial.maps[MATERIAL_MAP_DIFFUSE].texture = bladeTex;
    grassMaterial.maps[MATERIAL_MAP_OCCLUSION].texture = shadowMap.depth;
    grassMaterial.maps[MATERIAL_MAP_EMISSION].texture = shadowMap1.id ? shadowMap1.depth : shadowMap.depth;
    debrisShader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocationAttrib(debrisShader, "instanceTransform");
    debrisShader.locs[SHADER_LOC_MAP_OCCLUSION] = GetShaderLocation(debrisShader, "shadowMap");
    debrisShader.locs[SHADER_LOC_MAP_EMISSION] = GetShaderLocation(debrisShader, "shadowMap1");
    debrisMaterial = LoadMaterialDefault();
    debrisMaterial.shader = debrisShader;
    debrisMaterial.maps[MATERIAL_MAP_OCCLUSION].texture = shadowMap.depth;
    debrisMaterial.maps[MATERIAL_MAP_EMISSION].texture = shadowMap1.id ? shadowMap1.depth : shadowMap.depth;
    // Cada sampler de sombras a su unidad aunque no haya textura (sombras apagadas): un sampler2DShadow que
    // comparte unidad con el sampler2D de texture0 hace fallar el dibujado (GL_INVALID_OPERATION).
    for (Shader* sh : {&lit, &terrainShader, &grassShader, &debrisShader}) {
        const int units[2] = {MATERIAL_MAP_OCCLUSION, MATERIAL_MAP_EMISSION};
        SetShaderValue(*sh, sh->locs[SHADER_LOC_MAP_OCCLUSION], &units[0], SHADER_UNIFORM_INT);
        SetShaderValue(*sh, sh->locs[SHADER_LOC_MAP_EMISSION], &units[1], SHADER_UNIFORM_INT);
    }
    debris = GenSphereMesh(2, 5);

    box = GenBoxMesh();
    cylinder = GenCylinderMesh(16);
    disc = GenCylinderMesh(72);
    sphere = GenSphereMesh(10, 16);
    tire = GenTireMesh(1.0f, 0.2f, 28, 10);   // radio exterior 1.2, se escala por rueda
    rim = GenTireMesh(1.0f, 0.035f, 28, 6);
    tuft = GenTuftMesh();
    GenRiderMeshes(rider);
    for (int st = 0; st < BikeStyle::Count; ++st) {  // el chasis se arma en UpdateBikeBody
        const BikeStyleDef& style = GetBikeStyle(st);
        style.genParts(bikeParts[st]);
        for (int i = 0; i < kLiveries; ++i) {
            Mesh parts[2];
            style.genLivery(style.livery(i), parts);
            liveryParts[st][i][1] = parts[0];
            liveryParts[st][i][2] = parts[1];
        }
    }
    SetLook(MapLook{});
}

void Renderer::SetLook(const MapLook& look)
{
    // Los colores del cielo van en 0..255 (sRGB) y cada preset los oscurece un poco distinto: el
    // atardecer (el morro: sol bajo al sudoeste, cielo naranja en el horizonte y bruma cálida) o el
    // día de la pista de motocross.
    const bool sunset = look.preset == "sunset";
    auto color = [](const float* c) {
        return Color{(unsigned char)std::clamp(c[0], 0.0f, 255.0f), (unsigned char)std::clamp(c[1], 0.0f, 255.0f),
                     (unsigned char)std::clamp(c[2], 0.0f, 255.0f), 255};
    };
    sunDir = Vector3Normalize({look.sunDir[0], look.sunDir[1], look.sunDir[2]});
    ++shadowFarRevision;                      // otro mapa u otro sol: lo dibujado en la cascada lejana ya no sirve
    sunColor = {look.sunColor[0], look.sunColor[1], look.sunColor[2]};
    skyZenith = Linear(color(look.zenith), sunset ? 0.7f : 0.72f);
    skyHorizon = Linear(color(look.horizon), sunset ? 0.74f : 0.7f);
    groundBounce = Linear(color(look.ground), sunset ? 0.85f : 0.8f);
    fogDensity = look.fog;
    exposure = look.exposure;
    // Suelo: tierra de pista y pasto (dirt), pavimento y tierra roja (street) o asfalto y pasto (circuit).
    // Arena: las dos capas son arena y el color de vértice tiñe las dos (tono de la arena o de la huella).
    const bool street = look.groundTextures == "street", circuit = look.groundTextures == "circuit", sand = look.groundTextures == "sand";
    terrainMaterial.maps[MATERIAL_MAP_NORMAL].texture = sand ? sandTex : (street || circuit ? pavedTex : dirtTex);
    terrainMaterial.maps[MATERIAL_MAP_ROUGHNESS].texture = sand ? sandTex : (street ? soilTex : grassTex);
    const float tint = street || circuit || sand ? 1.0f : 0.0f;
    SetShaderValue(terrainShader, pavedTintLoc, &tint, SHADER_UNIFORM_FLOAT);
    const float sheen = street || circuit ? 1.0f : 0.0f;         // brillo rasante sólo donde hay asfalto (no en arena)
    SetShaderValue(terrainShader, sheenOnLoc, &sheen, SHADER_UNIFORM_FLOAT);
}

void Renderer::UpdateBikeBody(Vector3 steerHead, Vector3 forkUp)
{
    const int s = bikeStyle;
    if (bodyBuilt[s] && Vector3Equals(steerHead, bodySteerHead[s]) && Vector3Equals(forkUp, bodyForkUp[s])) return;
    for (int i = 0; i < kLiveries; ++i) {
        if (bodyBuilt[s]) UnloadMesh(liveryParts[s][i][0]);
        liveryParts[s][i][0] = GetBikeStyle(s).genBody(steerHead, forkUp, GetBikeStyle(s).livery(i));
    }
    bodySteerHead[s] = steerHead;
    bodyForkUp[s] = forkUp;
    bodyBuilt[s] = true;
}

void Renderer::Shutdown()
{
    UnloadMesh(box);
    UnloadMesh(cylinder);
    UnloadMesh(disc);
    UnloadMesh(sphere);
    UnloadMesh(tire);
    UnloadMesh(rim);
    UnloadMesh(tuft);
    UnloadMesh(debris);
    MemFree(debrisMaterial.maps);
    UnloadShader(debrisShader);
    for (Mesh& m : rider) UnloadMesh(m);
    for (int s = 0; s < 2; ++s) {
        for (int i = 0; i < BikeMesh::Count; ++i)
            if (i != BikeMesh::Body && i != BikeMesh::FrontFender && i != BikeMesh::ForkUpper) UnloadMesh(bikeParts[s][i]);
        for (int i = 0; i < kLiveries; ++i) {
            if (bodyBuilt[s]) UnloadMesh(liveryParts[s][i][0]);
            UnloadMesh(liveryParts[s][i][1]);
            UnloadMesh(liveryParts[s][i][2]);
        }
    }
    // Los materiales no son dueños de shaders ni texturas (no se usa UnloadMaterial): se sueltan aparte.
    UnloadShader(lit);
    UnloadShader(terrainShader);
    UnloadShader(skyShader);
    UnloadShader(depthShader);
    UnloadShader(grassShader);
    UnloadShader(skidShader);
    UnloadShader(postShader);
    if (sceneRT.id > 0) UnloadRenderTexture(sceneRT);
    UnloadTexture(detail);
    UnloadTexture(dirtTex);
    UnloadTexture(grassTex);
    UnloadTexture(pavedTex);
    UnloadTexture(soilTex);
    UnloadTexture(sandTex);
    UnloadTexture(bladeTex);
    rlUnloadFramebuffer(shadowMap.id);        // también libera la textura de profundidad
    if (shadowMap1.id) rlUnloadFramebuffer(shadowMap1.id);
}

void Renderer::SetCommon(Shader sh, const Locs& l, const Camera3D& cam)
{
    SetShaderValue(sh, l.sunDir, &sunDir, SHADER_UNIFORM_VEC3);
    const Vector3 sun = Vector3Scale(sunColor, graphics.sunlight / 100.0f);
    SetShaderValue(sh, l.sunColor, &sun, SHADER_UNIFORM_VEC3);
    SetShaderValue(sh, l.skyZenith, &skyZenith, SHADER_UNIFORM_VEC3);
    SetShaderValue(sh, l.skyHorizon, &skyHorizon, SHADER_UNIFORM_VEC3);
    SetShaderValue(sh, l.groundBounce, &groundBounce, SHADER_UNIFORM_VEC3);
    const float fog = fogDensity * graphics.fog / 100.0f;
    SetShaderValue(sh, l.fogDensity, &fog, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, l.camPos, &cam.position, SHADER_UNIFORM_VEC3);
    const float brightness = exposure * graphics.brightness / 100.0f;
    SetShaderValue(sh, l.exposure, &brightness, SHADER_UNIFORM_FLOAT);
    if (l.lightVP >= 0) SetShaderValueMatrix(sh, l.lightVP, lightVP);
    if (l.lightVP1 >= 0) SetShaderValueMatrix(sh, l.lightVP1, lightVP1);
    const float ambient = graphics.ambient / 100.0f;
    const float strength = ShadowsEnabled() ? graphics.shadowStrength / 100.0f : 0.0f;
    const float softness = graphics.shadowSoftness / 100.0f;
    // Metros por texel del mundo y 1/lado de cada cascada (la lejana tiene su propia resolución).
    const float worldTexel[2] = {2.0f * ShadowHalfSize(0) / (float)std::max(1, shadowResolution),
                                 2.0f * ShadowHalfSize(1) / (float)std::max(1, shadowResolution1)};
    const float invRes[2] = {1.0f / (float)std::max(1, shadowResolution), 1.0f / (float)std::max(1, shadowResolution1)};
    const float farOn = FarShadowEnabled() ? 1.0f : 0.0f, taps = graphics.shadowTaps >= 9 ? 9.0f : 5.0f;
    SetShaderValue(sh, l.ambientStrength, &ambient, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, l.shadowStrength, &strength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, l.shadowSoftness, &softness, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, l.shadowWorldTexel, worldTexel, SHADER_UNIFORM_VEC2);
    SetShaderValue(sh, l.shadowInvRes, invRes, SHADER_UNIFORM_VEC2);
    SetShaderValue(sh, l.shadowFarOn, &farOn, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, l.shadowTaps, &taps, SHADER_UNIFORM_FLOAT);
}

void Renderer::BeginFrame(const Camera3D& cam)
{
    SetCommon(lit, litLocs, cam);
    SetCommon(terrainShader, terrainLocs, cam);
    SetCommon(skyShader, skyLocs, cam);
    SetCommon(grassShader, grassLocs, cam);
    SetCommon(skidShader, skidLocs, cam);
    SetCommon(debrisShader, debrisLocs, cam);
    const float grassDistance=(float)graphics.grassDistance, wind=graphics.grassWind/100.0f, displacement=graphics.grassDisplacement/100.0f;
    const float soilDetail=graphics.soilDetail/100.0f, wetness=graphics.soilSoftness/100.0f;
    SetShaderValue(grassShader, grassDistanceLoc, &grassDistance, SHADER_UNIFORM_FLOAT);
    const float grassDensity = std::clamp(graphics.grassDensity, 0, 100) / 100.0f;
    SetShaderValue(grassShader, grassDensityLoc, &grassDensity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(grassShader, grassWindLoc, &wind, SHADER_UNIFORM_FLOAT);
    SetShaderValue(grassShader, grassDisplacementLoc, &displacement, SHADER_UNIFORM_FLOAT);
    SetShaderValue(grassShader, grassCameraLoc, &cam.position, SHADER_UNIFORM_VEC3);
    SetShaderValueV(grassShader, grassContactsLoc, grassContacts, SHADER_UNIFORM_VEC4, 10);
    SetShaderValue(terrainShader, soilDetailLoc, &soilDetail, SHADER_UNIFORM_FLOAT);
    SetShaderValue(terrainShader, soilWetnessLoc, &wetness, SHADER_UNIFORM_FLOAT);
    // Cuánto mide un píxel de la escena (la textura de la escena, que con renderScale es más chica que la ventana):
    // las marcas de goma se ensanchan a lo lejos para no quedar más finas que eso.
    const float pixelAngle = 2.0f * std::tan(cam.fovy * DEG2RAD * 0.5f) / (float)std::max(1, sceneRT.texture.height > 0 ? sceneRT.texture.height : GetRenderHeight());
    SetShaderValue(skidShader, skidPixelLoc, &pixelAngle, SHADER_UNIFORM_FLOAT);
    const float time = (float)GetTime();
    SetShaderValue(skyShader, skyTimeLoc, &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(grassShader, grassTimeLoc, &time, SHADER_UNIFORM_FLOAT);
    SetGloss(0.3f);
}

void Renderer::SetGloss(float gloss) { SetShaderValue(lit, glossLoc, &gloss, SHADER_UNIFORM_FLOAT); }

void Renderer::BeginScene()
{
    // Textura de la escena: el tamaño del framebuffer por renderScale (se rehace si cambia la ventana o la escala),
    // con 24 bits de profundidad pedidos (ver LoadSceneTarget). El post la estira a la ventana con filtro bilineal.
    const int scale = std::clamp(graphics.renderScale, 50, 100);
    const int w = std::max(1, (GetRenderWidth() * scale + 50) / 100), h = std::max(1, (GetRenderHeight() * scale + 50) / 100);
    if (sceneRT.id == 0 || sceneRT.texture.width != w || sceneRT.texture.height != h) {
        if (sceneRT.id > 0) UnloadRenderTexture(sceneRT);
        sceneRT = LoadSceneTarget(w, h);
        SetTextureFilter(sceneRT.texture, TEXTURE_FILTER_BILINEAR);   // FXAA y blur muestrean entre píxeles
        SetTextureWrap(sceneRT.texture, TEXTURE_WRAP_CLAMP);
    }
    BeginTextureMode(sceneRT);
}

void Renderer::EndScene() { EndTextureMode(); }

void Renderer::DrawPost(const PostFX& fx)
{
    const float res[2] = {(float)sceneRT.texture.width, (float)sceneRT.texture.height};
    const float focus[2] = {fx.focus.x, 1.0f - fx.focus.y};                 // la textura está al revés
    const float time = (float)GetTime();
    SetShaderValue(postShader, postResLoc, res, SHADER_UNIFORM_VEC2);
    SetShaderValue(postShader, postFocusLoc, focus, SHADER_UNIFORM_VEC2);
    SetShaderValue(postShader, postBlurLoc, &fx.speedBlur, SHADER_UNIFORM_FLOAT);
    SetShaderValue(postShader, postAberrationLoc, &fx.aberration, SHADER_UNIFORM_FLOAT);
    SetShaderValue(postShader, postVignetteLoc, &fx.vignette, SHADER_UNIFORM_FLOAT);
    SetShaderValue(postShader, postTimeLoc, &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(postShader, postGrainLoc, &fx.grain, SHADER_UNIFORM_FLOAT);
    const int fxaa = graphics.fxaa ? 1 : 0;
    SetShaderValue(postShader, postFxaaLoc, &fxaa, SHADER_UNIFORM_INT);
    // Estilo de color: exposición, saturación, contraste, nitidez. Vívido (el de siempre): el ACES por canal deja los
    // colores lavados y sin blancos, esto los levanta un poco sin quemar; natural: casi la imagen tal cual; suave: parejo.
    static const float kGrades[3][4] = {{1.00f, 1.00f, 0.00f, 0.20f},    // natural
                                        {1.05f, 1.08f, 0.25f, 0.40f},    // vívido
                                        {1.00f, 1.05f, 0.10f, 0.00f}};   // suave
    SetShaderValue(postShader, postGradeLoc, kGrades[std::clamp(graphics.colorStyle, 0, 2)], SHADER_UNIFORM_VEC4);
    BeginShaderMode(postShader);
    DrawTexturePro(sceneRT.texture, {0.0f, 0.0f, res[0], -res[1]}, {0.0f, 0.0f, (float)GetScreenWidth(), (float)GetScreenHeight()},
                   {0.0f, 0.0f}, 0.0f, WHITE);
    EndShaderMode();
}

void Renderer::DrawSky(const Camera3D& cam)
{
    // Esfera grande centrada en la cámara, vista desde adentro y sin escribir profundidad.
    rlDisableBackfaceCulling();
    rlDisableDepthMask();
    DrawMesh(sphere, skyMaterial, MatrixMultiply(MatrixScale(600.0f, 600.0f, 600.0f), MatrixTranslate(cam.position.x, cam.position.y, cam.position.z)));
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
}

void Renderer::BeginShadowPass(Vector3 focus, int cascade)
{
    // Base de la vista de la luz (la misma que arma BeginMode3D con up = Y). El foco se ajusta a la
    // grilla de texels de la cascada: si no, el borde de las sombras titila al moverse la cámara. Todo el
    // mundo sale de la misma orientación de luz (sólo cambian el radio y el foco).
    const float half = ShadowHalfSize(cascade);
    shadowCascade = cascade;
    const Vector3 right = Vector3Normalize(Vector3CrossProduct({0.0f, 1.0f, 0.0f}, sunDir));
    const Vector3 up = Vector3CrossProduct(sunDir, right);
    shadowRight = right;
    shadowUp = up;
    const float texel = 2.0f * half / (float)(cascade == 0 ? shadowResolution : shadowResolution1);
    const float a = Vector3DotProduct(focus, right), b = Vector3DotProduct(focus, up);
    focus = Vector3Add(focus, Vector3Add(Vector3Scale(right, std::round(a / texel) * texel - a), Vector3Scale(up, std::round(b / texel) * texel - b)));
    shadowFocuses[cascade] = focus;

    Camera3D cam{};
    cam.target = focus;
    cam.position = Vector3Add(focus, Vector3Scale(sunDir, 150.0f));   // 150 m hacia el sol: entra lo alto que da sombra
    cam.up = {0.0f, 1.0f, 0.0f};
    cam.fovy = 2.0f * half;
    cam.projection = CAMERA_ORTHOGRAPHIC;

    savedNear = rlGetCullDistanceNear();
    savedFar = rlGetCullDistanceFar();
    rlSetClipPlanes(1.0, 350.0);                                       // volumen de 349 m (kShadowDepth)
    BeginTextureMode(cascade == 0 ? shadowMap : shadowMap1);
    ClearBackground(WHITE);
    BeginMode3D(cam);
    (cascade == 0 ? lightVP : lightVP1) = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
    shadowPass = true;
}

bool Renderer::InShadowPrism(Vector3 lo, Vector3 hi, int cascade) const
{
    if (!shadowPass && cascade < 0) return true;
    if (cascade < 0) cascade = shadowCascade;
    const float half = ShadowHalfSize(cascade);
    const Vector3 f = shadowFocuses[cascade];
    const Vector3 c = {(lo.x + hi.x) * 0.5f - f.x, (lo.y + hi.y) * 0.5f - f.y, (lo.z + hi.z) * 0.5f - f.z};
    const Vector3 e = {(hi.x - lo.x) * 0.5f, (hi.y - lo.y) * 0.5f, (hi.z - lo.z) * 0.5f};
    for (const Vector3& axis : {shadowRight, shadowUp}) {
        const float reach = std::fabs(axis.x) * e.x + std::fabs(axis.y) * e.y + std::fabs(axis.z) * e.z;
        if (std::fabs(Vector3DotProduct(c, axis)) > half + reach) return false;
    }
    return true;
}

bool Renderer::BoxVisible(Vector3 lo, Vector3 hi) const
{
    const Matrix vp = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
    int outside[6] = {0, 0, 0, 0, 0, 0};
    for (int k = 0; k < 8; ++k) {
        const Vector3 p = {k & 1 ? hi.x : lo.x, k & 2 ? hi.y : lo.y, k & 4 ? hi.z : lo.z};
        const float x = vp.m0 * p.x + vp.m4 * p.y + vp.m8 * p.z + vp.m12, y = vp.m1 * p.x + vp.m5 * p.y + vp.m9 * p.z + vp.m13;
        const float z = vp.m2 * p.x + vp.m6 * p.y + vp.m10 * p.z + vp.m14, w = vp.m3 * p.x + vp.m7 * p.y + vp.m11 * p.z + vp.m15;
        outside[0] += x < -w;
        outside[1] += x > w;
        outside[2] += y < -w;
        outside[3] += y > w;
        outside[4] += z < -w;
        outside[5] += z > w;
    }
    for (int o : outside)
        if (o == 8) return false;
    return true;
}

Vector3 Renderer::CameraPosition() const
{
    const Matrix inv = MatrixInvert(rlGetMatrixModelview());
    return {inv.m12, inv.m13, inv.m14};
}

void Renderer::EndShadowPass()
{
    EndMode3D();
    EndTextureMode();
    rlSetClipPlanes(savedNear, savedFar);
    shadowPass = false;
}

void Renderer::DrawMeshColored(const Mesh& mesh, const Matrix& world, Color color)
{
    if (shadowPass) {
        DrawMesh(mesh, depthMaterial, world);
        return;
    }
    material.maps[MATERIAL_MAP_DIFFUSE].color = color;
    DrawMesh(mesh, material, world);
}

void Renderer::DrawTerrain(const Mesh& mesh, Texture2D marks)
{
    if (shadowPass) {
        DrawMesh(mesh, depthMaterial, MatrixIdentity());
        return;
    }
    terrainMaterial.maps[MATERIAL_MAP_DIFFUSE].texture = marks;          // -> texture0
    terrainMaterial.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    DrawMesh(mesh, terrainMaterial, MatrixIdentity());
}

void Renderer::DrawMeshTextured(const Mesh& mesh, const Matrix& world, Texture2D texture, bool twoSided, bool riderSurface)
{
    if (twoSided) rlDisableBackfaceCulling();
    if (shadowPass) {
        DrawMesh(mesh, depthMaterial, world);
    } else {
        int surface = riderSurface ? 1 : 0;
        SetShaderValue(lit, riderSurfaceLoc, &surface, SHADER_UNIFORM_INT);
        const Texture2D previous = material.maps[MATERIAL_MAP_DIFFUSE].texture;
        material.maps[MATERIAL_MAP_DIFFUSE].texture = texture;
        material.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
        DrawMesh(mesh, material, world);
        material.maps[MATERIAL_MAP_DIFFUSE].texture = previous;
        surface = 0;
        SetShaderValue(lit, riderSurfaceLoc, &surface, SHADER_UNIFORM_INT);
    }
    rlEnableBackfaceCulling();
}

void Renderer::DrawSkidMarks(const Mesh& mesh, int quads)
{
    if (shadowPass || quads <= 0) return;
    Mesh used = mesh;
    used.triangleCount = quads * 2;
    // Multiplicativa: con el color en negro queda destino · (1 − alfa). Sin escribir profundidad (una marca
    // encima de otra se suma) y de las dos caras (los cuadriláteros no tienen un orden fijo).
    BeginBlendMode(BLEND_MULTIPLIED);
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    DrawMesh(used, skidMaterial, MatrixIdentity());
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();
}

void Renderer::DrawGrass(const Matrix* transforms, int count, Texture2D marks, float terrainOrigin, float terrainSize)
{
    if (shadowPass || count <= 0) return;             // el pasto no proyecta sombra (sólo la recibe)
    grassMaterial.maps[MATERIAL_MAP_SPECULAR].texture = marks;           // -> texture1
    const float origin[2] = {terrainOrigin, terrainOrigin};
    SetShaderValue(grassShader, grassOriginLoc, origin, SHADER_UNIFORM_VEC2);
    SetShaderValue(grassShader, grassSizeLoc, &terrainSize, SHADER_UNIFORM_FLOAT);
    rlDisableBackfaceCulling();
    DrawMeshInstanced(tuft, grassMaterial, transforms, count);
    rlEnableBackfaceCulling();
}

void Renderer::DrawSoilChunks(const Matrix* transforms, int count)
{
    if (count <= 0 || shadowPass) return;
    DrawMeshInstanced(debris, debrisMaterial, transforms, count);
}
