# Física: problemas resueltos y reglas

Cada entrada: qué pasaba, por qué, qué se hizo y cómo se comprobó. El funcionamiento general está en el
README ("Cómo funciona la física"); esto es la bitácora de lo que costó. El piloto y el ragdoll tienen
su propio documento: [PILOTO.md](PILOTO.md).

## Reglas

- **La física de la Motocross 450 no cambia.** Antes de dar por bueno un cambio en código común, la
  telemetría de las pruebas de siempre tiene que salir idéntica a la del último paquete (ver
  [PRUEBAS.md](PRUEBAS.md)). Lo que es de una sola moto va en su `.ini` o detrás de una clave que en
  `tuning.ini` vale lo de antes.
- **Idéntico quiere decir bit a bit.** Cambiar el orden de una suma de floats ya cambia los resultados
  (ver "Orden de las sumas").
- **Todo número nuevo, con un valor por defecto que reproduce lo de antes**, registrado en
  `BikeParams::Register` (Bike.cpp) y comentado en `tuning.ini`.

## Bitácora

### Cola que raspa en el wheelie pasado (v0.2.4)
- **Pasaba**: pasado el punto de equilibrio, al apoyar la cola la moto rebotaba "de más" y las
  colisiones parecían buggeadas.
- **Por qué**: pasados ~70° la rueda trasera "se perdía". Su rayo va por el eje de la suspensión, que
  ya estaba casi acostado; la cubierta se metía en el suelo y al reencontrarlo la escupía.
- **Qué se hizo**: un patín de cola (caño de aluminio bajo el guardabarros y caja de colisión
  `BoxShape(0.12)` en (0, 0.04, -0.90), marcada con `kTailSkidTag`). Con la moto a más de 40° y
  tocando el piso, el rayo de cada rueda llega más lejos cuanto más acostado queda el eje de la
  suspensión (`reach = max(1, 0.7 / eje hacia abajo)` en BikePhysics.cpp), así sigue encontrando el
  suelo debajo de la cubierta. Al raspar hay chispas y sonido (`Scrape` en PhysicsWorld: muchas en pavimento,
  objetos y casas, pocas en tierra) y el piloto va de pie. Detalle en el README ("Wheelie pasado").
- **Trampa 1**: una caja de cola **más chica que la parte más fina del chasis** (0.12) achicaba el
  "radio interior" que Jolt calcula para la forma compuesta. Eso cambia el CCD (`LinearCast`) de toda
  la moto, y las regresiones de la motocross dejaban de dar idénticas. Con medio lado 0.12 no cambia.
- **Trampa 2 (orden de las sumas)**: el largo del rayo se escribe
  `above + travel + radius*1.6 + (reach-1)*(...)`. Poniendo el término nuevo en otro lugar de la suma,
  los floats cambian en el último bit y la motocross deja de dar idéntica aunque `reach` valga 1.
- **Se comprobó**: pruebas `colazo`/`colazof` (la telemetría imprime `scr`, la velocidad de raspado);
  motocross idéntica a la v0.2.4.

### Doblar en una rueda: la trasera no apuntaba hacia donde iba (Trilheira)
- **Pasaba**: haciendo grau y doblando, la moto quedaba apuntando para afuera de la curva.
- **Por qué**: en una rueda, la inclinación se sostenía alrededor del eje levantado de la moto, no de
  la línea del piso por donde va. Eso metía guiñada.
- **Qué se hizo**: `grau_turn` (la cubierta trasera curva la trayectoria y la moto gira a la par) y
  `grau_align` (alinear con la dirección de avance). Resultado: ~6° de deriva a 21 km/h en vez de ~34°.
- **Se comprobó**: `--map favela --flat --test graucurva` y `graulentoc`.

### "En primera le falta potencia" (Trilheira)
- **Qué se hizo**: relación corta (`engine_final_drive` 7.8, como la corona grande del grau) y
  embrague que patina hasta 5500 rpm (`engine_clutch_rpm`): el motor queda arriba de vueltas al salir.
- **Se comprobó**: 0-50 km/h en 2.4 s sin ahogarse (`--test arranque`).

