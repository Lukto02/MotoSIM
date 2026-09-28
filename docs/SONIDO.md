# Sonido

Todo el sonido es procedural (`EngineSound.cpp`, hilo de audio de raylib): no hay archivos de audio.

## Cómo se prueba sin escuchar

- `motocross.exe --sound-test archivo.wav [2t|raspado|chillido]` genera unos segundos:
  - sin modo: el motor de siempre, a fondo, con un cambio y petardeos;
  - `2t`: lo mismo con el dos tiempos y 1.5 s de ralentí;
  - `raspado`: el motor bajo y la cola raspando despacio y rápido;
  - `chillido`: el motor sin gas y la cubierta en el asfalto: chirridos que crecen hasta el bloqueo
    (0.25-1.5 s), trabada (1.5-2.5 s) y deslizando cada vez más despacio (2.5-3 s).
- `--headless ... --sound-log archivo.wav` graba el sonido de una prueba de verdad (cualquier `--test`,
  el bot, cualquier mapa y moto), paso a paso con el mismo sintetizador:
  - `archivo.wav`, como se oye (motor y cubiertas; el raspado no, que lo alimentan los efectos);
  - `archivo_cubiertas.wav`, las cubiertas solas (para medirlas contra el motor);
  - una línea `snd` por paso con lo que alimenta el derrape: `dirt`, `squeal`, `chirp` y, por rueda,
    deslizamiento (`fS`, `rS`), uso del agarre (`fU`, `rU`), carga, fuerzas y si apoya en un objeto (`fO`, `rO`).
- `python tools/sonido.py archivo.wav 0.2-0.9 1.3-1.9 ...` mide cada tramo:
  - nivel (RMS y pico, en dB);
  - brillo (centroide del espectro y % de energía arriba de 2 kHz);
  - la frecuencia de las explosiones.
- **Regla**: un cambio en un sonido no toca los otros. Se comprueba con el md5 del WAV del modo que no
  se tocó (el del 4 tiempos, por ejemplo).
- Para comparar contra una versión anterior que no tiene el modo nuevo, se puede reproducir el
  sintetizador viejo en numpy: son pocas líneas.

## Motor

- **Cuatro tiempos** (`Synth::Step`): una explosión cada dos vueltas por cilindro (`rpm/120 × cilindros`).
  Cada una excita la resonancia del escape (95 Hz + 0.017 × rpm, con dos armónicos). El acelerador
  abre el filtro y agrega ruido ("ladrido"). En el limitador, o sin gas arriba de 4000 rpm, falla alguna.
- **Dos tiempos** (`Synth::StepTwoStroke`, `engine_two_stroke = 1` en el `.ini`):
  - **Explosiones**: una por vuelta, con pulsos más cortos y una onda casi cuadrada (el "bzzz").
  - **Resonancia**: la del caño es más aguda (160 Hz + 0.026 × rpm).
  - **"En la pipa"**: desde ~55-75% del corte el sonido sube de volumen, brillo y armónicos; abajo
    queda apagado.
  - **Cuatritiempeo**: con poco gas saltea explosiones al azar (el ring-ding-ding del ralentí y el
    borboteo al cortar). Cada explosión hace sonar un "ding" metálico de 2.35 kHz que se oye sin carga.
  - Medido: a 8-9 mil rpm explota a ~140-150 Hz (el 4T, a ~60-67 Hz) y suena más brillante (centroide
    5.2-5.7 kHz contra 3.6-4.1).
- `engine_cylinders`: más explosiones por vuelta (una 4 cilindros grita).

## Cubiertas y raspado

- **Derrape** (`Skid`, alimentado por `Game::FeedTireSound`, rueda por rueda y igual para todas las motos):
  - en la tierra y el pasto, un arrastre grave y granulado (desliza más de 1.2 m/s, con carga);
  - en lo duro (pavimento, cemento, y también un objeto: rampa, caja, escalón, casa; `Wheel::onObject`),
    el chillido de la goma: seguido si la rueda desliza (trabada, patinando o de costado) y en chirridos
    sueltos frenando cerca del límite sin trabar. Ver "Chillido frenando".
- **Raspado de la cola** (`Scrape`):
  - Suena **sólo contra algo duro**: asfalto o cemento (`TrackMask` en mapas de calles), objetos y
    casas. En la tierra no suena; salen menos chispas y polvo.
  - Antes eran dos resonancias angostas a 3 y 5 kHz (casi un silbido), con golpecitos fuertes y el
    55% del volumen también en la tierra: sonaba feo y fuerte.
  - Ahora es un rechinar de banda ancha:
    - dos filtros de banda en serie, de ~0.9 a 2.2 kHz según la velocidad;
    - un rumor grave;
    - una aspereza irregular que modula el volumen;
    - algún chasquido chico.
  - Resultado: raspando fuerte, ~4.5 dB menos que antes, con la energía entre 0.7 y 6 kHz. Con el
    motor tranquilo, el raspado rápido suma ~2 dB: se oye sin tapar.

### Chillido frenando

