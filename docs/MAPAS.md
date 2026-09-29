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
  aplanada por la base suavizada de la pista (hay que dejarlas a más de hw + 11 m de la línea central).
  - `arc` (sólo `round`): nada más un sector, de un rumbo a otro visto desde el centro; pasando sus
    bordes se desvanece con la distancia al rayo del borde, en `edge` m. Es para los **peraltes**. El
    rumbo sale de `atan2` y `sin` del sistema (1e-7 m entre PCs, sin umbrales que lo agranden). Sin `arc`,
    todo igual que antes.
  - `dirt`: la forma se pinta con la tierra de la pista (`trackMask` = su cobertura: textura, agarre 1.0
    en vez de 0.82, polvo, sin matas). En un mapa de pasto los saltos de tierra se leen como saltos.
    `ShapeCover` da la cobertura (la misma cuenta que `ApplyShapes`, que ahora la usa).
- Pista `guide`: la vuelta no toca el terreno (ni base suavizada, ni franja de tierra, ni peraltes) y no
  lleva obstáculos ni estacas. Es para andar libre: el bot y la vuelta la siguen igual; el jugador reaparece
  donde quedó y no tiene cronómetro (ver [MENU.md](MENU.md), "Mapas libres").
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
y 1 m, y los mapas de siempre con la trial. Ver [PRUEBAS.md](PRUEBAS.md). `medanos_crater`,
`medanos_ola`, `medanos_grande` y `medanos_montes`: Los Médanos con la vuelta guía por lo que la vuelta
no pisa (mismo terreno; los escribe `tools/medanos.py`). `park_tierra`, `park_madera`, `park_pump`,
`park_plaza` y `park_bowl`: el Parque de física con una vuelta guía (estilo `guide`: no estampa la pista)
por cada zona, y `park` con la trial (los escribe `tools/parque.py`).

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
- **Una línea entera**: `python tools/tramo.py <exe> <mod/mapa> <spawn> <s0> <s1> 40,50,60,70 ambos`
  lista todos los vuelos entre s0 y s1 y dónde se cae, con los dos pilotos. En una línea de saltos
  seguidos o un serrucho, cómo se cae en uno depende de cómo se llegó del anterior (velocidad y cabeceo):
  `saltos.py` solo no alcanza. "Sin tocar nada" no frena en las curvas: una caída pasando s1 suele ser la
  curva que viene.
- **Todos los de Los Médanos**: `python tools/medanos.py --labios <exe>` da la s exacta de cada labio
  (del perfil de la vuelta) y `--medir <exe> 40,50,60,70 [nombre]` corre `saltos.py` en cada uno.

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
  - el público sólo a menos de `graphics.detailRadius` (260 m entonces; hoy 350 con un fundido de 40 m, ver
    [RENDER.md](RENDER.md), "Terreno, pasto y distancia de dibujado");
  - saltearse los bloques fuera de la vista (`Renderer::BoxVisible`; lo usan también `Terrain::Draw` y `Favela::Draw`, que
    guarda la caja `lo/hi` de cada bloque de 32 m).
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

### Looks por mapa después del pulido gráfico (septiembre de 2026)

Sólo visual (`look` de los JSON y el preset `sunset` de `Maps.cpp`): no cambia geometría, `ground_textures` ni física.
Contexto de render (estilo de color del post, `Ambient()` nuevo, colores de vértice del terreno) en
[RENDER.md](RENDER.md).

| Mapa | Cambio | Motivo |
|---|---|---|
| motocross | `fog` 0.0034 explícito (antes el default 0.0042) | el "100%" de niebla valía 4 veces distinto entre mapas; el look de día no se toca |
| park y `park_*` | `fog` 0.0023 (antes 0.0028; 0.0026 en la primera pasada) | igual; la pared lejana se lavaba (ver "Revisión de los críticos" en [RENDER.md](RENDER.md)) |
| circuito, valle | sin cambios (0.001, 0.0032) | |
| favela y `favela_trial` | `look` explícito: `sun_dir [-0.52, 0.42, -0.74]`, `sun_color [2.6, 1.78, 1.02]`, `zenith [62, 108, 158]`, `horizon [232, 168, 122]`, `ground [126, 106, 90]`, `fog` 0.0024, `exposure` 1.0; el mismo en el preset `sunset` de `Maps.cpp` | sombras mauve y rojos oscuros |
| medanos y `medanos_*` | `look` explícito: `sun_dir [-0.80, 0.46, -0.30]`, `sun_color [2.61, 1.85, 1.08]`, `zenith [66, 108, 178]`, `horizon [228, 178, 140]`, `ground [144, 108, 79]`, `fog` 0.0016, `exposure` 0.9 | arena quemada |

