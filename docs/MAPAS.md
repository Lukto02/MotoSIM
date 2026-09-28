# Mapas y terreno

El formato completo está en `MODDING.md`. Esto es lo aprendido armándolos.

## Terreno

- `terrain.size` (ancho en m, 64..4096) y `terrain.resolution` (m entre muestras, 0.25..4). Hay como
  máximo 2048 muestras por lado, y la cantidad se redondea a múltiplo de 8: un mapa grande necesita una
  grilla más gruesa (`"size": 1000, "resolution": 1`). Default: 255.5 m a 0.5 m (512 muestras), el de
  siempre.
- `edge` levanta el borde del mapa (pared natural); 0 = sin pared.
- `heightmap`: PNG en escala de grises estirado sobre todo el mapa (arriba de la imagen = norte).
- `dunes` (`Terrain::Dunes`): médanos transversales al viento (`wind`), barlovento suave que se empina
  hasta la cresta (un labio natural) y sotavento empinado que arranca plano en la cresta. Las crestas
  serpentean (`meander`) y se cortan en tramos; hacia el borde crecen (`border`). Sin azar, y los rumbos
  múltiplos de 90° (`wind`, el `yaw` de las formas) salen exactos, sin sin/cos (`Heading`): igual en
  todas las PCs. Un rumbo cualquiera usa sin/cos del sistema: 1e-7 m de diferencia, sin umbrales que la
  agranden (no es como las casas de la favela).
- `terrain.shapes` (`Terrain::PrepareShapes` / `ApplyShapes`): formas esculpidas en el suelo natural,
  en cualquier tipo de terreno. Un perfil de costado (tramos rectos, esquinas redondeadas con dos
  pasadas de promedio de `smooth` m, en una tabla cada 0.1 m) estirado de costado (`line`) o girado
  alrededor de un centro (`round`), que se suma al suelo o lo nivela (`level`). Sin formas, `Ground` es
  el de siempre. La pista (`track`/`street`) se estampa encima: una forma que la pista pisa queda
  aplanada por la base suavizada de la pista.
- Pista `guide`: la vuelta no toca el terreno (ni base suavizada, ni franja de tierra, ni peraltes) y no
  lleva obstáculos ni estacas. Es para andar libre: el bot, el reaparecer y la vuelta la siguen igual.
- `ground_textures: "sand"`: las dos capas del suelo son arena (textura `GenSandTexture`, ver
  [RENDER.md](RENDER.md)), el color del vértice da el tono (manchones de ~80 m), pocas matas secas sólo
  en lo plano y poco polvo (`Dustiness` 0.25).

## Generadores

- `favela`: casas (~1050, con colisión, se puede andar por las lajes), postes y cables, carteles,
  banderines, pipas y el fondo de Río, todo armado alrededor de la pista con **semilla fija**: iguales en
  todas las PCs (necesario para la red). La cámara no atraviesa las paredes.
- `circuit`: circuito de asfalto (`Circuit.cpp`). Va con `"ground_textures": "circuit"` (asfalto en la
  pista y pasto cortado en el resto, con matas de pasto fuera de la pista) y `"track": {"style":
  "street"}`. Todo sale de la forma de la pista, siempre igual:
  - líneas blancas y pianos puestos por la curvatura;
  - largada con grilla y pórtico con semáforo;
  - boxes con calle, muro y garajes; tribunas;
  - leca afuera de las frenadas fuertes; muro con alambrado, gomas y air fence;
  - carteles (textos en la sección `"circuit"` del mapa), puente, árboles y cerros.

  Al cargar imprime cuántas cajas puso y avisa si alguna tapa la pista.

## El bot en cada mapa

- `bot_speed` (m/s, default 17) es lo más rápido que va el bot; `bot_lateral` (m/s², default el de la
  línea de comandos, 6) cuánto encara las curvas. Mira adelante `max(60, v²/8 + 20)` m.
- El bot no sabe levantar la rueda delantera: en mapas de obstáculos (trial) los cajones grandes
  necesitan una rampita adelante y un `bot_speed` bajo (7 m/s en el de trial).
- Si queda trabado 4 s contra algo, reaparece.

## Mapas de prueba

En `pruebas/mods/prueba/` (no se empaquetan): el circuito de obstáculos de trial
(`trial_obstaculos`, con variantes a otra velocidad del bot y con la motocross), escalones de 0.5, 0.75
y 1 m, y los mapas de siempre con la trial. Ver [PRUEBAS.md](PRUEBAS.md). `medanos_crater` y
`medanos_ola`: Los Médanos con la vuelta guía por el Cráter y por la Ola (mismo terreno; los escribe
`tools/medanos.py`).

## Medir un salto

