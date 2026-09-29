#pragma once
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include "Engine.h"
#include "Suspension.h"
#include "Tire.h"

namespace JPH { class Body; }
class PhysicsWorld;
class Terrain;
class Tuning;
struct Renderer;

struct BikeInput {
    float throttle = 0.0f;     // 0..1
    float frontBrake = 0.0f;   // 0..1
    float rearBrake = 0.0f;    // 0..1
    float steer = 0.0f;        // -1..1 (+ = derecha)
    float lean = 0.0f;         // -1..1 (+ = piloto adelante)
    float side = 0.0f;         // -1..1: el cuerpo del piloto a la izquierda / derecha (+ = derecha)
    bool shiftUp = false;
    bool shiftDown = false;
};

// Todos los números ajustables de la moto. Se registran en Tuning con el mismo nombre que
// aparece en tuning.ini. Espacio local de la moto: origen = centro de masa, +Z adelante, +Y arriba.
struct BikeParams {
    int visualStyle = 0;             // 0 = motocross, 1 = trilheira (lo pone el mapa; no está en tuning.ini)

    // Masa (moto 105 kg + piloto 75 kg en un único rigid body)
    float mass = 180.0f;
    float inertiaPitch = 65.0f, inertiaYaw = 50.0f, inertiaRoll = 26.0f;   // piloto de pie incluido

    // Centro de masa efectivo: offsets de debug + desplazamiento del piloto
    float comHeight = 0.0f;
    float comForward = 0.0f;
    float riderShift = 0.25f;        // m hacia adelante/atrás con el stick a fondo
    float riderSpeed = 6.0f;         // 1/s, qué tan rápido se mueve el piloto
    float riderPitchDamping = 150.0f;// Nm/(rad/s): brazos y piernas absorben el cabeceo en el suelo
    // Cuerpo a los costados (flechas izquierda / derecha): el piloto (75 de los 180 kg) se corre ~25 cm
    // y lleva el centro de masa de todo ~10 cm de costado.
    float riderSideShift = 0.10f;    // m que se corre el centro de masa con el cuerpo a fondo de un lado
    float riderSideSteer = 0.35f;    // cuánto dobla la moto con el cuerpo solo (fracción de la curva máxima)

    // Geometría: posición del eje con la suspensión TOTALMENTE comprimida + dirección de extensión
    float frontMountY = -0.30f, frontMountZ = 0.74f, rakeDeg = 27.0f;
    float rearMountY = -0.29f, rearMountZ = -0.62f, rearAxisDeg = 8.0f;

    // {recorrido, resorte, amortiguación, rebote, cono de fin de carrera, progresividad,
    //  tope, amortiguación del tope, velocidad máx. que ve el amortiguador}
    SuspensionParams front{0.30f, 6800.0f, 1000.0f, 3.0f, 5.0f, 1.2f, 150000.0f, 6000.0f, 12.0f};
    SuspensionParams rear{0.32f, 7300.0f, 1100.0f, 3.0f, 5.0f, 1.2f, 150000.0f, 6000.0f, 12.0f};
    TireParams frontTire{0.35f, 0.60f, 1.00f, 1.15f, 0.06f, 400000.0f, 3500.0f, 11.0f};
    TireParams rearTire{0.33f, 0.90f, 1.00f, 1.10f, 0.06f, 400000.0f, 3500.0f, 14.0f};

    // Grip: fracción del patinaje que el neumático corrige por paso (tipo constraint) y rigidez
    // de deriva lateral (N de fuerza por N de carga por radián)
    float longStiffness = 0.9f;
    float latStiffness = 0.8f;
    float corneringStiffness = 14.0f;
    float minSlipSpeed = 3.0f;       // m/s, evita slip ratio infinito parado
    float tireRollCoupling = 0.5f;   // 1 = torque de rolido real de las fuerzas del neumático

    // Frenos y resistencias
    float frontBrakeTorque = 950.0f; // Nm
    float rearBrakeTorque = 520.0f;
    float absSlip = 0.25f;           // 0 = sin ABS en la delantera
    float brakeStandUp = 0.6f;       // cuánto se endereza la moto al frenar adelante inclinada
    float stoppieYawDamping = 1200.0f;// Nm/(rad/s) de guiñada frenada con sólo la delantera apoyada
    float rollingResistance = 0.03f;
    float dragArea = 0.55f;          // Cd * A (m^2)