- **La favela salía mauve.**
  - *Por qué*: el preset `sunset` viejo tenía el rebote del suelo en (118, 86, 66) y el cenit a (64, 96, 168): las caras
    a la sombra y el asfalto (a la sombra y a pleno sol) daban tono 355° (rosa violáceo). Con el estilo de color y el
    `Ambient()` nuevo, además, `exposure` 1.05 sumaba una exposición de más.
  - *Qué se hizo*: el `look` de la tabla (sol más alto y menos rasante, rebote más claro y cálido, cenit menos violeta),
    escrito completo en el JSON y en el preset para que un cambio futuro del preset no lo mueva.
  - *Se comprobó* (Alto y Medio, `--noprefs --size 1280 720 --nohud --test pose`, vistas `200 12 3`, `180 5 1.7`,
    `200 25 12`, y el bot a los 12 s): asfalto a la sombra de (132, 88, 91), tono 356°, a (132, 101, 88), tono 17°; píxeles
    con tono 270-345° y saturación > 0.10 (casas rosas y violetas incluidas) 4028 / 1026 / 10902 → **363 / 514 / 3412**;
    luminancia media 0.47 / 0.35 / 0.48 → **0.50 / 0.43 / 0.52** (bot: 0.35 → 0.41-0.47 según la corrida; criterio ≥ 0.38); rojos quemados
    (R > 0.95 y G < 0.35) 42 / 550 / 446 → 34 / 314 / 274; las paredes rojas siguen en (135, 43, 24).
- **Los Médanos, arena blanquecina y sin volumen.**
  - *Por qué*: con la arena nueva de `Terrain.cpp` (albedo más bajo) y el look viejo (`ground` 214, 164, 112, exposición
    1.05) la luminancia daba p5 / p50 / p95 0.46 / 0.67 / 0.74 en la vista general y el bot 0.41 / 0.70 / 0.81: las
    caras a la sombra casi tan claras como las del sol, poco relieve.
  - *Qué se hizo*: el `look` de la tabla. Se pidió ±10% de `sun_color`, `ground` y `exposure` sobre lo de la
    especificación (`sun_color [2.9, 2.05, 1.2]`, `ground [160, 120, 88]`, exposición 1.0): los tres al límite
    (−10%). Un primer intento con sólo `sun_color` −7% y `ground` −10% casi no movía la luz (0.708 de mediana): la
    curva ACES aplasta el sol, la exposición es la que la baja.
  - *Se comprobó* (mismas vistas, Alto y Medio): p5 / p50 / p95 **0.40 / 0.68 / 0.74-0.77** (objetivo ≈ 0.35 / 0.66 /
    0.75; con los valores de la especificación y `exposure` 1.0 daba 0.44 / 0.72 / 0.78-0.80), el bot 0.33 / 0.70 / 0.82 con cielo;
    las caras de las dunas a contraluz y la sombra de la moto se leen. Alto y Medio dan lo mismo (las sombras cercanas no cambian la luz).
    La curva de p5 queda algo por encima del objetivo: ir más abajo pedía pasar del −10%.
- **Motocross, circuito, park y valle** (sin tocar el look de día): luminancia p95 del bot a los 12 s motocross 0.82 y circuito
  0.90 (criterio ≥ 0.75, con píxeles blancos: 369 y 403); park 0.86; valle 0.70 (verde sin virar a lima, ver RENDER.md).
  La niebla nueva casi no mueve los promedios (motocross 0.612 → 0.611).
- **Regresión**: telemetría del bot y de las pruebas de `regresion.sh` **idéntica** contra un exe armado con las fuentes de
  antes: `--flat` brakeslide, cuerpo, flip, whip y wheelie, `--test accel`, bot 40 s de motocross, favela (1052 casas, 48
  postes, 132 cables), circuito, médanos, park y valle, y el bot de la motocross 150 s en **1:08.73 / 1:08.42**. El `look` no
  entra a la física (con `--headless` `SetLook` ni se llama).