`python tools/saltos.py <exe> <mod/mapa> <spawn> <s del labio> 40,50,60 [bajada|bot|ambos]`: aparece en
la pista, entra al salto a cada velocidad y resume el vuelo (velocidad al despegar, aire, altura, vy al
tocar, cabeceo, compresión, si se cae y si fue golpe). Dos pilotos: `bajada` es `--test bajadaN` ("sin
tocar nada": sostiene N con el gas, el cuerpo quieto) y `bot` es el bot a N km/h (en el aire suelta el
gas y busca caer con la trompa 8° abajo, como un piloto que acomoda el cuerpo; la velocidad sale de una
copia temporal del mapa con otro `bot_speed`). La s del labio sale de `--test profile`.

- **Ojo con la carpeta de mods**: desde `pruebas/` el juego ve `pruebas/mods` y la carpeta `mods/` de al
  lado del exe, que es la copia del momento de compilar: un mapa de `mods/sandbox` recién cambiado se ve
  viejo. `saltos.py` usa copias en `pruebas/`; a mano, correr desde la raíz del proyecto.
- Para diseñar antes de medir, `tools/medanos.py --sim` simula cada salto del mapa de médanos como un
  punto pegado al suelo que se despega donde la curvatura supera a g·cos θ/v², y el vuelo. Da bien el
  orden de las cosas (aire y largo dentro del 10-20%), pero no el cabeceo ni la suspensión.

## Lecciones por mapa

### Autódromo Sierra de los Vientos (circuito)
Lo hizo un agente; su informe tiene el trazado curva por curva:
- 3757 m, 14 m de ancho, recta de 950 m;
- horquilla de ~23 m de radio y chicana de ~19 m.

Lo aprendido:
- **Mapa grande**: 1380 m a 1 m de resolución carga rápido y el terreno solo sigue a 60 fps. La pista a
  más de 100 m del borde (la subida del borde empieza en tamaño/2 − edge − 2 m).
- **Imagen de alturas**: un PNG de 8 bits y 256 px alcanza (10 m de blanco ≈ 4 cm por escalón de gris).
  Las lomas anchas y bajas (≤ 6.5 m en más de 200 m) evitan crestas.
- **Agarre**: el asfalto tiene 1.08 y el pasto 0.84. Eso decide todo: una moto que se abre al pasto no
  vuelve a la línea.
- **Escapatorias**: las primeras barreras, a hw+13, las chocaba el bot en casi todas las salidas (vueltas
  de 2:31 a 3:04-3:17; de 100 a 15 km/h en medio segundo). Lo que anduvo:
  - barrera base a hw+20;
  - hasta 35 m más por afuera, y 150 m más allá de cada salida;
  - por adentro, a 0.8 R (0.42 R con radio menor a 40 m);
  - boxes a 200 m o más de la última curva;
  - nunca más cerca que la mitad de la distancia a otro tramo de la pista.
- **Pianos**: el de adentro donde la curvatura pasa el 55% de la del vértice; el de salida desde el 80%
  pasado el vértice hasta el final de la curva + R/4. Las calcomanías a 1-6 cm del suelo no parpadean.
- **Rendimiento**: la primera versión tenía 1.36 M de vértices y 32.7 ms por cuadro; bajó a 793 k y 17 ms
  con:
  - tramos de barrera de 6 m;
  - árboles más livianos;
  - el público sólo a menos de 260 m;
  - saltearse los bloques fuera de la vista.
- **Cómo se validó**: el trazado se diseñó en Python como rectas y arcos, con el cierre de la vuelta
  resuelto solo, y se pasó por el mismo spline y suavizado de curvatura que `Track.cpp` (largo, radios,
  distancia mínima entre tramos: 59 m). En el juego:
  - vueltas del bot sin ventana, contando golpes (bajar más de 35 km/h en medio segundo) aparte de las
    caídas;
  - con la colisión del circuito apagada volvían los 2:31: así se supo que eran las barreras;
  - el aviso de "caja sobre la pista" encontró un muro de 22 m que tapaba el comienzo de la recta.
- **El bot** en el circuito: `bot_lateral` hasta ~75% del `max_lateral_accel` de la moto. Con la de
  carreras (13), 10 daba ~2:01-2:06 con alguna caída.
  - Desde que un golpe fuerte tira al piloto, cada vez que el bot se abre y pega contra una barrera, se
    cae (antes rebotaba y seguía): ~5 por vuelta. Con las lisas en el pasto, además, no vuelve a la línea.
  - El mapa usa `bot_lateral` 7 y `bot_lean_throttle` 0.8 (acostado abre menos el gas): ~2:21 y unas 2
    caídas por vuelta.
  - `bot_brake` (m/s² del plan de frenada) existe, pero frenar más tarde (7) empeoró las entradas a la
    horquilla.
