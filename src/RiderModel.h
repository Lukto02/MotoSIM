#pragma once
// Piloto con un modelo glTF con esqueleto (nombres de huesos estilo Mixamo). La pose se arma por
// código: sobre la moto con cinemática inversa hacia manubrio y estriberas, y en una caída cada
// hueso sigue a su parte del ragdoll. El skinning se hace en la CPU.
#include <Jolt/Jolt.h>

#include "Bike.h"
#include "Rider.h"

#include "Render.h"

#include <map>
#include <string>
#include <vector>

// Cuerpo con masa y músculos (sólo dibujo: la física no lo ve). Toma la pose de RiderPoseLocal (a dónde
// quiere ir el piloto) y la sigue con resortes amortiguados, uno por grado de libertad, empujados por lo que
// siente el cuerpo: la aceleración y el giro de la moto medidos de su transformación cuadro a cuadro (igual
// para la moto propia y la de otro jugador). Ver docs/PILOTO.md, "Cuerpo con masa".
//   - cadera: se hunde al aterrizar y se va atrás / adelante al acelerar / frenar (inercia);
//   - torso: cabeceo y costado, con inercia de giro (se queda atrás cuando la moto cabecea o se tumba), hacia
//     adentro en la curva y, en el aire, buscando la vertical;
//   - cabeza: mira hacia donde va (la velocidad y la salida de la curva), casi nivelada con el horizonte;
//   - piernas: la pata afuera suave, y también cuando la cola sale de costado (derrape).
// Cada reacción suma al objetivo (o a la fuerza) de los grados de libertad: un truco (nac-nac, superman...)
// sería otra contribución con su peso, sin cortes, porque todo pasa por los mismos resortes.
class RiderMotion {
public:
    struct Input {
        JPH::Vec3 pos = JPH::Vec3::sZero();    // moto en el mundo, como se dibuja (interpolada)
        JPH::Quat rot = JPH::Quat::sIdentity();
        float dt = 0.0f;                       // s desde el cuadro anterior
        float ground = 1.0f;                   // 0..1: alguna rueda apoyada
        float smooth = 1.0f;                   // cuánto más filtrar lo medido (la moto de otro jugador trae las correcciones de la red)
    };
    // Resorte de un grado de libertad: x'' = w²(objetivo - x) - 2·zeta·w·x' + fuerza.
    struct Spring {
        float x = 0.0f, v = 0.0f;
        void Step(float target, float force, float w, float zeta, float dt);
        void Set(float value) { x = value; v = 0.0f; }
    };
    enum Dof { HipsX, HipsY, HipsZ, TorsoPitch, TorsoRoll, HeadYaw, HeadPitch, HeadRoll, Lean, OutL, OutR, SitUp, DofCount };

    void Reset() { valid = false; }
    // Avanza los resortes y devuelve la pose con las reacciones del cuerpo (espacio de la moto).
    RiderPose Apply(const RiderPose& base, const Bike& bike, const Input& in);

    // Cabeza (espacio de la moto) del último Apply: hacia dónde mira la cara y dónde queda arriba.
    JPH::Vec3 look = JPH::Vec3(0.0f, -0.2f, 1.0f).Normalized(), lookUp = JPH::Vec3::sAxisY();
    // Telemetría (MOTOSIM_PILOTO_LOG): lo medido en el último Apply.
    JPH::Vec3 accLocal = JPH::Vec3::sZero(), angAccLocal = JPH::Vec3::sZero();
    float drift = 0.0f;                        // rad: cola afuera (ángulo entre la moto y la velocidad)
    Spring dof[DofCount];

private:
    bool valid = false;
    JPH::Vec3 prevAnchor = JPH::Vec3::sZero(), velF = JPH::Vec3::sZero(), accF = JPH::Vec3::sZero();
    JPH::Quat prevRot = JPH::Quat::sIdentity();
    JPH::Vec3 angVelF = JPH::Vec3::sZero(), angAccF = JPH::Vec3::sZero();
    float groundS = 1.0f;
    float vertSlow = 0.0f;                     // aceleración vertical lenta (la que los músculos ya sostienen)
    int warm = 0;                              // cuadros desde que apareció sin medir aceleraciones
    Spring hipsS[3], shouldersS[3];            // la pose base, suavizada (sin saltos aunque ella salte)
};

class RiderModel {
public:
    // Prueba las rutas en orden. Si no encuentra el archivo o no tiene los huesos esperados, queda
    // sin cargar (y el juego usa el piloto generado).
    bool Load(const std::vector<std::string>& paths);
    void Unload();
    bool Loaded() const { return loaded; }