- **Los generadores escriben el look**: `tools/medanos.py` (plantilla del JSON, para los `medanos_*` de prueba) y
  `tools/parque.py` (`fog` del parque) llevan los valores nuevos; si no, regenerar los mapas de prueba los volvía a
  dejar con el look viejo.
- **Lecciones**: (1) sobre el estilo de color del post (exposición 1.05, saturación 1.08) y el `Ambient()` nuevo, un
  `look` calibrado antes de ellos sale quemado: hay que recalibrar sunset, no sólo comprobarlo; (2) para bajar la luz de
  un mapa con ACES la palanca es la `exposure`: bajar el `sun_color` un 7% casi no mueve la mediana; (3) el "mauve" se
  mide con tono (270-345°) y saturación, no con `R-G` y `B-G`, que también agarra los rosas del cielo; (4) probar un
  `look` sin tocar el proyecto: un `mods/<mod>/maps/<id>.json` en la carpeta desde donde se corre el exe (la carpeta
  actual gana por id) y capturas antes / después con el mismo exe; (5) un nivel de niebla del jugador multiplica lo que
  trae el mapa: la niebla base de cada mapa tiene que ser comparable (0.0016-0.0034).

### Los Médanos (dunas, andar libre)
`mods/sandbox/maps/medanos.json`, generado por `tools/medanos.py` (los perfiles de los saltos están ahí,
con nombre). 512 m a 0.5 m (1032² muestras), Motocross 450, atardecer y arena. Todo en `terrain.shapes`
en modo `level` (cada forma aplana los médanos debajo y deja sus pasillos).

**v2** (a pedido del usuario: "tiene todas las montañas muy separadas", y le gustó el serrucho de Los
Lomos). La v1 tenía una vuelta de 1236 m con seis cosas, a 90-150 m una de otra. Ahora la vuelta guía
(1326 m) va por el borde de la zona de médanos cargada de saltos, con peraltes de arena en las cuatro
esquinas, y lo grande para andar libre quedó adentro, a menos de 100 m entre sí. Los médanos naturales,
más juntos (`wavelength` de 64 a 52).

| Qué | Dónde | Forma | Medido (bot / "sin tocar nada") |
|---|---|---|---|
| El Gigante | playa, al este desde la largada; labio en s = 54 | mesa de 6 m, patada de 42°, 16 m de mesa, bajada de 36° que sigue 5 m bajo la playa (el de la v1) | bot 40-65 km/h sin caerse, 1.5-3.1 s y hasta 7 m; 70 está en el límite (una vez golpe, otra no); sin tocar nada 40-70 sin caerse. Mortal: 358° (v1) |
| La Cadena | x = 140, al norte; labios en s = 284, 322, 370, 420, 458, 514 | seis mesas, cada una distinta: 2.0 m y 22°; "el pozo", 2.6 m y 28°; "el trampolín", 3 m y 34° con cara de radio 8 (para whips); "el largo", 1.8 m y 18°; "el escalón", 3.2 m y 26° con 12 m de mesa; "la grande", 4.4 m y 32° con 14 m de mesa. Bajadas de 20-30° que siguen 0.3-1.5 m bajo el suelo, salida en S y la cara del siguiente pasando donde cae a 70 km/h: de labio a labio 38-56 m | 40-70 km/h las seis, los dos pilotos, sin caerse: 0.5-2.5 s y hasta 8 m; vy al tocar hasta -13.8 m/s (la grande a 70, suspensión a tope) |
| El Serrucho | z = 195, al oeste; desde s = 664 | cuatro lomitos de 0.7 m cada 10 m, 20 m de llano, Los Lomos (cuatro de 1.3 m cada 14 m, los de la v1), 14 m de llano y dos lomazos de 2 m cada 20 m | bot 40-70 sin caerse (0.5-1.1 s por loma, dobla desde 55); sin tocar nada 40-55 bien, a 60-70 se encabrita en Los Lomos y se cae |
| La Escalera y Los Dientes | x = -150, al sur; labios en s = 944, 984, 1066 y desde 1166 | las tres mesas de la v1 (1.4, 2.6 y 4 m) con 8 m entre la salida de una y la cara de la otra; seis dientes de 1.1 m (subida de 8 m que se curva hasta la cresta, bajada en S de 3.5 m) | 40-70 los dos pilotos: mesas 0.7-2.65 s, dientes 0.5-0.9 s |
| Peraltes | las cuatro esquinas | `round` con `arc` de 90°: piso hasta 28 m, pared cóncava (radio 14) hasta 36° y 3.4 m, lomo de 1.5 m y espalda de 24°; la guía va a 31 m (0.3 m de alto, 12°) | el bot dobla a ~12 m/s; más rápido se sube a la pared |
| Médano Grande | x = -30, sale de la playa; labio en z = -70 | el de la v1: 22 m, barlovento de 115 m (12°), labio de 18°, sotavento de 36° que sigue 8 m bajo el suelo | como en la v1: bot 40-55 sin caerse (1.7-2.7 s); 60 se pasa (golpe); sin tocar nada clava la trompa y se cae desde 50 (`prueba/medanos_grande`) |
| El Cráter | (-30, 112) | el de la v1 (bowl de 50 m, paredes de 55°) | bot 25-55 lo cruza; sin tocar nada se cae a 45 (`prueba/medanos_crater`) |
| La Ola | (40, -118), frente a la playa | la de la v1 (6.5 m, 55° arriba, espalda de 38°), con 30 m de entrada | bot 30-60 tira para arriba 1.8-3.1 s y cae en la espalda; sin tocar nada 40-60 igual, a 30 no llega (`prueba/medanos_ola`) |
| Los Montes | plaza nivelada de 50 m en (66, 80) | siete mesas redondas de 2.5-4.5 m, cima plana de 4-6 m de radio y laderas de 18-24°, a 33-35 m una de otra: se sube por cualquier lado | bot y sin tocar nada 30-60 km/h pasando por tres a cinco, sin caerse (`prueba/medanos_montes`) |