### Escaleras de la favela: la moto rebotaba para todos lados
- **Pasaba**: bajando la escadaria (12 escalones de 0.9 m con 27 cm de alto, y 29 cm contra el terreno
  abajo) la moto salía disparada. Bajando a 20 km/h la trasera hacía tope con 67 000 N en un paso, la
  velocidad saltaba de 18 a 33 km/h y la trompa bajaba 61°. El bot se caía ahí en cada vuelta.
- **Por qué** (con `--wheel-log`): la rueda es un rayo por el eje de la suspensión, y ese eje está
  inclinado: atrás hacia atrás, adelante hacia adelante (el rake). Apenas la trasera pasaba el borde, su
  rayo pegaba en la contrahuella del escalón que acababa de dejar: una pared con normal horizontal. La
  cubierta "tangente" a esa pared salía comprimida de más, con 105 700 N. Subiendo, el rayo delantero
  veía la pedada del escalón y la tomaba como un plano infinito que estaba debajo de la rueda, aunque la
  rueda estuviera todavía detrás del borde: 60 000 N y la moto volcaba para atrás.
- **Qué se hizo**: sobre objetos (no sobre el terreno) la cubierta tantea su propio perfil
  (`BikePhysics.cpp`, "Objetos"):
  - 7 rayos verticales repartidos bajo el círculo de la rueda (±0.84 del radio);
  - el piso bajo el eje es el apoyo plano;
  - donde dos sondas vecinas cambian de altura hay un canto: se lo ubica por bisección (5 rayos) y la
    cubierta rueda alrededor de él (el círculo de la rueda pasa por el canto, con la normal del canto
    al eje);
  - vale lo primero que toca al extenderse;
  - una diferencia de altura que explica la pendiente de la sonda vecina (una rampa) no es un canto.

  Si ningún rayo toca un objeto, todo queda como antes.
- **Se comprobó**:
  - motocross idéntica (su pista no tiene objetos) y 1:08.37 / 1:08.51;
  - bajando: sin golpes ni saltos de velocidad;
  - el bot en la favela: de 3 caídas en tres vueltas a 0 (1:37-1:38);
  - subiendo (`--spawn 90 --test subida8|15`): ya no vuelca. La delantera trepa el primer escalón y la
    moto queda frenada contra el segundo. Es realista: con una rueda de 35 cm, un escalón cuadrado de
    27-29 cm es casi una pared (el canto queda a ~78° de la vertical); hay que levantar la rueda.
  - **Efecto buscado en la trial**: antes subía el cajón de 0.5 m a fondo sin levantar la rueda (el
    mismo defecto: la trompa se apoyaba en la tapa del cajón antes de llegar). Ahora sólo lo sube
    levantando la rueda (`--test grau`); a fondo queda contra la cara.

### Doblando en wheelie la moto quedaba cruzada (todas menos la Trilheira)
- **Pasaba**: en una rueda y doblando, la motocross y la dos tiempos quedaban apuntando 20-25° para
  afuera de hacia dónde iban (`--map favela --flat --bike <moto> --test graucurva`, columna `beta`). La
  Trilheira no, porque tenía `grau_turn`. Desde que la moto elegida vale en todos los mapas se nota más.
- **Por qué**: la trasera inclinada curva la trayectoria, pero sin la delantera nada orienta la moto; el
  empuje, atrás del centro de masa, la cruza.
- **Qué se hizo**: `wheelie_turn`/`wheelie_align` (900 y 4 en `tuning.ini`) hacen lo mismo que el grau
  sólo mientras se dobla de verdad en una rueda: más de 0.3 s con la trasera sola en el piso, y
  inclinación o deriva de más de 2-6°. En ese caso la moto se inclina alrededor de la línea del piso y
  gira a la par de su trayectoria. Derecha, o un instante en una rueda (un salto), no cambia nada. Las
  motos con `grau_turn` siguen con lo suyo.
- **Se comprobó**: ~7° de deriva en todas (la Trilheira ~6.5°), y todas las pruebas de la motocross
  idénticas (wheelie derecho, saltos, vueltas del bot).

### Caja automática con rpm fijas
- **Pasaba**: subía a 9400 rpm y bajaba a 4800 en todas las motos. La de carreras (pico a 15300) subía
  muy abajo: 0-200 km/h en 7.2 s. La trial (corte a 9000) no subía nunca.
