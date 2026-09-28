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
  para que la espalda quede recta (girando sólo la columna se inflaba la panza). Con el manubrio
  girado, los hombros giran con él (60%, hasta 0.5 rad).
- **No se mete en la moto** (ver "El piloto se metía en la moto"): con la forma del chasis de cada moto
  (`BikeShape`, armada en `Draw`) y vértices de prueba del modelo, `PoseOnBike` sube la cadera hasta el
  asiento, endereza el torso si atraviesa el manubrio, corre los tobillos sobre las estriberas y abre las
  rodillas hasta que quedan afuera del tanque, los plásticos, el escape y el manubrio.
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

### El piloto se metía en la moto
- **Pasaba** (lo veía seguido el usuario): con el modelo, en las cinco motos, los muslos y la cola adentro
  del asiento, rodillas y canillas adentro del tanque, los plásticos, el carenado y el escape; en el aire,
  tirado adelante con el whip o el cuerpo corrido, el torso y los brazos atravesando el manubrio; sobre la
  cola (`colazo`), la cadera y el pecho adentro del tanque y el casco en la tija.
- **Medido** con `tools/holgura` (ver "Medir la holgura"): barrido de 870 poses por moto (1059 en la de
  carreras: suelo con cuerpo adelante/atrás, de costado, manubrio y gas; pata afuera con la suspensión con el
  peso y a tope; aire con whip, cuerpo y la moto derecha o a 70°; sobre la cola; aterrizaje a tope; rápido,
  agachado y colgado). Cuánto se mete cada zona (cm), lo peor / el percentil 90 de las poses:

  | moto | casco | torso | cadera | brazo | antebrazo | mano | muslo | canilla | bota |
  |---|---|---|---|---|---|---|---|---|---|
  | Motocross 450 | 8.5/0.0 → 0.0/0.0 | 13.2/0.4 → 0.0/0.0 | 11.5/8.8 → 0.1/0.0 | 12.3/0.0 → 0.0/0.0 | 11.2/0.8 → 0.0/0.0 | 0.0/0.0 → 0.0/0.0 | 9.8/9.1 → 6.1/3.8 | 6.5/6.0 → 1.1/0.1 | 4.1/2.0 → 1.1/1.1 |
  | Carrera 1000 | 10.9/2.2 → 0.0/0.0 | 13.3/9.4 → 1.6/0.0 | 12.2/8.6 → 8.7/0.4 | 12.5/7.5 → 0.1/0.0 | 5.1/2.8 → 0.8/0.0 | 2.9/2.7 → 2.7/2.6 | 11.2/9.7 → 8.8/4.2 | 10.2/7.3 → 2.6/0.0 | 5.0/3.8 → 1.0/0.9 |
  | Trial 300 | 4.6/0.0 → 0.0/0.0 | 13.0/2.1 → 5.8/0.0 | 11.3/2.7 → 6.0/0.0 | 12.4/0.0 → 4.2/0.0 | 7.2/0.0 → 2.7/0.0 | 0.0/0.0 → 0.0/0.0 | 10.8/7.1 → 5.5/3.8 | 5.9/5.6 → 2.0/0.2 | 4.8/4.5 → 1.9/1.8 |
  | Trilheira 450 | 11.4/2.7 → 4.0/0.0 | 13.3/7.5 → 0.0/0.0 | 12.0/9.0 → 0.0/0.0 | 12.7/4.9 → 0.0/0.0 | 11.1/2.4 → 1.4/0.0 | 0.0/0.0 → 0.0/0.0 | 9.9/9.4 → 3.9/3.5 | 7.5/6.0 → 0.1/0.0 | 4.7/4.3 → 1.1/1.1 |
  | Dos tiempos 250 | 8.4/0.0 → 0.0/0.0 | 13.2/0.4 → 0.0/0.0 | 11.5/8.8 → 0.0/0.0 | 12.3/0.0 → 0.0/0.0 | 11.2/0.8 → 0.0/0.0 | 0.0/0.0 → 0.0/0.0 | 9.8/9.1 → 6.1/3.9 | 6.5/6.1 → 1.9/1.0 | 5.6/1.7 → 1.1/1.1 |

  Y en las pruebas de manejo con el modelo (`MOTOSIM_HOLGURA=1`; lo peor de `cuerpo`, `airlean`, `airrot`,
  `whip`, `flip`, `frontflip`, `wheelie`, `brake`, `sharpturn`, `colazo` y 25 s del bot, sin el primer cuadro;
  muslo = lo que más queda, casi siempre la entrepierna contra el costado del asiento):

  | moto | casco | torso | cadera | brazos | muslo | canilla | bota |
  |---|---|---|---|---|---|---|---|
  | Motocross 450 | 7.7 → 0.0 | 13.2 → 0.0 | 12.3 → 0.0 | 12.3 → 0.0 | 10.3 → 4.4 | 6.3 → 0.0 | 2.2 → 1.1 |
  | Carrera 1000 | 7.2 → 0.0 | 11.4 → 0.0 | 8.5 → 0.0 | 10.1 → 0.0 | 11.6 → 2.2 | 7.6 → 0.0 | 3.4 → 0.0 |
  | Trial 300 | 5.0 → 0.0 | 13.3 → 0.0 | 12.4 → 0.0 | 12.3 → 0.0 | 10.6 → 3.5 | 5.6 → 0.0 | 4.6 → 1.9 |
  | Trilheira 450 | 11.4 → 0.0 | 13.3 → 0.0 | 12.0 → 0.0 | 12.0 → 0.0 | 10.4 → 3.5 | 7.4 → 0.0 | 4.3 → 1.1 |
  | Dos tiempos 250 | 7.7 → 0.0 | 13.2 → 0.0 | 12.3 → 0.0 | 12.3 → 0.0 | 9.9 → 4.4 | 6.4 → 1.5 | 4.4 → 1.1 |

  No cuentan (es el contacto que tiene que haber) la mano en el puño (1.9-2.9 cm, de antes y de ahora: los
  dedos cerrados sobre la goma) ni la bota en la estribera.