    // Dirección y balance
    float maxSteerDeg = 42.0f;       // tope de dirección de una 450 (KX450: 42°)
    float maxSteerHighSpeedDeg = 10.0f;
    float steerRate = 4.0f;          // rad/s
    float casterAlign = 2.0f;        // contravolante automático al cruzarse la trasera (0 = nada)
    float casterDeadzoneDeg = 3.0f;  // deriva trasera normal que no dispara el contravolante
    // Derrape (freno trasero trabado, como se "cuadra" una horquilla en motocross): la cubierta que
    // patina o está trabada roza en la dirección en que se desliza (no sostiene la cola), el piloto
    // aguanta el manubrio hacia la curva y lleva la moto más derecha.
    float slideFriction = 0.72f;     // roce cinético / grip pico con la cubierta deslizando
    float slideHoldBars = 0.85f;     // cuánto menos contravolante automático con el freno trasero a fondo
    float slideSpeedMax = 13.0f;     // m/s: de ahí para arriba el contravolante vuelve entero
    float slideUpright = 0.55f;      // cuánto menos se inclina la moto mientras la trasera derrapa
    float slideAngleMin = 20.0f;     // grados de cola afuera con la dirección apenas tocada...
    float slideAngleMax = 65.0f;     // ...y con la dirección a fondo (el piloto afloja el freno al llegar)
    float slideSteerIn = 6.0f;       // grados máximos que el piloto apunta la delantera hacia la curva (respecto de su camino)
    float slideWander = 0.12f;       // roce de costado irregular de una cubierta que patina (fracción del roce): la cola baila
    float minTurnRadius = 1.8f;      // m, a paso de hombre con la dirección a fondo
    float maxLateralAccel = 9.5f;    // m/s^2 que el input pide como máximo (en plano)
    float bankTurnGain = 1.0f;       // cuánto aprovechan los peraltes para doblar más fuerte (0 = como en plano)
    float maxLeanDeg = 55.0f;
    float leanSteerSpeedLow = 4.0f;  // debajo: se dobla con manubrio
    float leanSteerSpeedHigh = 12.0f;// encima: se dobla inclinando
    // Pata afuera: en las curvas lentas el piloto se sienta adelante, estira la pierna de adentro
    // hacia la rueda delantera y carga la estribera de afuera: más peso adelante y más mordida.
    float legOutSpeedMin = 1.0f;     // m/s: más despacio apoya el pie en vez de sacarlo
    float legOutSpeedMax = 14.0f;    // m/s: más rápido dobla parado, con los dos pies en las estriberas
    float legOutGrip = 0.1f;         // grip lateral extra con la pata afuera (fracción)
    float legOutComForward = 0.06f;  // m que se adelanta el centro de masa al sentarse adelante
    float balanceKLow = 4000.0f, balanceKHigh = 2200.0f;   // Nm/rad
    float balanceDLow = 900.0f, balanceDHigh = 750.0f;     // Nm/(rad/s)
    float balanceMaxTorque = 2600.0f;
    float balanceSpeed = 14.0f;      // m/s donde el assist llega a su valor de alta velocidad

    // Aire
    float airPitchTorque = 450.0f;   // Nm máximos para seguir la rotación pedida con el stick
    float airPitchRate = 2.5f;       // rad/s de rotación con el stick a fondo
    float airPitchGain = 500.0f;     // Nm por rad/s de diferencia entre rotación pedida y real
    float airNeutralDamping = 10.0f; // Nm/(rad/s) con el stick suelto (casi conserva el impulso)
    float airMaxPitchRate = 3.5f;    // rad/s, por encima se frena la rotación (acelerar/frenar la rueda)
    float airPitchRateDamping = 150.0f;
    float airRollAssist = 0.15f;     // fracción del balance que sigue activa en el aire
    float airReactionGain = 1.5f;    // exagera el efecto giroscópico de acelerar/frenar la rueda
    float airYawAlign = 350.0f;      // Nm/rad: alinea la moto con la dirección de vuelo
    float airYawDamping = 180.0f;    // Nm/(rad/s)
    // Whip / scrub: en el aire el piloto empuja la moto de costado con el cuerpo. Es una fuerza
    // interna: la moto gira para un lado lo que el cuerpo se va para el otro (se conserva el momento
    // angular) y vuelve cuando el cuerpo vuelve al centro. Lo que traiga de la cara del salto se conserva.
    float airBodyTiltDeg = 35.0f;    // cuánto se puede correr el cuerpo respecto de la moto
    float airBodyInertia = 22.0f;    // kg m^2 del cuerpo en rolido: cuánto empuja a la moto al moverse
    float airBodyRate = 8.0f;        // rad/s: qué tan rápido mueve el cuerpo el piloto
    float airWhipAssistFade = 1.0f;  // con el stick al costado deja de enderezar la moto (0..1)
    float airMaxRollRate = 1.8f;     // rad/s de rolido: más rápido, el piloto frena el giro con el cuerpo
    float airRollRateDamping = 150.0f;
    float airGyroGain = 0.5f;        // efecto giroscópico de las ruedas (1 = el físico; a la mitad se controla mejor)

