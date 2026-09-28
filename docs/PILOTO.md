# Piloto: modelo, IK y ragdoll

El piloto que se ve es el modelo glTF (`Low_Poly_Motorcyclist_2_rigged.gltf`, huesos con nombres estilo
Mixamo). Si no se encuentra, se usa el piloto generado (`Rider.cpp`; F9 cambia entre los dos,
`--genrider` arranca con el generado). **Las pruebas visuales del piloto se hacen con el modelo**: es el
principal y el que muestra los problemas de la malla.

## Sobre la moto (RiderModel.cpp)

- `Bike::RiderPoseLocal` da la pose (espacio de la moto): cadera, hombros, cabeza, rodillas, tobillos,
  hombros, codos y puños, según el estilo, el cuerpo y la velocidad.
- `RiderModel::PoseOnBike` la sigue por IK de dos huesos, con polos para codos y rodillas. Si las manos
  no llegan al manubrio, primero inclina más el torso (hasta `kMaxExtraPitch`) y después adelanta la
  cadera: nunca suelta el manubrio. La pelvis toma el 75% de la inclinación y cada vértebra un poco,
  para que la espalda quede recta (girando sólo la columna se inflaba la panza).
- Dedos: el modelo trae la mano en un solo hueso. Se arman falanges virtuales que envuelven el puño del
  manubrio; la derecha rueda sobre el puño con el acelerador.

### En el aire, tirado atrás, el torso atravesaba el manubrio
- **Pasaba**: tirándose adelante o atrás con las flechas en un salto, a veces el torso se iba para
  adelante y atravesaba el manubrio.
- **Por qué**: la pose "de pie en los pedales con el torso derecho" es para la moto casi vertical sobre la
  cola (hombros 62 cm adelante en el eje de la moto: con la moto parada, eso es "arriba"). Se mezclaba
  sólo por el cabeceo (55° a 80°), y en el aire, tirándose atrás, la moto pasa de 55°.
- **Qué se hizo**: la mezcla se multiplica por `Bike::supported` (0..1, suavizado en ~0.17 s). Vale 1
  cuando la moto toca el piso (ruedas o cola) y 0 en el aire; en red sale de las banderas
  `kFrontGround`/`kRearGround` de cada jugador. La pose no es física: la motocross da idéntica.
- **Se comprobó**: `--flat --drop 3 --test airrot` y `airlean` (capturas de costado, antes y después): el
  piloto queda agachado en su lugar. Con `colazo` (la moto vertical raspando) sigue de pie en los pedales.

## Ragdoll (Rider.cpp)

- **Partes**: 11 cuerpos que suman ~75 kg (pelvis 12, torso 26, cabeza 5, muslos 8, piernas 4.5, brazos
  2.2, antebrazos 1.6). `LinearCast`, amortiguación angular 0.5. Aparecen en la pose de la moto con la
  velocidad de ese punto de la moto, y chocan con ella recién cuando se separan (filtro de grupo).
- **Articulaciones** (espacio de la moto: +X izquierda, +Y arriba, +Z adelante):

| Articulación | Tipo | Flexión | Costado | Giro |
|---|---|---|---|---|
| columna | swing-twist | -25° .. 70° | 25° | 20° |
| cuello | swing-twist | -40° .. 50° | 30° | 50° |
| hombros | swing-twist | -45° .. 165° | 80° | 40° |
| caderas | swing-twist | -25° .. 125° | 35° | 20° |
| codos, rodillas | bisagra (`HingeConstraint`) | -4° .. 150° | — | — |

- Si la pose de la moto cae fuera de un rango (piloto muy agachado), el rango se agranda lo justo para
  que no salte al aparecer.
- **Pasadas del solver**: 24 de velocidad y 8 de posición en todas las articulaciones del ragdoll
  (`kVelocitySteps`, `kPositionSteps`; las de siempre son 10 + 2).

### El modelo pegado al ragdoll

- Cada hueso del modelo sigue rígido a una parte (`partOf`). Los que no están en la lista siguen a su
  padre. **El cuello va con la cabeza**: el ragdoll articula la cabeza en la base del cuello.
- **El ragdoll se arma con las articulaciones del modelo** (`RiderModel::JointPose`), no con las del
  piloto generado: cadera, rodillas, tobillos, hombros, codos, cuello y cabeza donde los tiene la malla.
  En red, `Multiplayer` arma las partes del otro con la misma cuenta.

### Lo que costó: "se dobla para cualquier lado" / "queda todo enroscado"

Tres causas, una detrás de la otra:

1. **Ejes cruzados** (primer arreglo). En el swing-twist de Jolt, `mNormalHalfConeAngle` limita el giro
   alrededor del eje "plane", no alrededor del "normal" (ver [FISICA.md](FISICA.md)). La flexión y el
   costado estaban intercambiados. Arreglo: el eje de flexión va como `mPlaneAxis`, y el eje twist del
   padre se centra en la mitad del rango anatómico.
2. **Los límites no aguantaban los golpes**. Medido en una caída a 60 km/h:
   - codo, de costado: 51° (límite 6°);
   - rodilla, de costado: 26°;
   - hombro, hacia atrás: 89° (límite 45°);
   - caderas, giro: ~50°.

   El swing-twist con un cono muy excéntrico (6° × 77°) sostiene mal una bisagra. Arreglo:
   - rodillas y codos como `HingeConstraint`, que no deja ni torcer ni doblar de costado;
   - más pasadas del solver;
   - menos giro permitido en las rótulas (la malla con skinning lineal se enrosca con giros grandes).

   Resultado en 6 caídas distintas: ningún límite pasado por más de 6°; las partes se separan ≤ 1.4 cm.
3. **Pivotes corridos respecto de la malla**. El ragdoll usaba las articulaciones del piloto generado,
   y el modelo tiene otros largos de hueso. Cada vez que una rodilla o un codo se doblaba, la malla se
   estiraba y se retorcía alrededor de un pivote que no era el suyo. Arreglo: `JointPose`.

### Cómo se mide

- `--telemetry` imprime, al terminar cada caída, cada articulación:
  - la flexión medida y su rango;
  - de costado y su límite;
  - el giro y su límite;
  - la separación máxima en el punto de unión;
  - los pasos fuera de rango;
  - lo mismo según Jolt (su descomposición swing-twist o el ángulo de la bisagra).

  En las rótulas vale el giro de Jolt: el propio depende del camino y con swings grandes da de más.
- Caídas para probar:
  - `--test crashloop`: gas a fondo, se estrella a ~60 km/h a los ~4 s, reaparece y repite;
  - `--flat --test circle3x`: se cae despacio doblando;
  - `--respawn-after 8`: deja al piloto 8 s en el piso en vez de 3.
- Para mirarlo: capturas en serie (`--screenshot 3.9 archivo.png --shots-every 0.1`), `--nohud` y
  `--view <yaw> <pitch> <zoom>` (la cámara sigue a la pelvis con retraso: para la pose final, capturar
  ya quieto). Comandos completos en [PRUEBAS.md](PRUEBAS.md).