- **Por qué**, cuatro cosas:
  1. La pose del estilo (`hips`, `knee`, `peg`) está hecha para el piloto generado: su pelvis es una caja que
     apoya 5 cm debajo de `hips`. En el modelo, cola y muslos quedan ~15 cm debajo del hueso `Hips`, y encima
     `kHipsDrop` la bajaba 5 cm (rodillas más dobladas): 8-9 cm adentro del asiento, en todas las motos.
  2. La IK de las piernas no sabía nada de la moto: tobillo en la estribera y la rodilla "adelante y apenas
     abierta" con un polo fijo. La cadera del modelo es angosta (articulaciones a ±8 cm; el tanque de la
     motocross mide ±12 cm y el carenado de la de carreras ±25): muslo, rodilla y canilla caían adentro.
  3. En `RiderPoseLocal` se suman los corrimientos de costado (whip 0.40·sen(35°), cuerpo 0.13, pata
     afuera, colgado) y hacia adelante (tirado adelante + pata afuera): la cadera llegaba a 36 cm del centro,
     los hombros a 55 cm (arriba de un puño) y la cola sobre el tanque.
  4. Sobre la cola, la pose "torso derecho respecto del mundo" ponía la cadera en (0, 0.05, 0.12) y los
     hombros en (0, 0.18, 0.62): con la moto a 80°, eso es a lo largo y adentro del tanque.
