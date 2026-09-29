#pragma once
// Jolt SIEMPRE antes que raylib: raylib define macros (PI, EPSILON...) que no deben tocar Jolt.
#include <Jolt/Jolt.h>

#include "raylib.h"
#include "raymath.h"

#include "BikeStyles.h"
#include "Maps.h"

// ---------------------------------------------------------------- conversiones Jolt <-> raylib
inline Vector3 ToRl(JPH::Vec3Arg v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
inline JPH::Vec3 ToJph(Vector3 v) { return JPH::Vec3(v.x, v.y, v.z); }

inline Matrix ToRl(const JPH::Mat44& m)
{
    const JPH::Vec4 c0 = m.GetColumn4(0), c1 = m.GetColumn4(1), c2 = m.GetColumn4(2), c3 = m.GetColumn4(3);
    Matrix r;
    r.m0 = c0.GetX(); r.m1 = c0.GetY(); r.m2 = c0.GetZ(); r.m3 = c0.GetW();
    r.m4 = c1.GetX(); r.m5 = c1.GetY(); r.m6 = c1.GetZ(); r.m7 = c1.GetW();
    r.m8 = c2.GetX(); r.m9 = c2.GetY(); r.m10 = c2.GetZ(); r.m11 = c2.GetW();
    r.m12 = c3.GetX(); r.m13 = c3.GetY(); r.m14 = c3.GetZ(); r.m15 = c3.GetW();
    return r;
}

// Mallas del piloto (low poly, con los colores en los vértices: se dibujan en blanco). Las de
// brazos, piernas, torso y cuello van de y = -0.5 (articulación de arriba) a y = 0.5 y miden 1 de
// grosor: se escalan por (grosor, largo, grosor). Pies, puños y cadera: cajas redondeadas de lado 1.
// Casco y visera: tamaño real, origen en el centro de la cabeza.
namespace RiderMesh {
enum Id { Torso, Pelvis, Neck, Thigh, Shin, Foot, UpperArm, Forearm, Fist, Helmet, Peak, Count };
}

// Piezas de la moto (colores y brillo en los vértices: se dibujan en blanco). Cada una en su espacio:
//  Body         chasis, motor, tanque, plásticos, asiento y escape: espacio local de la moto
//  Swingarm     basculante y cadena: origen en el eje del basculante, eje trasero en z = -kSwingarmLength
//  ShockBody    cuerpo del amortiguador: ojo de arriba en el origen, apunta a -Y
//  ShockSpring  resorte de largo kSpringLength (y de 0 a -kSpringLength): se escala en Y
//  ShockLower   ojo de abajo, asiento del resorte y vástago: origen en el ojo, apunta a +Y
//  ForkUpper    tijas, barras y porta número: origen en la pipa, +Y a lo largo de la horquilla
//  ForkLower    botellas, protectores, pinza y eje: origen en el eje delantero, +Y a lo largo de la horquilla
//  Cockpit      manubrio, puños y levas: origen en la pipa, ejes de la moto (gira con la dirección)
//  FrontFender  guardabarros delantero: origen en la tija de abajo, ejes de la moto
//  Wheel*/Tire* llanta con rayos, masa, disco (y corona) / cubierta con tacos: eje X, radio nominal
namespace BikeMesh {
enum Id { Body, Swingarm, ShockBody, ShockSpring, ShockLower, ForkUpper, ForkLower, Cockpit, FrontFender,
          WheelFront, WheelRear, TireFront, TireRear, Count };
constexpr float kFrontRadius = 0.35f, kRearRadius = 0.33f;   // radio de las cubiertas tal como se arman
constexpr float kSwingarmLength = 0.56f;
constexpr float kFenderDrop = 0.215f;                        // m de la pipa a la tija de abajo (sobre la horquilla)
inline constexpr Vector3 kSwingarmPivot{0.0f, -0.27f, -0.13f};
inline constexpr Vector3 kShockTop{0.0f, 0.02f, -0.17f};     // en el chasis
inline constexpr Vector3 kShockLow{0.0f, -0.075f, -0.14f};   // en el basculante
constexpr float kShockBodyLength = 0.16f;                    // del ojo de arriba al fin del cuerpo
constexpr float kSpringTop = 0.035f, kSpringBottom = 0.035f; // asientos del resorte desde cada ojo
constexpr float kSpringLength = 0.35f;                       // largo del resorte armado (amortiguador en reposo)
}

// Colores de la moto de cada jugador (Body, FrontFender y ForkUpper se arman una vez por esquema).
struct BikeLivery {
    Color plastic, stripe1, stripe2;
    const char* number;
};
constexpr int kLiveries = 4;
const BikeLivery& Livery(int i);
const BikeLivery& TrailLivery(int i);        // trilheira: colores de la calle (verde y amarillo, negro, ...)

// Render: luz del sol con sombras (shadow map), ambiente hemisférico, cielo con sol y niebla del
// color del cielo, todo en espacio lineal con tone mapping filmico. Un puñado de mallas primitivas
// (caja, cilindro, esfera, neumático) que se dibujan con una matriz y un color.
struct Renderer {
    // Ajustes relativos al look de cada mapa; se aplican sin recargar la partida.
    struct Graphics {
        int shadowQuality = 2; // 0 apagadas, 1 1024, 2 2048, 3 4096 (resolución de la cascada cercana)
        // Sombras en dos cascadas: la cercana (radio shadowNear, con todo lo que se mueve) y la lejana (radio
        // shadowFar, 0 = sin lejana; sólo el mundo quieto, en caché). shadowTaps: lecturas del filtro (5 o 9).
        int shadowNear = 24, shadowFar = 96, shadowTaps = 9;
        int shadowDistance = 32;   // legado (era el radio del único mapa): lo reemplaza shadowNear, queda por compatibilidad
        int shadowSoftness = 100, shadowStrength = 100;   // suavidad: separación de la grilla del filtro (60-130)
        int sunlight = 100, ambient = 100, brightness = 100, fog = 100;
        int vignette = 15, grain = 0, aberration = 0;
        bool fxaa = true;
        // Estilo de color del post (siempre encendido, ver DrawPost): 0 natural, 1 vívido, 2 suave.
        int colorStyle = 1;
        // Tamaño de la imagen 3D como % del alto de la ventana (se escala con filtro bilineal; el HUD queda nativo) y
        // filtro anisotrópico de las texturas del suelo y de los atlas de carteles (1, 4, 8 o 16).
        int renderScale = 100, aniso = 16;
        int grassDistance = 70, grassWind = 100, grassDisplacement = 100;
        // Cuánto pasto queda (%: multiplica la fracción de matas que se raleó con la distancia) y hasta dónde se
        // dibujan los props sueltos (propsRadius, m; 0 = sin límite) y la gente de las tribunas (detailRadius, m).
        int grassDensity = 100, propsRadius = 500, detailRadius = 350;
        // Física del suelo: no entra en ningún preset (alimenta las huellas físicas). soilDetail y soilParticles son visuales.
        int soilMode = 2, soilSoftness = 65, soilDepth = 24, soilDetail = 100, soilParticles = 100;
        // Presets de calidad (ver ApplyPreset): los defaults de arriba son los de Alto. preset: 0 Bajo, 1 Medio, 2 Alto,
        // 3 Ultra, -1 Personalizado (algo de calidad dejó de coincidir); customBase: el último preset aplicado, al que
        // vuelve Personalizado; autoPreset: lo eligió el juego por la placa (y puede bajarlo solo si anda lento).
        int preset = 2, customBase = 2;
        bool autoPreset = false;
    } graphics;

    // Presets de calidad Bajo / Medio / Alto / Ultra. Fijan sólo los campos de CALIDAD (los que cuestan rendimiento):
    // sombras (resolución, radios, filtro), pasto, props, gente de las tribunas, filtro de texturas, escala de la imagen,
    // FXAA, relieve fino, partículas y desplazamiento del pasto. El estilo (luz, niebla, viñeta...) y la física del suelo
    // (soilMode, soilSoftness, soilDepth) nunca entran. MatchPreset da el que coincide con `g` o -1 (Personalizado).
    static constexpr int kPresets = 4;
    static void ApplyPreset(Graphics& g, int preset);
    static int MatchPreset(const Graphics& g);
    static void ResetStyle(Graphics& g);                     // el estilo a sus defaults (no toca calidad ni suelo)
    static const char* PresetName(int preset);               // "Bajo", "Medio", "Alto", "Ultra" ("Personalizado" si < 0)
    static const char* PresetKey(int preset);                // "bajo"... "personalizado" (preferencias.ini)
    static int PresetFromKey(const std::string& key);        // -1 si no es un preset
    // Placa de video (glGetString) y el preset que le toca: hace falta el contexto de OpenGL (después de InitWindow).
    static std::string GpuDescription(int* detectedPreset = nullptr);
    bool ShadowsEnabled() const { return graphics.shadowQuality > 0 && shadowMap.id != 0; }
    bool FarShadowEnabled() const { return ShadowsEnabled() && graphics.shadowFar > 0 && shadowMap1.id != 0; }
    // Radio (m) de una cascada: 0 la cercana, 1 la lejana.
    float ShadowHalfSize(int cascade = 0) const { return (float)(cascade == 0 ? graphics.shadowNear : graphics.shadowFar); }
    // Sube cuando cambia algo que invalida lo dibujado en la cascada lejana (mapa, sol, resolución, radio).
    int ShadowFarRevision() const { return shadowFarRevision; }
    void UpdateGraphics();
    // Filtro anisotrópico actual (graphics.aniso) en una textura que no es del Renderer (los atlas de Circuit y
    // Favela). Sube AnisoRevision cuando cambia: quien la usa la reaplica si la revisión que vio es otra.
    void ApplyAniso(Texture2D tex) const;
    int AnisoRevision() const { return anisoRevision; }
    Vector4 grassContacts[10]{}; // xyz contacto, w radio; w=0 desactiva el slot
    void DrawSoilChunks(const Matrix* transforms, int count);
    Shader lit{}, terrainShader{}, skyShader{}, depthShader{}, grassShader{}, skidShader{}, debrisShader{};
    Material material{}, terrainMaterial{}, skyMaterial{}, depthMaterial{}, grassMaterial{}, skidMaterial{}, debrisMaterial{};
    Texture2D detail{}, dirtTex{}, grassTex{}, bladeTex{}, pavedTex{}, soilTex{}, sandTex{};
    Mesh box{}, cylinder{}, sphere{}, tire{}, rim{}, tuft{};
    Mesh debris{};
    Mesh disc{};                                    // cilindro de muchas caras (plataformas grandes)
    Mesh rider[RiderMesh::Count]{};
    // Estilos de moto (BikeStyles.h): las piezas iguales para todos y las pintadas de cada esquema
    // (Body, FrontFender, ForkUpper). bikeStyle: el de la moto que se dibuja ahora.
    int bikeStyle = 0;
    Mesh bikeParts[BikeStyle::Count][BikeMesh::Count]{};
    Mesh liveryParts[BikeStyle::Count][kLiveries][3]{};
    const Mesh& BikePart(BikeMesh::Id id, int livery) const
    {
        const int l = ((livery % kLiveries) + kLiveries) % kLiveries;
        if (id == BikeMesh::Body) return liveryParts[bikeStyle][l][0];
        if (id == BikeMesh::FrontFender) return liveryParts[bikeStyle][l][1];
        if (id == BikeMesh::ForkUpper) return liveryParts[bikeStyle][l][2];
        return bikeParts[bikeStyle][id];
    }
    // Luz, cielo y texturas del suelo del mapa (el "look" de su archivo: día, atardecer o colores propios).
    void SetLook(const MapLook& look);

    Vector3 sunDir{};          // dirección HACIA el sol (normalizada)
    Vector3 sunColor{};        // colores lineales
    Vector3 skyZenith{}, skyHorizon{}, groundBounce{};
    float fogDensity = 0.0042f;
    float exposure = 1.0f;

    void Init();
    void Shutdown();
    void BeginFrame(const Camera3D& cam);
    void DrawSky(const Camera3D& cam);         // dentro de BeginMode3D, antes que el resto

    // Sombras: entre BeginShadowPass y EndShadowPass se dibujan los objetos que proyectan sombra
    // (con las mismas funciones de siempre) en un mapa de profundidad visto desde el sol, centrado en
    // `focus`. cascade 0 = la cercana, 1 = la lejana.
    void BeginShadowPass(Vector3 focus, int cascade = 0);
    void EndShadowPass();
    bool InShadowPass() const { return shadowPass; }
    Vector3 ShadowFocus(int cascade = -1) const { return shadowFocuses[cascade < 0 ? shadowCascade : cascade]; }
    // ¿La caja lo..hi cae dentro del prisma de la luz de la cascada (cascade < 0: la de la pasada actual)?
    // Descarta lo que no puede dejar sombra en el mapa: se proyecta la caja sobre los ejes derecha y arriba de
    // la luz y se compara con el lado del mapa. Fuera de una pasada de sombras da siempre true.
    bool InShadowPrism(Vector3 lo, Vector3 hi, int cascade = -1) const;
    // ¿La caja lo..hi toca el frustum de la vista actual? Hay que estar dentro de BeginMode3D (sale de las matrices
    // de rlgl). Conservador: queda afuera sólo si sus 8 esquinas caen del lado de afuera de un mismo plano del
    // recorte. CameraPosition: dónde está esa cámara (para radios de dibujado).
    bool BoxVisible(Vector3 lo, Vector3 hi) const;
    Vector3 CameraPosition() const;

    // Post-proceso: la escena 3D se dibuja en una textura y pasa a pantalla con motion blur radial,
    // aberración cromática, estilo de color (graphics.colorStyle: siempre), viñeta, grano y FXAA (al dibujar en una
    // textura se pierde el MSAA). El HUD se dibuja después del post: no recibe el estilo de color.
    struct PostFX {
        Vector2 focus{0.5f, 0.5f};             // punto nítido del motion blur (pantalla, 0..1 desde arriba)
        float speedBlur = 0.0f;                // largo del desenfoque en los bordes (fracción de pantalla)
        float aberration = 0.0f;
        float vignette = 0.35f;
        float grain = 0.022f;
    };
    void BeginScene();                         // fuera de BeginDrawing, antes de BeginMode3D
    void EndScene();                           // después de EndMode3D
    void DrawPost(const PostFX& fx);           // dentro de BeginDrawing, antes del HUD

    void SetGloss(float gloss);                // brillo especular de lo que se dibuje después (0..1)
    void DrawMeshColored(const Mesh& mesh, const Matrix& world, Color color);
    void DrawTerrain(const Mesh& mesh, Texture2D marks);
    // Malla con textura propia (modelos cargados). Sin culling: los modelos suelen ser de doble cara.
    void DrawMeshTextured(const Mesh& mesh, const Matrix& world, Texture2D texture, bool twoSided = true, bool riderSurface = false);
    // twoSided = false: sólo la cara del frente (calcomanías espalda con espalda no se pisan).
    // Matas de pasto: una matriz por mata (con el tono en la fila de abajo, ver kGrassVS).
    void DrawGrass(const Matrix* transforms, int count, Texture2D marks, float terrainOrigin, float terrainSize);
    // Marcas de goma (TerrainDeformation): los primeros `quads` cuadriláteros de la malla, oscureciendo lo que ya
    // está dibujado, sin escribir profundidad. Después de lo opaco del suelo; no van en la pasada de sombras.
    void DrawSkidMarks(const Mesh& mesh, int quads);

    // Caja unitaria (1x1x1 centrada) / cilindro unitario (radio 1, eje Y de -0.5 a 0.5)
    // transformados por una matriz de Jolt.
    // El chasis depende de dónde queda la pipa de dirección (tuning.ini): se rearma si cambia.
    void UpdateBikeBody(Vector3 steerHead, Vector3 forkUp);

    void Box(const JPH::Mat44& world, Color c) { DrawMeshColored(box, ToRl(world), c); }
    void Cylinder(const JPH::Mat44& world, Color c) { DrawMeshColored(cylinder, ToRl(world), c); }
    void Sphere(const JPH::Mat44& world, Color c) { DrawMeshColored(sphere, ToRl(world), c); }
    void Disc(const JPH::Mat44& world, Color c) { DrawMeshColored(disc, ToRl(world), c); }

private:
    // Uniformes comunes a los shaders iluminados (lit, terreno, cielo).
    struct Locs {
        int sunDir = -1, sunColor = -1, skyZenith = -1, skyHorizon = -1, groundBounce = -1;
        int fogDensity = -1, camPos = -1, exposure = -1, lightVP = -1, lightVP1 = -1;
        int ambientStrength = -1, shadowStrength = -1, shadowSoftness = -1, shadowWorldTexel = -1;
        int shadowInvRes = -1, shadowFarOn = -1, shadowTaps = -1;
    };
    void FindLocs(Shader& sh, Locs& l);
    void SetCommon(Shader sh, const Locs& l, const Camera3D& cam);

    Locs litLocs, terrainLocs, skyLocs, grassLocs, skidLocs, debrisLocs;
    int grassDistanceLoc=-1, grassDensityLoc=-1, grassWindLoc=-1, grassDisplacementLoc=-1, grassCameraLoc=-1, grassContactsLoc=-1;
    int soilDetailLoc=-1, soilWetnessLoc=-1;
    int skidPixelLoc = -1;
    int glossLoc = -1, skyTimeLoc = -1, grassTimeLoc = -1, grassOriginLoc = -1, grassSizeLoc = -1, pavedTintLoc = -1;
    int riderSurfaceLoc = -1, postFxaaLoc = -1, postGradeLoc = -1, sheenOnLoc = -1;
    int anisoApplied = 0, anisoRevision = 0;           // nivel aplicado a las texturas propias (0 = ninguno todavía)
    int shadowResolution = 0, shadowResolution1 = 0;   // lado de cada mapa (la lejana: min(cercana, 3072))
    RenderTexture2D shadowMap{}, shadowMap1{}, sceneRT{};
    Shader postShader{};
    int postResLoc = -1, postFocusLoc = -1, postBlurLoc = -1, postAberrationLoc = -1, postVignetteLoc = -1, postTimeLoc = -1, postGrainLoc = -1;
    Matrix lightVP{}, lightVP1{};
    bool shadowPass = false;
    int shadowCascade = 0, shadowFarRevision = 0, shadowFarRadius = 0;
    Vector3 shadowFocuses[2]{}, shadowRight{1.0f, 0.0f, 0.0f}, shadowUp{0.0f, 1.0f, 0.0f};
    Vector3 bodySteerHead[BikeStyle::Count]{}, bodyForkUp[BikeStyle::Count]{};
    bool bodyBuilt[BikeStyle::Count] = {};
    double savedNear = 0.08, savedFar = 900.0;
};

// Generadores de mallas con normales y color de vértice blanco.
Mesh GenBoxMesh();
Mesh GenCylinderMesh(int slices);
Mesh GenSphereMesh(int rings, int slices);
Mesh GenTireMesh(float majorRadius, float minorRadius, int segments, int sides);
// Moto (BikeMeshes.cpp): las piezas iguales para todos, las pintadas de un esquema (guardabarros y
// porta número) y el chasis de un esquema para una pipa dada.
void GenBikeMeshes(Mesh* out);
void GenLiveryMeshes(const BikeLivery& livery, Mesh* out);   // out[0] FrontFender, out[1] ForkUpper
Mesh GenBikeBody(Vector3 steerHead, Vector3 forkUp, const BikeLivery& livery);
Mesh GenTwoStrokeBody(Vector3 steerHead, Vector3 forkUp, const BikeLivery& livery);   // la de motocross con motor 2T
const BikeLivery& TwoStrokeLivery(int i);
// Trilheira 450 (favela): manubrio alto, máscara con faro, tanque grande, asiento largo, parrilla,
// escape abierto y patente del Mercosur. Mismo espacio y mismas piezas que la de motocross.
void GenTrailMeshes(Mesh* out);
void GenTrailLiveryMeshes(const BikeLivery& livery, Mesh* out);
Mesh GenTrailBody(Vector3 steerHead, Vector3 forkUp, const BikeLivery& livery);
// Moto de carreras y moto de trial (sus secciones al final de BikeMeshes.cpp): mismas piezas y mismo
// espacio que la de motocross.
void GenRaceMeshes(Mesh* out);
void GenRaceLiveryMeshes(const BikeLivery& livery, Mesh* out);
Mesh GenRaceBody(Vector3 steerHead, Vector3 forkUp, const BikeLivery& livery);
const BikeLivery& RaceLivery(int i);
void GenTrialMeshes(Mesh* out);
void GenTrialLiveryMeshes(const BikeLivery& livery, Mesh* out);
Mesh GenTrialBody(Vector3 steerHead, Vector3 forkUp, const BikeLivery& livery);
const BikeLivery& TrialLivery(int i);
