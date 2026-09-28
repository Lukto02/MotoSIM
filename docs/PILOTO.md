# Piloto: modelo, IK y ragdoll

El piloto que se ve es el modelo glTF `Low_Poly_Motorcyclist_3_rigged.gltf` (huesos con nombres estilo
Mixamo, con los dedos; riggeado con `tools/riggear_piloto.py`, ver "Riggear un modelo nuevo"). De
respaldo, si no está: `Low_Poly_Motorcyclist_2_rigged.gltf` (la mano en un solo hueso) y
`Low_Poly_Motorcycle_Racer_rigged.gltf`. **El modelo 2 queda en el proyecto a pedido del usuario** ("no borres
el viejo, dejalo de backup"): no se borra aunque ya no sea el principal. Si no se encuentra ninguno, se usa el piloto generado
(`Rider.cpp`; F9 cambia entre los dos, `--genrider` arranca con el generado). **Las pruebas visuales del
piloto se hacen con el modelo**: es el principal y el que muestra los problemas de la malla.

**Sin ventana (`--headless`) el modelo no se carga** (no hay GPU para la malla ni la textura): el ragdoll
aparece con la pose del piloto generado y no con `JointPose`. Todo lo del ragdoll con el modelo (lo que
ve el jugador) se mide con ventana; ver "Cómo se mide".

## Sobre la moto (RiderModel.cpp)

- `Bike::RiderPoseLocal` da la pose (espacio de la moto): cadera, hombros, cabeza, rodillas, tobillos,
  hombros, codos y puños, según el estilo, el cuerpo y la velocidad.
- `RiderModel::PoseOnBike` la sigue por IK de dos huesos, con polos para codos y rodillas. Si las manos
  no llegan al manubrio, primero inclina más el torso (hasta `kMaxExtraPitch`) y después adelanta la
  cadera: nunca suelta el manubrio. La pelvis toma el 75% de la inclinación y cada vértebra un poco,
  para que la espalda quede recta (girando sólo la columna se inflaba la panza).
- Dedos (`BuildGripFingers`): si el modelo trae los dedos (`LeftHandIndex1..4`, ..., `LeftHandThumb1..4`,
  estilo Mixamo, el 4 es la punta), se usan esos huesos con sus pesos. La mano se mide con sus
  articulaciones: a lo largo, de la muñeca al nudillo del medio; a lo ancho, del meñique al índice; la
  palma, del lado del pulgar. El puño va contra la base de los dedos, 2 cm antes de los nudillos, y cada
  falange gira lo justo para que la articulación siguiente quede sobre la goma (8° más en las dos
  últimas: aprieta). El pulgar lo rodea del otro lado: cada hueso gira para un lado o el otro lo mínimo
  para quedar sobre la goma sin que el hueso se meta en ella. Si la mano es un solo hueso (modelo 2), se
  arman falanges virtuales (`BuildGrip`). La derecha rueda sobre el puño con el acelerador.
- Cabeza: mira adelante y 22° abajo (la pera hacia el pecho) y **la mitad del giro la toma el cuello**
  (ver "El cuello del modelo 3").

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

### El cuello del modelo 3
- **Pasaba**: con el modelo 3 se veía un cogote largo, color piel, entre el casco y el cuello de la
  campera; el casco flotaba arriba y adelante de los hombros.
- **Por qué**: dos cosas. El cuello de ese modelo no es parte del cuerpo sino de la pieza del casco (un
  tubo que baja adentro del cuello de la campera), y el script lo había dejado rígido con la cabeza. Y
  con el torso inclinado, para mirar la pista giraba sólo el hueso `Head` (hasta 40° hacia atrás
  respecto del torso): la garganta se estiraba.
- **Qué se hizo**: el script reparte el tubo del cuello entre `Spine`, `neck` y `Head` según la altura
  (el casco sigue rígido). En `SolveBody` el cuello toma la mitad del giro de la mirada, y la mirada va
  22° abajo (`{0, -0.4, 1}`, antes `{0, -0.2, 1}`, 11°), la pera hacia el pecho como en la posición de
  ataque. Vale también para el modelo 2 (se ve bien: mira un poco más abajo).
- **Se comprobó**: capturas de costado y de atrás (`--flat --test pose --side --view 0 0 0.3`) con los
  dos modelos, antes y después: el casco baja sobre el cuello de la campera.

## Riggear un modelo nuevo (tools/riggear_piloto.py)

Para una malla sin esqueleto (p. ej. un personaje de Tripo en pose A o T), en Blender sin ventana:

```
blender -b --python tools/riggear_piloto.py -- Low_Poly_Motorcyclist_3.gltf Low_Poly_Motorcyclist_3_rigged.gltf \
    [--altura 1.70] [--adelante auto|+x|-x|+y|-y] [--medidas m.json] [--articulaciones a.json] [--malla m.npz]
```

- **Orienta y escala**: mirando a -Y en Blender (+Z en glTF, como espera `RiderModel`), la izquierda del
  piloto en +X, 1.70 m, pies en z = 0. Hacia dónde mira lo saca de los pies (la suela se estira más
  adelante que atrás del tobillo); `--adelante` lo fuerza.
- **Mide las articulaciones** en una copia con los vértices repetidos unidos (las costuras de UV dejan
  la malla en 72 pedazos; unidos quedan las piezas de verdad: cuerpo, guantes, casco, botas):
  - piernas con cortes horizontales: entrepierna, cadera 5% del alto arriba de ella, tobillo a 5.8% del
    alto, rodilla por la proporción anatómica (51% de tobillo a cadera), corrida hacia la rodillera si
    sobresale; la posición, sobre las rectas del muslo y la canilla;
  - tronco: la línea media, el cuello donde el contorno se angosta a menos del 40% de los hombros, la
    cabeza a un cuarto de cuello a coronilla, la columna repartida como en el modelo 2;
  - brazos con cortes perpendiculares a su eje (ajustado en cuatro pasadas): muñeca en lo más angosto
    entre el puño del guante y la palma, hombro sobre el eje 3.5% del alto debajo de lo más alto del
    hombro, codo al 55.5% de hombro a muñeca;
  - dedos: distancia geodésica desde el puño y las **ramas del grafo de Reeb** (cada dedo es una rama que
    se separa del resto en su horqueta; el pulgar, la que se separa primero). El eje de cada dedo sale
    de cortes perpendiculares; el nudillo va adentro de la palma, 28% del largo visible antes de la
    horqueta, y las otras dos articulaciones al 46 y 74% de nudillo a punta. Pulgar: la base al 35% de
    muñeca a nudillo, el nudillo un poco antes de su horqueta y la otra articulación al 52%.
  - `--medidas` guarda todo (coordenadas de Blender, m) y `--articulaciones` corrige lo que haga falta.
- **Esqueleto**: los nombres y la jerarquía del modelo 2 (`Hips` → `Spine02` → `Spine01` → `Spine` →
  `neck` → `Head`, `LeftShoulder` → `LeftArm`..., `LeftUpLeg`..., `head_end`, `headfront`) más 5 × 4
  huesos por mano (`LeftHandThumb1..4`, `Index`, `Middle`, `Ring`, `Pinky`; el 4 es la punta y no
  deforma). La orientación de los huesos no importa: `RiderModel` usa sólo las posiciones de reposo.
- **Pesos**: el calor de Blender (`ARMATURE_AUTO`) **pieza por pieza**, cada una sólo con sus huesos:
  el guante con antebrazo, mano y dedos; la bota con canilla, pie y dedos del pie; el cuerpo con el resto
  (sin mano ni pie si los tiene otra pieza); el casco rígido con `Head`. Se calcula en la copia unida
  (con los 72 pedazos sueltos de las costuras, cada uno se pesaría por su lado y la malla se abriría en
  las costuras) y se pasa a la original por posición. Hasta 4 huesos por vértice (raylib lee sólo `JOINTS_0`), sin pesos de menos de
  0.02. Si el calor no encuentra solución para un vértice, va al hueso permitido más cercano.
- **Textura**: sólo la de color, en PNG: el raylib del juego no lee JPG (`SUPPORT_FILEFORMAT_JPG`
  apagado) y el shader usa sólo esa. El normal map y el ORM se sacan.
- La salida queda como la del modelo 2: `X.gltf` + `X_deps/` (el `.bin` y la textura).

### Lo que costó (modelo 3, de Tripo)
- **Pesos que se pasan de un dedo al vecino**: el calor le dio a vértices de un dedo hasta 0.33 del
  dedo de al lado (el meñique derecho). El script pasa ese peso a la falange del mismo nivel del dedo
  propio (pasarlo a la mano dejaría esos vértices quietos al cerrar el dedo) e imprime el peso ajeno y
  el propio de cada dedo.
- **El cuello venía en la pieza del casco**: ver "El cuello del modelo 3". El script lo reconoce como
  el tubo angosto (menos de 3.6% del alto del eje del cuello) debajo de la cabeza.
- **La palma del modelo 3 tiene pocos vértices** (triángulos grandes del pulgar a la base de los dedos):
  medir su superficie con el máximo de los vértices daba 4 cm; el puño se apoya contra la base de los
  dedos (nudillo + grosor del dedo), que sí está bien medida.
- **Muñeca y antebrazo**: se midió el giro sobre el hueso entre antebrazo y mano en la pose de manejo
  (7-8°, el codo doblado 19-22°): no hay "caramelo" que justifique repartir el giro ni pasar a dual
  quaternion.
- Para revisar: `--malla m.npz` guarda la copia unida con las islas y los pesos. Con numpy + PIL se
  dibujan cortes, vistas con las articulaciones y colores por hueso; se puede aplicar a la malla del glTF
  una pose volcada del juego (posición y giro de cada hueso) para mirar el agarre de cerca, con el puño
  dibujado, sin textura que confunda.

### Modelo 3: lo que salió
- 3270 vértices (2444 unidos), 4802 triángulos, 6 piezas: cuerpo 704, guantes 541 cada uno, casco con
  cuello 262, botas 198. 64 huesos, 52 deforman.
- Medido (m, desde la línea media): cadera ±0.08 a 0.83 de alto, rodilla 0.47, tobillo 0.10, `Hips`
  0.90, cuello 1.385, cabeza 1.464, hombro ±0.15 a 1.31, codo 1.09, muñeca 0.91 (el brazo en reposo a
  40° de la vertical). Dedos de 6.3 a 8.8 cm visibles y 1.2 cm de grosor.
- Dedos cerrados sobre el puño (log al cargar): índice 37-43° / 67-81° / 42-47°, pulgar 6° / -4..-9° /
  -47..-50° (la punta se dobla sobre la goma).

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
   (Medido sin ventana, o sea con el piloto generado; en 11 caídas, 10°. Con el modelo, ver "Con el
   modelo, los brazos saltaban al aparecer".)
3. **Pivotes corridos respecto de la malla**. El ragdoll usaba las articulaciones del piloto generado,
   y el modelo tiene otros largos de hueso. Cada vez que una rodilla o un codo se doblaba, la malla se
   estiraba y se retorcía alrededor de un pivote que no era el suyo. Arreglo: `JointPose`.

### Con el modelo, los brazos saltaban al aparecer
- **Pasaba**: medido **con ventana** (con el modelo, que es lo que ve el jugador), en el primer paso del
  ragdoll los hombros ya estaban 80° girados sobre el hueso (límite 40°) y se separaban 1.3 cm; el codo,
  de costado 26°. Con los dos modelos. Sin ventana (el piloto generado) no pasaba: 22° al aparecer.
- **Por qué**: en `crashloop` se cae tirado adelante, y ahí el modelo va con el pecho sobre el manubrio
  y los codos abiertos (el brazo casi horizontal hacia afuera: 75-85° de costado). El swing-twist se
  armaba con el eje "plane" del hijo proyectado (`Perpendicular(t2, a)`): con un arco grande que no es
  sólo flexión, eso deja un giro sobre el hueso según la descomposición de Jolt, y además el arco caía
  afuera del cono (una elipse en los senos de los medios ángulos). El solver lo corregía de un tirón.
- **Qué se hizo**: el eje "plane" del hijo es el del padre llevado por el arco más corto de `t1` a `t2`
  (`Quat::sFromTo(t1, t2) * a`): al aparecer el giro es 0. Y si ese arco cae afuera de la elipse, los dos
  medios ángulos se agrandan lo justo (al 95%), como ya se hacía con el rango de flexión.
- **Se comprobó**: en el primer paso, giro 0° y separación 0.0 cm en todas las rótulas (antes 80° y 1.3
  cm). La prueba de 150 s sin ventana (11 caídas) da como antes: lo más pasado 12° (la v0.2.6, 10°),
  separación ≤ 2.4 cm (1.8). Las pruebas que terminan en caída cambian desde la caída (`accel`, igual
  hasta t = 4.51 s, distinta desde 4.61 s).
- **Queda pendiente**: durante la caída, con el modelo tirado adelante, el hombro se sigue pasando del
  giro (hasta 73° contra 40°) y el codo se dobla de costado (hasta 26°), separación hasta 3 cm (modelo 3
  en 5 caídas; el 2, parecido). El brazo arranca al borde del cono (casi 90° de costado); sin ventana,
  con el piloto generado, arranca a 46°. Opciones: un cono de hombro más ancho de costado, el marco del
  hombro relativo al torso y no a la moto, o menos codo afuera cuando va tirado adelante (probado
  cambiando el polo del codo: casi no cambia, el pecho está tan bajo que el codo no tiene otro lugar).

### Cómo se mide

- `--telemetry` imprime, al terminar cada caída, cada articulación:
  - la flexión medida y su rango;
  - de costado y su límite;
  - el giro y su límite;
  - la separación máxima en el punto de unión;
  - los pasos fuera de rango;
  - lo mismo según Jolt (su descomposición swing-twist o el ángulo de la bisagra).

  En las rótulas vale el giro de Jolt: el propio depende del camino y con swings grandes da de más.
  **Trampas de la medición** (arregladas): `GetSwingTwist` puede dar el cuaternión con w < 0 y un giro de
  0° se leía 360° (en la v0.2.6 aparecen caídas con "giro 360 [20] fuera 359/359": son 0°); ahora se
  normaliza como hace Jolt. Y con el hueso casi de costado (más de 75°) la flexión no está definida (da
  vueltas de ±180°): no se cuenta.
- **Con el modelo hay que medir con ventana**: `--test crashloop --time 60 --telemetry --size 640 360`
  corriendo desde una carpeta que tenga el modelo (el juego lo busca en la carpeta actual, junto al exe y
  en `../`). Sin ventana el ragdoll aparece con la pose del piloto generado, que es otra.
- Caídas para probar:
  - `--test crashloop`: gas a fondo, se estrella a ~60 km/h a los ~4 s, reaparece y repite;
  - `--flat --test circle3x`: se cae despacio doblando;
  - `--respawn-after 8`: deja al piloto 8 s en el piso en vez de 3.
- Para mirarlo: capturas en serie (`--screenshot 3.9 archivo.png --shots-every 0.1`), `--nohud` y
  `--view <yaw> <pitch> <zoom>` (la cámara sigue a la pelvis con retraso: para la pose final, capturar
  ya quieto). Comandos completos en [PRUEBAS.md](PRUEBAS.md).

## Pendientes

- **El ragdoll con el modelo, tirado adelante**: se pasa del giro del hombro y el codo se dobla de
  costado durante la caída (ver "Con el modelo, los brazos saltaban al aparecer").
- **El rojo del modelo 3 se ve rosado** al sol (sobre todo la espalda): es el color de la textura de
  Tripo (carmesí, con degradés a violeta), no la luz: sin brillo (`SetGloss(0)`) y sin mipmaps da igual.
  Si molesta, se corrige la textura (o un tinte en el script), no el shader.
- **Pulgar**: la última falange se dobla -47..-50° hacia la goma (el pulgar del modelo 3 ya viene
  rodeando el puño por arriba). Visto de cerca se ve bien; si no, limitar ese giro en `BuildGripFingers`.
- **Red con versiones mezcladas**: las partes del ragdoll de otro jugador llegan armadas con el modelo de
  su PC; el nuestro se pega a ellas con nuestro `JointPose`. Con modelos distintos (v0.2.6 con el 2 y
  una nueva con el 3) los huesos quedan corridos unos centímetros respecto de sus partes. Es sólo dibujo.