- **Qué se hizo** (la pose es dibujo: la física no cambia; `RiderPoseLocal` también la usa el piloto generado
  y el ragdoll al aparecer):
  - **La forma de la moto** (`RiderModel::BikeShape`): en `Draw`, con el chasis (`BikeMesh::Body`) del estilo
    que se acaba de dibujar (`r.bikeStyle`: `Bike::Draw` lo pone justo antes, también para los remotos), sin
    estriberas, pedal ni palanca (ahí apoya la bota), dos mapas de 1 cm: de cada lado la |x| más afuera de la
    superficie en cada (y, z), y el techo (la y más alta) en cada (x, z), agrandados una celda. Un punto está
    adentro si está más cerca del centro que el costado y más abajo que el techo; sale por el costado (`dl`)
    o por arriba (`dv`). Se arma una vez por estilo (5-8 ms) y se rearma si cambia el chasis (F5).
  - **Vértices de prueba** del modelo, por el hueso que más pesa (uno por posición): cola y mitad de arriba de
    los muslos (asiento), piernas, botas, pecho (torso, casco y brazos) y antebrazos. Las manos no.
  - **`PoseOnBike`**, después de lo de siempre (que las manos lleguen): (1) sube la cadera hasta que cola y
    muslos se hunden como mucho 1.5 cm (la espuma); (2) si pecho, casco o brazos atraviesan el manubrio (un
    tubo de 3.5 cm de puño a puño) o la moto, endereza el torso lo justo, sin que los brazos dejen de llegar;
    (3) cada pierna: corre el tobillo sobre la estribera (hasta 6 cm; 15 más con la pata afuera) hasta que la
    bota no se mete en el motor, y abre la rodilla girándola alrededor de la recta cadera-tobillo (de a 0.1 rad,
    afinado, hasta 0.8) hasta que muslo y canilla quedan afuera del tanque, los plásticos, el escape y el
    manubrio (con la pata afuera y el manubrio a fondo para ese lado el puño vuelve hasta la rodilla); (4) con
    el cuerpo de costado, la cadera vuelve hacia el medio lo justo (el torso queda donde lo pide la pose) y
    sube lo que falte; (5) si los brazos dejaron de llegar, se vuelve a buscar la inclinación, y si ni así
    llegan (más del 102%, la mano a ~1 cm del puño), la cadera baja lo justo: las manos mandan sobre el asiento
    (con la pata afuera en la de carreras la cadera subía hasta el tope y las manos quedaban a 15-20 cm de los
    puños; se vio con `mano_lejos` del barrido; bajando hasta el 99% quedaba sentado 7-8 cm adentro del asiento
    tirado atrás con el manubrio a fondo, y con el 102% alcanza con 1 cm de mano afuera). Los mapas se
    leen bilineales y cada vértice cuenta "por arriba" o "por el costado" con una transición suave: la pose no
    salta de un cuadro a otro. Cuesta 0.4-0.6 ms por piloto y cuadro (lo peor medido, 1.8 ms).
  - **Hombros con el manubrio**: tirado atrás y con el manubrio a fondo, la mano de afuera quedaba a 8-9 cm de
    su puño. Los hombros giran con el manubrio (60%, hasta 0.5 rad) sobre el eje del torso: 2.3 cm, y con el
    paso (5) ≤ 1.1 cm en todo el barrido. Con 80% y 0.6 rad era peor (2.8 cm: el hombro de adentro se va atrás).
  - **`RiderPoseLocal`**: la cola no pasa de la punta del asiento (`style.hips.z + max(leanHipsZ, 0.19)`); el
    corrimiento de costado se frena suave (`0.25·tanh(x/0.25)` la cadera, `0.30·tanh(x/0.30)` los hombros: lo
    chico queda igual); sobre la cola, de pie en los pedales con las piernas casi estiradas y el torso hacia el
    manubrio (cadera (0, 0.34, -0.08), hombros (0, 0.66, 0.30), rodillas (±0.19, -0.03, 0.05)): con la moto a
    80°, en el mundo queda unos 30° tirado atrás, colgado del manubrio.
- **Se comprobó**: el barrido y las pruebas de arriba; capturas antes y después con el modelo (de costado, de
  frente, whip, colazo, las cinco motos) y vistas ortogonales con lo que se mete marcado (`tools/holgura/vista.py`);
  regresión idéntica contra la v0.2.7 (`brakeslide`, `cuerpo`, `flip`, `whip`, `wheelie`, `accel`, `colazo`,
  `--bot --time 40`) y el bot 1:08.73 / 1:08.42. Ragdoll con el modelo (`crashloop`, 4-5 caídas): igual que
  antes (hombro, giro según Jolt 52-54° antes y 54-57° ahora con límite 40°; separación hasta 3 cm).
