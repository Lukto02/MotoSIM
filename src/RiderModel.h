#pragma once
// Piloto con un modelo glTF con esqueleto (nombres de huesos estilo Mixamo). La pose se arma por
// código: sobre la moto con cinemática inversa hacia manubrio y estriberas, y en una caída cada
// hueso sigue a su parte del ragdoll. El skinning se hace en la CPU.
#include <Jolt/Jolt.h>

#include "Bike.h"
#include "Rider.h"

#include "Render.h"

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
    enum Dof { HipsX, HipsY, HipsZ, TorsoPitch, TorsoRoll, HeadYaw, HeadPitch, HeadRoll, Lean, OutL, OutR, DofCount };

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

private:
    friend struct RiderClearance;            // pruebas: cuánto se mete el piloto en la moto (tools/holgura, no va en el juego)
    int Bone(const char* name) const;
    // Pose completa con el torso inclinado `extraPitch` más y la cadera `hipShift` m más adelante;
    // devuelve cuánto se estira el brazo más exigido (distancia hombro-manubrio / largo del brazo).
    float SolveBody(const RiderPose& pose, float extraPitch, float hipShift);

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
    float lastReach = 0.0f, lastExtraPitch = 0.0f, lastHipShift = 0.0f;   // telemetría de la IK

    int hips = -1, spine = -1, neck = -1, head = -1;
    int spineChain[3] = {-1, -1, -1};
    int clavicle[2] = {-1, -1};                    // LeftShoulder / RightShoulder (opcionales)
    int arm[2] = {-1, -1}, foreArm[2] = {-1, -1}, hand[2] = {-1, -1};
    int upLeg[2] = {-1, -1}, leg[2] = {-1, -1}, foot[2] = {-1, -1}, toe[2] = {-1, -1};
};
