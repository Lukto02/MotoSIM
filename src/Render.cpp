#include "Render.h"
#include "BikeStyleDef.h"
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

vec3 Ambient(vec3 n)
{
    return mix(groundBounce, skyZenith * 0.8 + skyHorizon * 0.35, 0.5 + 0.5 * n.y);
}
)";

// Sombra del sol: mapa de profundidad ortográfico alrededor de la moto. Filtro tienda 3x3 sobre
// comparaciones bilineales (4x4 texels con pesos): bordes suaves sin escalones.
const char* kShadow = R"(
uniform sampler2D shadowMap;
uniform mat4 lightVP;
uniform float shadowTexel;

float Shadow(vec3 p, vec3 n, float ndl)
{
    vec4 lp = lightVP * vec4(p + n * 0.03, 1.0);
    vec3 c = lp.xyz / lp.w * 0.5 + 0.5;
    float edge = min(min(c.x, 1.0 - c.x), min(c.y, 1.0 - c.y));
    if (edge <= 0.0 || c.z >= 1.0) return 1.0;
    float z = c.z - (0.00012 + 0.0005 * (1.0 - ndl));
    vec2 st = c.xy / shadowTexel - 0.5;
    vec2 base = floor(st), f = st - base;
    float lit = 0.0;
    for (int j = -1; j <= 2; ++j) {
        float wy = j == -1 ? 1.0 - f.y : (j == 2 ? f.y : 1.0);
        for (int i = -1; i <= 2; ++i) {
            float wx = i == -1 ? 1.0 - f.x : (i == 2 ? f.x : 1.0);
            float d = texture(shadowMap, (base + vec2(i, j) + 0.5) * shadowTexel).r;
            lit += (z <= d ? 1.0 : 0.0) * wx * wy;
        }
    }
    return mix(1.0, lit / 9.0, smoothstep(0.0, 0.05, edge));   // se desvanece en el borde del mapa
}
)";