- **Queda**:
  - la entrepierna contra el costado del asiento, 2-4 cm (percentil 90: 3.5-3.9 cm; hasta 5-6 en poses raras
    con el cuerpo de costado): el modelo tiene los muslos juntos y el asiento mide 24 cm; queda tapado por las
    piernas;
  - tirado atrás con el manubrio a fondo (y más con el cuerpo de costado) los brazos no dan: la cadera queda
    más baja para que las manos lleguen y los muslos se hunden hasta 6 cm (motocross, 2T); en la de carreras,
    con la pata afuera y tirado atrás, la cola hasta 8.7 cm (percentil 90: 0.4). Con brazos más largos se
    arreglaría (ver "Proporciones");
  - en la de carreras, la canilla contra el semimanillar con la pata afuera y el manubrio a fondo (2.6 cm) y la
    mano contra el borde del carenado (2.5 cm: el puño está pegado al carenado, es de la moto);
  - la trial en el aire, tirado adelante con el whip a fondo: torso y cadera contra el manubrio hasta 6 cm (los
    brazos ya no llegan si se endereza más); en las pruebas de manejo no aparece;
  - el primer cuadro de cada estilo (antes de que se dibuje la moto) sale sin ajustar.
- **Proporciones**: no hace falta otro modelo. El 3 mide 1.70 m con brazos cortos (hombro a muñeca 0.52 m:
  brazo 0.29 + antebrazo 0.23, 31% del alto; lo común es 33%), piernas cortas (cadera a tobillo 0.74 m: muslo
  0.36 + canilla 0.38, 44%; lo común, 49%) y angosto (hombros a 0.30 m entre articulaciones, caderas a 0.16). Con el ajuste
  entra en las cinco motos; lo único que pide más brazo es tirado atrás con el manubrio a fondo (para que las
  manos lleguen, la cadera no sube del todo y los muslos quedan hasta 6 cm en el asiento). Agrandarlo entero
  no se probó: el torso crece igual que los brazos y el pecho se acerca al manubrio. Si se hace uno nuevo, que
  tenga: 1.75 m; hombro-muñeca 0.58 m (brazo 0.32 + antebrazo
  0.26); cadera-tobillo 0.85 m (muslo 0.43 + canilla 0.42), la articulación de la cadera a 0.90 m del piso;
  hombros (articulaciones) a 0.36-0.38 m entre sí; caderas a 0.18-0.20 m; muslos no muy gruesos (la
  entrepierna es lo que queda contra el asiento). El ajuste (asiento, rodillas, tobillos) se acomoda solo a
  otras medidas; el ragdoll se arma con sus articulaciones (`JointPose`) y en red cada PC usa las del suyo.

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

## Medir la holgura (tools/holgura)

Cuánto se mete el piloto (el modelo, skineado) en la moto. No va en el juego: `python tools/holgura/armar.py`
arma una copia en `build-msvc-holgura/` (src, mods, `RiderClearance.cpp/.h` de `tools/holgura` y un enganche
en `Game::Draw` después de `riderModel.Skin()`) y la compila; el exe se corre **con ventana** desde la raíz
del proyecto (ahí están el modelo y `mods/`). Si `Game.cpp` cambia y el enganche no entra, el script avisa.
- **Qué mide**, en cada pose: (A) vértices del piloto adentro de cada primitiva cerrada de las piezas de la
  moto (chasis, basculante, tijas, botellas, manubrio, guardabarros, cubiertas), con la profundidad hasta su
  superficie; (B) puntos de la superficie de la moto cada 1.2 cm adentro de las piezas del piloto (lo que A
  no ve: un caño fino que atraviesa un muslo). Por zona del cuerpo (el hueso que más pesa); la mano en el
  puño y la bota en la estribera, aparte.
- **Barrido**: `bash tools/holgura/barrido.sh build-msvc-holgura/b/motocross.exe <carpeta> <etiqueta>` (las
  cinco motos, 3-6 min cada una; la de carreras, más) y `python tools/holgura/tabla.py <carpeta> antes
  despues` para la tabla. El CSV trae por pose también el ajuste (`sube`, `centro`, `abre`, `tob`), lo que
  queda según las pruebas del propio ajuste (`pAsiento`, `pPierna`, `pBota`, `pManubrio`), el alcance del
  brazo y cuánto le falta a la mano para su puño (`mano_lejos`).
