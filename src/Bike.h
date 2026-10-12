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

// Trucos en el aire (con el botón de trucos; ver docs/PILOTO.md, "Trucos"). El piloto mueve el cuerpo: la moto
// recibe la reacción (Bike::UpdateTrick) y si toca el piso sin haber vuelto, se cae.
// NacNac: la pierna derecha pasa por arriba de la cola a la izquierda; CanCan: la izquierda cruza por delante al costado
// derecho. NacNacRight y CanCanLeft (v0.3.10): los mismos espejados. En red viajan como el original más un byte (ver
// Multiplayer.cpp, WriteState): una versión anterior ve el truco sin espejar.
namespace Trick {
enum : int { None, Superman, HeelClicker, NoHander, NacNac, CanCan, NacNacRight, CanCanLeft, Count };
// El truco que conoce una versión anterior (la v0.3.9 tenía hasta CanCan): el espejado, el original.
inline int Base(int t) { return t == NacNacRight ? (int)NacNac : (t == CanCanLeft ? (int)CanCan : t); }
}

struct BikeInput {
    float throttle = 0.0f;     // 0..1
    float frontBrake = 0.0f;   // 0..1
    float rearBrake = 0.0f;    // 0..1
    float steer = 0.0f;        // -1..1 (+ = derecha)
    float lean = 0.0f;         // -1..1 (+ = piloto adelante)
    float side = 0.0f;         // -1..1: el cuerpo del piloto a la izquierda / derecha (+ = derecha)
    int trick = Trick::None;   // el truco que pide el jugador (botón de trucos apretado); en el piso no hace nada
    bool shiftUp = false;
    bool shiftDown = false;
};

// Todos los números ajustables de la moto. Se registran en Tuning con el mismo nombre que
// aparece en tuning.ini. Espacio local de la moto: origen = centro de masa, +Z adelante, +Y arriba.
struct BikeParams {
    // Física y pose recuperadas de la primera v0.3.1 (26/09/2026), sólo para la Motocross 450.
    float original031 = 0.0f;
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
    // Golpe seco contra el piso que la haría girar más (cayendo de cola con la trasera a fondo): qué parte del giro le
    // da; el resto lo absorbe el piloto con las piernas. 1 = cuerpo rígido, como siempre. Ver PhysicsWorld.
    float hardLandingSpin = 1.0f;
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
    // Cuánto baja maxLateralAccel (lo que pide el input y, con eso, la inclinación) cuando el agarre lateral bajo las ruedas
    // (SurfaceGrip × *_tire_loose/paved_grip × *_tire_lat_grip, en g) es menor: 1 = pide lo que dan las cubiertas, 0 = no baja (como antes).
    float leanSurfaceGrip = 0.0f;
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