const char* kLitVS = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec3 fragPos;
out vec3 fragNormal;
out vec4 fragColor;
out vec2 fragTexCoord;
void main()
{
    fragPos = vec3(matModel * vec4(vertexPosition, 1.0));
    fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));
    fragColor = vertexColor;
    fragTexCoord = vertexTexCoord;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
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
uniform sampler2D texture0;   // huellas (UV del terreno)
uniform sampler2D texture1;   // ruido gris de detalle
uniform sampler2D dirtMap;    // rgb color, a altura
uniform sampler2D grassMap;
uniform float pavedTint;      // favela: el color del vértice tiñe también el pavimento (asfalto / cemento)
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

    // Huellas: gris neutro = suelo sin tocar. Más oscuro = surco (tierra compactada, hundida);
    // más claro = tierra suelta que la rueda empujó a los costados (levantada).
    vec3 marks = texture(texture0, fragTexCoord).rgb * (1.0 / 0.784);
    float relief = dot(marks, vec3(0.3333)) - 1.0;
    albedo *= pow(marks, vec3(1.6));

    // Altura para el relieve con un mip más borroso: la derivada de detalles de un píxel es ruido.
    float dirtH = texture(dirtMap, w * 0.42, 1.5).a, grassH = texture(grassMap, w * 0.65, 1.5).a;
    float height = mix(grassH * 0.008, dirtH * 0.012, t) + relief * 0.07;
    float bumpFade = 1.0 - smoothstep(8.0, 30.0, dist);                 // lejos el relieve sólo titilaría
    vec3 n = PerturbNormal(fragPos, n0, height * bumpFade);

    float ndl = max(dot(n, sunDir), 0.0);
    float sh = Shadow(fragPos, n0, max(dot(n0, sunDir), 0.0));
    vec3 col = albedo * (Ambient(n) + sunColor * ndl * sh);
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
out vec3 fragPos;
out vec2 fragTexCoord;
out vec3 fragTint;
void main()
{
    mat4 m = instanceTransform;
    fragTint = vec3(m[0][3], m[1][3], m[2][3]);
    m[0][3] = 0.0; m[1][3] = 0.0; m[2][3] = 0.0;
    vec3 root = m[3].xyz;
    vec3 marks = textureLod(texture1, (root.xz - terrainOrigin) / terrainSize, 0.0).rgb * (1.0 / 0.784);
    float squash = clamp((1.0 - dot(marks, vec3(0.3333))) * 3.0, 0.0, 0.85);
    vec4 wp = m * vec4(vertexPosition.x, vertexPosition.y * (1.0 - squash), vertexPosition.z, 1.0);
    float top = 1.0 - vertexTexCoord.y;                        // 0 en la base, 1 en la punta
    wp.xz += vec2(sin(time * 1.7 + root.x * 0.6 + root.z * 0.4), cos(time * 1.3 + root.z * 0.5)) * (0.05 * top * top);
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
    float ao = mix(0.55, 1.0, 1.0 - fragTexCoord.y);  // la base de la mata, entre hojas, recibe menos cielo
    vec3 col = albedo * (Ambient(n) * ao + sunColor * ndl * sh);
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
out vec4 finalColor;

// FXAA (versión compacta de la de Timothy Lottes): suaviza bordes siguiendo el gradiente de luma.
vec3 Fxaa(vec2 uv)
{
    vec2 px = 1.0 / resolution;
    vec3 nw = texture(texture0, uv + vec2(-1.0, -1.0) * px).rgb;
    vec3 ne = texture(texture0, uv + vec2(1.0, -1.0) * px).rgb;
    vec3 sw = texture(texture0, uv + vec2(-1.0, 1.0) * px).rgb;
    vec3 se = texture(texture0, uv + vec2(1.0, 1.0) * px).rgb;
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

void main()
{
    vec2 uv = fragTexCoord;
    float aspect = resolution.x / resolution.y;
    vec3 col = Fxaa(uv);

    // Motion blur radial con la velocidad: el centro (la moto) queda nítido, los bordes se estiran
    // hacia afuera como el suelo que pasa al costado de la cámara.
    vec2 d = uv - focus;
    float r = length(d * vec2(aspect, 1.0));
    float amount = speedBlur * smoothstep(0.12, 0.75, r);
    if (amount > 0.0005) {
        vec3 acc = col;
        float wsum = 1.0;
        for (int i = 1; i <= 10; ++i) {
            float t = float(i) / 10.0;
            float w = 1.0 - 0.6 * t;
            acc += texture(texture0, uv - d * (amount * t / max(r, 0.05))).rgb * w;
            wsum += w;
        }
        col = acc / wsum;
    }

    // Aberración cromática: rojo y azul se separan un poco hacia los bordes.
    vec2 c = uv - 0.5;
    float e = aberration * dot(c, c) * 4.0;
    col.r = mix(col.r, texture(texture0, uv + c * e).r, 0.85);
    col.b = mix(col.b, texture(texture0, uv - c * e).b, 0.85);

    // Viñeta y grano de película.
    col *= 1.0 - vignette * smoothstep(0.45, 1.25, length(c * vec2(aspect, 1.0) * 1.6));
    float g = fract(sin(dot(uv * resolution + time * 61.7, vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
    col += g * grain;
    finalColor = vec4(col, 1.0);
}
)";

std::string Fragment(const char* body, bool shadows)
{
    return std::string(kVersion) + kCommon + (shadows ? kShadow : "") + body;
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
    SetTextureFilter(tex, TEXTURE_FILTER_ANISOTROPIC_8X);   // el suelo se ve casi siempre de costado
    SetTextureWrap(tex, TEXTURE_WRAP_REPEAT);
    return tex;
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
    l.shadowTexel = GetShaderLocation(sh, "shadowTexel");
}

void Renderer::Init()
{
    sunDir = Vector3Normalize({-0.55f, 0.62f, -0.45f});      // ~41° de altura: sombras largas y relieve
    // Intensidades pensadas para el tone mapping: el cielo se escala para que salga del color elegido.
    sunColor = {2.35f, 2.15f, 1.85f};
    skyZenith = Linear({78, 128, 196, 255}, 0.72f);
    skyHorizon = Linear({186, 206, 224, 255}, 0.7f);
    groundBounce = Linear({92, 80, 64, 255}, 0.8f);

    // Mapa de sombras: framebuffer con sólo una textura de profundidad.
    shadowMap.id = rlLoadFramebuffer();
    shadowMap.texture.width = shadowMap.texture.height = kShadowResolution;
    rlEnableFramebuffer(shadowMap.id);
    shadowMap.depth.id = rlLoadTextureDepth(kShadowResolution, kShadowResolution, false);
    MakeDepth24(shadowMap.depth.id, kShadowResolution, kShadowResolution);
    shadowMap.depth.width = shadowMap.depth.height = kShadowResolution;
    shadowMap.depth.mipmaps = 1;
    shadowMap.depth.format = PIXELFORMAT_UNCOMPRESSED_R32;
    rlFramebufferAttach(shadowMap.id, shadowMap.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
    if (!rlFramebufferComplete(shadowMap.id)) TraceLog(LOG_WARNING, "sombras: framebuffer incompleto");
    rlDisableFramebuffer();

    lit = LoadShaderFromMemory(kLitVS, Fragment(kLitFS, true).c_str());
    terrainShader = LoadShaderFromMemory(kLitVS, Fragment(kTerrainFS, true).c_str());
    skyShader = LoadShaderFromMemory(kLitVS, Fragment(kSkyFS, false).c_str());
    depthShader = LoadShaderFromMemory(kDepthVS, kDepthFS);
    grassShader = LoadShaderFromMemory(kGrassVS, Fragment(kGrassFS, true).c_str());
    skidShader = LoadShaderFromMemory(kSkidVS, Fragment(kSkidFS, false).c_str());
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

    // El mapa de sombras va en el slot de "occlusion" del material; raylib lo enlaza en cada DrawMesh.
    lit.locs[SHADER_LOC_MAP_OCCLUSION] = GetShaderLocation(lit, "shadowMap");
    material = LoadMaterialDefault();
    material.shader = lit;
    material.maps[MATERIAL_MAP_OCCLUSION].texture = shadowMap.depth;

    detail = GenDetailTexture();
    dirtTex = GenDirtTexture();
    grassTex = GenGrassTexture();
    pavedTex = GenPavedTexture();
    soilTex = GenSoilTexture();
    sandTex = GenSandTexture();
    pavedTintLoc = GetShaderLocation(terrainShader, "pavedTint");
    terrainShader.locs[SHADER_LOC_MAP_NORMAL] = GetShaderLocation(terrainShader, "dirtMap");
    terrainShader.locs[SHADER_LOC_MAP_ROUGHNESS] = GetShaderLocation(terrainShader, "grassMap");
    terrainShader.locs[SHADER_LOC_MAP_OCCLUSION] = GetShaderLocation(terrainShader, "shadowMap");
    terrainMaterial = LoadMaterialDefault();
    terrainMaterial.shader = terrainShader;
    terrainMaterial.maps[MATERIAL_MAP_SPECULAR].texture = detail;    // -> texture1
    terrainMaterial.maps[MATERIAL_MAP_NORMAL].texture = dirtTex;
    terrainMaterial.maps[MATERIAL_MAP_ROUGHNESS].texture = grassTex;
    terrainMaterial.maps[MATERIAL_MAP_OCCLUSION].texture = shadowMap.depth;

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

    // Pasto: la matriz de cada instancia llega por atributo; texture1 = huellas (se asigna al dibujar).
    bladeTex = GenBladeTexture();
    grassShader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocationAttrib(grassShader, "instanceTransform");
    grassShader.locs[SHADER_LOC_MAP_OCCLUSION] = GetShaderLocation(grassShader, "shadowMap");
    grassMaterial = LoadMaterialDefault();
    grassMaterial.shader = grassShader;
    grassMaterial.maps[MATERIAL_MAP_DIFFUSE].texture = bladeTex;
    grassMaterial.maps[MATERIAL_MAP_OCCLUSION].texture = shadowMap.depth;

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
}

void Renderer::SetCommon(Shader sh, const Locs& l, const Camera3D& cam)
{
    SetShaderValue(sh, l.sunDir, &sunDir, SHADER_UNIFORM_VEC3);
    SetShaderValue(sh, l.sunColor, &sunColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(sh, l.skyZenith, &skyZenith, SHADER_UNIFORM_VEC3);
    SetShaderValue(sh, l.skyHorizon, &skyHorizon, SHADER_UNIFORM_VEC3);
    SetShaderValue(sh, l.groundBounce, &groundBounce, SHADER_UNIFORM_VEC3);
    SetShaderValue(sh, l.fogDensity, &fogDensity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, l.camPos, &cam.position, SHADER_UNIFORM_VEC3);
    SetShaderValue(sh, l.exposure, &exposure, SHADER_UNIFORM_FLOAT);
    if (l.lightVP >= 0) SetShaderValueMatrix(sh, l.lightVP, lightVP);
    const float texel = 1.0f / (float)kShadowResolution;
    SetShaderValue(sh, l.shadowTexel, &texel, SHADER_UNIFORM_FLOAT);
}

void Renderer::BeginFrame(const Camera3D& cam)
{
    SetCommon(lit, litLocs, cam);
    SetCommon(terrainShader, terrainLocs, cam);
    SetCommon(skyShader, skyLocs, cam);
    SetCommon(grassShader, grassLocs, cam);
    SetCommon(skidShader, skidLocs, cam);
    // Cuánto mide un píxel de la escena (la textura va al tamaño real del framebuffer): las marcas de goma se
    // ensanchan a lo lejos para no quedar más finas que eso.
    const float pixelAngle = 2.0f * std::tan(cam.fovy * DEG2RAD * 0.5f) / (float)std::max(1, GetRenderHeight());
    SetShaderValue(skidShader, skidPixelLoc, &pixelAngle, SHADER_UNIFORM_FLOAT);
    const float time = (float)GetTime();
    SetShaderValue(skyShader, skyTimeLoc, &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(grassShader, grassTimeLoc, &time, SHADER_UNIFORM_FLOAT);
    SetGloss(0.3f);
}

void Renderer::SetGloss(float gloss) { SetShaderValue(lit, glossLoc, &gloss, SHADER_UNIFORM_FLOAT); }

void Renderer::BeginScene()
{
    // Textura de la escena al tamaño real del framebuffer (se rehace si cambia la ventana), con 24 bits
    // de profundidad pedidos (ver LoadSceneTarget).
    const int w = GetRenderWidth(), h = GetRenderHeight();
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

void Renderer::BeginShadowPass(Vector3 focus)
{
    // Base de la vista de la luz (la misma que arma BeginMode3D con up = Y). El foco se ajusta a la
    // grilla de texels: si no, el borde de las sombras titila al moverse la cámara.
    const Vector3 right = Vector3Normalize(Vector3CrossProduct({0.0f, 1.0f, 0.0f}, sunDir));
    const Vector3 up = Vector3CrossProduct(sunDir, right);
    const float texel = 2.0f * kShadowHalfSize / (float)kShadowResolution;
    const float a = Vector3DotProduct(focus, right), b = Vector3DotProduct(focus, up);
    focus = Vector3Add(focus, Vector3Add(Vector3Scale(right, std::round(a / texel) * texel - a), Vector3Scale(up, std::round(b / texel) * texel - b)));
    shadowFocus = focus;

    Camera3D cam{};
    cam.target = focus;
    cam.position = Vector3Add(focus, Vector3Scale(sunDir, 100.0f));
    cam.up = {0.0f, 1.0f, 0.0f};
    cam.fovy = 2.0f * kShadowHalfSize;
    cam.projection = CAMERA_ORTHOGRAPHIC;

    savedNear = rlGetCullDistanceNear();
    savedFar = rlGetCullDistanceFar();
    rlSetClipPlanes(1.0, 220.0);
    BeginTextureMode(shadowMap);
    ClearBackground(WHITE);
    BeginMode3D(cam);
    lightVP = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
    shadowPass = true;
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

void Renderer::DrawMeshTextured(const Mesh& mesh, const Matrix& world, Texture2D texture, bool twoSided)
{
    if (twoSided) rlDisableBackfaceCulling();
    if (shadowPass) {
        DrawMesh(mesh, depthMaterial, world);
    } else {
        const Texture2D previous = material.maps[MATERIAL_MAP_DIFFUSE].texture;
        material.maps[MATERIAL_MAP_DIFFUSE].texture = texture;
        material.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
        DrawMesh(mesh, material, world);
        material.maps[MATERIAL_MAP_DIFFUSE].texture = previous;
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
