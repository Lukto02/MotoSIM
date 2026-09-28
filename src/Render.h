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
    Shader lit{}, terrainShader{}, skyShader{}, depthShader{}, grassShader{};
    Material material{}, terrainMaterial{}, skyMaterial{}, depthMaterial{}, grassMaterial{};
    Texture2D detail{}, dirtTex{}, grassTex{}, bladeTex{}, pavedTex{}, soilTex{};
    Mesh box{}, cylinder{}, sphere{}, tire{}, rim{}, tuft{};
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
    // (con las mismas funciones de siempre) en un mapa de profundidad visto desde el sol,
    // centrado en `focus`.
    void BeginShadowPass(Vector3 focus);
    void EndShadowPass();
    bool InShadowPass() const { return shadowPass; }
    Vector3 ShadowFocus() const { return shadowFocus; }
    static constexpr float kShadowHalfSize = 32.0f;   // m cubiertos alrededor del foco

    // Post-proceso: la escena 3D se dibuja en una textura y pasa a pantalla con motion blur radial,
    // aberración cromática, viñeta, grano y FXAA (al dibujar en una textura se pierde el MSAA).
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
    void DrawMeshTextured(const Mesh& mesh, const Matrix& world, Texture2D texture, bool twoSided = true);
    // twoSided = false: sólo la cara del frente (calcomanías espalda con espalda no se pisan).
    // Matas de pasto: una matriz por mata (con el tono en la fila de abajo, ver kGrassVS).
    void DrawGrass(const Matrix* transforms, int count, Texture2D marks, float terrainOrigin, float terrainSize);

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
        int fogDensity = -1, camPos = -1, exposure = -1, lightVP = -1, shadowTexel = -1;
    };
    void FindLocs(Shader& sh, Locs& l);
    void SetCommon(Shader sh, const Locs& l, const Camera3D& cam);

    Locs litLocs, terrainLocs, skyLocs, grassLocs;
    int glossLoc = -1, skyTimeLoc = -1, grassTimeLoc = -1, grassOriginLoc = -1, grassSizeLoc = -1, pavedTintLoc = -1;
    RenderTexture2D shadowMap{}, sceneRT{};
    Shader postShader{};
    int postResLoc = -1, postFocusLoc = -1, postBlurLoc = -1, postAberrationLoc = -1, postVignetteLoc = -1, postTimeLoc = -1, postGrainLoc = -1;
    Matrix lightVP{};
    bool shadowPass = false;
    Vector3 shadowFocus{};
    Vector3 bodySteerHead[BikeStyle::Count]{}, bodyForkUp[BikeStyle::Count]{};
    bool bodyBuilt[BikeStyle::Count] = {};
    double savedNear = 0.08, savedFar = 900.0;
    static constexpr int kShadowResolution = 2048;
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