    float wheeliePrediction = 0.35f; // segundos de anticipación del recorte de gas
    float wheelieThrottleFloor = 0.0f; // gas mínimo con la asistencia de wheelie activada
    float wheelieKeepThrottle = 0.0f; // 0 = el limitador corta el gas; 1 = deja el gas y anula el cabeceo de la tracción
    float wheelieKeepFromDeg = 5.0f; // ... sólo en un wheelie de verdad: desde estos grados de trompa arriba
    float wheelieKeepFullDeg = 12.0f;// ... y del todo desde éstos (menos que 1 entero se da vuelta)
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
    // flat_side_exact (0 = como antes): el costado con que se miden los ángulos en el plano (la deriva de brake_align, el
    // camino de la delantera libre, el derrape lento, el grau, el peralte) es el perpendicular al rumbo. Antes era el costado
    // de la moto aplanado, que con la moto cabeceada e inclinada a la vez queda torcido tan(cabeceo)·tan(inclinación) (frenando
    // inclinada, 0.3-1°): la delantera libre apuntaba para afuera y a 200-280 km/h la moto doblaba para el otro lado. Ver
    // FISICA.md, "Frenando y doblando rápido, la moto doblaba para el otro lado".
    float flatSideExact = 0.0f;
    // brake_align_rear (0 = como antes): brake_align también con el freno de atrás solo (cuánto cuenta: 1 = como el de adelante).
    // Soltando la curva con el trasero clavado, la trasera frenada no agarra de costado y la moto quedaba derrapando para el lado
    // de la curva inclinada para el otro (la de carreras, 4-7 s).
    float brakeAlignRear = 0.0f;
    // tire_track_exact (0 = como antes): en una rueda (la trasera, con la delantera en el aire) la cubierta rueda por su huella real,
    // la recta del piso perpendicular a su eje (n × eje), y no por el rumbo de la moto proyectado en el piso. Son lo mismo salvo
    // con la moto cabeceada respecto del piso e inclinada a la vez: en un wheelie doblando (cabeceo 20-30°, inclinada 45°) la
    // huella queda 18-24° cruzada respecto del rumbo, y la cubierta empujaba como si rodara por el rumbo: la moto iba de costado
    // respecto de su rueda de atrás. La moto, además, se alinea con la huella (wheelie_turn: en la física 031 sólo con esta clave).
    // Con las dos ruedas, como antes (en la favela, con la huella exacta siempre, la 450, la 600 y la 2T se caían en la escadaria).
    // Ver FISICA.md, "En wheelie doblando la moto iba de costado".
    float tireTrackExact = 0.0f;
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
    // Cuánto del barrido de costado que hace el rolido en los contactos no ven las cubiertas (0 = todo, como antes; 1 = nada).
    // El balance inclina la moto alrededor del centro de masa: al enderezarla rápido los contactos barren hacia afuera y las
    // cubiertas empujan para el otro lado (soltando una curva para frenar, la moto se iba para el lado contrario). En una de
    // verdad la moto rola alrededor de la línea de los contactos, que no se mueven. Sólo mientras se endereza.
    float leanSweepComp = 0.0f;
    // Freno de mano (Espacio / botón A): doblando y clavando el freno trasero, el piloto hace salir la cola hasta
    // handbrake_angle grados (handbrake_angle_fast a ~70 km/h o más) y la sostiene ahí. Torque de guiñada en Nm por rad
    // que le falta a la cola (0 = nada, como antes). Al soltar, durante handbrake_catch s el piloto ataja la cola.
    float handbrakeYaw = 0.0f;
    float handbrakeAngle = 24.0f, handbrakeAngleFast = 14.0f;
    float handbrakeCatch = 0.6f;
    // Freno de mano: inclinación (grados) que sostiene el piloto mientras colea, con la dirección a fondo (con media
    // dirección ~60%), firme hasta ~10 km/h. 0 = como antes (la moto se endereza: slide_upright).
    float handbrakeLean = 0.0f;
    // Freno de mano: s en que la cola sale hasta el ángulo buscado al clavar y vuelve a derecho al soltar (y el manubrio,
    // el contravolante y la inclinación pasan de a poco). 0 = de golpe, como antes.
    float handbrakeRamp = 0.0f;
    float tcSlip = 0.35f;            // slip trasero tolerado por el control de tracción
    float crashAngleDeg = 72.0f;
    // Golpe fuerte contra algo fijo: el piloto sale despedido (m/s con que se acercaban). De frente o de
    // costado (un muro, una casa, una barrera) y cayendo de panza a plomo; 0 = nunca.
    float crashImpactSpeed = 7.0f;
    float crashImpactVertical = 11.0f;
    // Equilibrio por la carga (0..1; 0 = como antes): la inclinación que busca el balance sale de la carga real (el peso y
    // la fuerza del suelo que pisan las ruedas, a lo largo de su normal) más la curva que pide el manubrio, y se mide y se
    // limita (max_lean_deg) contra la normal de ese suelo, no contra la vertical del mundo. Así en una ladera, un peralte o
    // la pared de un bowl la moto no se endereza "contra el mundo". Ver FISICA.md, "El equilibrio era un giroscopio".
    float leanLoad = 0.0f;
    // Con lean_load, tope de la inclinación contra el mundo (grados; 0 = sin tope). Sin él, la de carreras subía por la
    // pared del peralte de afuera de la pista de motocross acostada 90° y al salir al plano se caía.
    float leanLoadWorldDeg = 0.0f;
    // Con lean_load, cuánto de la carga de más (sobre el peso) se deja de contar para la inclinación en un suelo tendido
    // (menos de ~20°; desde ~40° cuenta entera): baches, whoops y compresiones duran décimas y el filtro las seguía tarde.
    // 0 = cuenta siempre. La 450, la 600 y la Trial: 1. Ver FISICA.md, "La 450 y la 600 con el equilibrio por la carga".
    float leanLoadFlat = 0.0f;
    // Balance a velocidad: K y D se multiplican por esto desde balance_fast_speed (m/s; empieza a la mitad). 1 = como antes.
    float balanceFastK = 1.0f, balanceFastD = 1.0f;
    float balanceFastSpeed = 5.5f;
    // ... y amortigua el rolido respecto de una inclinación de referencia que sigue a la pedida a lo sumo a estos rad/s (lo
    // que el piloto acuesta la moto), no todo el rolido: acompaña cuando el piloto la acuesta. 0 = todo el rolido.
    float balanceFastLead = 0.0f;
    // Whip de verdad (ver FISICA.md, "Un whip de verdad"). 0 = como antes.
    // whip_takeoff: en la cara del salto, con el stick al costado, el piloto patea la cola: la cubierta trasera empuja de
    // costado (fracción de su carga, a ~43 km/h; crece con la velocidad), la moto sale del labio girando de costado y de
    // guiñada. Es una fuerza del suelo en el contacto (con el tope de agarre de la cubierta), no se fija la rotación.
    float whipTakeoff = 0.0f;
    // whip_air: en el aire el whip no vuelve solo (0..1: cuánto se apagan el enderezado y la alineación de guiñada mientras la
    // moto está cruzada) y el stick al costado es una velocidad de giro sobre el eje del whip (rolido + guiñada) que el
    // torque persigue: para el mismo lado lo acentúa, para el otro lo frena y lo trae; suelto conserva lo que trae.
    float whipAir = 0.0f;
    float whipAirRate = 1.8f;        // rad/s con el stick a fondo
    float whipAirTorque = 350.0f;    // Nm máximos
    float whipAirGain = 300.0f;      // Nm por rad/s de diferencia
    float whipAirDamping = 200.0f;   // Nm por rad/s con el stick suelto: el piloto frena el whip con el cuerpo (no lo trae)
    // whip_air_return: el stick al otro lado del whip lo trae derecho (rolido contra la vertical y trompa hacia donde vuela,
    // cada uno con su torque) en vez de girar al revés sobre el eje del whip. Es la velocidad de vuelta por radián de
    // cruzada (1/s, con el tope whip_air_rate × el stick); whip_air_return_torque, los Nm máximos de rolido y de guiñada.
    // 0 = como antes. Ver FISICA.md, "Salir del whip".
    float whipAirReturn = 0.0f;
    float whipAirReturnTorque = 350.0f;
    // Aterrizar cruzado: más de esto (grados entre hacia dónde apunta y hacia dónde va, en el suelo) es caída. 0 = nunca.
    float whipLandCrashDeg = 0.0f;
    // Física 031 (la 450 y la 600; la otra física tantea los cantos con sondas): sobre objetos, una cara que mira hacia
    // adelante más empinada que esto (grados; la contrahuella del escalón que la rueda deja bajando) no es apoyo para el
    // rayo de la rueda. 0 = como antes. Ver FISICA.md, "La 450 y la 600 con el equilibrio por la carga y el whip".
    float objectWallDeg = 0.0f;
    // Trucos (sólo con el botón de trucos; sin él no actúan). trick_speed: qué tan rápido mueve el cuerpo el piloto (1/s,
    // resorte crítico: ~4.7/trick_speed s en llegar, lo mismo en volver). trick_reaction: fracción de la reacción del cuerpo
    // sobre la moto (1 = la física: se conserva el momento angular). Tocar el piso con el truco a más de trick_crash
    // (0..1) es caída; a más de trick_hit, un golpe: el piloto no controla bien la moto un momento.
    float trickSpeed = 9.0f;
    float trickReaction = 1.0f;
    float trickCrash = 0.4f;
    float trickHit = 0.12f;

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
    float gripFactor = 1.0f;         // agarre lateral (suelo × cubierta × lat_grip) en el último apoyo (lean_surface_grip)
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
    // Trucos (Bike::ApplyTrickPose): cuánto está en un truco (0..1) y lo que el truco corrió la cadera y los hombros (ya
    // sumado a hips / shoulders: RiderMotion suaviza el resto y esto lo suma aparte, porque ya viene suave).
    float trick = 0.0f;
    JPH::Vec3 trickHips = JPH::Vec3::sZero(), trickShoulders = JPH::Vec3::sZero();
    float legFree[2] = {0.0f, 0.0f};                // 0..1: el pie dejó la estribera (la rodilla dobla hacia `knee`)
    JPH::Vec3 toe[2] = {JPH::Vec3::sAxisZ(), JPH::Vec3::sAxisZ()};   // con el pie suelto, hacia dónde apunta la punta
    float handFree[2] = {0.0f, 0.0f};               // 0..1: la mano soltó el manubrio (grip = dónde va la muñeca)
    // Hacia dónde mira el piloto además de la pista (rad; el nac-nac mira atrás, por arriba del hombro): giro de los hombros
    // sobre el eje del torso y de la cabeza (+ hacia la izquierda) y cabeza abajo (+). Sólo el modelo (RiderModel).
    float twist = 0.0f, lookYaw = 0.0f, lookDown = 0.0f;
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
    void PrePhysics031(const BikeInput& input, float dt, PhysicsWorld& world, const Terrain& terrain);
    // lean_load: el suelo que pisan las ruedas, en el marco de la moto (BikePhysics.cpp).
    struct LoadFrame {
        float psi = 0.0f;    // inclinación respecto de la normal del suelo (rad, + a la derecha)
        float f0n = 9.81f;   // carga por kg a lo largo de la normal (m/s²; quieta en el plano, g)
        float f0r = 0.0f;    // el peso a lo largo del costado del suelo (m/s², + hacia la derecha de la moto)
        float weight = 1.0f; // cuánto vale (lean_load × 0..1): con la moto muy de punta respecto del suelo, nada
        float f0nTurn = 9.81f; // para cuánto se puede doblar: la carga, hasta el peso a lo largo de la normal
    };
    LoadFrame UpdateLoadFrame(JPH::Vec3 fwd, JPH::Vec3 up, float mass, JPH::Vec3 fallbackNormal, float dt);
    void TrackLeanTarget(bool valid, float dt);   // balance_fast_k/d
    // Whip (BikePhysics.cpp): la intención en la cara del salto, la patada de la trasera, cuánto está cruzada en el aire y
    // el estado (pico, despegue, aterrizaje cruzado).
    void UpdateWhipIntent(float steer, JPH::Vec3 linVel, float v, float mass, bool wasInAir, float dt);
    float WhipKickForce(float rearLoad, float grip, float v) const;
    float WhipAmount() const;
    float WhipAirStick(float steer, bool takeoff, float airTimeNow);   // whip_air: el stick que cuenta como whip en este vuelo
    void UpdateWhipState(bool grounded, bool wasInAir, float airTimeBefore, JPH::Vec3 fwd, JPH::Vec3 linVel, JPH::Vec3 fwdFlat);
    bool WhipAirReturn(float airSteer, float stick, JPH::Vec3 fwd, JPH::Vec3 up, JPH::Vec3 angVel, JPH::Vec3& torque);
    void UpdateHandbrake(const BikeInput& in, JPH::Vec3 linVel, JPH::Vec3 fwdFlat, float lowSlide, float dt);   // freno de mano (handbrake_yaw)
    float HandbrakeLean(float leanTarget) const;
    float HandbrakeSteer(float steerTarget, JPH::Vec3 linVel, float yawRate, JPH::Vec3 frontOffset, JPH::Vec3 fwdFlat, float maxSteerRad) const;
    void PostPhysics();

