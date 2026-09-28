#pragma once
// Definición de un estilo de moto: generadores de sus mallas (BikeMeshes.cpp) y la pose del piloto.
// Jolt antes que raylib (lo trae Render.h).
#include "BikeStyles.h"
#include "Render.h"

struct BikeStyleDef {
    const char* name;
    // Mallas (espacio de la moto, ver BikeMesh en Render.h). Las piezas iguales para todos los
    // esquemas de colores, las pintadas de un esquema (out[0] FrontFender, out[1] ForkUpper) y el
    // chasis de un esquema para una pipa de dirección dada (depende de la geometría del tuning).
    void (*genParts)(Mesh* out);
    void (*genLivery)(const BikeLivery& livery, Mesh* out);
    Mesh (*genBody)(Vector3 steerHead, Vector3 forkUp, const BikeLivery& livery);
    const BikeLivery& (*livery)(int i);

    // Piloto (espacio de la moto: +Y arriba, +Z adelante, +X izquierda; origen en el centro de masa).
    JPH::Vec3 barOffset;      // centro del manubrio respecto de la pipa de dirección
    float gripX;              // medio ancho entre puños
    JPH::Vec3 peg;            // estribera izquierda (la derecha es espejo en X)
    JPH::Vec3 hips;           // cadera con el piloto neutro (sentado)
    JPH::Vec3 shoulders;      // hombros con el piloto neutro
    JPH::Vec3 knee;           // rodilla izquierda con el piloto neutro
    float leanHipsZ;          // cuánto va la cadera adelante / atrás con el piloto adelante / atrás
    float leanShouldersZ;     // ídem hombros
    float stand;              // 0 sentado .. 1 de pie en los pedales siempre (trial)
    float tuck;               // 0 .. 1 agachado sobre el tanque a alta velocidad (carreras)

    // Escape: boca (espacio de la moto) y hacia dónde sale la llama; openExhaust = petardea mucho.
    JPH::Vec3 exhaustTip, exhaustDir;
    bool openExhaust;

    // Colgado en las curvas (carreras): 0 = nada; 1 = inclinada fuerte, la cadera sale del asiento hacia
    // adentro y la rodilla de adentro baja hasta el piso. Los estilos que no lo dan quedan en 0.
    float hangOff = 0.0f;
};

const BikeStyleDef& GetBikeStyle(int id);   // id fuera de rango = motocross

// Cada estilo nuevo define su tabla en su sección de BikeMeshes.cpp.
const BikeStyleDef& RaceStyleDef();
const BikeStyleDef& TrialStyleDef();
