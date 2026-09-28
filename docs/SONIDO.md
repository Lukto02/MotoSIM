# Sonido

Todo el sonido es procedural (`EngineSound.cpp`, hilo de audio de raylib): no hay archivos de audio.

## Cómo se prueba sin escuchar

- `motocross.exe --sound-test archivo.wav [2t|raspado]` genera unos segundos:
  - sin modo: el motor de siempre, a fondo, con un cambio y petardeos;
  - `2t`: lo mismo con el dos tiempos y 1.5 s de ralentí;
  - `raspado`: el motor bajo y la cola raspando despacio y rápido.
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

- **Derrape** (`Skid`): en la tierra, un arrastre grave y granulado; en el pavimento, un chillido
  (resonante cerca de 1 kHz, con el tono que tiembla).
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

## Mezcla

Los motores quedan por debajo del tope y se corren un momento con cada petardeo. Los petardeos
saturan aparte, para que no pierdan el chasquido. Todo pasa por un `tanh` final.

Después del `tanh`, el volumen: `kVolume` (0.8, el de siempre) por el de Ajustes (`EngineSound::SetVolume`, de
0 a 1 en décimos; 100% = como sonó siempre) o 0 si está apagado (M, `SetMuted`). El WAV de prueba
(`--sound-test`) sale antes de que se lean las preferencias: siempre con el de siempre.
