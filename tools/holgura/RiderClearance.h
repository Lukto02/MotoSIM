#pragma once
// Pruebas: cuánto se mete el piloto (el modelo glTF, skineado) en la moto. Dos medidas:
//  A) vértices del piloto adentro de cada pieza cerrada de la moto (cada primitiva soldada de sus mallas);
//  B) puntos de la superficie de la moto (cada ~1.2 cm) adentro de cada pieza cerrada del piloto.
// La profundidad es la distancia a la superficie de lo que se atraviesa. Se reporta por zona del cuerpo.
#include <Jolt/Jolt.h>

#include "Bike.h"
#include "RiderModel.h"
#include "Render.h"

#include <algorithm>
#include <vector>

struct RiderClearance {
    // Barrido de poses (copia el estado de la moto y lo varía): imprime tablas por grupo y un CSV.
    static void Sweep(const Bike& bike, RiderModel& model, Renderer& r, const char* csvPath);
    // Durante una prueba: mide la pose actual cada 0.05 s de simulación; al salir imprime lo peor.
    static void Frame(const Bike& bike, RiderModel& model, Renderer& r, float t);

    // Lo que hace falta del modelo (RiderModel lo declara amigo).
    struct View {
        const Model& model;
        const std::vector<Vector3>& bindVerts;
        const std::vector<Vector3>& bindPos;
        const std::vector<Vector3>& pos;
        const std::vector<Quaternion>& delta;
        const std::vector<int>& skinBone;
        const std::vector<float>& skinWeight;
        int realBones;
        float& throttleS;
    };
    static View Of(RiderModel& m) { return {m.model, m.bindVerts, m.bindPos, m.pos, m.delta, m.skinBone, m.skinWeight, m.realBones, m.throttleS}; }
    // Cuánto le falta a la mano más lejos para agarrar su puño (m): del punto de la mano que va en el centro
    // del puño al centro del puño.
    static float HandGap(const RiderModel& m, const RiderPose& p)
    {
        float worst = 0.0f;
        for (int i = 0; i < 2; ++i) {
            if (!m.grip[i].valid) continue;
            const int h = m.hand[i];
            const Vector3 c = Vector3Add(m.pos[h], Vector3RotateByQuaternion(Vector3Subtract(m.grip[i].channel, m.bindPos[h]), m.delta[h]));
            worst = std::max(worst, Vector3Distance(c, {p.grip[i].GetX(), p.grip[i].GetY(), p.grip[i].GetZ()}));
        }
        return worst;
    }
};
