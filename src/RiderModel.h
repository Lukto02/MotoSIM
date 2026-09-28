#pragma once
// Piloto con un modelo glTF con esqueleto (nombres de huesos estilo Mixamo, con o sin dedos). La pose
// se arma por código: sobre la moto con cinemática inversa hacia manubrio y estriberas, y en una caída
// cada hueso sigue a su parte del ragdoll. El skinning se hace en la CPU.
#include <Jolt/Jolt.h>

#include "Bike.h"
#include "Rider.h"

#include "Render.h"

#include <string>
#include <vector>

class RiderModel {
public:
    // Prueba las rutas en orden. Si no encuentra el archivo o no tiene los huesos esperados, queda
    // sin cargar (y el juego usa el piloto generado).
    bool Load(const std::vector<std::string>& paths);
    void Unload();
    bool Loaded() const { return loaded; }

    // Pose sobre la moto, en espacio local de la moto (se dibuja con la matriz de la moto).
    void PoseOnBike(const RiderPose& pose);
    // La pose con las articulaciones del modelo (cadera, rodillas, tobillos, hombros, codos, cuello y
    // cabeza donde las tiene la malla): el ragdoll se arma con esta, así cada pivote queda en la
    // articulación del modelo y al doblarse la malla no se estira ni se enrosca.
    RiderPose JointPose(const RiderPose& pose);
    // Al salir despedido: cada hueso queda pegado a su parte del ragdoll (llamar justo después de Spawn).
    void BindToRagdoll(const RiderPose& pose, const RiderRagdoll& ragdoll);
    // Pose del ragdoll, en espacio de mundo (se dibuja con la identidad).
    void PoseFromRagdoll(const RiderRagdoll& ragdoll, float alpha);
    // Lo mismo con las partes del cuerpo dadas (RiderPart::Count): el ragdoll de otro jugador llega
    // por red. partLocal: dónde estaba cada parte en la moto al caer (RiderPartLocals).
    void BindToParts(const RiderPose& pose, const JPH::Mat44* partLocal);
    void PoseFromParts(const JPH::Mat44* partWorld);

    void Skin();                                   // aplica la pose a la malla y la sube a la GPU
    void Draw(Renderer& r, const Matrix& world);

private:
    friend struct RiderClearance;            // pruebas: cuánto se mete el piloto en la moto (no va en el juego)
    int Bone(const char* name) const;

    // Para no meterse en la moto (ver docs/PILOTO.md, "El piloto se metía en la moto"). La forma del chasis
    // (espacio de la moto): de cada lado, la |x| más afuera de su superficie en cada celda (y, z), y arriba la
    // y más alta en cada celda (x, z). Algo está adentro si está más cerca del centro que el costado y más
    // abajo que el techo. Se arma en Draw con el chasis del estilo que se acaba de dibujar (sin estriberas,
    // pedales ni palancas: ahí apoyan las botas) y la usan las poses siguientes.
    struct BikeShape {
        int style = -1;
        const float* key = nullptr;          // vértices del chasis con que se armó (se rearma si cambian)
        float x0 = 0.0f, y0 = 0.0f, z0 = 0.0f;
        int nx = 0, ny = 0, nz = 0;
        std::vector<float> side[2];          // [0] +X, [1] -X: |x| (0 = nada)
        std::vector<float> top;              // y (kNothing = nada)
        // p adentro: cuánto le falta para salir de costado (dl) y hacia arriba (dv).
        bool Inside(Vector3 p, float& dl, float& dv) const;
    };
    BikeShape shape;
    void BuildShape(const Renderer& r);
    // Lo que busca PoseOnBike para no meterse en la moto.
    struct Fit {
        float lift = 0.0f;                   // m que sube la cadera: sentado arriba del asiento, no adentro
        float center = 0.0f;                 // 0..1: cuánto vuelve la cadera al medio del asiento (cuerpo de costado)
        float open[2] = {0.0f, 0.0f};        // rad que se abre cada rodilla (gira alrededor de cadera-tobillo)
        float ankleOut[2] = {0.0f, 0.0f};    // m que se corre cada tobillo hacia afuera sobre la estribera
    };
    // Vértices de prueba (uno por posición: la malla repite los de las costuras), por grupo.
    // ProbeSeat: la cola y la mitad de arriba de los muslos; ProbeDrape: la cola y casi todo el muslo (el que
    // cruza el asiento con el cuerpo corrido de costado).
    // ProbeChest: torso, casco y brazos; ProbeForearm: antebrazos (sólo contra el manubrio: cerca de los
    // puños pasan por adentro del carenado).
    enum { ProbeSeat, ProbeDrape, ProbeLeg, ProbeFoot = ProbeLeg + 2, ProbeChest = ProbeFoot + 2, ProbeForearm, ProbeCount };
    std::vector<int> probe[ProbeCount];
    Vector3 SkinVertex(int v) const;         // con la pose actual (como Skin)
    float SeatDepth(int group) const;        // cuánto se hunden en el asiento (hacia abajo) los del grupo
    float SideDepth(int group) const;        // cuánto se meten de costado los del grupo (las piernas, también en el manubrio)
    float ChestDepth(const RiderPose& pose) const; // pecho, casco y brazos contra el manubrio y la moto
    float lastReach = 0.0f;                  // pruebas: el brazo más estirado de la última pose...
    Fit lastFit;                             // ...y lo que se ajustó
    Vector3 barA{}, barB{};                  // puños de la pose que se está armando (el manubrio, para las rodillas)