El bot (`bot_speed` 15, 54 km/h) da la vuelta en 97.7-98.0 s sin caerse (5 vueltas; la v1, 86.4-86.9 s),
un 30% del tiempo en el aire. 53-60 fps con ventana (la v1, medida a la misma hora, 53).

Lo aprendido en la v1:
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

Lo aprendido en la v2 (más juntos):
- **Más juntos, el que se pasa cae en la cara del siguiente**: con la primera Cadena (llanos de 3-16 m entre
  salto y salto) la simulación daba, de 55 km/h para arriba, caídas sobre la subida del salto siguiente o
  sobre la S que sale de la hoyada (-10 a -34°, vn de 12 a 18 m/s): pegar contra una pared. → Cada salto
  lleva su bajada, una hoyada chica (0.3-1.5 m, no 2.5-3.5) con una S de 12-18 m (caer en la S, ≤ 6°, está
  bien: vn ≤ 13) y llano hasta **pasar donde cae a 70 km/h** (30-37 m desde el labio para mesas de 2-4.4
  m). Así quedaron a 38-56 m de labio a labio (en la v1, a 90-150). Medido: 40-70 los seis, los dos
  pilotos. La cara del siguiente arranca en el fondo de la hoyada: `Perfil` (en `tools/medanos.py`)
  encadena las piezas desde donde terminó la anterior.
- **En una cadena el bot no llega a su velocidad**: a 70 despega a 49-58 km/h en los del medio, porque
  en el aire suelta el gas y cada aterrizaje frena. Por eso se miden en fila (`tools/tramo.py`) y no
  sueltos.
- **Serrucho: el cabeceo crece de loma en loma**. Sin tocar nada, cada loma deja la moto cabeceando ±20-50°
  y la siguiente lo agranda. Los Lomos solos (cuatro, desde el llano) pasan de 45 a 75; con los lomitos
  antes (y 6-12 m de llano en el medio) o con una quinta loma, a 60-65 se cae. Veinte metros de llano
  entre juegos no alcanzan a 60-70. El bot, que en el aire pone la trompa a -8°, pasa de 40 a 70. Quedó:
  juegos de a cuatro como mucho, 14-20 m de llano entre juegos, y el aviso de que a 60+ hay que usar el
  cuerpo. Los primeros lomitos (seis de 0.8 m cada 9 m, 16° de pendiente) tiraban al que no toca nada a
  60-70 cayendo de trompa sobre el tercero → cuatro de 0.7 m cada 10 m (12°).