- **Qué pasaba**: frenando a fondo con la de carreras en el asfalto no sonaba nada. Medido con
  `--sound-log` (`frenada120` en `prueba/plaza_asfalto`): las cubiertas en silencio de 120 a 0 km/h.
  Y una rueda trabada sobre un objeto (una losa de cemento en un mapa de tierra) sonaba a tierra.
- **Por qué**:
  - El chillido sólo salía con el contacto deslizando (más de 1.2 m/s) y la carrera no traba en seco: la
    S le usa a la delantera el 59-61% del agarre (720 Nm de freno), con 1.5% de patinaje (0.6 m/s), y
    la trasera casi sin peso (130-180 N) satura pero apenas desliza. Sólo con la trasera (120 Nm), el 27%.
  - El patinaje no sirve para ver "cerca del límite": el neumático es un vínculo (la fuerza que anula el
    patinaje, con tope en el agarre) y abajo del tope el patinaje es casi cero, frene fuerte o suave.
  - El pavimento salía de `TrackMask` (sólo mapas de calles): un objeto contaba como tierra.
- **Qué se hizo**:
  - `Game::FeedTireSound` separa por rueda tres cosas (máximo de las dos ruedas):
    - `dirt`: el arrastre de siempre, sólo en la tierra (la misma cuenta que antes).
    - `squeal`: en lo duro, deslizando (1 a 6 m/s) por la carga (llena con 700 N; antes 1400).
    - `chirp`: en lo duro, cuánto del agarre usa **frenando** (`gripUsage` por la parte de la fuerza que va
      contra la marcha; desde 30%, lleno en 75%), o de costado pasado el 90%; por la carga y la velocidad.
      Acelerando no cuenta: la trasera de la carrera acelera al 90-95% del agarre (la frena el wheelie).
  - `Wheel::onObject` (sólo informativo, lo llena la física sin cambiar nada): lo duro es pavimento u objeto.
  - Sintetizador (`Skid`): la tierra quedó igual (su propio ruido, en el mismo orden). En lo duro, un tono
    de 0.8-1.1 kHz con armónicos (la goma que se pega y se suelta) que camina y tiembla al azar, un poco del
    ruido resonante de antes y un siseo deslizando. `chirp`: chirridos de decenas de ms que arrancan al
    azar y, cerca del bloqueo, se juntan (con 1, ~70% del tiempo sonando) sobre un fondo suave.
- **Cómo se comprobó** (`--sound-log`, las cubiertas solas contra todo; motor frenando ~ -20 dB):

  | Maniobra (asfalto salvo aclaración) | Antes | Ahora |
  |---|---|---|
  | carrera, S desde 120 (delantera al 60%) | nada | -30 a -33 dB; en 0.6-2.5 kHz, +14 a +18 dB sobre el motor (arriba de rpm, -3 a +4) |
  | carrera, sólo delantera a fondo (65%) | nada | -27 a -31 dB |
  | carrera, freno suave: `frenacurva120x0f0.3` (29%) / `f0.5` (42%) / `f0.7` (53%) | nada | nada / -50 dB (no se oye) / -36 a -38 dB |
  | motocross, trasera trabada a 30 km/h (`brakestraight`) | -33 dB | -24 a -26 dB (el motor, -23) |
  | motocross, cruzada con la trasera trabada (`brakeslide`) | -33 a -39 dB | -26 a -29 dB |
  | motocross, trasera trabada sobre una losa (objeto) en tierra | arrastre de tierra | chillido, -25 dB |
  | tierra (`plaza_tierra`, `--flat --test brakeslide`, bot 40 s) | | md5 idéntico |
  | bot de la carrera en el autódromo, 90 s | nada | casi nada: frena suave; algún chirrido de 50-80 ms al bajar la delantera de un wheelie (en el aire se frena y apoya deslizando ~30 m/s) |

  Timbre: la trabada antes era 98% de la energía entre 0.5 y 2 kHz (ruido angosto); ahora 76-78% ahí y
  21-24% entre 2 y 6 kHz (armónicos y siseo); los chirridos, 93% en 0.5-2 kHz. No silba más que antes
  (~50% de la energía a ±50 Hz del pico en los dos) y no clickea (salto máximo entre muestras < 1 RMS).
  Los otros WAV de prueba (4T, 2T, raspado) dieron el mismo md5.
- **Trampas**:
  - `gripUsage` abajo del tope es fuerza / agarre máximo; arriba es lo que se pide y puede dar 6-90 (una
    trasera trabada o casi sin peso): se recorta y se multiplica por la carga.
  - La delantera de la carrera vuelve de un wheelie casi parada y chilla un instante al apoyar: es la física.

## Mezcla

Los motores quedan por debajo del tope y se corren un momento con cada petardeo. Los petardeos
saturan aparte, para que no pierdan el chasquido. Todo pasa por un `tanh` final.

Después del `tanh`, el volumen: `kVolume` (0.8, el de siempre) por el de Ajustes (`EngineSound::SetVolume`, de
0 a 1 en décimos; 100% = como sonó siempre) o 0 si está apagado (M, `SetMuted`). El WAV de prueba
(`--sound-test`) sale antes de que se lean las preferencias: siempre con el de siempre.