- **Pendientes**:
  - el bot con la de carreras: seguir la línea sin abrirse en las salidas (hoy pega en las barreras);
  - leca y boxes son sólo visuales (físicamente son pasto; las matas de pasto se ven a través);
  - el plano lejano de la cámara (900 m) es más corto que la diagonal del mapa;
  - al bot le falta `bot_brake`, bajar el gas según la inclinación y no serpentear a alta velocidad.

### Morro do Grau (favela)
- La ladeira recta (~20%) cruza las cuatro calles del zigzag. Se hizo con `terraces`: mesetas a nivel de
  cada calle y labios entre una y otra (se vuela y se cae en la meseta de abajo).
- La escadaria son 12 cajas de 0.9 m con 27 cm de alto (cada una a la altura de su punta de arriba) más
  29 cm contra el terreno abajo. Llegan hasta los muritos: con una canaleta al costado la rueda caía al
  terreno y pegaba en la punta de los escalones. Hacía volar la moto hasta que la cubierta aprendió a apoyar en los
  cantos (ver [FISICA.md](FISICA.md)). Desde entonces el bot da 3 vueltas sin caerse (1:37-1:38).
- **Pendiente: la curva de abajo de la bajada (s≈246-264)**. El bot llega a ~43 km/h después de la loma de s≈241
  (vuela y cae de trompa, sin poder doblar) y pega contra la pared de afuera a ~28 km/h en casi todas las vueltas,
  con la Trilheira, la motocross y la 2T (la trial llega más despacio y no). Es casi toda la cuenta de caídas de la
  favela (con la Trilheira, 3-9 en 45 min, la mitad o más ahí). El plan del bot en las calles (frenar a 2.5 m/s²) no
  cuenta la pendiente ni el vuelo de la loma. Visto en la revisión de las motos después de la v0.2.7 ([FISICA.md](FISICA.md),
  "La Trilheira: frenando fuerte hacía un trompo").

### Los Médanos (dunas, andar libre)
`mods/sandbox/maps/medanos.json`, generado por `tools/medanos.py` (los perfiles de los saltos están ahí,
con nombre). 515 m a 0.5 m (1032² muestras), Motocross 450, atardecer y arena. Todo en `terrain.shapes`
en modo `level` (cada forma aplana los médanos debajo y deja sus pasillos), con una vuelta guía de
1236 m por lo mejor:

| Qué | Dónde | Forma | Medido (bot / "sin tocar nada") |
|---|---|---|---|
| El Gigante | playa, hacia el este desde la largada | mesa de 6 m, patada de 42°, 16 m de mesa, bajada de 36° que sigue 5 m bajo la playa | bot 40-65 km/h sin caerse, 1.4-3.1 s y hasta 7 m; 70+ se pasa la bajada (golpe). Mortal: `--spawn 104 --drop 1 --test flip`, 358° y cae a los 3.1 s en la bajada |
| Médano Grande | x = 75, hacia el norte | 22 m: barlovento de 115 m (12°), labio de 18°, sotavento de 36° que sigue 8 m bajo el suelo, medialuna (`bend` 22) | bot 40-55 km/h: 1.85-2.75 s, 4-8 m sobre la cara; 60+ se pasa (golpe). Sin tocar nada clava la trompa (-62°): se cae desde 50 |
| Los Lomos | franja norte, al oeste | 4 lomas de 1.3 m cada 14 m | 40-75: 0.75-1.3 s, se dobla a 70+ |
| La Escalera | x = -180, hacia el sur | mesas de 1.4, 2.6 y 4 m con bajadas que siguen 0, 1 y 2 m bajo el suelo | 40-75 km/h las tres, los dos pilotos (0.6-2.75 s; la grande sin tocar nada se cayó una vez, a 65) |
| El Cráter | (172, 55), fuera de la vuelta | bowl de 50 m: fondo a -2.5, paredes de 55° hasta el borde a 5 m, ladera de 18° afuera | bot 25-55 lo cruza (entra por el borde, sale volando por la pared de enfrente: 2.7 s a 55); sin tocar nada se cae a 45 |
| La Ola | (165, -150), frente a la playa | pared de 6.5 m que termina a 55° (quarter), cresta de 1.2 m y espalda de 38° | 40-60 km/h: tira para arriba 2.4-3 s, hasta 13 m, y cae en la espalda (bot y sin tocar nada); a 30 sin tocar nada llega arriba sin velocidad y se cae |

El bot (`bot_speed` 15, 54 km/h) da la vuelta en 86.4-86.9 s sin caerse (4 vueltas); 58-60 fps con
ventana en esta PC.