- **Un serrucho no termina en una curva**: el tercer lomazo terminaba en la entrada de la curva NO; en la
  vuelta 2 el bot cayó de trompa sobre él justo cuando empezaba a doblar (se tumbó a -59°). → Dos lomazos
  y 40 m hasta la curva; 5 vueltas sin caerse.
- **Peraltes con `arc`**: el primero llevaba la guía a 30 m del centro, a 1.9 m de alto sobre una pared
  de 30°. Pasando el borde del sector el peralte se desvanece en `edge` (12 m) y la guía bajaba 2 m con
  14% de pendiente: un escalón a la salida de cada curva (y uno a la entrada), que el perfil de la vuelta
  (`--test profile`) mostró enseguida. → La guía al pie de la pared (piso hasta 28 m, guía a 31: 0.3 m) y
  `edge` 16: se pasa a 0.3-0.4 m. Y las líneas tienen que arrancar pasando ese desvanecido (≥ `edge` desde
  el borde del sector): el peralte va después en la lista y si no, aplana el primer salto.
- **Médanos redondos**: los primeros Montes eran conos con laderas de 26-30° y 2 m de cima, sobre los
  médanos naturales: sin tocar nada se caía a 40-50 (cae con la trompa arriba, +32°, en la ladera de
  enfrente) y se llegaba lento. → Mesas redondas (cima plana de 4-6 m, laderas de 18-24°, pie de radio 14)
  en una plaza nivelada: 30-60 los dos pilotos.
- **Cómo se hizo**: el plano de arriba (la copia en numpy de `Terrain::Ground`, ahora con `arc`) para
  ubicar todo; `--sim` para la primera idea de cada salto; `tools/tramo.py` y `tools/saltos.py` (y
  `--medir`) en el juego; el bot 5 vueltas. Se comprobó: regresión contra la v0.2.7 idéntica (brakeslide,
  bot 40 s, favela, circuito y valle con el bot), motocross 1:08.73 / 1:08.42, favela 1052 casas.

Pendientes:
- ~~Reaparecer donde uno se cae y esconder el tiempo de vuelta en los mapas sin pista~~: hecho (el jugador
  reaparece donde quedó, en suelo parejo; arriba, los saltos en vez del cronómetro). Ver [MENU.md](MENU.md),
  "Mapas libres".
- El color del polvo y de la tierra que levantan las ruedas según el suelo (en la arena, arena clara) y
  subir `Dustiness` de la arena.
- Agarre propio de la arena (hoy es el del pasto: 0.82).
- Un test que entre a N km/h con el cuerpo atrás o adelante en el aire (hay `bajadaN`, sin tocar nada, y
  el bot, que busca -8°).
- Sin tocar nada, a 60-70 km/h se encabrita en El Serrucho y se cae (el cabeceo crece de loma en loma); con
  el cuerpo (el bot) pasa. Si molesta, lomas más tendidas o un amortiguamiento del cabeceo en el aire.

### Parque de física (sandbox/park)
`mods/sandbox/maps/park.json`, generado por `tools/parque.py`: es el mod de ejemplo, así que el JSON
queda comentado y se puede tocar a mano. Llano de 255.5 m, Dos tiempos 250, la pista de siempre por el
borde (la del bot, con sus obstáculos automáticos).

