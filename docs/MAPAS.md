# Mapas y terreno

El formato completo está en `MODDING.md`. Esto es lo aprendido armándolos.

## Terreno

- `terrain.size` (ancho en m, 64..4096) y `terrain.resolution` (m entre muestras, 0.25..4). Hay como
  máximo 2048 muestras por lado, y la cantidad se redondea a múltiplo de 8: un mapa grande necesita una
  grilla más gruesa (`"size": 1000, "resolution": 1`). Default: 255.5 m a 0.5 m (512 muestras), el de
  siempre.
- `edge` levanta el borde del mapa (pared natural); 0 = sin pared.
- `heightmap`: PNG en escala de grises estirado sobre todo el mapa (arriba de la imagen = norte).

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
y 1 m, y los mapas de siempre con la trial. Ver [PRUEBAS.md](PRUEBAS.md).

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