    // Pose sobre la moto, en espacio local de la moto (se dibuja con la matriz de la moto).
    void PoseOnBike(const RiderPose& pose);
    // Lo mismo con el cuerpo con masa (RiderMotion): la pose de la moto seguida con inercia. Una vez por
    // cuadro, con la moto como se dibuja. time: s de simulación (en pausa no avanza: el cuerpo se queda
    // quieto; si vuelve atrás, arranca de nuevo). ground: 0..1, alguna rueda en el piso. smooth: ver RiderMotion::Input
    // (otro jugador: 2.5).
    void PoseOnBike(const RiderPose& pose, const Bike& bike, JPH::Vec3 bikePos, JPH::Quat bikeRot, double time, float ground,
                    float smooth = 1.0f);
    RiderPose JointPose(const RiderPose& pose) { return pose; } // compatibilidad con Game/Multiplayer 0.3.2
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

    // Colores del traje (menú Colores; los de otro jugador, por red). Con on = false, los de la textura. main reemplaza al
    // rojo y second al azul marino, conservando el sombreado y los detalles pintados (ver BuildSuitMask y docs/PILOTO.md).
    struct SuitPaint {
        bool on = false;
        Color main{}, second{};
    };
    SuitPaint paint;
    Color SuitBase(int i) const { return suitRef[i & 1]; }   // los de fábrica (el color medio de cada zona de la textura)

private:
    // Máscara de los colores del traje: una por archivo de modelo, compartida por las copias (los demás jugadores).
    struct SharedMask {
        Texture2D mask{};
        Color ref[2]{};
        int users = 0;
    };
    static std::map<std::string, SharedMask>& SuitMasks();
    void BuildSuitMask();
    std::string modelPath;
    Texture2D suitMask{};
    Color suitRef[2] = {{205, 55, 45, 255}, {35, 48, 88, 255}};
    friend struct RiderClearance;            // pruebas: cuánto se mete el piloto en la moto (tools/holgura, no va en el juego)
    int Bone(const char* name) const;

    // Para no meterse en la moto (ver docs/PILOTO.md, "El piloto se metía en la moto"; volvió en la v0.3.10). La forma
    // del chasis (espacio de la moto): de cada lado, la |x| más afuera de su superficie en cada celda (y, z), y arriba la
    // y más alta en cada celda (x, z). Algo está adentro si está más cerca del centro que el costado y más abajo que el
    // techo. Se arma en Draw con el chasis del estilo que se acaba de dibujar (sin estriberas, pedales ni palancas: ahí
    // apoyan las botas) y la usan las poses siguientes.
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
    // Lo que busca PoseOnBike para que las manos lleguen y no meterse en la moto.
    struct Fit {
        float pitch = 0.0f;                  // rad más de torso adelante (las manos llegan)
        float shift = 0.0f;                  // m que se adelanta la cadera (si el torso no alcanzó)
        float sideIn = 0.0f;                 // 0..1: cuánto vuelve el cuerpo corrido de costado (whip) hacia el medio para llegar
        float backIn = 0.0f;                 // 0..1: tirado atrás, la cadera menos atrás y el torso más adelante (kBackHipsForward, kBackLean)
        float lift = 0.0f;                   // m que sube la cadera: sentado arriba del asiento, no adentro
        float center = 0.0f;                 // 0..1: cuánto vuelve la cadera al medio del asiento (cuerpo de costado)
        float open[2] = {0.0f, 0.0f};        // rad que se abre cada rodilla (gira alrededor de cadera-tobillo)
        float ankleOut[2] = {0.0f, 0.0f};    // m que se corre cada tobillo hacia afuera sobre la estribera
        float elbowOpen[2] = {0.0f, 0.0f};   // rad que se abre cada codo (gira alrededor de hombro-muñeca) para no meterse en la moto
    };
    // Vértices de prueba (uno por posición: la malla repite los de las costuras), por grupo.
    // ProbeSeat: la cola y la mitad de arriba de los muslos; ProbeDrape: la cola y casi todo el muslo (el que
    // cruza el asiento con el cuerpo corrido de costado). ProbeChest: torso, casco y brazos; ProbeForearm:
    // antebrazos (sólo contra el manubrio: cerca de los puños pasan por adentro del carenado). ProbeArm: cada brazo y la
    // mitad del antebrazo del lado del codo (contra la moto: el codo se abre lo justo, Fit::elbowOpen).
    enum { ProbeSeat, ProbeDrape, ProbeLeg, ProbeFoot = ProbeLeg + 2, ProbeChest = ProbeFoot + 2, ProbeForearm, ProbeArm, ProbeCount = ProbeArm + 2 };
    std::vector<int> probe[ProbeCount];
    void BuildProbes();
    Vector3 SkinVertex(int v) const;         // con la pose actual (como Skin)
    float SeatDepth(int group) const;        // cuánto se hunden en el asiento (hacia abajo) los del grupo
    float SideDepth(int group) const;        // cuánto se meten de costado los del grupo (las piernas, también en el manubrio)
    float ArmDepth(int side) const;          // cuánto se mete un brazo en la moto (tanque, carenado)
    float ChestDepth(const RiderPose& pose) const; // pecho, casco y brazos contra el manubrio y la moto
    Fit lastFit;                             // lo que se ajustó en la última pose (pruebas)
    Vector3 barA{}, barB{};                  // puños de la pose que se está armando (el manubrio, para las rodillas)
    float handMiss = 0.0f;                   // m que le faltan a la mano más lejos de su puño (telemetría)
    // El whip del modelo: la cadera y los hombros menos al costado que en la pose (ver RiderModel.cpp).
    static RiderPose WhipPose(const RiderPose& pose, const Bike& bike);
    // La de carreras (colgado en la curva), sólo el modelo: más cuerpo adentro y más abajo, y deja en hang / hangSide
    // cuánto está colgado y para qué lado (los polos de la rodilla y los codos en SolveBody). Ver docs/PILOTO.md,
    // "La Carrera 1000".
    RiderPose RacePose(const RiderPose& pose, const Bike& bike);
    float hang = 0.0f;                             // 0..1 colgado (RacePose)
    float hangSide = 1.0f;                         // lado de adentro de la curva (+1 = +X, izquierda)
    float tuckAmt = 0.0f;                          // 0..1 agachado sobre el tanque (RacePose)