    float wheelieStartDeg = 12.0f;   // limitador predictivo de wheelie (0/0 lo desactiva)
    float wheelieEndDeg = 27.0f;
    float wheelieLeanBackDeg = 18.0f;// grados extra de wheelie con el piloto tirado atrás
    float wheelieGearFade = 0.35f;   // en cada marcha el wheelie permitido (piloto neutro) se multiplica por esto
    float wheelieLeanTorque = 400.0f;// Nm: el piloto adelante/atrás baja/sube la rueda en un wheelie
    float wheelieAssistTorque = 500.0f; // Nm de "freno trasero automático" al pasarse
    // Grau (freestyle callejero brasileño: wheelies largos y lentos). grauAssist 0 = apagado (la de
    // motocross); lo prende la moto de la favela (mods/base/bikes/trilheira.ini). Tirado atrás, el
    // piloto sostiene la moto en el punto de equilibrio con el gas y el pie en el freno trasero, y
    // maneja la velocidad con la altura (ver BikePhysics.cpp).
    float grauAssist = 0.0f;         // 0..1: cuánto sostiene el piloto el grau
    float grauPitchNeutralDeg = 8.0f;// con el piloto neutro la rueda baja despacio hasta esta altura
    float grauSpeedMin = 2.2f;       // m/s del grau sin gas (tirado atrás y W suelto: a paso de hombre)
    float grauSpeedMax = 6.0f;       // m/s del grau con el gas a fondo
    float grauHoldHz = 4.0f;         // rad/s: qué tan rápido corrige la altura (más = más firme, puede oscilar)
    float grauBrake = 0.9f;          // freno trasero máximo que usa el piloto para no pasarse
    float grauClutchPop = 0.0f;      // pico de torque al soltar el embrague de golpe (tirado atrás, gas de golpe)
    float grauPopTime = 0.3f;        // s que dura ese pico
    float grauTurn = 0.0f;           // Nm por rad/s: en una rueda dobla acostando la moto (la cubierta trasera la gira)
    float grauAlign = 4.0f;          // 1/s: qué tan rápido la moto vuelve a apuntar hacia donde va en una rueda
    // Cualquier moto doblando en wheelie (sin grau_turn): lo mismo que el grau, sólo mientras dobla en una rueda.
    float wheelieTurn = 900.0f;      // Nm por rad/s (0 = nada alinea la moto en una rueda)
    float wheelieAlign = 4.0f;       // 1/s
    // Frenando fuerte y derecho: la moto vuelve a apuntar hacia donde va (0 = nada). Con la trasera casi
    // sin carga nada sostiene la cola; el piloto la mantiene derecha con el cuerpo y el manubrio.
    float brakeAlign = 0.0f;         // 1/s
    // Derrape lento con el freno trasero doblando (motocross): despacio no hay inercia para que la cola
    // salga sola; el piloto la empuja afuera con el cuerpo y el manubrio. Nm por rad que le falta a la cola.
    float slidePivot = 0.0f;
    float brakeAlignTorque = 1500.0f;// Nm por rad/s
    // Con cuánto freno de adelante empieza a actuar brake_align y con cuánto actúa entero.
    float brakeAlignFrom = 0.2f, brakeAlignFull = 0.6f;
    // Frenando derecho con brake_align, cuánto se suelta la delantera para que siga su camino (1 = del todo, 0 = nada:
    // sólo se alinea la moto).
    float brakeAlignFree = 1.0f;
    // Anti-levantamiento de la cola del ABS: con menos de rearLiftLoad N en la trasera afloja la delantera
    // (derecha, hasta rearLiftMitigation; inclinada, más).
    float rearLiftLoad = 300.0f;
    float rearLiftMitigation = 0.25f;
    // Frenando inclinada, cuánto se cancela el giro hacia afuera que hace la fuerza de frenado en el contacto
    // (que queda afuera del centro de masa): 1 = todo, como el piloto que sostiene la línea. 0 = no.
    float brakeYawComp = 0.0f;
    // Mientras la moto cambia mucho de inclinación (lo que pide el piloto contra la que tiene), afloja la
    // delantera hasta esta fracción: frenando fuerte, la trasera descargada no acompaña el cambio y la cola sale.
    float brakeTransitionRelease = 0.0f;
    float tcSlip = 0.35f;            // slip trasero tolerado por el control de tracción
    float crashAngleDeg = 72.0f;
    // Golpe fuerte contra algo fijo: el piloto sale despedido (m/s con que se acercaban). De frente o de
    // costado (un muro, una casa, una barrera) y cayendo de panza a plomo; 0 = nunca.
    float crashImpactSpeed = 7.0f;
    float crashImpactVertical = 11.0f;