- **En una prueba**: `MOTOSIM_HOLGURA=1 <exe> --flat --test cuerpo ...`: cada 0.05 s imprime lo que pasa de
  1 cm y al salir lo peor de toda la prueba (el primer cuadro sale sin ajustar: descartarlo).
- **Para mirarlo**: `MOTOSIM_HOLGURA_FILTRO="suelo L+0.0 S+0 st+0.0 g0"` (alternativas con `|`) y
  `MOTOSIM_HOLGURA_VOLCAR=prefijo` vuelcan cada pose; `python tools/holgura/vista.py prefijo_000.bin salida.png
  [--zoom x y z tam] [--vistas izq,der,frente,atras,arriba]` dibuja la moto y el piloto de costado, de frente
  y de arriba, con lo que se mete en rojo (A) y amarillo (B). `MOTOSIM_HOLGURA_DEBUG=1` lista las primitivas
  abiertas y el perfil de la forma de la moto que usa el ajuste.
- **Lo que costó** (trampas de la medición):
  - La malla del piloto no es cerrada: el cuerpo tiene agujeros en el cuello, los puños y los tobillos (los
    tapan el casco, los guantes y las botas) y el casco en la base del cuello. Con un rayo por paridad, los
    puntos adentro que salían por un agujero contaban afuera. Se tapan los bordes (se encadenan las aristas
    que están en un solo triángulo y se cierra cada lazo con un abanico). Primero hay que soldar los vértices
    repetidos (costuras de UV, caras de cajas): sin soldar, cada cara de una caja es una pieza "abierta".
  - Las piezas de la moto son primitivas cerradas, salvo tubos sin tapas y láminas: se tapan sólo las bocas
    chicas (menos de 12 cm). Tapando la lámina del carenado quedaba un volumen falso con los semimanillares y
    las manos adentro (daba 8-10 cm de "mano adentro del chasis").
  - Un solo rayo da falsos adentro cuando roza una arista (salió un punto "34 cm adentro del guante", 25 cm
    debajo de él). Se usan tres rayos torcidos y vale si dos dan impar.
  - Velocidad: el primer intento tardaba 1.2 s por pose (la confirmación y la profundidad recorrían todos los
    triángulos del piloto). Con los triángulos proyectados a lo largo de cada rayo en grillas de 2 cm y una
    grilla 3D de 3 cm para la distancia: ~0.2 s por pose.
  - Sin ventana el modelo no se carga, y la medición necesita las mallas de la moto de `Renderer`: todo con
    ventana (`--size 640 360`).

## Pendientes

- **El ragdoll con el modelo, tirado adelante**: se pasa del giro del hombro y el codo se dobla de
  costado durante la caída (ver "Con el modelo, los brazos saltaban al aparecer"). Medido otra vez con el
  piloto que ya no se mete en la moto (`crashloop`, 4-5 caídas): giro del hombro según Jolt 52-54° antes y
  54-57° ahora (límite 40°), codo de costado +10..13°, separación hasta 3 cm: no cambió. No se tocó.
- **El piloto contra la moto, lo que queda**: ver "El piloto se metía en la moto", "Queda".
- **El rojo del modelo 3 se ve rosado** al sol (sobre todo la espalda): es el color de la textura de
  Tripo (carmesí, con degradés a violeta), no la luz: sin brillo (`SetGloss(0)`) y sin mipmaps da igual.
  Si molesta, se corrige la textura (o un tinte en el script), no el shader.
- **Pulgar**: la última falange se dobla -47..-50° hacia la goma (el pulgar del modelo 3 ya viene
  rodeando el puño por arriba). Visto de cerca se ve bien; si no, limitar ese giro en `BuildGripFingers`.
- **Red con versiones mezcladas**: las partes del ragdoll de otro jugador llegan armadas con el modelo de
  su PC; el nuestro se pega a ellas con nuestro `JointPose`. Con modelos distintos (v0.2.6 con el 2 y
  una nueva con el 3) los huesos quedan corridos unos centímetros respecto de sus partes. Es sólo dibujo.
