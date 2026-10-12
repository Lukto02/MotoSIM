# Piloto: modelo, IK y ragdoll

El piloto que se ve es el modelo 2 (desde la v0.3.4, con la pose, la IK y los dedos de la primera v0.3.1; ver
[PORT-ORIGINAL031.md](PORT-ORIGINAL031.md)): `Low_Poly_Motorcyclist_2_lowpoly.gltf`, el mismo modelo con un tercio
menos de triángulos (ver "Menos triángulos: el modelo 2 reducido"), y si no está, el original
`Low_Poly_Motorcyclist_2_rigged.gltf` (la mano en un solo hueso; los dedos son virtuales, `BuildGrip`). Lo de
abajo sobre el modelo 3 (`Low_Poly_Motorcyclist_3_rigged.gltf`, riggeado con `tools/riggear_piloto.py`) es de
cuando era el principal; hoy queda en el proyecto sin usarse. **Los modelos viejos quedan en el proyecto a pedido
del usuario** ("no borres el viejo, dejalo de backup"): no se borran aunque no sean el principal. Si no se
encuentra ninguno, se usa el piloto generado
(`Rider.cpp`; F9 cambia entre los dos, `--genrider` arranca con el generado). **Las pruebas visuales del
piloto se hacen con el modelo**: es el principal y el que muestra los problemas de la malla.

**Sin ventana (`--headless`) el modelo no se carga** (no hay GPU para la malla ni la textura): el ragdoll
aparece con la pose del piloto generado y no con `JointPose`. Todo lo del ragdoll con el modelo (lo que
ve el jugador) se mide con ventana; ver "Cómo se mide".

## Sobre la moto (RiderModel.cpp)

- `Bike::RiderPoseLocal` da la pose (espacio de la moto): cadera, hombros, cabeza, rodillas, tobillos,
  hombros, codos y puños, según el estilo, el cuerpo y la velocidad.
- `RiderModel::PoseOnBike` la sigue por IK de dos huesos, con polos para codos y rodillas (en la de carreras
  los codos van abajo, cerca del tanque: `elbowsIn`). Si el brazo queda lejos del manubrio, primero la
  clavícula adelanta el hombro (hasta `kClavicleReach`, 20°: ~4.4 cm), después inclina más el torso (hasta
  `kMaxExtraPitch`) y después adelanta la cadera: nunca suelta el manubrio. La pelvis toma el 75% de la inclinación y cada vértebra un poco,
  para que la espalda quede recta (girando sólo la columna se inflaba la panza). Con el manubrio
  girado, los hombros giran con él (60%, hasta 0.5 rad).
- **No se mete en la moto** (ver "El piloto se metía en la moto"; se había perdido en la v0.3.4 y volvió en la v0.3.10:
  ver "Los pendientes de la v0.3.9"): con la forma del chasis de cada moto
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
- **Cuerpo con masa** (`RiderMotion`): la pose de arriba no se dibuja tal cual: la siguen resortes con la inercia
  de la moto (aterrizajes, acelerar y frenar, curva, whip, derrape) y la cabeza mira hacia donde va. Ver "Cuerpo con
  masa".
- **La de carreras** (`RacePose`): colgado, el cuerpo más adentro y la rodilla al piso; agachado, los codos cerrados; frenando,
  sale de atrás de la cúpula. Ver "La Carrera 1000".