    // drawRider: el piloto generado; livery: colores y número (uno por jugador).
    void Draw(Renderer& r, float alpha, bool drawRider = true, int livery = 0) const;
    void DrawDebug(float alpha) const;

    RiderPose RiderPoseLocal() const;
    RiderPose RiderPoseLocal031() const;
    // Manubrio dibujado (moto, manos): sigue a steerAngle con un resorte crítico (kSteerShownW). La dirección de la física es un
    // seguidor con tope de velocidad que suma tasas (derrape, freno de mano, delantera libre: hasta 22 rad/s) y el contravolante
    // automático, que cambian de golpe; dibujado tal cual, el manubrio y las manos pegaban tirones de 10-15° por cuadro. Lo avanza
    // Game una vez por cuadro (sólo con ventana): sin eso (sin ventana, otras motos, la vitrina) se dibuja el de la física.
    void UpdateSteerShown(double time);
    float DrawSteer() const { return steerShownOn ? steerShown : steerAngle; }
    // Trucos (Bike.cpp): lleva la pose al truco `t` (Trick::...) con `amount` 0..1 (la pose de la moto, o la neutra para
    // la física). TrickBody: los centros de masa de las partes del cuerpo (RiderPart, kg en kTrickMass) con la pose neutra
    // del estilo y el truco: de ahí sale la reacción sobre la moto (BikePhysics.cpp, UpdateTrick).
    void ApplyTrickPose(RiderPose& p, int t, float amount) const;
    void TrickBody(int t, float amount, JPH::Vec3* centers) const;
    // Trucos (BikePhysics.cpp): el truco pedido, el cuerpo que va y viene (resorte), la reacción y el centro de masa; y al
    // tocar el piso (o algo) sin haber vuelto, la caída o el golpe.
    void UpdateTrick(int request, bool flying, JPH::Vec3 angVelLocal, float dt);
    void TrickTouch(bool touching, float dt);
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
    float steerShown = 0.0f, steerShownRate = 0.0f;   // UpdateSteerShown
    double steerShownTime = -1.0;
    bool steerShownOn = false;
    float riderSide = 0.0f;                  // cuerpo del piloto de costado (-1 izquierda .. +1 derecha)
    float steerBase = 0.0f, steerCaster = 0.0f;   // manubrio pedido y contravolante automático (rad)
    float leanGrip = 1.0f;                        // agarre bajo las ruedas, filtrado (lean_surface_grip)
    JPH::Vec3 loadNormal = JPH::Vec3::sAxisY();   // lean_load: normal del suelo que pisan las ruedas (filtrada)
    float loadAccel = 9.81f;                      // lean_load: carga por kg a lo largo de esa normal (filtrada)
    float leanSurface = 0.0f;                     // lean_load: inclinación respecto de ese suelo (rad, telemetría)
    float leanTargetPrev = 0.0f, leanTargetRate = 0.0f;   // balance_fast_k/d: cómo cambia la inclinación buscada (rad/s)
    bool leanTargetValid = false;
    float balanceSettle = 0.0f;                   // balance_fast_k/d: 0..1, asentada sobre las dos ruedas
    // Whip (para el piloto animado y los trucos): se leen en cualquier moto; con whip_takeoff / whip_air en 0 igual se calculan.
    float whipIntent = 0.0f;                 // -1..1: el piloto tira la moto de costado en la cara del salto (+ a la derecha)
    float whipPushTime = 0.0f;               // s desde que el labio dejó de empujar (cargas de más de ~1.6 g)
    float whipYaw = 0.0f;                    // rad: la trompa respecto de hacia dónde vuela, en el plano (+ a la izquierda)
    float whipPeakYaw = 0.0f, whipPeakRoll = 0.0f;   // lo máximo de este vuelo (rad, con signo)
    float whipLaunch = 0.0f;                 // -1..1: con cuánto whip despegó (la intención en el labio); hasta el próximo despegue
    bool whipCrossedLanding = false;         // el último aterrizaje fue cruzado de más (whip_land_crash_deg): caída
    // whip_air: el stick que venía apretado desde el piso (doblando al pasar una cresta) no es un whip hasta que se suelta;
    // y un vuelo es "del piloto" si despegó con whip o tiró el stick en el aire (si no, el enderezado de siempre).
    bool whipArmed = true, whipRiderFlight = false;
    float whipReturn = 0.0f;                 // whip_air_return: el lado del stick que está trayendo la moto (0 = no la trae)
    float bodyTiltRate = 0.0f;
    // Trucos: cuál (Trick::...), cuánto está hecho (0 en la moto .. 1 del todo) y con qué velocidad; trickTorque y trickCom
    // (espacio de la moto): la reacción del cuerpo y cuánto corre el centro de masa de todo; trickHitAmount: golpe al tocar
    // el piso a medio volver (0..1, se va en 0.6 s); trickLanded: con cuánto truco tocó (telemetría; -1 = todavía no).
    int trick = Trick::None;
    float trickAmt = 0.0f, trickRate = 0.0f;
    JPH::Vec3 trickTorque = JPH::Vec3::sZero(), trickCom = JPH::Vec3::sZero();
    JPH::Mat44 trickScale = JPH::Mat44::sIdentity();   // I0·(I0 + ΔI)⁻¹: el cuerpo estirado pesa para todos los torques
    float trickHitAmount = 0.0f, trickLanded = -1.0f;
    unsigned resetCount = 0;                // cuántas veces apareció o reapareció (Reset): el dibujo del piloto arranca de nuevo
    float slideAngle = 0.0f;                // telemetría: cola afuera en la dirección de la curva (rad)
    float rearBrakeUsed = 0.0f;              // freno trasero que queda después de que el piloto lo module
    float slideRate = 0.0f;                  // rad/s con que se abre la cola (suavizado)
    float slideWant = 0.0f, slideTurn = 1.0f;    // ángulo de derrape que busca el piloto y hacia qué lado
    float slideIntent = 0.0f;                // 0..1: derrape buscado (freno trasero solo)
    float handbrake = 0.0f;                  // 0..1: freno de mano (handbrake_yaw): trasero clavado doblando
    float handbrakeTorque = 0.0f;            // Nm de guiñada (+Y) que pone el piloto para sacar o sostener la cola
    float hbSlide = 0.0f, hbRate = 0.0f;     // cola afuera (rad, + hacia afuera de la curva) y con qué rapidez se abre
    float hbWant = 0.0f, hbTurn = 1.0f;      // cola que busca (rad) y hacia qué lado dobla
    float hbCatch = 0.0f;                    // 0..1: atajando la cola después de soltar el freno de mano
    float hbLow = 0.0f;                      // 0..1: cuánto manda el derrape lento de siempre (slideIntent)
    float hbCatching = 0.0f;                 // 0..1: cuánto la levanta al salir del freno de mano (handbrake_lean)
    float hbUp = 0.0f;
    float hbIn = 0.0f, hbW = 0.0f;           // freno de mano en rampa (handbrake_ramp) y el peso de lo que hace el piloto
    float hbAim = 0.0f, hbCatchWant = 0.0f;  // cola que busca ahora (rad) y la que busca al atajar (baja a 0 en rampa)
    float hbLean = 0.0f;                     // inclinación que sostiene el piloto con el freno de mano (rad, + a la derecha)
    bool hbValid = false;
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
    float tcFactor = 1.0f, wheelieCut = 0.0f, wheelieKeep = 0.0f;
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
    float trackExactW = 0.0f;                // tire_track_exact: cuánto rueda la trasera por su huella (entra y sale en 0.15 s)
    float oneWheelTime = 0.0f;               // s con la trasera en el piso y la delantera en el aire
    float autoShiftCooldown = 0.0f;
    float reverseHold = 0.0f;        // s manteniendo el freno parado (caja automática -> R)
};