    EngineParams engine;

    void Register(Tuning& t);
};

struct Wheel {
    // Config (espacio local, se recalcula de BikeParams cada paso)
    JPH::Vec3 mountLocal = JPH::Vec3::sZero();   // eje con la suspensión totalmente comprimida
    JPH::Vec3 axisLocal = -JPH::Vec3::sAxisY();  // dirección de extensión (unitaria)
    float radius = 0.33f;
    float travel = 0.3f;

    // Estado
    SuspensionState susp;
    float extension = 0.3f;          // posición real de la rueda en el eje (0 = comprimida)
    float axialVel = 0.0f;           // velocidad absoluta de la rueda a lo largo del eje
    float tireDelta = 0.0f;          // compresión del neumático (m)
    float groundExtension = 1e6f;    // extensión a la que tocaría el suelo (paso anterior)
    bool grounded = false;
    bool onTerrain = false;               // tipo del apoyo real; no inferirlo por altura en surcos
    bool onObject = false;           // apoya en un objeto (rampa, caja, escalón, casa), no en el terreno: duro como el
                                     // pavimento. Sólo informativo (sonido, efectos): la física usa su propia copia
    bool locked = false;
    JPH::Vec3 contactPoint = JPH::Vec3::sZero();
    JPH::Vec3 contactNormal = JPH::Vec3::sAxisY();
    float omega = 0.0f;              // rad/s, + = rodando hacia adelante
    float angle = 0.0f;              // para dibujar
    float longVel = 0.0f, latVel = 0.0f;
    float slipRatio = 0.0f, slipAngle = 0.0f;
    float normalForce = 0.0f, longForce = 0.0f, latForce = 0.0f, gripUsage = 0.0f;
    JPH::Vec3 suspForce = JPH::Vec3::sZero(), tireForce = JPH::Vec3::sZero();

    float Compression01() const;     // 0..1 para el HUD
    JPH::Vec3 AxleLocal() const;     // posición del eje según la compresión actual
};

// Pose del piloto en espacio local de la moto: la dibuja Bike::Draw y de ella sale el ragdoll en
// una caída. [0] = izquierda, [1] = derecha.
struct RiderPose {
    JPH::Vec3 hips, shoulders, head;
    JPH::Vec3 hip[2], knee[2], ankle[2], foot[2];   // foot = centro de la bota
    JPH::Vec3 shoulder[2], elbow[2], grip[2];
    float lean = 0.0f;                              // -1 tirado atrás .. +1 adelante
    float throttle = 0.0f;                          // puño del acelerador (gira la mano derecha)
    float legOut[2] = {0.0f, 0.0f};                 // pierna estirada afuera en la curva (0..1)
};

class Bike {
public:
    enum { FRONT = 0, REAR = 1 };
    static constexpr float kRiderMass = 75.0f;     // kg del piloto dentro de BikeParams::mass

    void Create(PhysicsWorld& world, BikeParams& params, JPH::Vec3 position, float yaw);
    // Moto de otro jugador: sin cuerpo propio, se dibuja con el estado que llega por la red (se
    // cargan wheels, steerAngle, riderLean y gripThrottle a mano y la pose con SetVisualState).
    void InitVisual(BikeParams& params);
    void SetVisualState(JPH::Vec3 position, JPH::Quat rotation, bool riderOn);
    JPH::ShapeRefC CollisionShape(bool withRider) const { return withRider ? shapeWithRider : shapeNoRider; }
    const BikeParams* Params() const { return P; }
    void Reset(PhysicsWorld& world, JPH::Vec3 position, float yaw);
    void ApplyMassProperties();      // tras recargar tuning.ini
    void SetVelocity(PhysicsWorld& world, JPH::Vec3 velocity);
    void SetAngularVelocity(PhysicsWorld& world, JPH::Vec3 angularVelocity);

    // BikePhysics.cpp: todas las fuerzas, antes de cada paso de Jolt.
    void PrePhysics(const BikeInput& input, float dt, PhysicsWorld& world, const Terrain& terrain);
    void PostPhysics();