    // Pose completa con los ajustes de `fit`; devuelve cuánto se estira el brazo más exigido (distancia hombro-manubrio
    // / largo del brazo).
    float SolveBody(const RiderPose& pose, const Fit& fit);

    // Dedos: el modelo trae la mano como un solo hueso. Se arman huesos virtuales (hijos de la mano:
    // tres falanges para los cuatro dedos juntos y dos para el pulgar) y se reparten entre ellos los
    // vértices de los dedos, según su posición en la mano.
    struct Grip {
        bool valid = false;
        Vector3 across{}, along{}, palm{};   // ejes de la mano en reposo: a lo ancho (hacia el pulgar), a lo largo, normal de la palma
        Vector3 channel{};                   // punto de la mano (en reposo) que va en el centro del puño del manubrio
    };
    void BuildGrip(int side);
    Quaternion GripRotation(int side, Vector3 barInward, Vector3 forearmDir) const;
    void CurlFingers();                      // falanges según su padre y gripAmount (después del ragdoll)
    Grip grip[2];
    float gripAmount = 1.0f;                 // 1 agarrando el manubrio, 0 mano abierta
    float throttleS = 0.0f;                  // acelerador suavizado (gira la mano derecha sobre el puño)
    int realBones = 0;                       // huesos del modelo; los virtuales van después
    std::vector<float> curlAngle;            // flexión de cada falange virtual (0 en los huesos del modelo)
    std::vector<Vector3> curlAxis;           // eje de flexión en reposo
    std::vector<int> curlSide;               // mano de cada falange virtual (0 izquierda, 1 derecha)
    float handOpen[2] = {0.0f, 0.0f};        // truco: la mano soltó el manubrio (0..1, RiderPose::handFree)
    // Cuánto cierra la falange b: agarrando (gripAmount) y menos con la mano suelta (relajada, apenas doblada).
    float FingerGrip(int b) const { return gripAmount * (1.0f - 0.8f * handOpen[curlSide[b] & 1]); }
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
    // Al caer, el modelo arranca como se veía (con las reacciones del cuerpo) y pasa en kRagdollBlend s a
    // la pose con que se armó el ragdoll: sin salto en el primer cuadro.
    std::vector<Vector3> relPosShown;
    std::vector<Quaternion> relDeltaShown;
    float ragdollBlend = 1.0f;                     // 0 recién caído .. 1 pegado del todo a las partes

    RiderMotion motion;
    bool motionShown = false;                      // el último cuadro sobre la moto salió de RiderMotion
    double motionTime = -1.0;                      // time del último PoseOnBike con RiderMotion
    bool headFree = false;                         // SolveBody: la cabeza mira hacia motion.look
    float elbowsIn = 0.0f;                         // 1: codos abajo, cerca del tanque (la de carreras); 0: afuera (ataque)
    float lastReach = 0.0f;                        // telemetría de la IK (el resto, en lastFit)
    // Detector de saltos (MOTOSIM_SALTOS): la pose del cuadro anterior y lo que la armó.
    void CheckJumps(double time, float dt, const RiderPose& base, const RiderPose& shown, const Bike& bike);
    // Con el cuerpo con masa, el ajuste que se dibuja sigue al buscado con un resorte (no salta de un cuadro al otro).
    Fit fitShown, fitVel;
    bool fitValid = false;
    std::vector<Vector3> jumpPos;
    std::vector<Quaternion> jumpRot;
    std::vector<Vector3> jumpVel, jumpAngVel;
    bool jumpHasVel = false;
    float jumpSteer = 0.0f;
    Fit jumpFit;
    double jumpTime = -1.0;

    int hips = -1, spine = -1, neck = -1, head = -1;
    int spineChain[3] = {-1, -1, -1};
    int clavicle[2] = {-1, -1};                    // LeftShoulder / RightShoulder (opcionales)
    int arm[2] = {-1, -1}, foreArm[2] = {-1, -1}, hand[2] = {-1, -1};
    int upLeg[2] = {-1, -1}, leg[2] = {-1, -1}, foot[2] = {-1, -1}, toe[2] = {-1, -1};
};
