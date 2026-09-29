# Motocross — prototipo jugable

Prototipo de motocross simcade en C++17 con **raylib** (render, input, cámara, audio, ventana) y
**Jolt Physics** (rigid body + colisiones). Prioridad absoluta: que manejar la moto sea divertido.

## Descargar y jugar

**[Bajá la última versión](https://github.com/Lukto02/MotoSIM/releases/latest)**: en *Assets*, el
`MotoSim-vX.zip`. Descomprimí la carpeta entera y ejecutá `MotoSim.exe` (Windows 10 u 11 de 64 bits,
OpenGL 3.3; no hace falta instalar nada). Las versiones anteriores están en
[Releases](https://github.com/Lukto02/MotoSIM/releases). Adentro del zip, `LEEME.txt` tiene los
controles y cómo jugar en red.

## Estado

| Etapa | Contenido | Estado |
|---|---|---|
| 1 | Ventana, render 3D, Jolt, terreno, rigid body, cámara | Hecha |
| 2 | Dos ruedas virtuales, raycasts de suspensión, resorte + amortiguador | Hecha |
| 3 | Acelerador, motor con curva de torque, RPM, 5 marchas, grip trasero | Hecha |
| 4 | Dirección, inclinación, balance automático, grip lateral | Hecha (el bot da vueltas sin caerse) |
| 5 | Saltos, peso del piloto, control en el aire, aterrizajes | Hecha |
| 6 | Circuito completo, partículas, polvo, huellas | Hecha |
| 7 | Surcos con efecto físico, superficies distintas | Hecha, activa por defecto en juego local (F8) |
| 8 | Multijugador LAN: código de invitación, hasta 4, choques entre motos | Hecha (v0.2; protocolo actual: el de la v0.2.1) |
| 9 | Mapas: selector en el menú y la favela "Morro do Grau" con la Trilheira 450 | Hecha (v0.2.1) |
| 10 | Mods (mapas y motos en archivos), selector de motos, motos de carreras y trial, circuito de velocidad | Hecha (v0.2.4-v0.3.2; protocolo 7) |

## Compilar

Requisitos: CMake ≥ 3.20, un compilador C++17 y conexión a internet la primera vez
(CMake descarga raylib 5.5 y Jolt 5.6 con FetchContent).

```bat
build.bat
```

o a mano:

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build\motocross.exe
```

Con Visual Studio: `cmake -S . -B build-vs` y abrir la solución, o abrir la carpeta directamente.

**Mac** (Apple Silicon; hace falta `xcode-select --install` y `brew install cmake ninja`):

```sh
./build.sh                      # compila en build-mac/ y arranca el juego
./build.sh --map favela         # los argumentos pasan al juego
```

Si el firewall de macOS está activado, la primera vez que se crea una partida en red pregunta si deja
que `motocross` reciba conexiones: hay que permitirlo. Las teclas F de la ayuda, en un teclado de Mac, con `fn`. Ver
`docs/PRUEBAS.md` ("En Mac") por qué la física no da idéntica bit a bit a la de Windows.

> La carpeta `build/` pesa ~450 MB (fuentes de las dependencias + objetos). Como el proyecto está
> dentro de OneDrive, conviene excluirla de la sincronización o compilar en otra ruta
> (`cmake -S . -B C:\dev\motocross-build`).

**Versión para compartir** (la v0.3.2, `dist/MotoSim-v0.3.2.zip`, que se publica en
[GitHub Releases](https://github.com/Lukto02/MotoSIM/releases); el workflow `.github/workflows/release.yml` la arma y la sube
cuando `src/Version.h` cambia en `main`; la de Mac, `MotoSim-vX-mac.zip` para Apple Silicon, la arma y la agrega a la misma
release `.github/workflows/release-mac.yml`): con MSVC, compilar con el runtime
estático para que el .exe no pida instalar el "Visual C++ Redistributable" en la otra PC
(`dumpbin /dependents` sólo debe listar DLLs de Windows):

```bat
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DMOTOSIM_STATIC_RUNTIME=ON
cmake --build build-release
```

y armar la carpeta y el zip con `python tools/package_release.py build-release/motocross.exe` (en el workflow, `build-release/Release/`): `MotoSim.exe`
(el `motocross.exe` renombrado), `tuning.ini`, la carpeta `mods/` (sin `pruebas/`), `MODDING.md`,
`Low_Poly_Motorcyclist_3_rigged.gltf` + `Low_Poly_Motorcyclist_3_rigged_deps/` y `LEEME.txt` (el de la raíz del repo: las
instrucciones para el que lo recibe, con las novedades arriba; se escribe en UTF-8 con BOM y CRLF). `RELEASE_NOTES.md` es
el cuerpo de la release. La versión está en `src/Version.h` y el protocolo de red en `Multiplayer.cpp` (`kProtocol`).

## Controles

| Acción | Gamepad | Teclado |
|---|---|---|
| Acelerador | RT | W |
| Freno delantero | LT | S (frena con las dos) |
| Freno trasero (solo: derrape) | A | Espacio |
| Dirección / inclinación | Stick izq. X | A / D |
| Piloto adelante / atrás | Stick izq. Y | ↑ / ↓ |
| Cuerpo a los costados | Stick der. X | ← / → |
| Bajar / subir marcha | LB / RB | Q / E |
| Reaparecer (caído no reaparece solo; en la pista, en la pista; en un mapa libre, donde quedaste, mirando para donde ibas) | Y | R |
| Volver a empezar (a la largada, y los objetos sueltos a su lugar) | Back | Retroceso |
| Menú (ajustes y controles) | Start | Esc |
| Pausa (no anda en red) | — | P |
| Caja automática / manual (arranca en automática; se recuerda) | X | F3 |
| Cámara lateral (debug) | R3 | C |
| Cámara orbital | — | arrastrar con el mouse (rueda: zoom); al soltar vuelve sola atrás |
| Ayuda de teclas (sale sola al empezar; en pausa también) | — | H |
| Datos técnicos (velocidad, motor, suspensión, agarre, cuadros por segundo) | — | T |
| Sonido | — | M |
| Pantalla completa | — | F11 o Alt+Enter (también en Ajustes); se recuerda en `preferencias.ini` |
| Marcha atrás (lenta) | auto: mantener LT parado · manual: LB desde 1ª | auto: mantener S parado · manual: Q desde 1ª |

**Esc** (Start en el joystick) abre el menú: la carrera sigue de fondo y la cámara pasa (en ~1 s, desde
donde estaba) a un plano de cine, bajo y con teleobjetivo, que gira despacio alrededor de la moto (corrida a
la derecha); abajo a la derecha, la tarjeta con el mapa y la moto de ahora (potencia, peso, velocidad
máxima). Se elige con flechas, mouse o joystick (stick o cruz; también 1, 2, 3...); Esc, B o el botón
derecho del mouse vuelven. Jugar solo / seguir, crear partida en red (abre la sala con el código grande, C
lo copia, y los pilotos conectados), unirse (el código en casilleros, Ctrl+V lo pega), mapa, moto, tu nombre
(el que ven los demás; se recuerda), **Ajustes**, **Controles** (todas las teclas y botones) y salir.
Corriendo, F10 copia el código de invitación (anfitrión).

**Ajustes** (en el menú principal y en el de la partida en red; se guardan solos en `preferencias.ini`, se
cambien ahí o con su tecla), en **tres páginas** (la primera fila, Sección, las cambia con ← →; LB y RB en el joystick):
**Gráficos** (Calidad gráfica Bajo / Medio / Alto / Ultra, que elige el juego según tu placa de video y que pasa a
"Personalizado" si tocás algo suelto; sombras, distancia de dibujado, pasto, filtro de texturas, resolución de imagen,
FXAA, pantalla completa y unas opciones avanzadas), **Imagen** (estilo de color, brillo, niebla, viñeta, grano, aberración,
motion blur, sacudón de cámara y, en avanzadas, luz del sol y ambiental, intensidad de sombra y viento del pasto) y **Juego y
sonido** (volumen (M lo apaga), ayuda de teclas (al empezar, siempre o nunca), datos técnicos (T), caja (F3), control de
tracción (F6) y la deformación del terreno). ↑ ↓ eligen, ← → o Enter cambian. Si hay un `preferencias.ini` de una versión
anterior a los presets, se copia a `preferencias.ini.v1.bak` y los gráficos vuelven a los recomendados para tu placa.

En los mapas libres (pista `guide`, como Los Médanos) no hay cronómetro de vuelta: arriba se ve cuánto
volás en cada salto y el mejor.

Debug: **F1** vectores físicos (suspensión verde, neumático rojo, normal azul, COM naranja),
F2 esconde todo el HUD, F11 / Alt+Enter pantalla completa (sin bordes, del tamaño del monitor; `--fullscreen` y `--windowed` la fuerzan sin guardarla), F4 cámara lenta, **F5 recarga `tuning.ini`** (Shift+F5: los mods), F6 control de tracción, F7 efectos de cámara (motion blur, viñeta, grano), F8 surcos físicos/visuales (física local activa por defecto; en red sólo visual), F9 piloto modelo/generado

En Ajustes, Gráficos (con "Opciones avanzadas") están los controles de sombras (detalle, radios de las dos
cascadas, filtro y suavidad) y la distancia del pasto (0–120 m); en Imagen, la iluminación y el viento del pasto. En
Juego y sonido, el grupo Terreno permite regular blandura y profundidad, o restaurar el suelo; el relieve fino y
las partículas están en las avanzadas de Gráficos. Los surcos modifican geometría y colisión; los terrones salen de la rueda y rebotan.
El detalle local usa muestras de 5 cm en los mapas incluidos. Bot y pruebas requieren `--ruts` para deformar el suelo.
(F4 y P no andan en red). `[` `]` mueven el centro de masa adelante/atrás, `-` `=` lo suben/bajan, `0` lo resetea.

Consejos: el piloto adelante (↑ / stick arriba) mantiene la rueda delantera abajo al acelerar
fuerte; atrás ayuda en los aterrizajes y en los wheelies. En el aire, adelante/atrás gira la moto
un poco; acelerar levanta la nariz y el freno trasero la baja. Al costado (A / D) en el aire la tirás
de costado (whip); si ya doblás subiendo la cara del salto sale acostada (scrub). Soltá antes de caer
para que vuelva derecha: aterrizar cruzado se paga.

## Multijugador LAN

Uno crea la partida (menú → Crear partida en red, o `--host`) y la sala (y, corriendo, el panel de
arriba a la derecha) muestra el **código de invitación** (10 letras, p. ej. `60N00-VM07N`): la IPv4
de la PC en la red local y el puerto (UDP 27015 por defecto), con un dígito de control, en base 32
sin letras confundibles. Los demás lo escriben o pegan en menú → Unirse a una partida (o
`--join CÓDIGO`). Hasta 4 jugadores, cada uno con su color y número (`Livery` en `BikeMeshes.cpp`)
y su nombre arriba de la moto (el que se elige en el menú; si no, el usuario de Windows o `--name`).
Los dos tienen que tener la misma versión (el protocolo va en cada paquete; si no coincide, avisa).

- **Red** (`Net.cpp`, sin librerías): un socket UDP no bloqueante. El cliente manda HELLO a la IP
  del código y, por si esa IP no se ve desde su red, también por broadcast; el anfitrión contesta
  WELCOME con el número de jugador. Cada uno manda su estado 60 veces por segundo (posición,
  rotación, velocidades, suspensión, ruedas, dirección, piloto, motor, petardeos y, caído, las 11
  partes del ragdoll) y el anfitrión lo reenvía a los demás. PING/PONG cada 0,5 s, lista de
  jugadores cada 1 s, 5 s sin noticias = desconectado. Todo se repite o se refresca solo: no hace
  falta entrega confiable.
- **Motos remotas** (`Multiplayer.cpp`): cada uno simula sólo su moto. Las de los demás se dibujan
  donde se predice que están ahora: la última foto más su velocidad lineal y angular por el tiempo
  que pasó (medio ping, hasta 0,25 s). Cuando llega una foto nueva, la diferencia con lo que se
  estaba mostrando se reparte en ~0,1 s: no saltan. En la prueba con dos instancias la corrección
  media es de ~6-9 cm a 12 m/s.
- **Choques**: cada moto remota tiene un cuerpo cinemático de Jolt (capa `REMOTE`, la misma forma
  de colisión que la propia) que va, paso a paso, a su pose predicha con `MoveKinematic`, así empuja
  con la velocidad justa. Cada jugador resuelve en su PC cómo lo empujan a él y si se cae (con la
  velocidad que mandó el otro, no la del cuerpo, que lleva las correcciones de la predicción). Si te
  pegan de costado, de atrás o de arriba a más de 4 m/s (~15 km/h de diferencia), te caés; si fuiste
  vos el que pegó con la trompa, recién a más de ~10.5 m/s (~38 km/h de diferencia: la otra moto cede);
  de frente, a más de 7 m/s (`crash_impact_speed`, como un muro), se caen los dos. Un roce (hasta ~3 m/s) no tira a nadie. El ragdoll propio también choca con las motos
  de los demás. Detalle y mediciones en `docs/RED.md`.
- **Huellas, tierra y polvo** de los demás: cada uno manda también, por rueda, dónde apoya, la
  carga, cuánto patina y si está en el suelo (~50 bytes más por estado). Cada PC dibuja con eso sus
  surcos en la textura de huellas (cada rueda de cada jugador tiene su propio trazo) y larga su roost
  y su polvo, como con la moto propia: el surco que deja el otro queda marcado en la pista.
- **Lo demás**: el piloto remoto con el mismo modelo (una copia por jugador, pose por IK o por las
  partes del ragdoll que llegan), sus petardeos (llamarada en su escape y estampido según la
  distancia) y su motor (voces 1-3 del sintetizador, más bajas cuanto más lejos).
- Cada uno aparece en su carril de la largada. En red no hay pausa ni cámara lenta.

Firewall: la primera vez Windows pregunta si deja pasar a MotoSim; hay que permitirlo (red privada,
y pública si el wifi figura así) en las dos PCs. Wifi de invitados o routers con aislamiento de
clientes no dejan que las PCs se vean.

## Mapas

En el menú, **Mapa** abre el selector (en red lo elige el que crea la partida y a los demás se les carga
solo: el id del mapa viaja en el saludo y en la lista de jugadores). Cada mapa trae su moto, pero en
**Moto** se puede elegir otra para todos los mapas (ver **Motos** abajo).
Los mapas y las motos son archivos de texto en `mods/` (ver **Mods** abajo): los del juego están en
`mods/base/` y el mod de ejemplo `mods/sandbox/` trae tres más.

- **Pista de motocross**: la de siempre, con la Motocross 450.
- **Morro do Grau** (favela, Brasil), con la **Trilheira 450**: la largada es arriba del morro (~40 m).
  Se baja por la escadaria y la ladeira recta (~20%), que cruza las cuatro calles del zigzag: cada
  cruce es una meseta a nivel de la calle que cruza y, entre una y otra, la bajada sale por un labio
  empinado (se vuela) y se aplana hasta la meseta de abajo (donde se cae). Desde la avenida de abajo se
  vuelve a subir por el zigzag, con una horquilla cerrada en cada punta. Calles de asfalto abajo, de
  cemento en el medio y de tierra roja arriba (con rampas de escombro), quebra-molas pintados y un
  lomo en plena bajada. Alrededor, ~1050 casitas con colisión (se puede andar por las lajes): ladrillo a
  la vista (dibujado en el shader con juntas), revoque pintado con manchas, pisos que bajan por la
  ladera, lajes, hierros esperando el próximo piso, caixas d'água, antenas y ropa tendida; postes con
  cables y "gatos" enredados, banderines de colores cruzando las calles, carteles (BORRACHARIA 24H,
  MOTO TÁXI, OFICINA DE MOTOS, AÇAÍ...), grafitis, banderas de Brasil, motos estacionadas (algunas con
  el baú de motoboy), pipas en el cielo y, de fondo, otros morros llenos de casitas, el mar y el Cristo
  en el Corcovado, con luz de atardecer. Las casas salen con semilla fija: son iguales en todas las PCs.
  La cámara no atraviesa las paredes (se acerca a la moto).
- **Trilheira 450**: trail de calle estilo "grau": manubrio alto con travesaño y cubremanos, máscara con
  faro redondo, horquilla con fuelles, tanque grande con franjas (verde y amarillo...), asiento largo,
  parrilla, escape esportivo cromado levantado, cubrecárter, patente del Mercosur bien empinada y
  cubiertas mixtas. Más pesada y con más motor (`mods/base/bikes/trilheira.ini`, que se lee después de
  `tuning.ini` y pisa sólo lo suyo; F5 recarga los dos); con el escape aberto petardea mucho más al
  soltar el gas. Con esta moto el HUD cuenta el **grau** (wheelie con la trasera en el piso): el actual,
  el último y el mejor.
- **Freestyle de favela (sólo la Trilheira, `trilheira.ini`; la de motocross no cambia)**: como se "da
  grau" en Brasil, largo y lento, a paso de hombre (tirado atrás, relación corta, el gas sostiene la
  rueda y el pie nunca sale del freno trasero):
  - *Cómo se hace*: flecha abajo (tirarse atrás) y W. Con la rueda arriba, **W suelto = grau lento**
    (~8 km/h) y **W a fondo = grau rápido** (~20 km/h); soltando la flecha la rueda baja despacio.
    Con A/D se dobla en una rueda. Sale desde parado, a 8 km/h o andando en 1ª y 2ª.
  - *Relación corta* (`engine_final_drive` 7.8, como la corona grande que se usa para el grau) y el
    piloto que se tira bien atrás (`rider_shift` 0.30): el punto de equilibrio baja a ~35° y levantar
    la trompa cuesta menos.
  - *Largada con el embrague*: al acelerar fuerte en 1ª (y 2ª) la trompa se levanta; con el cuerpo
    neutro el gas queda como lo pide el jugador (el motor arriba de vueltas: el embrague patina a 5500
    rpm, `engine_clutch_rpm`) y el piloto patina el embrague lo justo para que no pase de
    `grau_pitch_neutral_deg`. Acelera lo que deja el wheelie (0-50 km/h en 2.4 s), sin ahogarse.
  - *Embrague de golpe* (`grau_clutch_pop`, `grau_pop_time`): tirado atrás, despacio, abrir el gas
    rápido (de casi cerrado a casi a fondo en menos de 0.25 s: anda con el teclado y con el gatillo)
    descarga la energía del volante del motor en la rueda.
  - *Punto de equilibrio con el pie en el freno* (`grau_assist`, `grau_speed_min`, `grau_speed_max`,
    `grau_hold_hz`, `grau_brake`): el piloto busca el punto donde el centro de masa queda justo encima
    del contacto de la trasera (sale de la geometría de cada momento, con el cuerpo atrás incluido) y
    maneja la velocidad como en la calle: un poco antes del equilibrio el gas que sostiene la rueda
    acelera la moto; justo encima casi no hace falta gas, y apenas pasado (hasta 6°, más atrás la cola
    toca el piso) el freno trasero que baja la trompa la frena. Subiendo deja el gas abierto hasta que
    la trompa trae el impulso justo para llegar sola. Sólo usa el gas y el freno trasero. Medido: 8 s en
    una rueda a ~8 km/h (con W, ~18 km/h) sin caerse, también doblando. El limitador de wheelie de
    motocross (que corta gas) está apagado para esta moto.
  - *Doblar en una rueda* (`grau_turn`, `grau_align`): acostando la moto, la cubierta trasera curva la
    trayectoria y la moto gira a la par de ella (la trasera apunta hacia donde va: ~6° de deriva a 21
    km/h en vez de ~34°). En una rueda la inclinación se sostiene alrededor de la línea del piso por
    donde va la moto, no de su eje levantado (que metía guiñada y la dejaba apuntando para afuera).
  - *Giros cerrados*: 45° de manubrio y 1.55 m de radio mínimo (~1.5 m medido a paso de hombre), el
    control de tracción deja patinar más (derrapes con el gas en las curvas) y con el trasero solo la
    cola sale hasta 85° (cavalo de pau).
  - *Frenando fuerte con la S* va derecha como las otras de tierra (antes hacía un trompo desde 60-90 km/h).

- **Parque de física** (mod `sandbox`), con la **Dos tiempos 250** (moto de ejemplo, `dostiempos.ini`):
  un parque de saltos llano, con la pista alrededor y adentro zonas que se andan todas hacia el norte,
  desde la recta de largada: la **línea de tierra** (tres mesas, la grande da para un mortal), la
  **línea de madera** (tres cajones con rampa, mesa y bajada) y un **pump track** (óvalo de lomitos con
  peraltes); al norte, un **bowl** y **La Pared** (quarter de tierra). Al final de cada línea hay algo
  suelto para voltear: la pirámide de cajas, la bolera, el muro de tambores y las pelotas gigantes (viven
  en el bowl). Lo genera `tools/parque.py`.
- **Valle** (mod `sandbox`): terreno sacado de una imagen (`valle.png`); se baja por el fondo del valle y
  se vuelve por la ladera.
- **Los Médanos** (mod `sandbox`), con la Motocross 450: dunas de arena al atardecer para andar libre,
  sin pista (una vuelta guía invisible de 1.3 km para el bot y el reaparecer, con peraltes de arena en
  las esquinas). La vuelta es una línea de saltos tras otra: el **Gigante** (mesa de 6 m con patada de
  42°: da para un mortal a 55-65 km/h), **La Cadena** (seis saltos de médano seguidos, cada uno de otro
  alto y otro ángulo), **El Serrucho** (lomitos, **Los Lomos** y lomazos) y la **Escalera** (tres mesas de
  1.4, 2.6 y 4 m) con **Los Dientes** (un serrucho de médanitos). Adentro, cerca uno del otro: el
  **Médano Grande** (22 m, se vuela 2-3 s cayendo en su cara de 36°), un **Cráter** con paredes de 55°,
  **La Ola** (una pared de 6.5 m que tira para arriba y se cae en su espalda) y **Los Montes** (mesas
  redondas para saltar para cualquier lado). Lo genera `tools/medanos.py`.
- **Autódromo Sierra de los Vientos** (circuito de velocidad), con la **Carrera 1000**: 3757 m y 14 m de
  ancho. Recta de casi un kilómetro, horquilla a fondo de frenos, chicana, eses y un curvón final. Todo
  armado desde la forma de la pista:
  - pianos, líneas y grilla;
  - pórtico con semáforo;
  - boxes con garajes, torre de control y paddock;
  - tribunas con público;
  - leca, muros con alambrado, gomas y air fence;
  - carteles, puente, árboles y cerros de fondo.

  Mapa de 1380 m con lomas suaves.
- **Escaleras**: sobre escalones, cordones y cajas la cubierta apoya en los cantos y rueda alrededor de
  ellos. Antes la escadaria del morro disparaba la moto. Subir un escalón cuadrado de 27 cm sin
  levantar la rueda no se puede, como con una moto de verdad.
- **Doblar en wheelie**: con cualquier moto, la moto gira a la par de su trayectoria (antes, salvo la
  Trilheira, quedaba cruzada 20-25°).
- **Golpes fuertes**: contra un muro, una casa o una barrera a más de ~25 km/h (de frente o de costado) el
  piloto sale despedido con la velocidad que traía, aunque la moto no vuelque.
- **Derrape lento** (motos de motocross): despacio, con el freno trasero y doblando, la cola sale un
  poco (22-40°) en vez de frenarse derecha.
- **Carrera 1000**: las lisas patinan en la tierra y el pasto; frenando fuerte va derecha a cualquier
  velocidad (la delantera sigue su camino, como una rueda libre).

- **Wheelie pasado (las dos motos)**: si la trompa se pasa del equilibrio, la cola (con un caño de acero
  debajo de la patente / del guardabarros) apoya en el piso a ~84° y la moto se queda raspando sin
  rebotar: el centro de masa queda entre la cola y la rueda. Saca chispas (muchas en el pavimento, en
  objetos y casas; en la tierra menos y polvo) y suena el raspado; el piloto va de pie en los pedales.
  Tirándose adelante la moto vuelve a las dos ruedas; quieta sobre la cola más de 3 s, el piloto se baja
  por atrás. El raspado suena sólo contra algo duro (asfalto, cemento, objetos): un rechinar áspero y
  bajo; en la tierra no suena. Antes la rueda trasera "se perdía" pasados ~70° (su rayo iba a lo largo de la suspensión
  casi acostada), la cubierta se metía en el suelo y al reencontrarlo la escupía.

## Motos

En el menú, **Moto** abre el selector: a la izquierda la lista (al abrir queda marcada la que se usa,
con "EN USO"; elegir la del mapa vuelve a "cada mapa con su moto") y a la derecha la marcada, sin
piloto, girando sobre una plataforma, con sus números abajo: potencia, peso sin piloto, velocidad máxima, 0 a 100 km/h, agarre en curva, recorrido de
suspensión y giro más cerrado. Las barras son relativas a la mejor de todas en cada cosa. Los números
salen de la física de cada moto (su `tuning.ini` más su `.ini`): la potencia es el máximo de la curva
de torque, la velocidad máxima la menor entre la que dan la última marcha y el corte y la que deja el
aire, el 0 a 100 una simulación en recta limitada por el agarre de la trasera y por el wheelie. La
elegida se guarda en `preferencias.ini` (`--bike mod/archivo` la fuerza). Enter la carga y rearma el
mapa.

En red cada uno corre con la suya: el id de la moto viaja en el saludo y en la lista de jugadores
(protocolo 7) y los demás la ven con sus piezas, su piloto y su sonido (si tienen esa moto en su
`mods/`; si no, con la propia).

- **Motocross 450** (`base/motocross`): la de siempre. Su física no cambia con las motos nuevas: cada
  estilo tiene sus piezas y su pose, pero la de motocross usa exactamente los mismos números que antes.
- **Carrera 1000** (`base/carrera`): superbike de 4 cilindros, 245 hp a 15300 rpm y 330 km/h. Carenado
  con doble burbuja y alerones, chasis de doble viga, escape 4-2-1 con silenciador de carbono, horquilla
  invertida dorada, semimanubrios, llantas forjadas y cubiertas lisas. El piloto va agachado detrás del
  parabrisas a velocidad y, acostado, se cuelga hacia adentro con la rodilla al piso. En el aire es torpe:
  gira a menos de la mitad que la de motocross (no da mortales). Cuatro esquemas
  (roja 27, azul 33, negra con verde 72, naranja 8).
- **Trilheira 450** (`base/trilheira`): ver Morro do Grau arriba.
- **Trial 300** (`base/trial`): de trial de competición, 70 kg y ~28 hp llenos abajo, 48° de manubrio
  (gira en ~1.3 m), suspensión blanda y cubiertas de trial que se pegan a todo; el piloto va siempre de
  pie. Chasis de aluminio a la vista, cubrecárter atornillado, tanque mínimo, máscara con el número y
  cuatro esquemas (roja 3, amarilla 8, azul 15, lima 27). Sube escalones de medio metro con carrera.
- **Dos tiempos 250** (`sandbox/dostiempos`): la de ejemplo, liviana y gritona, con el motor que se despierta
  arriba (menos fuerza abajo y más arriba que la 450; la caja automática la estira hasta ~10 800 rpm) y casi sin
  freno motor. Estilo `mx2t`: la de
  motocross con motor dos tiempos (caño de expansión que pasa panzón por delante del motor, silenciador
  corto de carbono, tapa de cilindro chata con la bujía arriba) y colores de los 90 (amarilla con asiento
  azul, número 92). Suena a dos tiempos: explota en cada vuelta, se pone áspera y chillona "en la pipa"
  y con poco gas hace el ring-ding-ding.

Cómo se diseña y afina una moto, las fichas de todas y lo que aprendimos en cada una:
**[docs/MOTOS.md](docs/MOTOS.md)**.

## Mods

Todo mapa es un `.json` en `mods/<mod>/maps/` y toda moto un `.json` (más un `.ini` opcional que pisa
`tuning.ini`) en `mods/<mod>/bikes/`: trazado (puntos de control, ancho, superficies), terreno (lomas,
plano, imagen de alturas o el morro), obstáculos (a mano o un plan por recta), objetos (cajas, rampas,
cilindros y esferas; fijos para andar encima o sueltos con masa), luz y cielo, y qué moto se usa. La
referencia completa, en inglés, está en **[MODDING.md](MODDING.md)**.

- Con el juego abierto, **guardar el archivo del mapa que se está corriendo lo rearma** ahí mismo (la
  moto queda donde estaba). **Shift+F5** relee todos los mods (mapas o motos nuevas).
- Un error de formato no cierra el juego: se muestra con archivo, línea y columna (al cargar y en la
  lista de mapas) y sigue el mapa anterior.
- Se buscan en `mods/` de la carpeta actual, la del ejecutable y la de arriba (al correr desde `build/`);
  si un id está en dos, vale el primero. El build copia `mods/` junto al ejecutable.
- En red cada uno necesita el mismo mod (el id `mod/archivo` es lo que viaja; protocolo 7). Los objetos
  sueltos los simula cada PC por su cuenta.

## Tuning

Todos los números de la moto están en `tuning.ini` (se copia junto al ejecutable al compilar).
Editalo con el juego abierto y apretá **F5**. Los que más cambian el feeling:

- `com_height`, `com_forward`, `rider_shift` — transferencia de peso, wheelies, stoppies.
- `front_spring` / `rear_spring`, `*_damping`, `*_rebound_ratio`, `*_progressivity`,
  `*_tire_stiffness`, `*_tire_rebound_damping`, `*_tire_unsprung_mass` — suspensión (la amortiguación de
  rebote de las cubiertas evita que la moto rebote sobre ellas al hacer tope en una caída grande).
- `engine_torque_scale`, `engine_gear*`, `engine_final_drive` — empuje y facilidad de wheelie.
- `rear_tire_long_grip`, `*_lat_grip`, `cornering_stiffness` — cuánto agarra y cómo derrapa.
- `balance_k_*`, `balance_d_*`, `lean_steer_speed_*` — qué tan asistida y ágil es la inclinación.
- `max_lateral_accel`, `max_lean_deg`, `bank_turn_gain` — qué tan cerrado se dobla en plano y cuánto
  más dejan doblar los peraltes.
- `air_pitch_rate`, `air_pitch_torque`, `air_pitch_gain`, `air_reaction_gain`, `air_yaw_align` — control en el aire.
- Asistencias: `wheelie_start_deg`/`wheelie_end_deg` (limitador de wheelie con el piloto neutro:
  en 1ª la rueda sube con el torque, `wheelie_gear_fade` lo achica en cada marcha y
  `wheelie_lean_back_deg` deja hacer wheelies más altos tirándose atrás), `abs_slip` (ABS + anti-endo +
  anti-levantamiento de la trasera), `tc_slip` (control de tracción, F6), `caster_align` /
  `caster_deadzone_deg` (contravolante automático cuando se cruza la cola), `brake_stand_up`
  (frenar inclinado endereza la moto), `stoppie_yaw_damping`.

## Gráficos

Todo es generado por código, salvo el modelo opcional del piloto (glTF con su textura):

- **Luz**: sol + ambiente hemisférico (cielo arriba, rebote de tierra abajo) en espacio lineal, con
  tone mapping ACES. Especular Blinn-Phong con reflejo del cielo por Fresnel: plásticos brillantes,
  aluminio, goma mate (`Renderer::SetGloss`, o por vértice en las piezas armadas: ver `MeshBuilder::Append`).
- **Moto** (`BikeMeshes.cpp`): low poly armada por código, cada pieza una sola malla con colores y
  brillo en los vértices: cubiertas con tacos, llantas con 36 rayos cruzados, masas, discos
  lobulados con agujeros y corona dentada; chasis de doble viga con cuna y subchasis; motor con
  aletas, tapas de embrague y encendido, radiadores y mangueras; colector con el tornasolado del
  calor y silenciador; horquilla invertida con protectores y pinza; tijas, manubrio con almohadilla,
  puños y levas; tanque, asiento, cachas con calcos, laterales y porta número con el 21. Todo se
  mueve con la física: ruedas girando, basculante siguiendo al eje trasero, amortiguador cuyo
  resorte se comprime, botellas que se hunden, adelante girando con la dirección.
- **Sombras**: dos cascadas ortográficas desde el sol, cada una en su textura: la cercana (2048² con 24 m de
  radio en Alto; todo lo que se mueve) y la lejana (96 m en Alto, sólo el mundo quieto, en caché), ajustadas a la grilla de
  texels para que no titilen, con comparación por hardware (filtro de 5 o 9 lecturas) y sesgo en metros del mundo.
  Proyectan la moto, el piloto, los postes y el propio terreno (los saltos hacen sombra). Los presets Bajo / Medio
  llevan la lejana más corta (48 y 80 m; Alto 96, Ultra 140). Detalle en `docs/RENDER.md`.
- **Cielo**: degradé, halo y disco del sol, nubes procedurales que se mueven. La niebla toma el
  color del cielo en la dirección de la mirada.
- **Terreno**: texturas de tierra (con piedras) y pasto mezcladas por altura en el borde de la
  pista, a dos escalas para que no se note la repetición, con relieve por derivadas de pantalla.
- **Huellas**: textura de 4096² sobre todo el mapa (6 cm por píxel). Cada rueda deja un surco
  oscuro y hundido con bordes claros de tierra empujada; las pasadas se acumulan.
- **Pasto 3D**: miles de matas instanciadas delante de la cámara, con viento, sombras y aplastadas
  donde pasó una rueda.
- **Piloto con modelo** (`Low_Poly_Motorcyclist_3_rigged.gltf` en la raíz, o si no está
  `Low_Poly_Motorcyclist_2_rigged.gltf` o `Low_Poly_Motorcycle_Racer_rigged.gltf`; F9 alterna con el
  generado): glTF con esqueleto estilo Mixamo, posado por código. El 3 es un personaje de Tripo sin
  esqueleto riggeado con `tools/riggear_piloto.py` (Blender sin ventana: mide las articulaciones de la
  malla, dedos incluidos, y pone los pesos; ver `docs/PILOTO.md`). Sobre la moto la cadera va en su lugar, el torso se inclina y piernas y
  brazos llegan a estriberas y manubrio con cinemática inversa de dos huesos. Las manos nunca sueltan
  el manubrio: si el brazo no llega, primero se inclina más el torso (hasta ~25°) y después la
  cadera se corre hacia adelante; tirado adelante el pecho baja sobre el manubrio. **Dedos**: el
  modelo 3 trae los dedos (tres falanges y la punta de cada uno); al cargarlo se mide la mano con sus
  articulaciones y cada falange gira lo justo para envolver el puño, y el pulgar lo rodea del otro
  lado. Si la mano viene en un solo hueso (modelo 2), al cargarlo se miden los vértices de la mano
  (largo, ancho, normal de la palma, nudillos, pulgar) y se agregan huesos virtuales: tres falanges
  para los cuatro dedos y dos para el pulgar, con los vértices repartidos y transiciones suaves. La
  flexión se calcula para envolver el puño del manubrio, la mano se orienta con el manubrio (pulgar
  hacia adentro, muñeca alineada con el antebrazo) y la muñeca se ubica para que el puño quede en
  la palma. Al acelerar, la mano derecha rueda sobre el puño (hasta 25° con el gas a fondo) como
  quien abre el acelerador. En una caída cada hueso sigue a su parte del ragdoll y las manos se
  abren en un cuarto de segundo. Skinning en la CPU
  (raylib escala los vértices por el nodo del esqueleto y no la pose de los huesos: se corrige al
  cargar). Si el archivo no está, se usa el piloto generado.
- **Piloto generado**: mallas low poly, con el equipo pintado en los vértices (colores en
  `GenRiderMeshes`). Casco con mentonera en punta, antiparras, correa y visera; torso más ancho en
  el pecho con franjas y el panel del número; brazos y piernas torneados que se afinan en codos,
  rodillas y muñecas; rodilleras, botas con correas y hebillas, guantes y collarín.
- **Post-proceso** (F7): motion blur radial con la velocidad (la moto queda nítida), aberración
  cromática, viñeta, grano, FXAA y un estilo de color (vívido, natural o suave) que F7 no apaga. La cámara tiembla con giro y golpe de zoom al aterrizar o chocar.
- **Petardeos**: al subir un cambio con el motor arriba de 5500 rpm (el corte de encendido deja
  nafta sin quemar que explota en el escape), casi siempre al reducir y a veces al soltar el gas de
  golpe con muchas vueltas: ráfagas de 2 a 4 explosiones. Cada una tira fuego aditivo (en el espacio
  de la moto, alimentado desde la boca mientras dura: no se despega del escape aunque vayas rápido)
  y humo por la salida del silenciador y un estampido sintetizado (chasquido seco, golpe grave que
  baja de tono y cola ronca, con un eco corto); el motor se corre un instante para que se destaque.

## Cómo funciona la física (resumen)

- **Un solo rigid body** de 180 kg (moto + piloto) en Jolt, a 120 Hz con 1 collision step. La
  forma de colisión (cajas + esferas) sólo actúa en caídas o si la suspensión hace tope.
- **Ruedas virtuales**: raycast por el eje de cada suspensión. La rueda es un disco de perfil
  redondeado: se busca la extensión que la deja tangente al suelo (funciona con rake e
  inclinación). La fuerza de suspensión va por la normal del suelo, en el punto de contacto.
- **Suspensión**: `k·x·(1 + p·x²) + c·v`. El resorte es casi lineal (p = 1.2) para que guarde poca
  energía al fondo; los aterrizajes los frena el hidráulico: compresión, un cono que multiplica la
  amortiguación ×6 en el último 20% del recorrido y tope con histéresis. El rebote va 3× más
  amortiguado: no patea al aterrizar pero recupera entre baches. Cada rueda
  tiene **masa propia** (11/14 kg) y el neumático es un resorte rígido y amortiguado: la suspensión
  trabaja entre chasis y rueda, así los baches secos mueven primero la rueda y llegan filtrados.
- **Motor**: curva RPM→torque × acelerador × relación de marcha × final. Embrague automático que
  patina al arrancar. La fuerza sale del contacto trasero: aceleración, descarga de la
  delantera, wheelies y patinaje aparecen solos.
- **Neumático**: fuerza tipo constraint (la que anularía el patinaje en ese paso, estable a
  cualquier velocidad) limitada por `μ·N·curva(slip)`; lateral con rigidez de deriva; ambas en
  una elipse de fricción. La rueda tiene su propia velocidad angular (inercia + motor reflejado).
- **Piloto**: no es un cuerpo físico; desplaza el centro de masa efectivo tomando los torques de
  las fuerzas respecto a un COM corrido (misma física que mover el COM, sin tocar Jolt).
- **Dirección**: el input pide una curvatura. A baja velocidad se convierte en manubrio; a alta
  velocidad el manubrio sigue a la inclinación real (auto-direccionamiento) y el input decide
  cuánto inclinar. El límite de curvatura sale de `max_lateral_accel` (≈ μ·g en plano) y del peralte
  bajo la moto: en un berm se dobla más fuerte con el mismo grip. **Balance automático**: torque `error·K − ω·D`, fuerte parado y más suave rápido.
  **Autoalineación** (trail): si la trasera se cruza, la delantera hace contravolante sola, así la
  moto derrapa de costado en vez de hacer un trompo como un auto.
- **Giro máximo**: tope de dirección de 42° como una KX450 real (antes 32°) y radio mínimo de 1.8 m a
  paso de hombre (distancia entre ejes de ~1.49 m / tan 39.5°, el ángulo efectivo en el piso con 26–27°
  de rake). Medido con `circleN` en plano: 1.7–1.8 m hasta 12 km/h; más rápido manda el grip:
  ~0.9 g con la pata afuera (3 m a 18 km/h, 7 m a 29 km/h, 17 m a 44 km/h), 0.8 g a 58 km/h.
- **Pata afuera**: doblando cerrado entre 1 y 14 m/s (con la trasera en el piso), la pierna de adentro
  sale estirada hacia el eje delantero con la punta de la bota arriba, el piloto se sienta en la punta
  del asiento y el torso queda más derecho que la moto. Sentarse adelante corre el peso 6 cm
  (`leg_out_com_forward`: la delantera muerde más y no se lava) y cargar la estribera de afuera da un
  10% más de grip lateral (`leg_out_grip`). Para cambiar de lado primero vuelve el pie a la estribera.
- **Whip / scrub**: en el aire, el stick al costado mueve el cuerpo del piloto (un grado de libertad
  propio, con resorte y amortiguación como sus músculos, hasta 35°) para el otro lado; la moto recibe
  la reacción `-I·α` y se acuesta hacia el lado pedido mientras el cuerpo se mueve. Es una fuerza
  interna: se conserva el momento angular, así que al soltar el cuerpo vuelve y la moto también. Lo
  que traiga de la cara del salto (inclinación y giro) sigue solo, porque mientras se tira de costado
  el piloto deja de enderezarla; al soltar la endereza de nuevo. **Efecto giroscópico** de las ruedas
  (`L × ω`, a la mitad del físico para que se controle): acostarla en el aire hace doblar la nariz
  hacia ese lado, que es lo que le da la forma de whip. Si sale rolando muy rápido (más de 1.8 rad/s)
  el piloto frena el giro con el cuerpo. Medido saltando a 14 m/s: la moto se acuesta ~26° y la nariz
  dobla ~12°; con la cara del salto, 30–45°.
- **Derrape con el freno trasero** (Espacio / A sin el de adelante, como se "cuadra" una horquilla en
  motocross: se entra frenando, se suelta el delantero y con la trasera trabada la cola da la vuelta
  alrededor de la delantera; alineado, se suelta y se sale acelerando). Una cubierta que patina o está
  trabada roza en contra de la velocidad con que se desliza (círculo de fricción, roce cinético ~0.72
  del pico): no sostiene la cola, que es direccionalmente inestable. El piloto lo controla: arrastra la
  rueda en vez de trabarla del todo, afloja el pedal cuando la cola llega al ángulo que busca (20–65°
  según la dirección, menos cuanto más rápido) o si se está abriendo rápido, la delantera se alinea
  con su camino (el trail) y él la apunta sólo un poco hacia la curva o contravolantea, y lleva la moto
  más derecha. Medido en plano: a 15 km/h la moto gira ~60° mientras frena; a 32 km/h la cola sale
  ~30–40° y al soltar agarra y sale derecha. Frenando con los dos (S) es una frenada normal.
- **Cuerpo a los costados** (← / →, stick derecho): el piloto (75 de los 180 kg) se corre ~25 cm y lleva
  el centro de masa del conjunto ~10 cm (`rider_side_shift`): los torques de las cubiertas se toman
  respecto de ese punto, como con el adelante/atrás. Lo que se inclina en una curva es el conjunto, así
  que con el cuerpo hacia adentro la moto va más derecha y con el cuerpo afuera (el peso en la estribera
  de afuera, "la moto debajo") más acostada: a 29 km/h y la misma curva, 12° contra 32°. Sin tocar el
  manubrio el cuerpo solo dobla, más despacio (`rider_side_steer`); derecho y con el cuerpo de un lado,
  la moto se inclina un poco para el otro (equilibrio). En el aire mueve el cuerpo del whip.
- **Rueda clavada**: una cubierta que patina no va derechita: el suelo (piedritas, surcos, juntas) le da
  un roce de costado irregular (`slide_wander`) y una trasera clavada baila y abre la cola unos grados
  (~5° en recta), hasta que el contravolante la agarra. El piloto sólo dosifica el pedal en el derrape
  buscado (Espacio doblando); frenando fuerte de verdad la rueda se clava. Suena: chilla en el pavimento
  y arrastra en la tierra, según cuánto y qué tan rápido desliza.
- **Aire**: momentum normal de Jolt. El stick adelante/atrás pide una velocidad de rotación
  (hasta 2.5 rad/s) y un torque limitado la persigue: frenar o invertir un giro responde rápido y con
  el stick suelto se conserva el impulso. Acelerar/frenar la rueda trasera también rota la moto
  (efecto giroscópico, exagerado ×1.5).
- **Caídas**: la moto pasada de `crash_angle_deg` cuenta como caída sólo mientras toca el suelo
  (ruedas, chasis o piloto); de cabeza contra el suelo es caída en el acto. En el aire se puede
  girar libremente: un flip completo que aterriza derecho no es caída.
- **Ragdoll**: en una caída el piloto se suelta y sigue como ragdoll de Jolt (11 cuerpos unidos por
  articulaciones swing-twist con límites humanos), con la velocidad que tenía ese punto de la moto.
  Cada articulación dobla sólo para donde corresponde: rodillas y codos son bisagras que van de
  derechos (unos grados de más) a ~150° hacia su lado, casi sin torcerse ni doblar de costado; las
  caderas llevan la pierna 125° adelante y 25° atrás; los hombros el brazo 165° adelante y 45° atrás;
  la columna se dobla más adelante que atrás. Como los límites de Jolt son simétricos, el eje de cada
  articulación se centra a mitad de su rango (`joint` en `Rider.cpp`). Con `--telemetry` se imprime
  cuánto dobló cada rodilla y codo en la caída.
  La moto pierde la caja del piloto y queda con su masa sola; la cámara sigue al piloto. Como
  aparece encimado a la moto, cada parte del cuerpo empieza a chocar con ella recién cuando se
  separa (filtro de grupo de Jolt). Reaparecer lo vuelve a sentar.

## Pruebas automáticas

El ejecutable tiene un modo sin ventana para probar la física:

```bat
motocross.exe --headless --bot --time 600 --telemetry      :: el bot da vueltas; imprime LAP y telemetría
motocross.exe --headless --test accel --telemetry          :: tests: idle, accel, wheelie, turn, sharpturn, slide, brake, braketurn, burnout, crashloop, reverse
motocross.exe --headless --test idle --drop 2.5 --telemetry :: aterrizaje desde 2.5 m a 12 m/s
motocross.exe --headless --flat --test flip --drop 5        :: mortal atrás (frontflip: adelante); imprime el giro y si hubo caída
motocross.exe --headless --flat --test launch --telemetry  :: terreno plano; launch / launchfwd / launchlate
motocross.exe --bot --debug --side --screenshot 5 shot.png :: con ventana, vectores + cámara lateral, captura
```

Red con dos instancias en la misma PC (el anfitrión imprime el código; `--telemetry` agrega una línea
`net` con ping, fotos recibidas, corrección media y cada jugador, y `CHOQUE` en cada golpe entre motos):

```bat
motocross.exe --headless --host --bot --test netduel --telemetry
motocross.exe --headless --join 60N00-VM07N --bot --test netduel --telemetry
```

`netduel`: al entrar otro jugador todos vuelven a la largada juntos (los bots se juntan en el centro
de la pista y se van rozando); `netcrash`: además el anfitrión se cae 1 s después (para ver el
ragdoll remoto desde el invitado, que queda quieto). Sin ventana, en red, la simulación va a tiempo real.
`netchoque-TIPO-KMH[-KMH]` (con `--flat`, sin `--bot`): el invitado embiste al anfitrión (`lado`, `atras`,
`frente`, `roce` o `angN`) y cada uno imprime si se cayó; `python tools/netchoque.py <exe> <exe> caso...`
hace el par. `--net-lag MS` y `--net-jitter MS` demoran lo que llega (como una partida por internet).

Otras opciones: `--spawn <metros de pista>`, `--telemetry-dt <s>`, `--ruts`, `--size <w> <h>`,
`--bot-lat <m/s²>` (qué tan fuerte encara las curvas el bot, 6 por defecto). El test `circleN`
(p. ej. `--flat --test circle12`) sostiene N m/s con la dirección a fondo, para medir el radio de giro
(`circle3x`: además se cae a los 8 s, para ver el ragdoll que sale con la pata afuera).
Whip: `--flat --drop 1 --test whip` sale como de un salto y tira la moto a la derecha 0.6 s (`whipL`
a la izquierda, `whiphold` la mantiene hasta el suelo); `--bot --test scrub` da vueltas tirando scrubs
en todos los saltos (un poco de dirección en la cara y whip en el aire; `whipair` sólo en el aire).
Para mirar de cerca: `--view <yaw> <pitch> <zoom>` fija la cámara (grados; zoom 1 = distancia
normal) y `--norider` dibuja la moto sola; p. ej. `--test pose --flat --side --norider --view 0 0 0.3`.
Menú: `--menu join|name|lobby|maps|bikes|ajustes|controles|principal` abre esa pantalla al arrancar (con `--host`
para ver la sala o, con `principal`, el menú de la partida en red); `--menu no` arranca corriendo, como "Jugar
solo" (con la ayuda de teclas del principio). `--datos` muestra los datos técnicos (T) sin tocar
`preferencias.ini`. `--gfx bajo|medio|alto|ultra` aplica ese preset de calidad encima de lo leído y no guarda nada
(mientras dura, el archivo de preferencias no se escribe); `--noprefs` ni lee ni escribe `preferencias.ini` (capturas que no
dependen de la PC). `--respawn-here`: en un mapa libre, las pruebas reaparecen donde quedaron, como el
jugador con R (con `--telemetry` imprime `REAPARECE`: dónde, a cuánto y con qué pendiente).
`--bike mod/archivo` corre cualquier mapa con esa moto (p. ej. `--map favela --bike base/motocross`).
`--map favela` arranca en el Morro do Grau (acepta el id `mod/archivo`, el nombre del archivo o el
nombre visible: `--map sandbox/park`); `--test profile` imprime la altura y la pendiente de la pista
cada 2 m. Con `--telemetry`, al salir se imprime cuántos objetos sueltos se movieron. Derrape: `--flat --test brakeslide` (32 km/h; `brakeslide4`, `brakeslide12`: otra velocidad en
m/s; terminado en `b`, entra frenando con los dos) y `brakestraight` (trasera trabada en recta). `--flat --test cuerpo`: cuerpo a la derecha derecho, y doblando con el cuerpo adentro y afuera.
Grau (con `--map favela --flat`): `grau` (1ª, 12 km/h, tirado atrás y gas de golpe 8 s), `grau2` (2ª, 20
km/h), `graucurva` (dobla en una rueda) y, como con el teclado (W y la flecha en rampa), `graulento` (a 8
km/h, flecha y W 8 s), `graulento0` (desde parado), `graulentos` (levanta y suelta W: grau lento) y
`graulentoc` (grau lento doblando); la telemetría imprime `GRAU` al terminar cada uno y `gB` (grados que
le faltan para el punto de equilibrio). Cola: `colazo` (sin ayudas, la trompa se pasa del equilibrio y
apoya la cola; a los 6 s se tira adelante) y `colazof` (más fuerte); `scr` en la telemetría es la
velocidad de raspado (-1 sin contacto). `arranque` / `arranquea`: largada con W a fondo (neutro / tirado
adelante).
El test `pose` deja la moto quieta con el piloto neutro, tirado atrás (2 s), adelante (4 s), neutro (6 s)
y con gas a fondo frenado (8 s: gira la muñeca). `--sound-test archivo.wav` genera 3 s del sintetizador
(motor a fondo, un cambio con petardeos y un corte de gas) para revisar el sonido sin abrir el juego.

## Estructura

La documentación de desarrollo (cómo se hace cada cosa, problemas resueltos, trampas y cómo probar) está
en **[docs/](docs/README.md)**; `tools/` tiene la regresión y la grilla de capturas, y `pruebas/mods/`
los mapas de prueba que no se empaquetan.

```
src/
  main.cpp               argumentos y arranque
  Game.cpp/.h            loop, input, HUD, vueltas, efectos, bot de pruebas
  PhysicsWorld.cpp/.h    boilerplate de Jolt
  Bike.cpp/.h            parámetros, creación, reset y dibujo de moto y piloto
  BikePhysics.cpp        TODA la física de la moto (ruedas, suspensión, neumáticos, balance, aire)
  Engine.cpp/.h          curva de torque, caja, embrague automático, limitador
  Suspension.cpp/.h      resorte + amortiguador + tope
  Tire.cpp/.h            curvas de grip y elipse de fricción
  Terrain.cpp/.h         heightmap, HeightFieldShape, mallas por chunks, surcos, médanos y formas esculpidas
  TerrainDeformation.*   huellas con relieve (textura) y surcos físicos
  Track.cpp/.h           trazado desde los puntos del mapa, saltos, whoops, rollers, peraltes, lomos, pavimento
  Maps.cpp/.h            mods: lee mods/*/maps y bikes (MapDef, BikeDef) y junta los errores de formato
  Json.cpp/.h            lector de JSON con comentarios y comas de más (errores con línea y columna)
  Props.cpp/.h           objetos de los mapas: cajas, rampas, cilindros, esferas (fijos o sueltos)
  Favela.cpp/.h          Morro do Grau: casas, escadaria, postes y cables, banderines, carteles, pipas, fondo
  Camera.cpp/.h          cámara con resortes, FOV dinámico, sacudón (con giro y zoom), vista lateral
  Particles.cpp/.h       tierra y polvo
  Rider.cpp/.h           dibujo del piloto generado y ragdoll de las caídas
  RiderModel.cpp/.h      piloto con modelo glTF: pose por IK, dedos, pegado al ragdoll, skinning
  EngineSound.cpp/.h     sonido de motor procedural
  Render.cpp/.h          shaders (luz, sombras en dos cascadas, cielo, terreno, pasto, post-proceso con estilo de color), texturas generadas, mallas, presets de calidad
  GpuPreset.h            qué preset de calidad le toca a una placa de video según lo que dice OpenGL (sin dependencias)
  BikeStyles.cpp/.h      estilos de moto: piezas, pose del piloto y escape de cada uno (BikeStyleDef.h)
  BikeMeshes.cpp         piezas de las motos (cada estilo: primitivas, tubos barridos, placas, números)
  Circuit.cpp/.h         circuito de velocidad ("generator": "circuit"): pianos, boxes, tribunas, barreras, carteles
  MeshBuilder.h          armado de mallas y combinación de primitivas transformadas
  Coplanar.h             diagnóstico de caras que se pisan en las mallas del circuito y la favela (MOTOSIM_COPLANARES)
  Net.cpp/.h             socket UDP, direcciones IPv4 y código de invitación
  Multiplayer.cpp/.h     partida LAN: anfitrión/cliente, paquetes, motos remotas (predicción, cuerpo cinemático, piloto)
  Version.h              versión del juego
  Tuning.cpp/.h          lector de tuning.ini
```

Nota MinGW: Jolt se compila sin AVX en MinGW porque GCC en Windows no alinea la pila a 32 bytes y
genera stores AVX alineados que crashean; con SSE4.2 sobra para un solo cuerpo.