    // Pose completa con el torso inclinado `extraPitch` más, la cadera `hipShift` m más adelante y los
    // ajustes de `fit`; devuelve cuánto se estira el brazo más exigido (distancia hombro-manubrio / largo
    // del brazo).
    float SolveBody(const RiderPose& pose, float extraPitch, float hipShift, const Fit& fit);

    // Dedos. Si el modelo los trae (huesos estilo Mixamo: LeftHandIndex1..4, pulgar LeftHandThumb1..4),
    // se usan esos huesos con sus pesos (BuildGripFingers). Si trae la mano como un solo hueso, se arman
    // huesos virtuales (hijos de la mano: tres falanges para los cuatro dedos juntos y dos para el
    // pulgar) y se reparten entre ellos los vértices de los dedos, según su posición en la mano (BuildGrip).
    struct Grip {
        bool valid = false;
        Vector3 across{}, along{}, palm{};   // ejes de la mano en reposo: a lo ancho (hacia el pulgar), a lo largo, normal de la palma
        Vector3 channel{};                   // punto de la mano (en reposo) que va en el centro del puño del manubrio
    };
    void BuildGrip(int side);
    void BuildGripFingers(int side);
    Quaternion GripRotation(int side, Vector3 barInward, Vector3 forearmDir) const;
    void CurlFingers();                      // falanges según su padre y gripAmount (después del ragdoll)
    Grip grip[2];
    float gripAmount = 1.0f;                 // 1 agarrando el manubrio, 0 mano abierta
    float throttleS = 0.0f;                  // acelerador suavizado (gira la mano derecha sobre el puño)
    int realBones = 0;                       // huesos del modelo; los virtuales van después
    int fingerBone[2][5][4] = {};            // dedos del modelo [lado][pulgar, índice, medio, anular, meñique][falange 1..3, punta]
    bool modelFingers[2] = {false, false};
    std::vector<int> fingerBones;            // las falanges (del modelo o virtuales), de padres a hijos
    std::vector<float> curlAngle;            // giro de cada falange para agarrar el puño (0 en los demás huesos)
    std::vector<Vector3> curlAxis;           // eje de ese giro, en reposo
    std::vector<int> skinBone;               // 4 huesos y pesos por vértice (los del modelo, con los dedos repartidos)
    std::vector<float> skinWeight;

    Model model{};
    bool loaded = false;
    Texture2D texture{};
    std::vector<Vector3> bindVerts, bindNormals;   // malla en la pose de reposo (m)
    std::vector<Vector3> bindPos;                  // posición de cada hueso en reposo
    std::vector<int> parent, order;                // order: de padres a hijos
    std::vector<Vector3> pos;                      // pose actual: posición de cada hueso...
    std::vector<Quaternion> delta;                 // ...y su giro respecto del reposo
    std::vector<int> partOf;                       // parte del ragdoll que arrastra a cada hueso
    std::vector<Vector3> relPos;                   // hueso respecto de su parte (ragdoll)
    std::vector<Quaternion> relDelta;

    int hips = -1, spine = -1, neck = -1, head = -1;
    int spineChain[3] = {-1, -1, -1};
    int arm[2] = {-1, -1}, foreArm[2] = {-1, -1}, hand[2] = {-1, -1};
    int upLeg[2] = {-1, -1}, leg[2] = {-1, -1}, foot[2] = {-1, -1}, toe[2] = {-1, -1};
};