**v2** (el usuario: "parece un collage sin sentido y algunas rampas son muy empinadas para subirlas y
están puestas en cualquier lado"). Lo que tenía la v1:
- el quarter pipe era una cuña de 45° apoyada en el piso: la rueda se encuentra con un quiebre de 45°,
  una pared;
- las rampas de las líneas (y la de lanzamiento, 3.5 m a 21°) eran cuñas con la espalda vertical de 1.4
  a 3.5 m: del otro lado, o llegando corto, son paredes; y cada cosa mirando para cualquier lado en el
  medio del campo;
- el pump track eran caños de 3 m enterrados hasta dejar 1.1 m afuera: el piso toca el círculo a 74°,
  troncos y no lomas;
- los objetos sueltos, amontonados lejos de todo: nadie pasaba por ahí (el bot, 0 de 35 movidos).

Ahora son zonas, y la circulación es la de la vuelta: **todas las líneas se andan hacia el norte**, se
entra desde la recta de largada doblando a la izquierda y se sale a la recta de enfrente (otra vez a la
izquierda), que es para donde va la vuelta. Lo que hay para voltear está al final de cada línea.

| Zona | Qué | Medido (Dos tiempos, bot / "sin tocar nada") |
|---|---|---|
| Línea de tierra (x = -50) | tres mesas de terreno pintadas de tierra (`dirt`): 1.2 m y 20°, 2 m y 26°, y "la de mortales", 3.6 m y 34° con cara de radio 16, 10 m de mesa y bajada de 30° que sigue 1.5 m bajo el suelo. Al final, la pirámide de cajas | 30-60 km/h los dos, sin caerse (0.3-2.4 s); mortal: `--spawn 80 --drop 0.5 --test flip` en `prueba/park_tierra`, 356° y cae a los 2.8 s |
| Línea de madera (x = 0) | tres cajones de madera: rampa de 11-20°, mesa y bajada de 9-11°, de 0.8, 1.2 y 1.8 m (se saltan o se pasan rodando, para los dos lados). Al final, la bolera | 25-55 km/h los dos (0.2-1.3 s) |
| Pump track (x = 40-66) | óvalo de tierra: dos rectas de ocho lomitos de 0.6 m cada 6.25 m y dos peraltes (`round` con `arc` de 180°, radio 13). Al salir, el muro de tambores | el bot da vueltas de 15 a 35 km/h sin caerse (de 25 para arriba salta de lomo en lomo) |
| La Pared (42, 52) | quarter de tierra de 3.2 m (62° arriba) con espalda de 24° | bot 20-50 pasa por arriba y cae en la espalda (1-2.4 s); sin tocar nada a 20 llega arriba sin velocidad y se cae |
| El bowl (-20, 58) | 2.4 m de hondo, paredes de 50° y borde de 0.8 m; las tres pelotas gigantes viven adentro (siempre vuelven al fondo) | 15-35 lo cruza; a 25 empuja las tres pelotas (hasta 3.3 m) y sale; a 60 vuela el bowl entero y se frena contra la pared de enfrente, sin caerse |

La vuelta del bot es la misma: 0:50.90-0:50.91 (las formas están a más de 17 m de la línea central).

Lo aprendido:
- **Rampas de madera**: nunca de más de ~20° desde el piso, y siempre con una mesa atrás (rampa, mesa y
  bajada): así no hay pared vertical para ningún lado. Las piezas se solapan 5 cm (sin canaletas) y la
  mesa es 4 cm más angosta que las rampas (sus costados no quedan en el mismo plano y no titilan).
- **Tierra sobre pasto**: las mesas de terreno salían con la textura del pasto y parecían lomas verdes, no
  saltos → `dirt` en las formas.
- **La mesa de mortales**: con cara de radio 10, 36-38° y 3.8-4 m, sin tocar nada caía con la trompa
  arriba (+35 a +55°) y se daba vuelta a 40-50 km/h; con radio 16, 34° y 3.6 m pasan los dos pilotos de 30
  a 60 y el mortal sigue dando 356°. Una cara más corta y empinada levanta más la trompa.
- **Formas y pista**: con una pista `track`, las formas a más de hw + 11 m de la línea central (acá 17 m)
  o la base suavizada las aplana. Cada línea arranca en el borde de la pista (su llano de entrada llega
  hasta ahí): desde la pista se ve dónde empieza.
- **El bot sólo anda la vuelta**: cada zona se midió con una copia con la guía por ahí (`prueba/park_*`,
  estilo `guide` para que la pista no estampe nada) con `tools/tramo.py` y `tools/saltos.py`.
- **Objetos sueltos**: sólo lo fijo da `GOLPE`; las ruedas no pisan lo suelto (sólo lo empuja el chasis):
  se pueden poner al final de una línea para atravesarlos.

Pendientes:
- ~~Los objetos sueltos quedan donde cayeron hasta que se vuelve a cargar el mapa.~~ Hecho: "Volver a empezar"
  (Retroceso / Back) lleva a la largada y llama a `Props::Reset`. Al reaparecer con R no, así se puede
  seguir jugando con lo que ya se volteó.
- El bot no anda las líneas del parque (sólo la vuelta); están medidas con las copias de prueba.