Lo aprendido:
- **La pista borra los saltos**: `StampTrack` pone la base de la pista (promedio de ±15 m, 4 pasadas) y la
  mezcla con el terreno hasta hw + 9 m: una forma que la pista pisa queda aplanada. → Pista `guide`, que
  no toca el terreno (ni franja de tierra ni estacas) y no lleva obstáculos (el bot frenaría y el
  reaparecer retrocedería por saltos que no están).
- **Terreno y no objetos**: una rampa (`objects`) es una cuña de caras planas con cantos; una forma del
  terreno es suelo liso con radios, y el labio y la bajada se diseñan con la curva que hace falta.
- **Cuánto vuela**: el largo crece con v² (10% más rápido, 20% más lejos) y para que caiga bien la
  bajada tiene que estar donde baja la parábola y con su ángulo. Ningún perfil sirve a todas las
  velocidades; lo que abrió las ventanas:
  - **mesa** entre el labio y la bajada: el que llega corto cae en plano desde poca altura;
  - **bajada que sigue bajo el suelo** (hoyada) con un fondo de radio chico: el largo cae todavía en
    bajada. La Escalera así aguanta de 40 a 75 km/h;
  - una **S suave** (smoothstep, ≤ 8°) para salir de la hoyada: la primera salida era una recta de 9.5°
    que terminaba en plano, un kicker sin querer (1.34 s de vuelo a 58 km/h, se vio con el bot).
- **Cuánto aguanta la moto**: soltada en plano a 12 m/s, recién se cae desde 20 m (golpe del chasis a 16
  m/s; desde 15 m, vy = -17 m/s, aterriza a tope de suspensión). En bajada aguanta vy de -19 a -22 m/s.
  Lo que tira al piloto es pasarse la bajada y caer en plano desde 10 m o más (golpes de 12-20 m/s).
- **Médano grande**: desde una cresta de 22 m a 60 km/h se vuela 45 m; para caer en la cara haría falta
  un médano de ~30 m. Se probó patear más el labio (22° con radio 12 y 26° con radio 10), una meseta
  arriba y bajar el fondo: la ventana se corre pero no se agranda. Quedó 40-55 km/h, que es lo que hace
  un piloto en un médano de verdad (se sube a fondo y se suelta antes de la cresta).
- **Sin tocar nada, el nudillo de una cresta clava la trompa**: la delantera pasa la cresta y cae
  mientras la trasera sigue empujando; en 2 s de vuelo llega a -62° (-89° con más patada) y se cae. En
  las mesas la cara levanta la trompa (+22 a +42° al despegar) y cae de -12 a -43°, sin caerse. En el
  Médano hay que tirarse atrás (lo dice el `hint`).
- **Paredes de 55°** (Cráter y Ola): la moto las sube y sale casi vertical; el campo de alturas a 0.5 m
  las representa bien (entre muestras de 2 m el perfil marca 111%).
- **Diseño**: primero en Python (`tools/medanos.py --sim`, el punto pegado al suelo y el vuelo) y después
  con el juego (`tools/saltos.py`, bot y "sin tocar nada" de 40 a 75 km/h); el plano de arriba con curvas
  de nivel se sacó con una copia en numpy de `Terrain::Ground`.
- **Cómo se ve**:
  - el primer tono de la arena (manchones de 6 m) parecía camuflaje de arriba → manchones de ~80 m y
    sólo ±4% de cerca;
  - con el preset `sunset` las caras a la sombra salían violetas y oscuras (rebote del suelo 118, 86, 66)
    → `ground` color arena (214, 164, 112) y el sol más alto (0.42);
  - la pared del borde (`edge`) se veía como un muro parejo → médanos más altos cerca del borde
    (`border`) y `edge` 12;
  - las crestas cortadas con `ValueNoise` quedaban como dientes parejos → `Fbm`;
  - el polvo que levantan las ruedas lo pinta `Game.cpp` color tierra: sobre la arena queda una estela
    de humo gris → en la arena `Dustiness` 0.25 (como el pasto) hasta que el color salga del suelo.
- **Se comprobó**: regresión contra la v0.2.6 idéntica (brakeslide, flip, bot 40 s, accel, grau en la
  favela, parque, valle, favela con bot) con sólo estos cambios sobre `HEAD` y con el árbol entero; bot
  de motocross 1:08.37 / 1:08.51; favela 1052 casas.

Pendientes:
- Reaparecer donde uno se cae (hoy va al punto de la guía más cercano, que en el medio de los médanos
  puede quedar a 100 m) y esconder el tiempo de vuelta en los mapas sin pista (`Game.cpp`).
- El color del polvo y de la tierra que levantan las ruedas según el suelo (en la arena, arena clara) y
  subir `Dustiness` de la arena.
- Agarre propio de la arena (hoy es el del pasto: 0.82).
- Un test que entre a N km/h con el cuerpo atrás o adelante en el aire (hay `bajadaN`, sin tocar nada, y
  el bot, que busca -8°).