    // drawRider: el piloto generado; livery: colores y número (uno por jugador).
    void Draw(Renderer& r, float alpha, bool drawRider = true, int livery = 0) const;
    void DrawDebug(float alpha) const;

    RiderPose RiderPoseLocal() const;
    // Caída: el piloto sale como ragdoll; la moto pierde su caja de colisión y su masa. Reset lo repone.
    void DetachRider(PhysicsWorld& world);
    bool RiderOnBike() const { return riderOnBike; }

    JPH::Vec3 RenderPosition(float alpha) const;
    JPH::Quat RenderRotation(float alpha) const;
    JPH::Vec3 Position() const { return currPos; }
    JPH::Quat Rotation() const { return currRot; }
    JPH::Vec3 Velocity() const;
    JPH::Vec3 AngularVelocity() const;
    JPH::BodyID BodyID() const;

    // Telemetría (lectura desde HUD / cámara / bot)
    Engine engine;
    Wheel wheels[2];
    float pitch = 0.0f, roll = 0.0f;         // rad (+ nariz arriba, + inclinada a la derecha)
    float speed = 0.0f, forwardSpeed = 0.0f;
    float steerAngle = 0.0f, leanTarget = 0.0f, riderLean = 0.0f;
    float riderSide = 0.0f;                  // cuerpo del piloto de costado (-1 izquierda .. +1 derecha)
    float steerBase = 0.0f, steerCaster = 0.0f;   // manubrio pedido y contravolante automático (rad)
    float bodyTiltRate = 0.0f;
    float slideAngle = 0.0f;                 // telemetría: cola afuera en la dirección de la curva (rad)
    float rearBrakeUsed = 0.0f;              // freno trasero que queda después de que el piloto lo module
    float slideRate = 0.0f;                  // rad/s con que se abre la cola (suavizado)
    float slideWant = 0.0f, slideTurn = 1.0f;    // ángulo de derrape que busca el piloto y hacia qué lado
    float slideIntent = 0.0f;                // 0..1: derrape buscado (freno trasero solo)
    float clutchPop = 0.0f;                  // s que quedan del pico de torque del embrague soltado de golpe
    float sinceThrottleLow = 0.0f;           // s desde que el gas estaba casi cerrado (para detectar el golpe de gas)
    bool popArmed = false;
    float grauToBalance = 0.0f;              // rad que le faltan al grau para el punto de equilibrio (telemetría)
    float grauClutch = 1.0f;                 // fracción del torque que deja pasar el embrague que patina el piloto del grau
    float pathRate = 0.0f, pathYawPrev = 0.0f;   // cuánto gira la dirección de marcha (rad/s) y la del paso anterior
    bool pathYawValid = false;
    float wander[2] = {0.0f, 0.0f};          // irregularidad del suelo bajo cada cubierta que patina (-1..1, lenta)
    uint32_t wanderRng = 0x1234567u;
    float legOut = 0.0f;                     // pata afuera en curva: + la derecha, - la izquierda (0..1)
    float bodyTilt = 0.0f;                   // cuerpo corrido de costado respecto de la moto (rad, + a la derecha)
    float supported = 1.0f;                  // 0..1 (suavizado): apoyada en el piso (ruedas o cola); 0 en el aire
    float throttle = 0.0f, frontBrake = 0.0f, rearBrake = 0.0f;
    float gripThrottle = 0.0f;               // cuánto gira el piloto el puño del acelerador (0..1)
    float airTime = 0.0f;
    bool wheelLog = false;           // pruebas (--wheel-log T0 T1): imprime cada rueda en cada paso
    bool crashed = false;
    bool tractionControl = true;
    float tcFactor = 1.0f, wheelieCut = 0.0f;
    float crashedTime = 0.0f;
    JPH::Vec3 comShiftWorld = JPH::Vec3::sZero();

private:
    void UpdateWheelConfig();
    void BuildShapes();

    JPH::Body* body = nullptr;
    BikeParams* P = nullptr;
    JPH::ShapeRefC shapeWithRider, shapeNoRider;
    bool riderOnBike = true;
    JPH::Vec3 prevPos = JPH::Vec3::sZero(), currPos = JPH::Vec3::sZero();
    JPH::Quat prevRot = JPH::Quat::sIdentity(), currRot = JPH::Quat::sIdentity();
    float crashTimer = 0.0f;
    float tailStill = 0.0f;                  // s quieta parada sobre la cola
    float oneWheelTime = 0.0f;               // s con la trasera en el piso y la delantera en el aire
    float autoShiftCooldown = 0.0f;
    float reverseHold = 0.0f;        // s manteniendo el freno parado (caja automática -> R)
};