- **Qué se hizo**: los umbrales se multiplican por `engine_rpm_scale` (con 1 dan exactamente lo mismo).
- **Se comprobó**: la de carreras hace 0-200 en 5.1 s; la motocross, la Trilheira y la 2T no cambian.

### Escaleras, segunda vuelta: la rueda se trababa al costado
- **Pasaba**: bajando sin seguir la línea (la prueba `bajada` sin dirección), la moto se iba al borde y se
  quedaba trabada meciéndose, con picos de 20 000 a 64 000 N en la delantera.
- **Por qué**: entre la punta de los escalones (media calzada + 0.2 m) y el murito (su cara, + 0.33 m)
  quedaba una canaleta con el terreno en pendiente al fondo. Entrando de costado, las sondas de la cubierta
  encontraban de golpe la pedada 24 cm más arriba.
- **Qué se hizo**: los escalones llegan al murito (+ 0.36 m). Sobre objetos, el piso bajo la rueda sube
  como mucho `0.02 + 1.5·v·dt` por paso (lo que permite rodar trepando); viniendo del aire, sin límite.
- **Se comprobó** (`--spawn 68 --test bajadaN`, que sigue la línea con la dirección del bot, y
  `tools/escalera.py 74 88`), con la Trilheira, la motocross y la trial de 8 a 35 km/h:
  - fuerzas de 2 000 a 13 000 N;
  - saltos de velocidad ≤ 2 km/h;
  - ninguna trabada ni caída;
  - a 35 km/h vuela de escalón en escalón y cae de trompa, como una moto de verdad.

### La de carreras: frenando fuerte se cruzaba y hacía wobble
- **Pasaba** (`--map base/circuito --bike base/carrera --test frenadaN`): frenaba ~1 g, pero quedaba
  cruzada ~9° (a 120 km/h). A 250 km/h la cola saltaba (se levantaba y volvía) y el manubrio oscilaba
  entre 0 y -18°.
- **Por qué**: con la trasera casi sin carga (~250 N de 2500), la moto es inestable de guiñada, como un
  auto que frena con las de atrás trabadas. En cuanto giraba un poco, la delantera, con el manubrio
  "fijo", empujaba de costado y la hacía girar más; la inclinación crecía sola. En una moto de verdad la
  rueda delantera es libre: el trail la hace seguir su propio camino y no empuja de costado.
- **Qué se hizo**:
  - `brake_align` (0 en `tuning.ini`, 4 en la de carreras): frenando fuerte y sin pedir curva, la
    delantera sigue su camino y la moto vuelve a apuntar hacia donde va;
  - el anti-levantamiento de la cola del ABS con umbral y fuerza por moto (`rear_lift_load`, `rear_lift_mitigation`).
- **Se comprobó**: de 80 a 280 km/h, 0.0° de deriva, sin mover el manubrio, sin levantar la cola, a
  0.85-1 g (`tools/frenada.py`). La motocross, idéntica.
- **Pendiente**: la motocross (con `brake_align` 0) todavía gira frenando a fondo con la S de 90-120 km/h.

### Golpe fuerte: el piloto sale despedido
- **Pasaba**: sólo había caída por inclinación. Contra un muro, el piloto salía recién cuando la moto se
  daba vuelta, y con la velocidad de después del golpe (casi quieto).
- **Qué se hizo**: `PhysicsWorld::LastImpact()` anota, de cada contacto nuevo de la moto (con su piloto)
  contra algo fijo, la velocidad con que se acercaban por la normal.
  - Umbrales: más de `crash_impact_speed` (7 m/s) contra algo de frente o de costado (normal casi
    horizontal), o de `crash_impact_vertical` (11 m/s) cayendo a plomo.
  - Pasado el umbral, caída: el ragdoll sale con la velocidad de antes del golpe y pasa por encima del
    manubrio o se estampa.
  - No cuentan la cola raspando ni los objetos sueltos; rozar un muro de costado tampoco (la velocidad
    hacia el muro es chica).
- **Se comprobó**:
  - la motocross da idéntica en todas las pruebas y vueltas;
  - la de carreras contra el muro del circuito a 16 m/s: el piloto sale volando;
  - el bot de la favela se estrella una vez contra una pared a 8.7 m/s.
- **Consecuencia**: el bot del circuito, que pegaba en las barreras y seguía, ahora se cae ahí (ver
  [MAPAS.md](MAPAS.md)).