- **Sin saltos**: el ajuste a la moto (`Fit`) se busca cada cuadro y lo que se dibuja lo sigue con un resorte; muslo y canilla
  con el giro fijado por el frente de la rodilla. `MOTOSIM_SALTOS=1` mide los saltos por cuadro. Ver "Saltos de la pose".

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
- **Proporciones** (del modelo 3, cuando era el principal; el 2 ya tiene los brazos alargados: ver "Brazos más largos"):
  no hace falta otro modelo. El 3 mide 1.70 m con brazos cortos (hombro a muñeca 0.52 m:
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

### Cuerpo con masa (RiderMotion, octubre de 2026)
- **Pedido**: "pulir las animaciones, que todo se sienta smooth y físico y no scripteado". Antes el piloto pasaba de
  una pose a otra: `RiderPoseLocal` + IK, sin inercia. `riderLean` y `legOut` van en rampa (velocidad constante:
  arranca y frena en seco), la pose de pie sobre la cola entra por el cabeceo, la cabeza miraba siempre al frente de
  la moto (en la curva y en el whip, torcida con ella), al aterrizar o frenar el cuerpo no se enteraba y al caerse el
  modelo saltaba a la pose sin reacciones.
- **Qué se hizo**: `RiderMotion` (en `RiderModel.*`), entre `RiderPoseLocal` y la IK. **Es sólo dibujo**: el ragdoll
  sigue apareciendo con la pose de siempre y sin ventana no corre (la telemetría da idéntica). Cada grado de
  libertad es un resorte amortiguado `x'' = w²(objetivo - x) - 2·zeta·w·x' + fuerza` (pasos de 1/240 s: estable con
  cualquier cuadro), y las fuerzas son lo que siente el cuerpo:
  - **Lo que mide**: la transformación de la moto **como se dibuja** (interpolada), cuadro a cuadro, en tiempo de
    simulación (en pausa no se mueve; en cámara lenta va lento). Velocidad y aceleración de un punto en la cadera
    (con el giro de la moto) y velocidad y aceleración angular, filtradas (35 y 40 ms). Lo mismo para la moto propia
    y la de otro jugador (para ésa, filtros 2.5 veces más largos: trae las correcciones de la red). No hace falta
    ningún dato nuevo de la moto ni cambiar el protocolo; `ground` (alguna rueda apoyada) sale de las ruedas o, en
    red, de las banderas `kFrontGround`/`kRearGround`.
  - **Qué aceleración**: de costado la "propia" (con la gravedad: en una curva bien inclinada da ~0, el piloto no se
    va para afuera; con la cinemática daba 5.7 m/s² hacia adentro y el cuerpo se iba 2.5 cm afuera en toda la curva);
    adelante la de la moto a lo largo, sin la gravedad (en una subida no se tira atrás); arriba la propia menos 1 g,
    pasada por un filtro pasa-altos de 0.6 s (una carga sostenida la sostienen los músculos; en el aire es -1 g y se
    absorbe), y lo que alivia cuenta la mitad (estira menos de lo que se hunde). En el aire, ni de costado ni
    adelante (cae junto con la moto).
  - **Cadera** (`HipsX/Y/Z`): w 9 / 12 / 8, zeta 0.8 / 0.7 / 0.75, ganancia 0.35 / 1 / 0.5, saturación suave (tanh)
    a 4 / 5.5 / 5 cm (arriba, 3). Aterrizando de 3 m se hunde 5.5 cm; acelerando a fondo va 5-6 cm atrás; frenando,
    5-6 adelante. Con 7.5 cm de hundimiento la cola entraba en el asiento; se bajó a 5.5 y el resto lo absorbe el torso.
  - **Torso** (`TorsoPitch/Roll`, giro de hombros alrededor de la cadera): w 7.5, zeta 0.65. Péndulo: 1.5 rad/s² por
    m/s² adelante (acelerando ~10° atrás, frenando ~10-15° adelante); el 60% de la aceleración angular de la moto no
    la acompaña al instante (la moto cabecea o se tumba y el torso se queda); se dobla 3 rad por m que se hunde la
    cadera (absorbe con todo el cuerpo: codos doblados, casco adelante); 0.12 rad hacia adentro por g de curva; en el
    aire busca la vertical con el 25% del cabeceo y el tumbado de la moto (whip: la moto se acuesta, él no tanto).
    Satura a 18° / 16°. Agachado (carreras, rápido) la aceleración casi no lo levanta (30%): se sostiene con las rodillas.
  - **Cabeza** (`HeadYaw/Pitch/Roll`): w 10, zeta 0.85. Mira hacia donde va: la velocidad sobre el piso (adelante si
    va despacio, para atrás o con la cola afuera, que ahí la velocidad apunta afuera de la curva), girada lo que gira
    la curva en 0.45 s; 11° abajo en el piso y hacia el aterrizaje en el aire; endereza el 45% del tumbado (horizonte).
    Topes: ±57° de costado, -52°..+20° arriba/abajo, ±23° de giro. El cuello toma la mitad (con el giro completo, no
    sólo la cara: `SolveBody`, `headFree`).
  - **Piernas**: `legOut` de la pose pasa por un resorte crítico (w 7) y la pata sale también **con la cola afuera**:
    12°-24° entre la moto y la velocidad, de 2.5 a 24 m/s, apoyada, la pierna de la punta de la moto (no en la de
    carreras, que se cuelga). Misma forma que `RiderPoseLocal` (sentado adelante, arco por afuera de los plásticos).
  - **La pose pedida, suavizada**: cadera, hombros y `lean` siguen a `RiderPoseLocal` con un resorte crítico (w 13,
    ~0.25 s): las rampas y los saltos de la pose (de pie sobre la cola, `supported`) arrancan y frenan suave.
  - **Al caerse**: `BindToParts` guarda los huesos como se veían respecto de cada parte y en 0.35 s (smoothstep) pasa a
    los de la pose con que se armó el ragdoll. Sin salto en el primer cuadro y el ragdoll igual que antes.
  - **Arranque**: al aparecer (o reaparecer, o si salta más de 1 m o 0.6 rad en un cuadro, o el tiempo vuelve atrás)
    arranca quieto en la pose pedida y los dos primeros cuadros toman la velocidad que trae sin aceleración.
- **Trampas**:
  - Con la velocidad de 0 a la que trae al aparecer (`--drop`), el primer cuadro daba 56 m/s² y el torso se iba 27°
    atrás en el aire.
  - **El dt recortado** (a 0.05 s) con un cuadro largo (guardar una captura tarda ~0.1 s) daba la velocidad al doble
    y un "golpe" de 58 m/s²: se usa el dt real (hasta 0.25 s) y los resortes van en pasos chicos.
  - Las capturas con la cámara de persecución no sirven para comparar poses (`--drop` arranca con la cámara arriba, el
    sacudón de los aterrizajes, el resorte): se compararon con una copia con la cámara rígida a la moto (ver abajo).
- **Para sumar un truco** (hecho: ver "Trucos en el aire"; al final entró por la pose, `Bike::ApplyTrickPose`, porque
  también la usan la física y el ragdoll): otra contribución a los objetivos de los mismos resortes
  con un peso que también pasa por un resorte (0 a 1): cadera y torso como acá, y para soltar una pierna o las manos,
  un objetivo de tobillo / mano con su peso, como `legOut` (la IK ya lleva el tobillo a donde se le pida). Así entra y
  sale sin cortes y encima siguen las reacciones (un aterrizaje en medio del truco).
- **Se comprobó** (Motocross 450 salvo donde se dice; `MOTOSIM_PILOTO_LOG=1` imprime cada cuadro, con ventana,
  `PILOTO <id> t= us= a= al= cadera= torso= cabeza= afuera= deriva= alcance= +torso= +cadera=`):
  - Capturas antes y después (mismo exe salvo el piloto, cámara rígida): aceleración y frenada (`accel`), aterrizaje
    de 3 m (`--drop 3`), whip (`--drop 3 --test whip`), curva de frente (`turn`: la cabeza mira hacia adentro y queda
    más derecha), derrape con freno de mano (`teclas:W+40,_D2,W1`), cuerpo atrás (`pose`), la caída de `crashloop`,
    la de carreras (`accel`, `giro60`), la Trial cayendo de 3 m, `colazo` en la favela (Trilheira) y un salto del bot.
  - Saltos cuadro a cuadro (sin los cuadros largos de las capturas), 20 s de bot en red con 40 ms de jitter, el
    propio y el del otro: cadera ≤ 2.3 cm, torso ≤ 3.7°, cabeza ≤ 3° por cuadro.
  - Costo: `PoseOnBike` entero (con la IK) 4-8 µs de mediana, 34 µs el percentil 99 (antes la IK sola): los fps no
    cambian.
  - Física: `tools/regresion.sh` contra la v0.3.8, idéntico en `brakeslide`, `cuerpo`, `flip`, `whip`, `wheelie`,
    `--bot --time 40`, `accel`, `crashloop`, `colazo` (favela) y `airlean`; el bot 1:04.26 / 1:04.10; y `crashloop`
    **con ventana** (el ragdoll con el modelo): la telemetría da idéntica a la de antes.
- **Queda**: el piloto generado (F9, `--genrider`) no lo tiene (lo dibuja `Bike::Draw` con `RiderPoseLocal`). La
  inclinación hacia adentro en la curva es chica a propósito (la pose de pata afuera tira el torso afuera). No se midió
  la holgura (`tools/holgura`) con las reacciones: la cadera se hunde como mucho 5.5 cm y en las capturas no se ve
  meterse, pero en la de carreras colgada o la Trial convendría barrerlo.

### Trucos en el aire (octubre de 2026)
- **Pedido**: "agregá el superman para hacer con un botón, el resto si se pueden también"; smooth y físico, no
  scripteado, sin puntaje ni HUD de trucos.
- **Botón**: **Shift o Ctrl** (cualquiera de los dos lados) en el teclado y **B** en el joystick. Estaban libres en el
  juego (Shift sólo se usa con F5, Ctrl sólo en el menú para pegar; B sólo en el menú, "volver"). Los dos sirven a la vez:
  Shift y Ctrl, porque apretar Shift cinco veces seguidas abre las Teclas especiales de Windows, y por AnyDesk (donde
  algunas letras no llegan) los modificadores sí pasan; no se probó por AnyDesk.
- **Controles revisados** (pedido "dejá los controles ideales"): quedan Shift (el principal en la ayuda) o Ctrl, y B.
  - **Shift**: es el modificador natural (meñique izquierdo, al lado de A y Z, con la mano en WASD). El problema de las
    Teclas especiales se arregló de raíz (`src/SystemKeys.*`): mientras la ventana del juego tiene el foco se apagan los
    atajos de accesibilidad de Windows (Shift cinco veces, Shift derecho 8 s = Teclas filtro, Bloq Num 5 s), como
    recomienda Microsoft para los juegos ("Disabling Shortcut Keys in Games"); sólo los atajos, y sólo si la función
    está apagada (si el jugador usa Teclas especiales, no se toca). Vuelven como estaban al perder el foco y al salir;
    el cambio es de la sesión (sin `SPIF_UPDATEINIFILE`): si el juego se cierra mal, vuelve al reiniciar la sesión. En
    las pruebas automáticas (`--size`, `--screenshot`, `--test`, `--bot`) no se toca. En Mac no hace nada.
    Comprobado con un programa aparte que llama a las mismas funciones y lee `SPI_GET*`: con foco, sticky `0x1fe` →
    `0x1f2` (sin `SKF_HOTKEYACTIVE`), toggle `0x03e` → `0x032`, filter `0x07e` → `0x072`; sin foco y al salir, los
    valores del usuario de nuevo.
  - **Ctrl** queda como alternativa (Ctrl + Esc abre el menú Inicio de Windows: no es para el principal).
  - **Joystick, B**: en el aire el pulgar derecho está libre (A es el freno trasero, que no se usa en el aire); X cambia
    la caja e Y reaparece (apretarlos sin querer es peor), LB / RB son los cambios y apretar un stick mientras se lo
    mueve es incómodo. En los juegos de motocross con trucos (la serie MX vs ATV) también son un botón que se mantiene
    más una dirección; MXGP no tiene trucos. Se dejó B.
- **Cuál truco**: el botón solo, **superman**; con la dirección (stick izquierdo, o A D ↑ ↓): ↑ **heel clicker**, ↓
  **no-hander**, ← **nac-nac** (la pierna derecha pasa por arriba de la cola a la izquierda), → **can-can** (la izquierda
  cruza por delante, al costado derecho). Es la dirección al apretarlo o en los primeros 0.2 s (botón y enseguida la
  dirección también vale); después queda fijo hasta soltar. Para otro truco, soltar y volver a apretar: el cuerpo vuelve
  primero y después arranca el nuevo. Se eligió la dirección del stick izquierdo (no otro botón) porque es el mismo
  pulgar que hace el whip y el mortal, y el botón cambia lo que significa.
  Desde la v0.3.10, en diagonal: ↘ nac-nac a la derecha y ↖ can-can a la izquierda (ver "Los pendientes de la v0.3.9").
- **Sin disparos accidentales** (`Game::TrickButton`, y en la física `PrePhysics`): con el botón apretado en el aire el
  stick no tira whip ni mortal (dirección, cuerpo y costado en 0). Al soltar, el stick sigue sin contar en el aire hasta
  que pasa por el centro (o se toca el piso): el que suelta con el stick tirado no se encuentra un whip que no pidió. En
  el piso el botón no hace nada (ni bloquea la dirección); el truco arranca recién con 0.12 s de vuelo.
- **Físico** (`Bike::UpdateTrick`, BikePhysics.cpp; ver [FISICA.md](FISICA.md), "Trucos: el cuerpo mueve la moto"): el
  truco es un resorte crítico 0..1 (`trickAmt`, los músculos: arranca y llega suave) y el cuerpo, 11 masas (las del
  ragdoll, 75 kg) en la pose del truco (`Bike::TrickBody`). La moto recibe la reacción de mover esas masas (se conserva el
  momento angular) y, estirado, el conjunto tiene más inercia. Superman: la trompa sube ~4.5° al estirarse (+3° netos al
  volver); nac-nac y can-can rolan 2-3° para un lado al salir y para el otro al volver. Con las poses nuevas de superman
  y heel clicker (ver "Brazos más largos"; `TrickBody` usa la misma pose) cambian poco: superman, contra la misma caída
  sin truco, +5.8° a 1.3 s de apretar (antes +6.5°; al arrancar baja 1° porque el cuerpo va más atrás) y +2.7° netos
  (antes +2.2°); heel clicker +7.7° (antes +6.5°) y +2.1° netos (antes +2.7°). Los otros tres, idénticos.
- **Si no volvés**: tocar el piso (o cualquier contacto) con más del 40% del truco (`trick_crash`) es caída: el ragdoll
  aparece en la pose del truco (superman: estirado sobre la moto); entre 12% y 40% (`trick_hit`), golpe: sacudón de cámara y
  el balance afloja (K -70%, D -50%) y se recupera en 0.6 s. Volver (de soltar a menos de 12%): superman y heel clicker
  0.42 s, nac-nac y can-can 0.38, no-hander 0.31 (con `trick_speed` 9; a menos de 40%: 0.25 / 0.23 / 0.19 s).
- **Pose** (`Bike::ApplyTrickPose`, Bike.cpp): sale del estilo (asiento, estriberas, manubrio), así sirve en las cinco
  motos; la usan el modelo (por IK), el generado, el ragdoll al caer y la física (con la pose neutra). Cadera y hombros van
  hacia su objetivo; cada pie o mano suelta va por una curva (Bézier) que esquiva la cola, el asiento o el tanque, y la
  rodilla / el codo salen de una IK de dos huesos con los largos de la pose (el generado y el ragdoll se arman con ellos);
  la diferencia con la rodilla de siempre se va en el primer 30% (sin salto al empezar). `RiderPose` lleva `trick`,
  `trickHips/Shoulders` (lo que el truco corrió, ya suave: `RiderMotion` no lo vuelve a suavizar, quedaba atrás de las
  piernas), `legFree`, `toe` y `handFree`. En el modelo (`SolveBody`): sin los ajustes de la posición de ataque, la
  rodilla dobla hacia donde la dobla la pose, el pie apunta a `toe`, la mano suelta sigue al antebrazo con la palma abajo
  y los dedos casi abiertos (`FingerGrip`, por mano); las reacciones del torso valen 40% (el cuerpo lo sostienen los brazos).
- **Trampa: la pierna del modelo no es la de la pose**. Con el tobillo donde lo pide la pose (proporciones del
  generado), el modelo 2 (pierna más larga) quedaba con la rodilla doblada 90° en el heel clicker. Suelta, la pierna de
  la pose (cadera → tobillo) se lleva al largo de la del modelo. Se vio comparando con `--genrider` (la pose estaba bien).
- **Heel clicker**: con los brazos cortos del modelo no se podía tirar el torso atrás y seguir agarrado: quedaba en
  carpa (torso 33° adelante). Con los brazos largos (ver "Brazos más largos") va con la cola levantada en la punta del
  asiento (`sh + (0, 0.06, 0.28)`), el torso derecho (`hipsT + (0, 0.45, -0.03)`; la IK lo inclina 2° en la motocross,
  12° en la de carreras) y los pies arriba y adelante del manubrio, a ~45° desde la cadera (`gc + (0, 0.72, 0.20)`),
  entre los brazos.
- **Superman**: los hombros en `gc + (0, 0.12, -0.51)` (antes `-0.40`, con los brazos doblados): brazos estirados
  (alcance 0.88-0.91 en las cinco motos con el manubrio derecho). Con `-0.54` daba 0.93-0.96, pero en la de carreras con
  el manubrio girado la IK bajaba el torso contra la cúpula (6 cm, medido con `tools/holgura`, grupo `trucos`).
- **Red**: el truco y cuánto (dos bytes) van **al final** del estado: una versión anterior lee lo suyo y no mira lo que
  sobra (y el anfitrión reenvía el paquete tal cual), así que el protocolo sigue en 7 (ver [RED.md](RED.md)).
- **Se comprobó** (Motocross 450, modelo 2):
  - Pruebas `--flat --drop 3 --test trucoNOMBRE[pP][sS][x]` (superman, heel, nohander, nacnac, cancan; aprieta a los P s,
    0.3, y suelta a los S, 0.8; x: no suelta) con líneas `TRUCO` en la telemetría: con 1.2 s de truco toca con 12% (a
    tiempo), 1.32 → 25% golpe, 1.38 → 36% golpe, 1.45 → 56% caída, sin soltar → caída en los cinco.
  - Reacción: la misma prueba con el truco y sin él (`trucosupermanp9`), cabeceo y rolido cada 0.1 s (arriba). Sin la
    inercia del cuerpo para los demás torques (sólo la reacción), después del truco la moto seguía girando 35% más rápido:
    ver FISICA.md.
  - Capturas con el modelo y cámara rígida (copia `compilaciones/piloto-cam-src`, `MOTOSIM_CAM`; ver abajo): cada truco
    de costado, atrás, frente y 3/4, secuencias de entrada y salida cada 0.1 s (sin meterse en la moto ni codos o
    rodillas al revés) y las caídas de superman y nac-nac sin soltar. Superman con el generado para comparar.
  - Red: anfitrión con `--trucos superman` (el bot aprieta el botón en cada salto) y el invitado ve `truco=1 NN%` en su
    línea `net` y el piloto remoto con las piernas atrás y su caída (capturas con ventana); `tools/red.py` contra la
    v0.3.9-beta en los dos sentidos y un anfitrión nuevo haciendo nac-nac con un invitado v0.3.9-beta: conectan igual.
  - Regresión (`tools/regresion.sh`) idéntica contra la v0.3.9-beta y contra el árbol sin trucos en 16 pruebas (las de
    la lista, `airlean`, `airrot`, `crashloop`, `colazo`, `circle3x`, `frenada280`, bots en la favela y con la 600); bots
    450 1:04.41 / 1:04.24, 600 1:04.52 / 1:04.34. Sin el botón no corre nada nuevo (todo detrás de `trickAmt > 0`).
- **Quedaba** (resuelto en la v0.3.10, ver "Los pendientes de la v0.3.9"): el piloto en superman cayendo sobre la moto
  quedaba un rato acostado sobre ella; la cabeza no miraba atrás en el nac-nac; sin variantes espejadas.

### Los pendientes de la v0.3.9 (v0.3.10, octubre de 2026)
Cinco cosas que habían quedado de "Cuerpo con masa", "Trucos en el aire", "Brazos más largos" y "Medir la holgura". Todo
con el modelo 2 reducido de brazos largos; capturas con la cámara rígida en `compilaciones/capturas-piloto-pendientes/`
(`antes/` = v0.3.9, `d3/`, `d2/`, `trucos/` = ahora; grillas `g_*.png`; holgura en `holgura/`, tabla en `holgura/tabla.md`).

**1. Cola y muslos adentro del asiento (el ajuste a la moto volvió)**
- **Pasaba**: en todas las motos la cola y los muslos 7-10 cm adentro del asiento (la carreras, hasta 12), parado y sentado.
- **Por qué**: al portar la pose de la primera 0.3.1 (v0.3.4) se perdió todo `BikeShape` / `Fit` de "El piloto se metía en
  la moto" y quedó `kHipsDrop` (la cadera 5 cm más abajo, que el ajuste después subía). Con los brazos largos, encima, la
  IK ya no corría la cadera adelante y arriba tirado atrás: 0.5-1.5 cm más adentro.
- **Qué se hizo**: se trajo el ajuste del historial (`git show c115c4d:src/RiderModel.cpp`) a la IK de ahora (clavícula,
  trucos, cabeza libre, codos de la de carreras): `BikeShape` (se arma en `Draw`, una por `RiderModel`: cada jugador remoto
  tiene el suyo), los vértices de prueba (`BuildProbes`), la cadera que sube hasta el asiento, el pecho que no atraviesa el
  manubrio (no en un truco: su pose ya lo evita), rodillas y tobillos afuera del tanque (no la pierna suelta de un truco) y
  los hombros que giran con el manubrio (`kShoulderTurn`; con las manos sueltas, no). Todo en `Fit` (con `pitch`, `shift`,
  `sideIn` y `backIn` de la IK). Cuesta 0.25-0.5 ms por piloto y cuadro (`us=` del log).
- **Se comprobó** con `tools/holgura` (barrido de ~900 poses por moto, cm, lo peor / percentil 90; antes = v0.3.9):

  | moto | cadera | muslo | canilla | bota | torso |
  |---|---|---|---|---|---|
  | Motocross 450 y 600 | 9.9/7.3 → 4.6/1.1 | 9.8/7.5 → 4.6/3.5 | 5.9/5.6 → 3.2/0.9 | 3.0/2.9 → 1.3/1.1 | 0 → 0 |
  | Carrera 1000 | 12.8/10.7 → 5.2/3.5 | 12.4/10.1 → 5.5/3.8 | 11.8/6.0 → 5.3/0.0 | 5.4/4.4 → 4.1/1.5 | 12.9/10.6 → 1.2/0.0 |
  | Trial 300 | 6.7/4.7 → 2.9/0.6 | 7.6/6.9 → 6.7/6.1 | 6.0/5.7 → 2.5/0.9 | 4.5/4.4 → 1.9/1.6 | 0.6 → 0.2 |
  | Trilheira 450 | 10.6/9.0 → 2.9/1.2 | 10.6/9.1 → 5.6/4.9 | 6.7/5.7 → 3.8/1.1 | 4.5/4.3 → 2.4/1.3 | 4.9 → 0 |
  | Dos tiempos 250 | 10.1/8.3 → 4.6/1.1 | 10.3/8.4 → 5.2/3.6 | 6.7/5.7 → 3.2/0.7 | 3.6/3.5 → 4.8/1.1 | 0 → 0 |

  Y en las pruebas de manejo, con el cuerpo con masa (`MOTOSIM_HOLGURA=1`, lo peor desde 0.1 s; cadera / muslo):
  motocross `pose` 8.4/8.5 → 1.3/3.2, bot 25 s 7.5/7.6 → 2.2/3.6, `whip` 7.2/7.5 → 1.6/3.9; carreras bot 12.7/12.8 → 3.5/3.7
  (y torso, brazos, casco 11-12 → 0); Trilheira `pose` 9.4/9.4 → menos de 1/3.4; Trial bot 1.2/6.5 → 1.2/3.4. Capturas de costado y
  3/4 de las seis motos (neutro, tirado atrás, adelante): sentado sobre el asiento.
- **Queda**: el muslo, 3-4 cm (percentil 90) contra el costado del asiento (la entrepierna: los muslos del modelo van juntos,
  como antes de la v0.3.4); en la Trial 6 cm, pero con el cuerpo de costado (`S±1`), que en la Trial no se usa (sentado
  neutro, 2 cm); lo peor (4.5-5.5 cm) con la pata afuera, tirado atrás y el manubrio a fondo, donde la cadera no sube del
  todo para que las manos lleguen (`kLiftReach`). La pierna suelta del nac-nac y del can-can roza la cola al pasar (bota
  y canilla 4-5 cm en la de carreras): no se ajusta, va por su curva.

**2. Whip con las manos en el manubrio**
- **Pasaba**: con el whip a fondo el cuerpo se iba al costado (cadera del modelo a 22.6 cm del centro de la moto, cuello a
  36.8) y para llegar a los puños la IK inclinaba el torso al tope (+26°) y adelantaba la cadera al tope (30 cm); aun así
  una mano quedaba a 1 cm de su puño (`--flat --drop 3 --test whip`, log con una copia de la v0.3.9 que imprime la mano).
- **Por qué**: la pose del whip (`RiderPoseLocal031`: cadera 0.40·sen(inclinación), hombros 0.62) es la del piloto
  generado, de brazos elásticos; y la IK sólo sabía inclinar adelante y correr la cadera adelante, no volver de costado.
- **Qué se hizo** (sólo dibujo del modelo: la pose compartida no se tocó, porque de ella sale el ragdoll y `accel` y
  `crashloop`, que se caen con algo de whip, daban distintos):
  - `RiderModel::WhipPose`: la cadera se corre 0.14·sen (no 0.40), los hombros 0.40 (no 0.62) y las rodillas quedan cerca
    del tanque: aprieta la moto con las piernas, la deja acostarse debajo y el torso queda más derecho en el mundo.
  - En la IK, antes de inclinar el torso, un paso nuevo (`Fit::sideIn`): el cuerpo corrido de costado vuelve hacia el medio
    lo justo (cadera y hombros juntos: el torso no cambia de inclinación) hasta que las manos llegan.
- **Se comprobó**: el mismo whip, cadera 6.9 cm del centro (antes 22.6), cuello 19.9 (36.8), sin torso ni cadera extra (antes
  +26° y 30 cm), mano 0 cm (1); `--drop 1`, igual (7.8 / 19.9 cm). Capturas de frente, atrás y costado (`g_whip3.png`):
  antes colgado al costado con una mano lejos; ahora sobre la moto, con las dos en los puños.

**3. Tirado atrás, para el modelo de brazos largos**
- **Pasaba**: tirado atrás la IK inclinaba el torso 26° (el tope) en la motocross, la 600 y la 2T (18° en la Trilheira):
  la pose (cadera 22 cm atrás) es la del generado y con el asiento ajustado (la cadera 16 cm más arriba) peor.
- **Qué se hizo**: otro paso de la IK, antes de inclinar el torso (`Fit::backIn`, 0..1, lo mínimo que haga falta): la
  cadera va hasta `kBackHipsForward` (12 cm) menos atrás y el torso hasta `kBackLean` (0.35 rad) más adelante, con los
  brazos estirados. Donde los brazos llegan (Trial, carreras) no hace nada.
- **Se comprobó** (`--flat --test pose`, a los 3.9 s): motocross, 600 y 2T `atras=0.92`, sin torso extra (antes +0.45 rad),
  la cadera 11 cm detrás de la neutra y el torso 40° de la vertical (neutro 35°); Trilheira `atras=0.69` (antes +0.31 rad);
  carreras 0.08; Trial nada. Capturas de costado de las seis motos (`g_pose_*.png`).

**4. Nac-nac y can-can para los dos lados**
- **Control**: con el botón, el costado del stick es **el lado para donde sale la pierna**, y la diagonal elige el truco:
  **hacia atrás (↙ ↘) nac-nac**, porque la pierna pasa por arriba de la cola, y **hacia adelante (↖ ↗) can-can**, porque
  cruza por delante. Derecho al costado sigue como en la v0.3.9 (← nac-nac, → can-can): lo nuevo es **↘ nac-nac a la
  derecha** (en el teclado ↓ + D) y **↖ can-can a la izquierda** (↑ + A). Ocho sectores de 45°: arriba / abajo pasados los
  67.5° (heel clicker / no-hander), al costado hasta 22.5°, en el medio la diagonal. Superman (nada), heel clicker (↑) y
  no-hander (↓) no cambian. Cambia ↑+A / ↑+D en el teclado (antes heel clicker, ahora can-can) y ↓+A / ↓+D (antes no-hander).
- **Elegir sin trabarse**: en el teclado las dos teclas de la diagonal no llegan juntas y el stick pasa por un costado
  antes de llegar a la diagonal. En los primeros 0.2 s (`kTrickChoose`) vale la última dirección (no volver a nada), y la
  física (`Bike::UpdateTrick`) cambia de truco si el cuerpo se movió menos de 15% (`kTrickSwitch`; antes 2%: había que
  volver del todo). Con `teclas:`: → y ↓ 40 ms después, nac-nac a la derecha (el can-can se corta al 8%); 100 ms después
  ya no (sigue el can-can). Para pruebas, `--test trucoNOMBRE` y `--trucos NOMBRE` aceptan `nacnacder` y `cancanizq`.
- **Pose y física**: `Trick::NacNacRight` y `CanCanLeft` usan la pose de siempre espejada (`ApplyTrickPose`) y por ella
  la reacción: con `--flat --drop 3`, el nac-nac rola +0.9° y el espejado -0.9°, el can-can -0.7° / +0.7°, el mismo
  cabeceo. Los de siempre dan idéntico a la v0.3.9.
- **La cabeza en el nac-nac**: mira atrás, por arriba del hombro del lado de la pierna (`RiderPose::lookYaw` 1.35 rad y
  `lookDown` 0.30, con el peso del cuerpo del truco), y los hombros giran 0.25 rad con ella (`twist`). Sólo el modelo:
  `RiderMotion` lo suma al objetivo de la cabeza (reemplaza a la pista) y el cuello toma la mitad, como siempre.
- **Red**: ver [RED.md](RED.md): el espejado viaja como el original más un tercer byte; la v0.3.9 lo ve sin espejar.
- **Textos**: ayuda del HUD ("trucos en el aire (+ dirección o diagonal)"), Controles (dos entradas más, con dos flechas:
  la fuente no trae ↖ ↘), LEEME y README.
- **Se comprobó**: líneas `TRUCO` de los cuatro (`--test truconacnacder`...), capturas de atrás, 3/4 y frente de los cuatro
  (`g_trucos.png`, `g_nac_head.png`: la pierna sale para el lado pedido, la cabeza girada hacia ella, sin codos ni rodillas
  al revés), red en los tres sentidos.

**5. Caída en superman: se suelta**
- **Pasaba**: tocando el piso en superman, el ragdoll quedaba acostado sobre la moto ~2.5 s mientras ella seguía andando
  derecha a 40 km/h, hasta que se caía.
- **Por qué** (medido con la línea nueva `RAGDOLL ... partes que chocan con la moto` de `--telemetry`): no era el filtro de
  las partes encimadas (las 11 chocan con la moto desde el primer paso: el superman va arriba de ella). El cuerpo aparece
  con la velocidad de la moto, apoyado encima, y la moto, sola y derecha en el plano, es estable: nada lo hace caer.
- **Qué se hizo**: al caerse por tocar en medio de un truco (`trickAmt` > `trick_crash`), el piloto aparece con 0.8 m/s
  de costado (`kTrickCrashSlide`, Game.cpp), para donde ya se iba la moto (su rolido, si no su giro, si no el manubrio):
  sin los pies en las estriberas ni las manos firmes no puede sostener el golpe. Con 1.5 m/s se desprendía igual pero
  más brusco.
- **Se comprobó**: `--flat --drop 3 --test trucosupermanx`: a los 0.34 s de tocar la moto ya está a 21° y a 0.84 s el piloto
  está en el piso, separado (antes, a 2.5 s seguía encima); capturas `g_caida_sup_antes.png` y `g_caida_sup_d2.png`. Cambia
  sólo lo que se cae en un truco (`trucosupermanx`, `--trucos` con caídas); lo demás, idéntico.

**Regresión** (contra la v0.3.9): idéntico en `brakeslide`, `cuerpo`, `flip`, `whip`, `wheelie`, `--bot --time 40`, `accel`,
`airlean`, `airrot`, `crashloop` (30 s), `colazo`, `circle3x`, `frenada280`, el bot de la 600 y de la favela con la 450 y los
cinco trucos de siempre (`trucoX`); bots 450 1:04.41 / 1:04.24 y 600 1:04.52 / 1:04.34; `crashloop` con ventana (el ragdoll
con el modelo) da la misma telemetría. Distinto, a propósito: lo que termina en caída en un truco.

**Lecciones**:
- Al portar código viejo, buscar en el historial lo que se pierde (`git log -S BikeShape`): el ajuste a la moto estaba
  entero en la v0.3.3 y nadie lo extrañó en los números porque la herramienta de medir se "adaptó" a no tenerlo.
- La pose compartida (`RiderPoseLocal`) arma el ragdoll: cambiarla para el dibujo cambia las caídas aunque la física de la
  moto no se toque (el whip quedaba distinto de cero al caerse en `accel` y `crashloop`). Lo que es del modelo, en el modelo
  (`WhipPose`, los pasos de la IK).
- Los pasos de la IK "lo mínimo que haga falta" (búsqueda binaria de un parámetro 0..1) se adaptan solos a cada moto: el
  tirado atrás no toca la Trial ni la de carreras.
- "Queda colgado" puede no ser colisión: medir qué chocaba (la telemetría `RAGDOLL`) mostró que el problema era la
  estabilidad de la moto sola, no el filtro de grupos.
- Para que un truco nuevo sea compatible en red, mandar además "el más parecido que conoce la versión anterior": ve algo
  razonable en vez de nada.
- `sed -i` en el bash de Git pasa los CRLF a LF: normalizar después (o editar con Python en binario).

**Cámara rígida para comparar poses** (no está en el juego): en una copia del repo, al final de `ChaseCamera::Update`,
si existe `MOTOSIM_CAM="yaw pitch dist [sigue] [alto]"` la cámara queda pegada a la moto (sin resorte ni sacudón),
`yaw` grados respecto del rumbo inicial de la moto (o del actual con `sigue` = 1; -90 = de costado del lado derecho,
180 = atrás), `alto` m sobre el centro de masa. Con eso las capturas de antes y después salen con el mismo encuadre
cuadro a cuadro. Armada en `compilaciones/piloto-cam-src` (`sync.sh` copia `src/`, `mods/` y el CMake del repo y le
pone el parche con `camhack.py`; `compilar.bat` compila en `compilaciones/build-msvc-piloto-cam`). Ahí `MOTOSIM_CAM="yaw
pitch dist alto"` usa el rumbo inicial (no `sigue`): -90 = costado izquierdo, 90 = derecho, 0 = atrás, 180 = frente.

**El ragdoll de hoy** (el de la v0.3.1, desde la v0.3.4) arma cada articulación centrada en la pose con que aparece y
no imprime las articulaciones (lo de "Cómo se mide" es del ragdoll anterior): cualquier pose (un truco) aparece adentro
de sus rangos.

### La Carrera 1000 (v0.3.10, octubre de 2026)
Pedido: "revisá las animaciones en la moto de carreras". Revisada en movimiento con el modelo 2 (cámara rígida a la moto,
`MOTOSIM_CAM` con un quinto número `sigue` = 1 que toma el rumbo actual, y la de persecución): parado y arrancando, a fondo
agachado, frenada fuerte, curvas, chicanas (el bot en el circuito y `teclas:` en `prueba/plaza_asfalto_ancha`), wheelie,
aire, trucos, caída y el piloto remoto. Capturas en `compilaciones/capturas-carrera/` (`antes/` = el árbol de antes, `d3`..`d8`
= los pasos; `ab_*.png` antes arriba y después abajo; holgura en `holgura/`).

**Qué estaba mal y qué se hizo** (todo dibujo del modelo: `RiderPoseLocal`, la pose compartida que arma el ragdoll, no se
tocó):
- **Agachado, codos en ala**: con la pose agachada (los hombros 20 cm más abajo) y los brazos largos, el polo de los codos de
  la de carreras (`elbowsIn`, afuera y apenas abajo) dejaba los codos abiertos como alas, a la altura de los hombros (se ve
  de frente y de atrás a más de 90 km/h). Ahora el polo va hacia abajo y adentro con lo agachado (`tuckAmt`, `kTuckElbowOut`):
  los codos cerrados contra el tanque, los antebrazos sobre él (`d4/ab_accel.png`).
- **Colgado rígido y "sentado en una moto inclinada"**: la pose (la del generado) corre la cadera 14 cm y los hombros 20 hacia
  adentro: con la moto a 55° el cuerpo quedaba casi en el eje de la moto. Y lo poco que pedía lo deshacía el ajuste:
  `Fit::center` devolvía la cadera al medio del asiento (medido: `centro` 1.00 en `colgado roll+60`) porque el muslo de afuera
  cruza el asiento, y `Fit::sideIn` (el paso del whip) traía el cuerpo al medio si las manos no llegaban. Y **la rodilla al
  piso de la pose nunca llegaba al modelo**: el polo de la rodilla era siempre "adelante" (`SolveBody` no miraba `p.knee` salvo
  con la pierna suelta de un truco). Qué se hizo (`RiderModel::RacePose`, entre `WhipPose` y `RiderMotion`, y `SolveBody`):
  - colgado (`hang`, la misma cuenta que la pose: 12°-45° de inclinación, 6-12 m/s), la cadera 6 cm más adentro y 2 más abajo y
    los hombros 12 más adentro y 3 más abajo (`kHangHips`, `kHangShoulders`, `kHangDrop`): media cola afuera del asiento, el
    pecho sobre el costado de adentro del tanque, el casco hacia el espejo de adentro;
  - la rodilla de adentro con el polo afuera, adelante y apenas abajo (`kHangKneeOut`): sale hacia el piso; la de afuera, arriba
    contra el tanque;
  - el codo de adentro baja hacia el piso y el de afuera apoya sobre el tanque (antes cruzaba por arriba de la cúpula);
  - `Fit::center` vuelve como mucho el 15% de lo colgado (`kHangKeep`) y lo demás lo resuelve subir la cadera; colgado no se usa
    `sideIn` (si las manos no llegan, el torso se inclina).
- **Los brazos se metían en el tanque** (con los codos cerrados el de afuera lo cruzaba 4 cm, y para sacarlo el ajuste del
  pecho enderezaba el torso 0.36 rad: colgado se veía sentado derecho). Paso nuevo del ajuste (`Fit::elbowOpen`, como las
  rodillas): cada codo se abre girando alrededor de la recta hombro-muñeca lo justo para que el brazo y la mitad del antebrazo
  del lado del codo (`ProbeArm`) queden afuera de la moto, con un margen de 1.5 cm hacia adentro del brazo (`kArmMargin`: la
  malla es de pocos triángulos y el borde del tanque se metía entre vértices). Vale para todas las motos; en las otras no
  cambia nada (barrido idéntico en la motocross y la Trial).
- **Frenada fuerte agachado**: seguía acostado sobre el tanque y la inercia lo tiraba más adelante, el casco contra la cúpula.
  Ahora (`RiderMotion`, grado de libertad `SitUp`, resorte crítico w 6) con la frenada (de 2 a 7 m/s² de desaceleración, sólo
  agachado y apoyado) sale de atrás de la cúpula: el pecho sube 13 cm y va 5 atrás, la cola 3 cm adelante contra el tanque y los
  brazos sostienen (`d5/ab_frenada.png`).
- **El piloto de otro jugador nunca se colgaba ni se agachaba**: la moto remota (`Multiplayer::UpdateVisuals`) no tenía `roll`,
  `pitch`, `speed` ni `forwardSpeed` (quedaban en 0) y la pose los usa (colgado, agachado y también de pie sobre la cola). Ahora
  salen de la rotación y la velocidad que llegan, como en `PrePhysics`. Medido con `red.sh` (anfitrión sin ventana con el bot en
  el circuito, invitado con ventana y `MOTOSIM_PILOTO_LOG`): hombros al costado del remoto como mucho 5 cm antes (el propio, 21)
  y 31 cm ahora, igual que el propio (`colgado` 1.00). Sólo dibujo; el protocolo no cambia.
- **Visto y bien**: cabeza hacia el vértice (la de `RiderMotion`), transiciones de la chicana sin tirones (el bot 60 s en el
  circuito: lo más que se mueve la cadera, el cuello o el torso en un cuadro, ≤ 1.2 cm / 1.2°, igual que antes), aire, wheelie
  (casi no levanta: anti-wheelie), trucos (idénticos: `RacePose` no hace nada en un truco) y la caída (idéntica).

**Holgura** (`tools/holgura`, grupo nuevo `colgado`: colgado y agachado a la vez a 30 m/s, que es lo de las curvas rápidas;
`rapido` sólo cuelga a 10 m/s), cm, lo peor / percentil 90, antes → después:

| grupo | torso | cadera | brazo | antebrazo | muslo | canilla |
|---|---|---|---|---|---|---|
| colgado (108 poses) | 0.8/0.4 → 0.0/0.0 | 2.8/2.7 → 1.4/1.3 | 0.0/0.0 → 1.1/0.8 | 0.0/0.0 → 0.9/0.7 | 3.5/3.5 → 3.2/3.2 | 0.1/0.1 → 3.0/0.7 |
| rapido (189) | 1.2/0.0 → 1.2/0.0 | 4.1/3.5 → 4.7/3.7 | 0.0/0.0 → 1.3/0.0 | 0.0/0.0 → 1.4/0.0 | 4.1/3.7 → 4.8/3.8 | 2.3/0.0 → 2.3/0.0 |

Antes "0" en los brazos porque iban abiertos en ala; ahora apoyan (1 cm). La canilla de 3 cm es la de afuera contra el
asiento colgado y tirado atrás a la vez (`roll-60 L-1`, raro). En el juego (`MOTOSIM_HOLGURA=1`): la curva a fondo de
`teclas:W+100,WD4,W2` sin brazos adentro y sin enderezar el torso (antes de `elbowOpen`: brazo 4.3 cm y el torso -0.33 rad); el
bot 60 s en el circuito, igual que antes salvo el codo de afuera apoyado en el borde del tanque (2.3 cm, puntos del tanque
adentro del codo; la vista `holgura/f587.png`).

**Queda**: el brazo de adentro colgado va casi estirado hacia el puño (el hombro queda lejos del semimanillar); el pecho no
llega a apoyar en el tanque como un piloto de MotoGP (la IK no deja que las manos dejen de llegar). En el circuito el bot frena
suave (0.5 g): la frenada fuerte se vio con `teclas:_1,W+200,S-3,_2` en la plaza de asfalto.

**Trampas de esta prueba**:
- En el bash de Git, `--test teclas:W+100,...` llega cambiado (lo convierte como una lista de rutas): `MSYS_NO_PATHCONV=1`. Y
  con eso las rutas `/c/...` de `--screenshot` ya no se convierten: pasar `pwd -W` (`C:/...`).
- Las lisas en la tierra de `--flat` patinan (`loose_grip` 0.35): para la de carreras, `--map prueba/plaza_asfalto_ancha` desde
  `pruebas/`.
- `RacePose` corre antes de `RiderMotion` (lo suaviza), pero los polos usan `hang` / `tuckAmt` sin suavizar: son continuos
  porque la inclinación y la velocidad lo son.


### Saltos de la pose (v0.3.10, octubre de 2026)
- **Pedido**: "por momentos se ven las piernas y a veces otras animaciones como que 'saltan' de una posición a otra de forma
  instantánea, pasa con prácticamente todas las motos al hacer ciertos movimientos".
- **Cómo se mide** (`MOTOSIM_SALTOS=1`, con ventana: `RiderModel::CheckJumps`): cada cuadro sobre la moto, cada hueso del modelo
  en espacio de la moto (lo que se mueve la moto no cuenta) contra los dos cuadros anteriores: el **cambio de velocidad** en un
  cuadro (la diferencia segunda), llevado a 60 fps. Un movimiento rápido pero continuo (el pie de un truco a 6 m/s) da poco; un
  salto de una solución a otra da lo que saltó. Avisa si pasa de 2.5 cm o 6° (`kJumpDist`, `kJumpAngle`) con una línea `SALTO`
  que trae los huesos, el ajuste de la IK buscado en el cuadro anterior y en éste (`fit ... antes>ahora`), la pose (truco, pie y
  mano sueltos, colgado...), la moto (y el manubrio `steer antes>ahora`) y `[pose N cm]`: el mismo cambio en los tobillos y puños
  que pide la pose compartida (si es grande, el salto viene de la pose y no del modelo). Al salir, un resumen por hueso.
  `compilaciones/capturas-saltos/correr.sh EXE CARPETA` corre 21 pruebas en las 6 motos (pose, cuerpo, accel, wheelie, brake,
  sharpturn, slide, circle3, reverse, whip, airlean, airrot, colazo, freno de mano y frenar, superman, nac-nac, heel clicker, el
  bot 45 s en la pista de motocross, la favela y el circuito, y una chicana y frenada en la plaza de asfalto).
- **Qué saltaba** (5568 avisos en las 126 corridas; lo peor 42 cm):
  1. **La rodilla que se abre** (`Fit::open`, ~70% de los avisos, en todas las motos): el ajuste busca "la primera apertura, de a
     0.1 rad, con la que el muslo y la canilla quedan afuera del tanque". Con la pose cambiando apenas, la primera que alcanza
     pasa de 0.76 a 0.31 rad de un cuadro al otro: la canilla salta 7-17 cm. Parado (`pose` de la Trilheira, t = 0.7-0.95 s)
     **titilaba**: 8.8 / 17.8 / 8.9 cm, ida y vuelta. Lo mismo, menos seguido, el tobillo (`ankleOut`), la cadera que sube
     (`lift`), la que vuelve al medio (`center`: de 1 a 0 en un cuadro, la cadera 17 cm, en el nac-nac) y los pasos de "que las
     manos lleguen" (`backIn` de 0.33 a 0, el torso 3 cm), porque cada cuadro elige de nuevo y por un orden de pasos fijo.
  2. **El ajuste de la pierna se cortaba** al soltar el pie en un truco (`legFree` > 0.5: rodilla y tobillo volvían a 0).
  3. **El muslo daba la vuelta sobre sí mismo**: `aim` gira cada hueso por el arco más corto desde el giro heredado; con el muslo
     casi al revés que en reposo (las piernas arriba del heel clicker) eso no está definido y el muslo giraba 70-150° por cuadro (el
     pie, que lo hereda, hasta 160°). Al arreglarlo con el frente de la rodilla, en el nac-nac el muslo titilaba 180°: el frente
     tomado del polo se da vuelta cuando la cadera pasa los 90°.
  4. **El pie del truco**: mezclar el vector de siempre con el de la punta del truco pasa cerca de cero si son casi opuestos (el
     pie daba media vuelta en un cuadro).
  5. **Reaparecer cerca** (el bot trabado contra una pared reaparece a menos de 1 m): `RiderMotion` sólo arrancaba de nuevo con
     más de 1 m o 0.6 rad; si no, sentía el salto como un golpe (el torso 27 cm).
- **Qué se hizo** (sólo el modelo):
  - **Lo que se dibuja del ajuste sigue a lo buscado con un resorte** (crítico, `kFitW` 15 rad/s, ~0.15 s): cada cuadro el ajuste
    busca lo de siempre (`lastFit`) y se dibuja `fitShown`, que lo sigue (`PoseOnBike` con `RiderMotion`; el barrido y el ragdoll
    usan lo buscado). Un instante puede rozar la moto, pero no salta. Con 30 rad/s, un cambio grande del buscado (0.77 rad de
    rodilla) todavía daba un tirón de velocidad de 7 cm por cuadro; con 15, 2 cm.
  - El ajuste de la pierna se va suave mientras el pie se suelta (`legFit` = 1 - smoothstep(0.2, 0.6, `legFree`)).
  - **Muslo y canilla con el giro fijado** (`aimFront`): el frente del hueso en reposo (+Z, la rodilla) va hacia el frente de la
    rodilla: doblada, el del plano de la pierna (del lado contrario a donde se pliega la canilla); casi estirada, el del polo;
    mezclados con smoothstep según cuánto dobla. Sentado se ve igual (`capturas-saltos/sentado/g_sentado.png`).
  - El polo de la rodilla suelta pesa según cuánto está doblada en la pose (con la pierna estirada no está definido); el pie gira
    de una dirección a la otra (`DirSlerp`) en vez de mezclar vectores.
  - `RiderMotion` arranca de nuevo también si lo que se corrió la moto en un cuadro no sale de su velocidad (más de 25 cm + 60·dt²).
- **Resultado** (avisos / lo peor en cm, las 6 motos; `compilaciones/capturas-saltos/antes` = sin los arreglos, compilado aparte
  de una copia con el mismo detector; `d3` = ahora):

  | prueba | antes | ahora |
  |---|---|---|
  | bot favela | 1312 / 42 | 88 / 42 |
  | bot motocross | 1295 / 20 | 26 / 12 |
  | bot circuito | 628 / 20 | 0 |
  | heel clicker | 251 / 29 | 102 / 16 |
  | freno de mano y frenar | 243 / 25 | 62 / 5 |
  | chicana y frenada (asfalto) | 233 / 16 | 23 / 5 |
  | nac-nac | 186 / 47 | 91 / 24 |
  | superman | 171 / 17 | 82 / 11 |
  | brake | 164 / 16 | 0 |
  | cuerpo | 152 / 20 | 6 / 4 |
  | slide, accel, sharpturn, wheelie, circle3 | 140, 136, 113, 105, 103 / 14-20 | 4, 20, 14, 4, 21 / ≤ 5 |
  | whip, pose, airlean, colazo, airrot, reverse | 87, 71, 67, 63, 40, 8 / 8-13 | 21, 4, 12, 13, 6, 0 / ≤ 9 |
  | **total** | **5568** | **599** |

- **Lo que queda** (no es del modelo; resuelto en la v0.3.10, ver "Saltos de la pose: lo que no era del modelo", abajo):
  - lo peor de la favela (41 cm) es reaparecer: la moto entera vuelve a la pista con el manubrio de 40° a 0 (`steer 39.9>0.0`);
  - los trucos: la pose del truco (`Bike::ApplyTrickPose`, compartida: la usa la física, `TrickBody`) pide al empezar y al volver
    cambios de velocidad de 4-7 cm por cuadro en tobillos y puños (`[pose N cm]`) y el modelo los sigue; suavizarla cambia la
    física de los trucos;
  - manos y antebrazos 4-12 cm cuando el manubrio mismo salta 10-15° en un cuadro (`steer -5.6>-15.2>-24.7>-10.3`, la Trial
    derrapando en el bot): las manos siguen al manubrio. Es de la física de la dirección (para revisar en FISICA).
- **Trampas**:
  - **Ninja y los headers**: después de cambiar `RiderModel.h` hay que tocar *todos* los `.cpp` que lo incluyen, también por otro
    header (`Assists.cpp`, `main.cpp` por `Game.h`); con objetos viejos el exe se colgaba al segundo cuadro (0xC0000409 o
    acceso inválido) y una compilación desde cero andaba.
  - Las capturas en serie alargan los cuadros (0.05-0.1 s): el titileo de la rodilla depende del cuadro y no se reproduce
    capturando; se midió sin capturas.
  - En los heredoc de bash de esta PC (los de las herramientas del agente) una barra doble se vuelve simple: un `\\n` en un script de
    Python llega como `\n` y se escribe un salto de línea de verdad adentro de un string de C++. Los `printf` con `\n` se
    editan con la herramienta de archivos (o con `chr(92)`).

### Saltos de la pose: lo que no era del modelo (v0.3.10, física y pose compartida)
Los tres pendientes de arriba. Medido con `compilaciones/capturas-saltos/correr.sh` (126 corridas con ventana) con el exe de
antes de esto (`capturas-saltos/fisica-antes`) y el de después (`fisica-despues`), y una medición sin ventana de la pose
compartida (un `printf` de prueba, sacado: cada dos pasos, la diferencia segunda de tobillos, puños, rodillas, codos, pies,
cadera, hombros y cabeza de `RiderPoseLocal`, en espacio de la moto).
1. **Trucos** (`Bike::ApplyTrickPose`, la usan el dibujo y `TrickBody`):
   - **qué pasaba**: lo peor no era el arranque ni la vuelta del resorte del truco (`trickAmt` es continuo en posición y
     velocidad: se probó pasarlo por un filtro de primer orden antes del resorte, a la misma velocidad de conjunto, y los
     tirones fueron iguales o mayores: es la aceleración del movimiento, no un corte), sino dos cosas de la pose:
     - **el tope del alcance** (`MidJoint`): el tobillo o la mano que pasaba de largo se frenaba de golpe en el 99.5% del largo
       de la pierna, y la rodilla, que cerca de estirada se mueve muy rápido, pegaba un tirón: en el heel clicker, al volver,
       10 cm por cuadro;
     - **el pie del nac-nac pasaba a 17 cm de la cadera** (la curva iba por arriba del asiento, `ctrl` a 0.42 m atrás): el muslo
       giraba 130° en cuatro cuadros y la rodilla saltaba 22 cm por cuadro (la 450; la Carrera 12, la Trial 9). Mezclar los polos
       con slerp o hacerlos pasar por arriba no cambia nada (o empeora: 70 cm): manda la dirección cadera-tobillo;
   - **qué se hizo**: el tope suave (desde el 90% del alcance la punta se acerca con `tanh`, sin quiebre) y el pie del nac-nac
     sale para atrás por su lado y cruza por arriba del guardabarros (`ctrl` (−0.30·s, +0.40, −0.80) desde la cadera del
     estilo: pasa a 33 cm de la cadera y cruza 0.31 m arriba y 0.8 m atrás del centro de masa). Capturas antes y después de costado, iguales a la vista
     (`compilaciones/capturas-fisica/nac_zoom.png`);
   - **se comprobó** (la pose compartida, la 450): rodilla del nac-nac 21.8 → 8.1 cm por cuadro (la Carrera 12.4 → 6.0, la
     Trial 9.4 → 5.3); heel clicker al volver 10.1 → 6.2 (la rodilla) y tobillos 4.8 → 4.5; tobillos y puños al arrancar, 3-4 cm,
     igual (el pie que sale rápido: no es un corte). En el detector, el nac-nac: lo peor 18 → 7 cm (más avisos chicos: 85 → 109),
     el superman y el no-hander 86 → 82 avisos y 8 → 5 cm; el heel clicker igual (97 → 108 / 14 cm): lo que queda son las
     canillas del modelo con las piernas casi estiradas a mitad del movimiento (la pose pide 0.2-1 cm en tobillos y puños): la
     IK del modelo, para revisar acá. La física de los trucos cambia poco (FISICA.md, "Trucos: el cuerpo mueve la moto"): los
     tiempos de volver iguales y tocar el piso con el truco sigue siendo caída;
2. **Reaparecer** (la favela, lo peor 41-42 cm): la moto vuelve a la pista con el manubrio derecho desde el primer paso
   (`Bike::Reset` ya ponía la dirección en 0: no quedaba nada guardado); lo que saltaba era el dibujo: el piloto entero
   (cadera 14 cm, hombros 32 cm) porque la moto, trabada contra una pared, reaparecía a menos de 1 m y con la velocidad casi en
   0, y ni `RiderMotion` ni el detector lo veían como un salto de la moto. **Qué se hizo**: `Bike::resetCount` (sube en cada
   `Reset`); `RiderModel::PoseOnBike` arranca de nuevo (el cuerpo con masa y el ajuste) y el detector no compara ese cuadro
   cuando cambia (por modelo y moto: el mismo modelo dibuja la vitrina). **Se comprobó**: al reaparecer 4 → 0 avisos; la favela
   120 / 42 cm → 35 / 6 cm.
3. **El manubrio que saltaba 10-15°** (la Trial derrapando): la física no se tocó; lo que se dibuja (manubrio, rueda delantera y
   manos) sigue a la dirección con un resorte (FISICA.md, "El manubrio dibujado saltaba 10-15° por cuadro"). **Se comprobó**:
   avisos con el manubrio moviéndose más de 6° en el cuadro 77 → 5 (lo peor 41 → 5 cm); manos y antebrazos 323 → 58 avisos, lo
   peor 23 → 6.8 cm; la chicana del asfalto 19 → 0, freno de mano y frenar 56 → 11, el bot de motocross 69 → 18.

| prueba (las 6 motos) | antes | después |
|---|---|---|
| bot favela | 120 / 42 | 35 / 6 |
| heel clicker | 97 / 14 | 108 / 14 |
| superman | 86 / 8 | 82 / 5 |
| nac-nac | 85 / 18 | 109 / 7 |
| bot motocross | 69 / 10 | 18 / 5 |
| freno de mano y frenar | 56 / 5 | 11 / 5 |
| whip, circle3, chicana del asfalto, sharpturn, accel | 27, 20, 19, 15, 14 / ≤ 11 | 5, 6, 0, 4, 0 / ≤ 6 |
| colazo, airlean, cuerpo, bot circuito, pose, slide, wheelie, airrot | 13, 9, 7, 6, 4, 4, 4, 2 / ≤ 6 | 6, 12, 6, 5, 4, 0, 4, 4 / ≤ 7 |
| **total** | **657** | **419** |

(Con ventana los cuadros duran distinto en cada corrida: la misma prueba da ±20-40% de avisos de una vez a la otra; lo que
se mueve de verdad es lo peor y los avisos de cada causa.)
- **Lecciones**:
  - **Un tirón en la pose puede ser geometría, no tiempo**: antes de suavizar en el tiempo, medir la pose como función del truco
    (cada paso, sin ventana) y ver qué punto salta: la rodilla del nac-nac saltaba porque el tobillo pasaba al lado de la cadera,
    y ningún filtro del tiempo lo arregla sin hacer lento el truco.
  - **Un tope duro es un quiebre en la velocidad**: `clamp` de un largo frena de golpe lo que viene; un tope suave (`tanh` desde
    un poco antes) lo frena de a poco y el resultado es el mismo lejos del tope.
  - **"Reaparece" tiene que ser un evento, no una adivinanza**: medir saltos de posición no sirve cuando la moto reaparece casi
    donde estaba y parada; un contador en `Reset` lo dice siempre.

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
arma una copia en `compilaciones/build-msvc-holgura/` (src, mods, `RiderClearance.cpp/.h` de `tools/holgura` y un enganche
en `Game::Draw` después de `riderModel.Skin()`) y la compila; el exe se corre **con ventana** desde la raíz
del proyecto (ahí están el modelo y `mods/`). Si `Game.cpp` cambia y el enganche no entra, el script avisa.
- **Qué mide**, en cada pose: (A) vértices del piloto adentro de cada primitiva cerrada de las piezas de la
  moto (chasis, basculante, tijas, botellas, manubrio, guardabarros, cubiertas), con la profundidad hasta su
  superficie; (B) puntos de la superficie de la moto cada 1.2 cm adentro de las piezas del piloto (lo que A
  no ve: un caño fino que atraviesa un muslo). Por zona del cuerpo (el hueso que más pesa); la mano en el
  puño y la bota en la estribera, aparte.
- **Barrido**: `bash tools/holgura/barrido.sh compilaciones/build-msvc-holgura/b/motocross.exe <carpeta> <etiqueta>` (las
  cinco motos, 3-6 min cada una; la de carreras, más) y `python tools/holgura/tabla.py <carpeta> antes
  despues` para la tabla. El CSV trae por pose también el ajuste (`sube`, `centro`, `abre`, `tob`), lo que
  queda según las pruebas del propio ajuste (`pAsiento`, `pPierna`, `pBota`, `pManubrio`), el alcance del
  brazo y cuánto le falta a la mano para su puño (`mano_lejos`).
- **En una prueba**: `MOTOSIM_HOLGURA=1 <exe> --flat --test cuerpo ...`: cada 0.05 s imprime lo que pasa de
  1 cm y al salir lo peor de toda la prueba (el primer cuadro sale sin ajustar: descartarlo).
- **De la v0.3.4 a la v0.3.9** (la pose de la primera 0.3.1) `RiderModel` no tuvo el ajuste a la moto (`BikeShape`, `Fit`)
  y `RiderClearance` sólo medía. En la v0.3.10 el ajuste volvió: el barrido arma la forma (`BuildShape`), pasa la pose por
  `WhipPose` (como el juego) y el CSV trae otra vez `sube`, `centro`, `abre`, `tob` y las pruebas del ajuste (`pAsiento`...),
  más al final `medio`, `mas_torso` y `mas_cadera` (lo que hizo la IK para que las manos lleguen). El grupo `trucos` recorre
  todos (`Trick::Count`, con los espejados). `tabla.py` suma la 600 (`motocross600`).
- **Desde la v0.3.10** (ver "La Carrera 1000"): el barrido pasa la pose por `RacePose` (como el juego) y tiene el grupo
  `colgado` (colgado y agachado a la vez, 30 m/s; `tabla.py` lo suma); en una prueba, `MOTOSIM_HOLGURA_VOLCAR=prefijo` con
  `MOTOSIM_HOLGURA_VOLCAR_T="desde hasta"` vuelca las poses de esos segundos para `vista.py`.
- **Lo peor de una prueba sin el primer cuadro**: el resumen de `MOTOSIM_HOLGURA=1` cuenta el primer cuadro (sin la forma de
  la moto, sin ajustar: 6-7 cm de cola "adentro" en todas); para comparar, tomar el máximo de las líneas `holgura t=` desde
  0.1 s.
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

## Espalda lisa y material del traje (septiembre de 2026)

El modelo 3 tenía un aro en relieve en el centro de la espalda, además de una marca blanca en la
textura. Aplanar sus vértices solamente dejaba caras superpuestas. Al cargar ese modelo se retiran
las 74 caras del aro y se cierra su contorno de diez aristas con diez triángulos, reutilizando un
vértice interior. Se ajusta el perfil hacia el de la campera y se recalculan las normales por área,
compartidas en las costuras de UV. Otra causa del bulto eran pesos de los brazos en la espalda
central: una transición suave la vincula a las vértebras correspondientes a su altura.

El cambio se hace antes del skinning y de construir las muestras de holgura; conserva el rig, las
manos y el ragdoll. Se limita a `Low_Poly_Motorcyclist_3_rigged.gltf`; el modelo 2 de respaldo y los
archivos originales quedan intactos. Para otro modelo se debe revisar el contorno y las coordenadas.

El shader del piloto usa el atlas original como máscara de paneles rojos, oscuros, blancos y piel;
reduce la iluminación pintada del atlas y agrega trama fina filtrada por derivadas. Una máscara
en el alfa de los colores de vértice unifica la zona reparada; en este material no representa
transparencia. Se carga su VBO de colores (raylib no lo había creado), se baja el brillo a 0.08 y
se habilitan mipmaps/trilineal y anisotropía 8x cuando el driver la soporta. El uniforme del material
se restablece después de cada dibujo para no afectar motos ni carteles. No se reemplaza el PNG.

Comprobado con el glTF principal: capturas antes/después de atrás (`--view 25 5 0.4`), de frente
y costado, y en el aire (`--flat --drop 3 --test airlean`); sin aro, sin caras superpuestas y con
sombras del piloto. Compilación Release y prueba básica de aceleración sin ventana completadas.

### Corrección del traje con manchas dentadas

La clasificación del atlas por umbrales RGB convertía sus degradados y sombras pintadas en
manchas negras con bordes irregulares. Se reemplazó por paneles de campera, laterales, mangas,
vivos claros y pantalón definidos en coordenadas de reposo. Esas coordenadas viajan en RGB de
vértice y siguen el skinning: no se deslizan al mover el cuerpo. El casco y el cuello conservan
su atlas; el traje usa tejido fino filtrado y la luz real. Las normales de la ropa se recalculan
soldadas por posición para eliminar cortes de iluminación entre islas UV. El arreglo del aro sigue.
Verificado desde atrás y de costado con el glTF principal: `build/soil-checks/rider-clean.png`.

## Menos triángulos: el modelo 2 reducido (tools/reducir_piloto.py)

- **Pedido**: bajarle el polycount al piloto sin que cambien las animaciones ni la textura.
- **Cómo era** (`Low_Poly_Motorcyclist_2_rigged.gltf`): una malla, una primitiva, 5210 triángulos y 5901 vértices
  (2607 posiciones distintas: una sola pieza cerrada), 24 huesos, un material, una textura PNG de 2048² (7.9 MB).
  El `.bin` pesaba 8.2 MB, pero 7.9 MB eran una copia de la textura que ningún accesor ni imagen usa (la imagen
  se lee del PNG): la malla son 0.34 MB, y las dos animaciones de Blender (que el juego no usa), unos KB.
  Las UV son de un desplegado automático: **674 islas, y 2622 de las 7815 aristas son costura** (34%); en 1023
  posiciones se cruzan tres islas o más. Por zona (hueso que más pesa): manos 1166 triángulos (22%), cabeza 778
  (15%), canillas 711, muslos 398, cadera 266...
- **Qué usa el código**: sólo `meshes[0]`; los huesos por nombre; las posiciones de reposo de los huesos. El
  skinning es en la CPU, vértice por vértice, cada cuadro (`RiderModel::Skin`). **Los dedos se arman midiendo los
  vértices de la mano** (`BuildGrip`: los que tienen ≥ 0.5 de peso de `LeftHand` / `RightHand`, en cortes de 3-4 mm
  a lo largo de los dedos): reduciendo también las manos, la flexión de los dedos cambiaba (la derecha, de
  40/58/61° a 51/34/105°) y el agarre se veía distinto. Por eso las manos no se tocan.
- **Qué se hizo**: `python tools/reducir_piloto.py Low_Poly_Motorcyclist_2_rigged.gltf Low_Poly_Motorcyclist_2_lowpoly.gltf`
  (Python + numpy, sin Blender; por defecto `--objetivo 0.65 --error 40 --cabeza 8 --pies 6 --costura 4 --uv 2
  --peso-max 0.25 --manos-fijas 1`; tarda ~20 s y da siempre lo mismo). No se usó el Decimate de Blender: al
  reexportar, Blender reescribe nodos, matrices de bind, animaciones y material, e interpola UV y pesos. Acá:
  - **Colapsos de medio lado**: el vértice que se va se junta con un vecino que queda donde estaba. Todo vértice
    del resultado es uno del original con sus bytes (posición, normal, UV, huesos y pesos): no se interpola nada.
  - **Costuras**: un vértice sin costura va con cualquier vecino de su isla; uno en medio de una costura (dos
    copias, dos aristas de costura), sólo a lo largo de ella y cada copia con la del vecino del mismo lado; los
    cruces de islas no se tocan. Ninguna isla se queda sin triángulos.
  - **Orden**: cuádricas de error (Garland-Heckbert) en posición + UV (1 unidad de UV = 2 m: 1 texel ~ 1 mm), más
    planos que sostienen cada costura en 3D y el borde de su isla en la UV (×4). Se rechaza lo que da vuelta un
    triángulo (más de 60° en 3D, o en la UV), lo que lo deja muy fino, lo que rompe la variedad (condición del
    enlace) y juntar vértices con pesos distintos (más de 0.25): codos, rodillas y hombros conservan sus anillos.
  - **Zonas**: cuesta 8 veces más sacar de la cabeza (con 4-5 el casco se veía facetado de perfil) y 6 de los pies
    (la punta de la bota quedaba en bloque); las manos, fijas. Tope: 40 mm de desvío medio por la importancia de
    la zona.
  - El glTF nuevo es el original con la malla cambiada: nodos, esqueleto, matrices de bind, animaciones y
    material iguales; el `.bin` sin la copia de la textura; la textura copiada byte a byte (mismo SHA-256) a
    `Low_Poly_Motorcyclist_2_lowpoly_deps/`.
  - `Game.cpp` busca primero `Low_Poly_Motorcyclist_2_lowpoly.gltf` y después el original; `package_release.py`
    empaqueta el reducido. **El original queda en el proyecto, sin tocar.**
- **Resultado**: 5210 → **3484 triángulos (33% menos)**, 5901 → 4726 vértices. Por zona: cabeza 778 → 516, manos
  1166 → 1166, canillas 711 → 355, muslos 398 → 237, cadera 266 → 116, brazos 484 → 254, antebrazos 406 → 231,
  pies y botas 503 → 304, columna 275 → 170, hombros 181 → 106, cuello 42 → 29. Sin las manos, 4044 → 2318 (43%
  menos). Archivos: `.bin` 8.2 MB → 0.29 MB; el zip del paquete, 17.8 → 9.8 MB.
- **Por qué no más**: con las costuras respetadas, el piso (sacar todo lo que se puede) es ~2050 triángulos, y ahí
  el casco y las botas se ven facetados (hasta 4 cm de desvío). Con la cabeza a importancia 4 y sin tope: 40% menos
  y el casco ya muestra facetas de perfil. Más que eso pide tocar las UV (rehacer la textura) o las manos (cambia
  el agarre).
- **Se comprobó**:
  - Que sólo cambia la malla: nodos, skins, animaciones y material iguales; los 222 accesores de fuera de la malla
    con los mismos bytes; los 4726 vértices idénticos a uno del original; la textura con el mismo SHA-256.
  - Desvío (distancia de cada vértice original a la superficie reducida, en reposo): mediana 0, percentil 95 4.6
    mm, lo peor 13 mm (una canilla); cabeza p95 3.3 mm (lo peor 6); manos 0. Textura (dónde queda, en el reducido,
    el punto de la textura de cada vértice original): p95 6.9 mm; 78 de 5901 vértices, en bordes de islas, quedan a
    más de 3 texels de su isla: en las capturas no se ve.
  - El juego lo carga ("24 huesos (+10 de dedos) y 4726 vértices") y los dedos dan la misma flexión que con el
    original (51/38/76° y 40/58/61°).
  - Capturas con ventana, original y reducido: cuatro vistas fijas de `--test pose` (costado, frente, atrás,
    arriba) y parado, curva (`circle3x`), `wheelie`, en el aire (`airlean`), `brake`, la caída de `crashloop`, la
    cara y las manos de cerca: iguales; la textura no se corre y las manos son las mismas.
  - Telemetría de `crashloop` con ventana (20 s, con caída y ragdoll): idéntica con los dos modelos (el ragdoll no
    depende de los vértices). Regresión contra la v0.3.5 (`wheelie`, `accel`): idéntica. `package_release.py` en
    una copia del repo arma el paquete con el reducido y el juego lo carga desde ahí.
  - Fps: con vsync, 16.7 ms con los dos; el skinning en la CPU recorre un 20% menos de vértices.
- **Lecciones**:
  - Un modelo "low poly" de tienda puede tener un tercio de las aristas como costura de UV: un decimador que
    respete la textura no puede sacar nada en los cruces de islas. Contar islas y costuras antes de prometer un
    porcentaje.
  - Medir la textura con "¿dónde queda en el modelo nuevo el punto de la textura de cada vértice viejo?" (buscar
    ese punto en las UV nuevas y comparar posiciones 3D): la distancia 3D sola no ve las texturas corridas. Las
    islas de uno o dos triángulos desaparecen con un colapso: hay que contar triángulos por isla.
  - El error de una cuádrica pesada por área no está en mm: para un tope que se entienda, dividir por el área que
    junta (desvío medio) y usar la pesada sólo para el orden.
  - Si el código mide la malla (acá, los dedos con los vértices de la mano), esa zona no se reduce, o el
    resultado cambia aunque la malla se vea igual.
  - Mirar el `.bin` además de la malla: éste traía la textura dos veces.

## Brazos más largos (tools/proporciones_piloto.py, octubre de 2026)

- **Pedido**: "estirale los brazos al modelo, y dejalo con las proporciones perfectas para las animaciones, fijate que no
  se rompa la geometría ni las texturas". El agente de los trucos había visto que con los brazos cortos el heel clicker
  no podía tirar el torso atrás agarrado al manubrio (quedaba en carpa).
- **Medido** (`python tools/proporciones_piloto.py modelo.gltf` mide; m, entre articulaciones):

  | | antes | después | piloto real ~1.70-1.80 |
  |---|---|---|---|
  | brazo (hombro-codo) izq / der | 0.272 / 0.267 | 0.310 / 0.310 | 0.29-0.33 |
  | antebrazo (codo-muñeca) | 0.246 / 0.255 | 0.260 / 0.260 | 0.25-0.28 |
  | hombro-muñeca | 0.518 / 0.522 (30.5% del alto) | 0.570 (33.5%) | ~33% |
  | mano (muñeca a la punta, el guante) | 0.198 / 0.192 | igual | 0.18-0.19 |
  | envergadura (hombros a ±0.16) | 1.75 | 1.85 | ≈ alto |
  | muslo + canilla (cadera-tobillo) | 0.33 + 0.39 = 0.72 | igual | 0.42 + 0.42 |

  El alto es 1.70 con un casco grande: los hombros están a 1.325 m (en una persona de 1.70, ~1.39). Por eso los brazos
  "de antes" no eran cortos para el cuerpo (colgando, la punta de los dedos llegaba a 0.61 m, como una persona real) pero
  sí para manejar: el brazo superior era lo corto (0.27 contra el antebrazo 0.25; lo común es ~1.2 veces). Se alargó
  sobre todo el brazo (+3.8 / +4.3 cm) y un poco el antebrazo (+1.4 / +0.5 cm), los dos lados iguales (venían
  distintos). La envergadura queda 9% más que el alto: colgando, la punta de los dedos llega a 0.56 m, 6 cm sobre la
  rodilla; el piloto nunca se ve parado (la vitrina muestra la moto sola), sólo sobre la moto o tirado.
- **Las piernas no se tocaron**: la malla está bien (rodilla a 0.49 m, entrepierna ~0.80: proporciones de 1.70). Lo
  "corto" del muslo es el rig: la articulación de la cadera (`UpLeg`) está en la entrepierna (0.825 m; la anatómica
  estaría a ~0.90, donde está `Hips`) y la del tobillo en la bota (0.12). Moverlas cambiaría cómo se deforma la cola
  con los pesos que pintó el autor; la diferencia con la pierna de la pose (la del piloto generado, 0.85) ya la toma la
  IK (el tobillo suelto de un truco se lleva al largo del modelo: ver "Trucos en el aire").
- **Cuánto pedía cada pose** (`MOTOSIM_PILOTO_LOG=1` con una copia que imprime además el alcance antes de compensar:
  distancia hombro-muñeca pedida / largo del brazo; > 0.97 = la IK inclina el torso o corre la cadera), mediana / lo
  peor, antes → después (después, con la clavícula de abajo):
  - motocross y 2T, `pose`: neutro 0.95 → 0.82; tirado atrás 1.42 → 1.22 (la cadera se corría 10.5 cm → 1 cm);
    `cuerpo` hasta 1.14 → 0.97 (el torso se inclinaba 15° → nada); whip, la cadera hasta 30 cm → igual (el cuerpo 30 cm
    de costado: una mano no llega);
  - Trilheira, tirado atrás 1.32 → 1.13; `cuerpo` 1.05 → 0.91 (el torso, en el 48% de los cuadros → nunca);
  - de carreras 0.80 → 0.72 neutro; Trial 0.68 → 0.62 (de pie, siempre llegaba).
- **Cómo se estiró** (`python tools/proporciones_piloto.py entrada.gltf salida.gltf --brazo 0.31 --antebrazo 0.26
  [--suave 0.02]`; Python + numpy, sin Blender, ~1 s):
  - Huesos: la traslación local del codo (`LeftForeArm`) y de la muñeca (`LeftHand`) va a lo largo del hueso padre; se
    alarga y nada gira. Las matrices de bind de esos huesos y los que cuelgan de ellos, `IBM · T(-d)`; las traslaciones de
    esos huesos en las animaciones guardadas (Walking, Running; el juego no las usa), por el mismo factor.
  - Vértices: cada hueso tiene un "cambio de reposo": el brazo estira a lo largo de su eje lo que está entre hombro y
    codo; el antebrazo se corre con el codo y estira entre codo y muñeca; la mano se corre entera; el resto, quieto. El
    estiramiento entra y sale suave (2 cm a cada lado de cada articulación). Cada vértice se mueve con el promedio de
    esos cambios con sus pesos (como el skinning): donde se mezclan los pesos (codo, hombro, axila) el paso es suave, y el
    grosor del brazo no cambia. Las normales, con la inversa transpuesta, normalizadas. **No se reexporta con Blender**
    (reescribe nodos, matrices y material, ver "Menos triángulos"): el `.bin` se reescribe con los mismos accesores y
    tamaños (sólo POSITION, NORMAL, las matrices de bind y esas traslaciones; el min/max de POSITION se actualiza).
  - El juego usa las posiciones de reposo de los nodos (`model.bindPose`) y los vértices tal cual; las matrices de bind
    no, pero quedan bien para cualquier otro programa.
- **El original** quedó en `dist/_archivo/piloto-brazos-cortos-v0.3.9/` (fuera de git; en git está en el historial) y
  se puede rehacer: `reducir_piloto.py` sobre el `_rigged` da siempre el mismo reducido y `proporciones_piloto.py` sobre
  él, el de ahora. `Low_Poly_Motorcyclist_2_rigged.gltf` (el respaldo si falta el reducido) queda sin tocar, con los
  brazos de antes: la IK se arma con los largos de cada modelo.
- **La IK, con los brazos largos**:
  - **Clavícula** (`kClavicleReach`, nuevo): si el hombro queda lejos del puño (brazo al 80% → nada; al 105% → 20°), la
    clavícula gira hacia el manubrio alrededor del eje del torso (la escápula se adelanta, ~4.4 cm), antes de inclinar
    el torso o correr la cadera. Es lo que hace un piloto para estirarse. Sin la mano agarrada (truco), nada.
  - **Codos en la de carreras** (`elbowsIn`, del estilo: `tuck` o `hangOff`): con el brazo largo y doblado, el codo con
    el polo de ataque (afuera y arriba, `(1, 0.1, -0.3)`) se abría como un ala; ahí va más abajo, `(0.75, -0.25, -0.3)`.
  - **Trucos** (`Bike::ApplyTrickPose`, también la física: `TrickBody`): superman con los brazos estirados y heel clicker
    con el torso derecho (ver "Trucos en el aire").
- **Se comprobó**:
  - La malla: mismos triángulos (3484), vértices (4726), UV, huesos y pesos (iguales byte a byte); se movieron 2514
    vértices (brazos y manos; del torso, hasta 6 mm cerca de la axila); ninguna cara dada vuelta ni girada más de 10°;
    las aristas se estiran hasta 20% (el brazo) y ninguna se achica más de 3.4%; la textura, el mismo archivo (SHA-256
    igual). Vistas ortogonales antes y después pintando lo que se movió.
  - Cargado en el juego: "24 huesos (+10 de dedos) y 4726 vértices" y los dedos con la misma flexión (51/38/76° y
    40/58/61°): la mano sólo se trasladó.
  - Capturas con ventana y cámara rígida, antes (el modelo viejo y el código de antes) y después: las cinco motos
    neutro, tirado atrás y adelante, de costado, de frente y 3/4; los cinco trucos de costado, 3/4, frente y atrás; la
    caída de `crashloop`; hombros y codos de cerca (sin pliegues ni costuras abiertas, también con la clavícula a fondo);
    el piloto remoto (anfitrión sin ventana, invitado con ventana). Quedaron en `compilaciones/capturas-brazos-largos/`
    (no va a git).
  - Física: `tools/regresion.sh` contra la v0.3.9-beta idéntico en 14 pruebas (`brakeslide`, `cuerpo`, `flip`, `whip`,
    `wheelie`, `--bot --time 40`, `accel`, `airlean`, `airrot`, `crashloop`, `colazo`, `circle3x`, `frenada280`, la 600
    con el bot); bots 1:04.41 / 1:04.24 y 1:04.52 / 1:04.34; `crashloop` con ventana (el ragdoll con el modelo), la
    telemetría idéntica a la del paquete: el ragdoll se arma con la pose (`JointPose` devuelve la pose), no con el modelo.
  - Holgura (`tools/holgura`, ver abajo; antes = modelo y código de antes, compilados aparte), cm, lo peor / percentil
    90 de las ~900 poses de cada moto. Mejora lo de arriba del cuerpo en el aire: motocross torso 5.5 → 0, brazo 5.2 → 0;
    Trilheira torso 9.1 → 4.9 (en el aire 9.1 → 0.9), brazo en el aire 4.9 → 0.9. Lo demás queda igual salvo la cadera y
    los muslos, que se hunden 0.5-1.5 cm más en el asiento en la motocross, la 2T y la Trilheira (motocross 9.4/6.9 →
    9.9/7.3): tirado atrás la IK ya no corre la cadera adelante (y 30% hacia arriba) para llegar. Los trucos: nada nuevo
    adentro de la moto en ninguna (superman con `-0.54` metía casco y torso 6 cm en la cúpula de la de carreras con el
    manubrio girado: por eso `-0.51`). Codos de la de carreras: con el polo `(0.6, -0.45, -0.3)` el antebrazo tocaba el
    tanque (percentil 90 0 → 2.1 cm); con `(0.75, -0.25, -0.3)`, igual que antes (0 y el brazo 7.1).
    Tabla entera en `compilaciones/capturas-brazos-largos/holgura_antes_despues.md`.
- **Quedaba** (resuelto en la v0.3.10, ver "Los pendientes de la v0.3.9"): el whip a fondo con el cuerpo 30 cm al costado
  y la cadera corrida; tirado atrás, la IK inclinando el torso 26°; la cola y los muslos 7-10 cm adentro del asiento.
- **Lecciones**:
  - Medir las proporciones contra el cuerpo y no contra el alto: con un casco grande, los hombros están bajos y unos
    brazos "normales" para el alto quedan cortos para el manubrio. Lo que importa es lo que pide la pose (el alcance).
  - Para alargar un hueso de una malla con skinning, mover cada vértice con el promedio pesado de los "cambios de reposo"
    de sus huesos (un estiramiento a lo largo del eje, con transición suave): el resultado es tan suave como los pesos y
    no hace falta Blender. Comparar UV, índices y pesos byte a byte después.
  - Con brazos largos, el polo del codo pasa a decidir la silueta: el exceso de largo se va al codo. Revisarlo por estilo.

## Colores del traje (octubre de 2026)

- La textura del modelo 2 es un atlas pintado con dos colores de equipo: el **rojo** (principal, ~199,55,50) y el **azul
  marino** con sus tonos más claros (secundario, ~37,50,90); además blanco/gris, negro, piel y el **visor violeta** (~53,37,73,
  tono 266°). El casco lleva los mismos colores: cambia con el traje.
- `RiderModel::BuildSuitMask` (al cargar; detalle del shader en RENDER.md, "Esquemas a gusto y traje recoloreado"):
  1. por texel, tono, saturación y valor con bordes suaves: rojo 355-15° (para el color medio) y, para la máscara, también
     sus sombras pintadas que se van a magenta (318-355°); marino 205-245° y sus sombras violáceas hasta 258°. El visor (266°)
     queda afuera. Saturación y brillo mínimos dejan afuera los grises y el negro.
  2. el color medio de cada zona (`SuitBase`, "los de fábrica"), y por texel su brillo relativo a ese medio;
  3. los texels de borde de la propia textura son mezclas de dos colores: si un texel cae a menos de 0.05 del segmento entre
     dos de {rojo, marino, blanco, negro, visor}, se toma esa mezcla (cuánto tiene de cada uno). Sin esto quedaba una línea
     magenta entre el rojo y el marino en un traje blanco.
- **Qué pasaba en las primeras pruebas**: sólo con el tono, un traje blanco tenía líneas magenta entre paneles (los texels
  de mezcla y las sombras magenta del rojo); sumando las sombras al rango y las mezclas de a dos se fueron. Queda apenas un
  filo lavanda en algunos bordes negro-blanco vistos muy de cerca (`rider_zoom3.png`).
- Se comprobó con capturas a la misma cámara (`--test idle --view 140 8 0.35 --nohud`, `--colores "piloto=..."`): rojo y
  azul originales, verde y negro, blanco y negro, amarillo y azul, rosa y blanco (`compilaciones/build-msvc-interfaz/cap/col/
  rider_grilla.png`): sólo cambia el color; los blancos, los guantes, la piel y el visor quedan.