### Derrape lento con el freno trasero (motocross)
- **Pasaba**: entrando despacio (9-15 km/h) y doblando con el trasero, la cola salía apenas 11-24° y la
  moto se frenaba antes de derrapar: no hay inercia para que la cola salga sola.
- **Qué se hizo**: `slide_pivot` (800 en `tuning.ini`, 0 en la de carreras): debajo de ~13 km/h, con el
  trasero y doblando, el piloto empuja la cola hacia afuera hasta el ángulo que busca.
- **Trampa**: la ventana de velocidad se mide con la velocidad real, no la de adelante. Derrapando con la
  cola a 33°, la de adelante es un 16% menor y el empuje se colaba al final del derrape rápido de siempre
  (la regresión `brakeslide` dejó de dar idéntica hasta cambiarlo).
- **Se comprobó**: cola afuera 22-40° de 9 a 16 km/h (`--flat --test brakeslideN`), sin caídas; el
  derrape de siempre (32 km/h) idéntico.

### Lisas en la tierra (la de carreras)
- **Pasaba**: el suelo tenía un solo factor de agarre para todas las cubiertas: la de carreras agarraba
  en la tierra casi como en el asfalto.
- **Qué se hizo**: cada cubierta multiplica el agarre del suelo según cuánto es pavimento:
  - `*_tire_loose_grip` en tierra y pasto (la de carreras: 0.35);
  - `*_tire_paved_grip` en asfalto, cemento y objetos;
  - `Terrain::PavedAmount`: en los mapas de calles, la calzada; en los de pista, nada.

  Con los dos en 1 ni se multiplica.
- **Se comprobó**: en un círculo sobre tierra la deriva pasa de 2.5° a 6.9° y la vuelta a la pista de
  motocross de 1:04.7 a 1:07.3 (con 0.45; después se bajó a 0.35); en el asfalto del circuito el
  pavimento vale 1 en toda la calzada (`--test profile` lo imprime).

### Ragdoll que se doblaba para cualquier lado
- Ver [PILOTO.md](PILOTO.md): los ejes del swing-twist estaban cruzados, y después los límites no
  aguantaban los golpes (hasta 50° de más). Se arregló con bisagras de Jolt en rodillas y codos, más
  pasadas del solver y las articulaciones puestas donde las tiene el modelo.

## Notas de Jolt

- **SwingTwistConstraint**: espacio del constraint X = twist, Y = normal (plane × twist), Z = plane.
  El giro alrededor de **Z** (el eje "plane") lo limita `mNormalHalfConeAngle`, y el giro alrededor de
  **Y** lo limita `mPlaneHalfConeAngle`. Los nombres confunden: se leen al revés de lo que parece.
  Los límites son simétricos; para un rango asimétrico, el eje twist del padre se centra a mitad del
  rango. `GetRotationInConstraintSpace()` + `Quat::GetSwingTwist` da lo mismo que usa Jolt para medir.
- **HingeConstraint**: rango asimétrico real (`mLimitsMin` en [-π, 0], `mLimitsMax` en [0, π]). El
  ángulo 0 es cuando `mNormalAxis1` y `mNormalAxis2` coinciden; crece con la mano derecha alrededor de
  `mHingeAxis1`. Para que el ángulo actual sea el de la pose, `mNormalAxis2` es la dirección actual.
- **Pasadas del solver por constraint**: `mNumVelocityStepsOverride` / `mNumPositionStepsOverride`.
  Rigen para toda la isla (se usa el máximo), así que sólo cuestan cuando el ragdoll existe.
- **Partes chicas y rápidas**: `EMotionQuality::LinearCast`, para que no atraviesen el suelo.
- **Cuerpos**: el mundo admite hasta 8192 (la favela tiene ~1050 casas con colisión; el circuito ~2950
  cajas).
- **Rayos que empiezan adentro de un cuerpo**: Jolt devuelve un choque en la fracción 0. Las sondas de
  la cubierta descartan esos (`mFraction > 0`): un rayo que arranca adentro de un escalón es una pared.

## Terreno

- El tamaño y la resolución son por mapa (`terrain.size`, `terrain.resolution`). La cantidad de
  muestras por lado, N, se redondea a múltiplo de 8 y va de 64 a 2048 (`Terrain::Build`).
- La simulación va a 120 Hz con 1 collision step.
