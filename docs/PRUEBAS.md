# Compilar y probar

## Compilar

- `build.bat` / `build/` son de otra PC (MinGW). En esta PC: `tools\compilar.bat [carpeta] [release]`
  (VS 2022 Build Tools: vcvars64 + CMake + Ninja, reusando las fuentes de raylib y Jolt ya bajadas en
  `build/_deps`, sin internet). Por defecto compila en `build-msvc`; los agentes que trabajan a la vez,
  cada uno en `build-msvc-<área>`. El exe queda en `<carpeta>\motocross.exe` con `tuning.ini` y `mods\`.
- **Sin Visual Studio** (pasó en una sesión: no existe `C:\Program Files (x86)\Microsoft Visual Studio`, `compilar.bat` da "El
  sistema no puede encontrar la ruta especificada"): `cmake -S <proyecto> -B <carpeta> -G Ninja -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_MAKE_PROGRAM=<WinLibs>\mingw64\bin\ninja.exe -DCMAKE_C_COMPILER=<WinLibs>\mingw64\bin\gcc.exe
  -DCMAKE_CXX_COMPILER=<WinLibs>\mingw64\bin\c++.exe -DFETCHCONTENT_SOURCE_DIR_RAYLIB=<proyecto>\build\_deps\raylib-src
  -DFETCHCONTENT_SOURCE_DIR_JOLTPHYSICS=<proyecto>\build\_deps\joltphysics-src` con WinLibs en
  `%LOCALAPPDATA%\Programs\WinLibs` y CMake en `C:\Program Files\CMake\bin` (ponerlos en el `PATH`); con MinGW, ninja sí sigue los
  headers. Las carpetas de prueba conviene sacarlas del proyecto (scratchpad), con el glTF del piloto en la carpeta de arriba.
- **Ninja no sigue bien los headers acá** (el MSVC en castellano imprime "Nota: inclusión del archivo"
  y ninja no lo entiende). Después de tocar un `.h` que cambia el tamaño de una estructura (un enum con
  `Count`, un miembro nuevo), tocar los `.cpp` que lo incluyen o recompilar todo. Si no, se linkean
  objetos viejos y el juego se cierra sin avisar en el primer cuadro.
- **Dos agentes en el mismo árbol**: si el otro deja un archivo a medio cambiar, tu carpeta tampoco compila.
  Para seguir midiendo: una copia (`git archive HEAD | tar -x -C <scratch>`) con sólo tus archivos encima,
  compilada en su propia carpeta (en `Game.cpp`, compartido, sólo tus partes: `git diff` filtrado y `patch`).
- El paquete para compartir es otra compilación, con runtime estático: `tools\compilar.bat build-release release`
  (ver "Paquete para compartir").
- **Scripts con saltos de línea**: los scripts de Python que editan C++ se escriben con la herramienta
  de archivos, no en un heredoc de bash. En un heredoc, un `\n` dentro de un string de C++ terminó
  convertido en un salto de línea real (tres veces; una cuarta en un `'\\n'` de Python que armaba un
  reemplazo, y otra con un `'\\'` que cerró mal un string: también los scripts sueltos, con la herramienta).
- **Rutas largas**: en una copia del proyecto muy adentro del scratchpad, un objeto de Jolt pasa los
  260 caracteres de Windows (error C1083). Se compila en una carpeta de nombre corto o con `subst W: <copia>`.

- **Fin de línea**: Python en Windows, abriendo en modo texto, escribe CRLF aunque el archivo fuera
  LF (pasó con una docena de archivos). Para editar sin cambiarlo: leer y escribir en binario, o
  `open(..., newline='')`. Chequeo: contar `\r\n` contra `\n` con Python.
  - `sed -i` de Git Bash también: un reemplazo de una línea dejó `Game.cpp` (CRLF) entero en LF. Para
    reemplazos chicos, la herramienta de edición (conserva el fin de línea).

### Windows y Mac en `main`

`main` compila en los dos: `build.bat` en Windows, `./build.sh` en Mac. El workflow
`.github/workflows/build.yml` lo comprueba en cada push (MSVC en `windows-latest`, Apple clang en
`macos-latest`) con una prueba corta sin ventana: `--test bikestats` sin errores de mods, dos vueltas
del bot y la favela con **1052 casas** (si no da eso en Mac, se perdió el `-ffp-contract=off`). No
compara tiempos de vuelta: difieren entre sistemas (ver abajo). Lo que se agregue para un sistema va
detrás de `if(APPLE)` / `if(MSVC)` en el CMake o probando rutas de los dos (como las fuentes).

### En Mac

`./build.sh [args del juego]` compila en `build-mac/` (CMake + Ninja de Homebrew, Apple clang) y
arranca el juego desde ahí; `build-mac/motocross` corre desde esa carpeta (encuentra el glTF en `../`).
Las herramientas de `tools/` andan igual (`bash tools/regresion.sh`, sin permiso de ejecución en git).
Lo que hubo que resolver (v0.2.5, M4 con macOS 26):

- **Jolt 5.6 compila shaders de GPU** si encuentra Vulkan (el de Homebrew, sin `dxc`/`glslang`: falla) o
  Metal (pide Xcode completo). El juego no los usa → `JPH_USE_VK`/`JPH_USE_MTL` en OFF en el CMake.
- **clang no acepta** un struct anidado con inicializadores de miembros como argumento por defecto
  (`= {}`) dentro de la misma clase (MSVC y GCC sí): `EngineSound::Character` pasó a ser un alias de
  `EngineSoundCharacter`, declarado afuera.
- **`typeinfo for JPH::GroupFilter` sin definir** al linkear: Jolt va sin RTTI y el juego hereda de sus
  clases. En Mac el juego se compila con `-fno-rtti` (no usa `dynamic_cast` ni `typeid`).
- **Fuentes**: la interfaz y los carteles del circuito cargaban sólo de `C:/Windows/Fonts` y en Mac caía
  la pixelada de raylib (las flechas salían `?`). Ahora prueba Arial Bold + Monaco y el Impact de
  `/System/Library/Fonts/Supplemental`; raylib no lee `.ttc` (Helvetica, Menlo). Monaco no tiene ← ↑,
  pero la mono sólo muestra números y códigos.
- **FMA**: clang en ARM fusiona `a*b+c` (`-ffp-contract=on`) y MSVC en x64 no. La favela salía con 1035
  casas en vez de 1052 (un umbral de ubicación que cambia arrastra a todas las siguientes), y en red
  Mac-Windows cada uno vería otras paredes. El juego va con `-ffp-contract=off` en Mac: 1052 casas.
- **La telemetría no da idéntica a la de Windows** ni así (ni con Jolt sin FMA): `sin`/`cos`/`atan2` de
  cada sistema y NEON contra SSE. Vueltas del bot en Mac: **1:08.41 y 1:08.55** (Windows 1:08.37 /
  1:08.51). La regresión en Mac se hace Mac contra Mac (un `build-mac` de referencia del commit anterior).
- Anda sin cambios: la red (rama POSIX de `Net.cpp`; anfitrión y cliente sin ventana en la misma Mac),
  el audio (miniaudio sobre Core Audio), Retina (se dibuja y se captura a la resolución real: una
  ventana de 1280×720 da capturas de 2560×1440) y la pantalla completa.

## Regresiones: la motocross tiene que dar idéntica

`tools/regresion.sh <exe nuevo> <exe de referencia> "args" ...` corre cada prueba sin ventana con los
dos ejecutables y compara la telemetría (md5). La referencia es el último paquete
(hoy `dist/MotoSim-v0.3.2/MotoSim.exe`; el más nuevo es el de `ls -t dist`). El juego nuevo agrega ` scr=` al final de cada línea; el script
lo saca.

```
tools/regresion.sh <nuevo> <viejo> "--flat --test brakeslide --time 8" "--flat --test cuerpo --time 12" \
  "--flat --drop 1 --test flip --time 6" "--flat --drop 1 --test whip --time 5" \
  "--flat --test wheelie --time 8" "--bot --time 40" "--test accel --time 12"
```

- Vueltas del bot en la pista de motocross (`--headless --bot --time 150`): **1:08.73 y 1:08.42** (con la
  suspensión de `motocross.ini`; hasta la v0.2.6, 1:08.37 y 1:08.51).
- **Trampa: la carpeta `mods/` de donde se corre gana.** El juego busca las motos y los mapas en `mods` (la
  carpeta actual), después al lado del exe y después en `../mods`, y vale el primero que tiene cada id. Si
  corrés el exe de `dist/` desde la raíz del proyecto, usa los `.ini` del proyecto, no los del paquete: para
  comparar un cambio de `.ini` contra el paquete, cada exe desde su carpeta (como hace `tools/regresion.sh`).
  Una comparación de vueltas "antes y después" hecha desde la raíz dio igual al centésimo y parecía que el
  cambio no hacía nada.
- Contra la v0.2.6 la motocross da distinta en todo (su suspensión cambió a pedido del usuario, ver
  [FISICA.md](FISICA.md)); contra la v0.2.7, que ya la trae, idéntica.
- Favela (`--map favela --bot`): 1052 casas, 48 postes, 132 cables.
- **Pruebas que terminan en caída** (`accel` a los ~5 s, el bot de la favela a los ~20 s de la vuelta
  1): dan idénticas hasta la caída y distintas después si se tocó el ragdoll. Es esperable; mirar desde
  qué `t=` difieren.

## Pruebas sin ventana

`--headless` corre la simulación lo más rápido posible. Lo más usado:

| Qué | Comando |
|---|---|
| telemetría cada N s | `--telemetry --telemetry-dt N` |
| el bot da vueltas | `--bot` (el mapa puede fijar `bot_speed` y `bot_lateral`) |
| terminar a los N s | `--time N` |
| moto de otro mapa | `--bike mod/archivo` |
| fichas de todas las motos | `--test bikestats` |
| perfil de la pista | `--test profile` |
| caídas en serie | `--test crashloop [--respawn-after S]` (`-1`: sólo con R, como el jugador) |
| reaparecer donde quedó (mapas libres) | `--map sandbox/medanos --test crashloop --respawn-here --telemetry`: cada `REAPARECE` dice dónde, a cuántos m de donde quedó el piloto, con qué pendiente y a cuánto de la guía |
| la pista al revés desde S | `--spawn S --test subidaN` (N km/h, recto: p. ej. subir la escadaria con `--spawn 90`) |
| cuerpo en el aire | `--flat --drop 3 --test airrot` (atrás a fondo) o `airlean` (adelante y atrás cada 0.6 s) |
| frenada a fondo desde N km/h | `--map base/circuito --bike base/carrera --test frenadaN` y `python tools/frenada.py` |
| frenar y doblar a la vez desde N km/h | desde `pruebas/`: `--map prueba/plaza_asfalto --bike base/carrera --test frenacurvaN[xS][dD][aA][n]` (S: cuánto manubrio, 1 = la D, < 0 la A; dD: la dirección empieza D s después de frenar, D < 0 antes, viniendo inclinada; aA: la suelta a los A s; n: sin freno; fF: frena con F en vez de a fondo, `x0f0.3` es una frenada suave derecha) y `python ../tools/frenacurva.py`. Como con teclado: `frenacurva150x0.6d-1.5a1.5` viene doblando y suelta para frenar, `frenacurva150x1d0.3a0.15` toca la D frenando |
| el freno de cada rueda en cada paso | `--wheel-log T0 T1`: además de lo de la rueda, una línea `freno` (vueltas, patinaje, si está retenida, freno, uso y agarre máximo) |
| bajar la escadaria a N km/h | `--map favela --spawn 68 --test bajadaN` (sigue la línea) y `python tools/escalera.py 74 88` |
| un salto a N km/h | `python tools/saltos.py <exe> sandbox/medanos 300 370 40,50,60 ambos --time 16`: aparece en S, entra al salto que despega en s = 370 (La Cadena 3) sin tocar nada (`bajadaN`) y con el bot a N; aire, altura, vy al tocar, cabeceo y si se cae (ver [MAPAS.md](MAPAS.md), "Medir un salto"). Todos los de la vuelta de Los Médanos: `python tools/medanos.py --medir <exe> 40,50,60,70` |
| un tramo entero a N km/h | `python tools/tramo.py <exe> sandbox/medanos 600 650 850 40,50,60,70 ambos --time 30`: todos los vuelos entre s = 650 y 850 (El Serrucho) y dónde se cae, con los dos pilotos. Para líneas de saltos seguidos, donde un salto depende de cómo se llegó del anterior |
| cada rueda en cada paso | `--wheel-log T0 T1`: contra qué pega el rayo, normal, apoyo, compresión y fuerzas |
| columnas de la telemetría | `... --telemetry --telemetry-dt 0.05 \| python tools/telemetria.py t v pitch fN rN gnd s --s 70 95` |
| sonido | `--sound-test archivo.wav [2t\|raspado]` y `python tools/sonido.py` (ver [SONIDO.md](SONIDO.md)) |
| el sonido de una prueba | `--headless ... --sound-log archivo.wav`: graba el motor y las cubiertas de la moto propia paso a paso (`archivo.wav`, como se oye, y `archivo_cubiertas.wav`, derrape y raspado solos) e imprime una línea `snd` por paso con lo que alimenta el chillido (ver [SONIDO.md](SONIDO.md), "Chillido frenando") |
| choque entre motos en red | `python tools/netchoque.py <exe anfitrión> <exe invitado> [--lag MS] [--jitter MS] [--bike m] [--ventana] lado-15 atras-60-30 frente-12 roce-40 ang45-25-25`: el invitado embiste al anfitrión (`--test netchoque-TIPO-KMH[-KMH]`, `--flat`) y cada uno dice si se cayó y con qué cierre; también qué decidía la regla de la v0.2.6 (ver [RED.md](RED.md), "Choques entre motos") |
| red como por internet | `--net-lag MS --net-jitter MS`: lo que llega se procesa MS ms más tarde, más hasta MS al azar (en tandas) |
| arrancada (0-50, 0-100) | `--test arranquea` (parado, W en rampa a 1 s, piloto adelante, caja automática). No con `frenadaN`/`accel`: arrancan con la moto en el aire (ver abajo) |
| freno motor | `--test frenacurvaNx0n`: llega a N km/h y suelta todo (sin freno ni dirección); restar el aire (0.6 · `drag_area` · v² / m) y la rodadura |
| curvas y agarre lateral | desde `pruebas/`: `--map prueba/plaza_tierra` o `prueba/plaza_asfalto_ancha --test circleN` (N m/s, dirección a fondo desde los 4 s). La guiñada del mundo es `wy` / cos(`roll`): `wy` es la del eje de la moto |
| el resto | la lista de `--test` está en el README ("Pruebas automáticas") |

- **Trampas de las pruebas con guion** (de la revisión de las motos, [MOTOS.md](MOTOS.md)):
  - `frenadaN`, `accel` y los que dan gas desde `t = 0` arrancan con la moto cayendo al aparecer: la rueda se pasa de
    vueltas en el aire y al tocar el piso la caja automática sube a 2ª a 4-5 km/h (la 2T y la Trilheira parecían
    arrancar en 2ª). Para una arrancada, `arranquea`.
  - `--drop H` deja la moto en 1ª a 43-50 km/h: con una 1ª corta (la trial llega a 31) el freno motor pasado de
    vueltas frena la rueda en el aire y la trompa baja (el `whip` de la trial termina dado vuelta). Es la prueba.
  - Los círculos a más de ~15 m/s no entran en las plazas de 120 m (el círculo va al costado de la largada): con
    lisas, en el pasto la de carreras "no doblaba". Las plazas de 400 m sirven hasta ~100 m de radio.
  - `prueba/recta` no sirve para la velocidad final sin dirección: la largada está en la curva del óvalo y la moto se
    va de la calle de 16 m. En `plaza_asfalto_ancha` (1.5 km derechos) la de carreras llega a 328 km/h.
- **Caídas del bot en la favela**: la simulación es caótica y el bot pega en la pared de s≈253 casi en cada vuelta
  (que sea caída depende de si el golpe pasa de 7 m/s). La misma moto con la masa corrida ±0.01 kg da de 3 a 9
  caídas en 45 min. Para comparar dos versiones: varias corridas de 45 min (`--time 2700`) de cada una, cada una con
  una perturbación así (en una copia de la moto en `pruebas/mods/prueba/bikes/`), y comparar las medias; ver
  [FISICA.md](FISICA.md), "La Trilheira: frenando fuerte hacía un trompo".

## Capturas

- `--size W H --screenshot T archivo.png`: una captura a los T s de simulación. **No cierra el juego**:
  sumar `--time` (un poco más que T), si no queda abierto hasta que lo mate el `timeout` (se perdieron
  3 minutos por captura así).
- `--shots-every DT`: desde T, una cada DT (`archivo_000.png`, `archivo_001.png`...).
- Para capturar un punto `(x, z)` del mapa: `--headless --test profile` da `s → x, z, h` cada 2 m, y con
  `--spawn s` y `--view` se apunta. Un parpadeo (z-fighting) con la cámara quieta es un patrón fijo de
  rayas; con `--shots-every` y una pose que se mueve (`--test pose`) se ve cambiar entre cuadros.
- `--nohud`, `--norider`, `--genrider` (piloto generado), `--menu bikes|maps|join|name|lobby|ajustes|controles|principal`
  (el menú principal es el que aparece al arrancar, sin `--menu`; `principal` sirve con `--host`, para el de la
  partida en red) y `--menu no` (corriendo, como "Jugar solo": el HUD con la ayuda de teclas del principio).
- El panel de datos técnicos (velocidad, rpm, suspensión, agarre...) ya no está siempre: `--datos` lo muestra (o
  T, o Ajustes). Las capturas con el bot o una prueba no llevan la ayuda de teclas (es para el jugador).
- **`--noprefs`** ni lee ni escribe `preferencias.ini` (las capturas no dependen del archivo de la carpeta ni lo pisan) y
  **`--gfx bajo|medio|alto|ultra`** aplica ese preset de calidad encima de lo leído (`Renderer::ApplyPreset`: sólo calidad; el
  estilo y la física del suelo quedan) y **no guarda nada** mientras dura la corrida. Sin ninguno de los dos, sin archivo, el
  preset sale de la placa (esta PC, RTX 5070: Alto). Ejemplo: `--size 1280 720 --nohud --map base/motocross --test pose
  --view 200 12 3 --gfx medio --screenshot 3 f.png --time 4`. La red de seguridad de fps no corre con `--size`, capturas,
  pruebas, bot, `--noprefs` ni `--gfx`.
- Ajustes tiene tres páginas: con `tools\teclas.ps1`, `RIGHT` sobre la primera fila pasa de Gráficos a Imagen y a Juego y
  sonido; las filas de cada página y sus posiciones están en MENU.md. Para el mouse, `PostMessage` a la ventana
  (`WM_MOUSEMOVE`, `WM_LBUTTONDOWN/UP`) con las coordenadas de la captura **divididas por la escala de pantalla de Windows** (125%: 320, 238
  para clickear en 400, 297).
- Migración de preferencias: un `preferencias.ini` sin `graficos_version` se copia a `preferencias.ini.v1.bak` y se reescribe;
  para probarla, dejar uno viejo (o el del jugador) junto al exe y abrir el juego una vez.
- `preferencias.ini` se lee al lado del exe también en las capturas: si el de tu carpeta tiene el sacudón, el
  motion blur o los datos técnicos cambiados, las capturas cambian.
- **Surcos físicos en una captura**: las baldosas finas de `TerrainSoil` (y la colisión que las acompaña) sólo existen con
  `--ruts` (o Deformación "Física" manejando uno mismo); un bot o un test sin `--ruts` sólo deja huellas visuales. Para ver
  dónde están las baldosas, teñirlas un momento en `kTerrainFS` (`if (physical > 0.5) albedo *= vec3(1, 0.3, 1);`).
- **Conducir sobre surcos**: con `--ruts` la moto tiene que manejarse casi igual que sin surcos (ver [FISICA.md](FISICA.md),
  "El surco no tiene que manejar la moto"). Desde `pruebas/`: `--map prueba/plaza_tierra --test circleN --time 60 --ruts`
  (N = 6 a 16; con `--telemetry` el radio sale de `v / (wy / cos roll)`, sin caídas y a pocos % del radio sin `--ruts`),
  `--test accel|brakestraight|brakeslide --ruts` y el bot con `--ruts` (`--bot --time 700`: 10 vueltas de 1:08.3 a 1:08.9). Los
  círculos al límite (roll ~47°) son el detector: cualquier rebote de la cubierta es una caída. `--test soilcheck` mide lo que
  sienten las ruedas (tope, sin bancos, normal) y que sin surcos `RideHeight`/`RideNormal` den `Height`/`Normal`.
- **Culling y matas**: para comprobar que el culling no descarta de más, proyectar con la matriz de la cámara los vértices de
  cada chunk descartado y contar los que caen adentro del frustum (con una variable temporal; debe dar 0). Las capturas
  con y sin culling difieren por el grano y el viento del pasto (0.01-0.5% de píxeles > 24 niveles entre dos corridas iguales).
- **Teclas de verdad** (menú, Ajustes, H, T, M, R...): `tools\teclas.ps1 -Dir <carpeta> -Keys "ESC,DOWN,ENTER"`
  con el juego abierto desde esa carpeta (en segundo plano, con `--screenshot T` y `--time`). Manda
  `WM_KEYDOWN`/`WM_KEYUP` con `PostMessage` sólo a esa ventana, cada tecla apretada 90 ms (si baja y sube en
  el mismo cuadro, raylib no ve el `IsKeyPressed`). Lo que cambia en Ajustes queda en su `preferencias.ini`.
  - **Trampa**: `SendKeys` y `keybd_event` van a la ventana que está al frente, no al juego: Windows no deja que
    un proceso de fondo pase otra ventana al frente (`SetForegroundWindow` da `False`). En la primera prueba dos
    tandas de flechas y Enter fueron a la ventana del usuario.
  - GLFW saca la tecla del scancode del `lParam` (bits 16-23, y el 24 para las flechas), no del código virtual.
- Cámara:
  - `--side --view 0 0 0.3`: costado **izquierdo**, fija (`--side` ignora el yaw);
  - `--view 90 15 0.5`: costado **derecho** desde un poco arriba (ahí va el escape);
  - `--view 270 15 0.5`: el izquierdo.

  El yaw de la órbita no es igual con distintos pitch: confirmar mirando dónde queda la trompa.
- `tools/grilla.py salida.png columnas escala [crop x0 y0 x1 y1] archivos...` arma una grilla numerada
  (para mirar una serie de golpe) y recorta si hace falta.
- **Cuánto se mete el piloto en la moto** (con el modelo, con ventana): `python tools/holgura/armar.py` compila
  una copia con la medición y `tools/holgura/barrido.sh` barre ~900 poses por moto; `tabla.py` compara antes
  y después y `vista.py` dibuja una pose con lo que se mete marcado. Ver [PILOTO.md](PILOTO.md), "Medir la holgura".

- **El bot con ventana es mucho menos estable que sin ventana** (puede irse 100 m afuera): las vueltas se
  validan sin ventana.

## Mapas de prueba

`pruebas/mods/prueba/`: mapas y motos que no van en el juego:
- circuito de obstáculos de trial y escalones;
- los mapas de siempre con otra moto;
- pistas de la de carreras (`pista_carrera`, `recta`);
- plazas llanas para medir una moto: `plaza_asfalto` (120 m de ancho), `plaza_asfalto_ancha` (400 m, 1.5 km derechos
  desde la largada) y `plaza_tierra` (400 m, 1.1 km derechos, con el agarre de la tierra de pista);
- copias del circuito con otro bot o motos de prueba (`pista`, `agarre`);
- Los Médanos y el Parque de física con la vuelta guía por lo que la vuelta no pisa (`medanos_crater`,
  `medanos_ola`, `medanos_grande`, `medanos_montes`; `park_tierra`, `park_madera`, `park_pump`,
  `park_plaza`, `park_bowl`), para medir cada zona con `tools/saltos.py` o `tools/tramo.py`; los escriben
  `tools/medanos.py` y `tools/parque.py` (y `park`, el parque con la trial).

El juego junta los `mods/` de la carpeta actual y los de la del
ejecutable, así que se usan corriendo desde `pruebas/`:

```
cd pruebas && <exe> --headless --map prueba/trial_obstaculos --bot --time 120
```

## Paquete para compartir

### v0.3.2 (29/09/2026): el paquete que junta lo hecho desde la v0.2.8

`dist/MotoSim-v0.3.2.zip` (25 archivos, 7.1 MB). **Es la que se publica en GitHub Releases**: la v0.3.0 y la v0.3.1 se
armaron sólo acá (`dist/MotoSim-v0.3.0.zip`, `v0.3.1.zip`, nunca se subieron) y las notas de la v0.3.2 son acumulativas
desde la v0.2.8. Protocolo 7 (`Multiplayer.cpp` idéntico al de la v0.2.8): juega con la v0.2.5 a la v0.2.8 y con las v0.3.x.
Ejecutable Windows x64 con runtime MinGW estático (`build-release`; MSVC no está en esta PC): `objdump -p` lista
KERNEL32, USER32, GDI32, SHELL32, WINMM, WS2_32 y las `api-ms-win-crt-*` (la UCRT, que trae Windows 10/11).

- Lo que se cuenta al jugador sale de `LEEME.txt` (raíz del repo, con las novedades arriba) y `RELEASE_NOTES.md` (el
  cuerpo de la release, lo usa `.github/workflows/release.yml`). Al armar la versión siguiente se **edita `LEEME.txt`**
  de la raíz (el bloque "NOVEDADES DE LA vX" nuevo arriba, la primera línea con la versión, "Los dos tienen que tener")
  y `RELEASE_NOTES.md`; el script rechaza un `LEEME.txt` que no diga la versión de `src/Version.h`.
- **`python tools/package_release.py <exe>`** hace la carpeta `dist/MotoSim-vX/` **y** el zip (el mismo que corre el
  workflow con `build-release/Release/motocross.exe`): copia `MotoSim.exe`, `tuning.ini`, `mods/` sin `pruebas/`,
  `MODDING.md`, el modelo 3 del piloto con sus `_deps` y escribe `LEEME.txt` en UTF-8 con BOM y CRLF; nunca lleva
  `preferencias.ini`; verifica el zip (sano, sin prefs ni pruebas, igual a la carpeta, LEEME CRLF puro). Rehace la
  carpeta y el zip cada vez, así que se corre una vez para probar desde la carpeta y otra al final (antes del zip
  definitivo). No depende de nada de esta PC (sólo el exe que se le pasa).
- Verificado con el exe final (desde la carpeta del paquete y desde el zip descomprimido en otra carpeta):
  - `--test bikestats`: `mods: 6 mapas, 5 motos`, sin errores (sólo las advertencias de `rut_dig_rate` y `rut_max_depth`);
  - bot 150 s en motocross: **1:08.73 / 1:08.42**; `--test soilcheck` (motocross, `--flat`, `--map base/circuito`):
    0 fallos (rodadura sin patinaje: 47.7 mm en la pista, 21.5 mm fuera de ella, 54.0 mm en la tierra del circuito); favela
    1:37.40 / 1:37.22, circuito con la de carreras 2:11.65, park 0:51.16;
  - `tools/regresion.sh` contra `dist/MotoSim-v0.3.1` (la última): **idéntico en 13 pruebas** (las 7 de la lista, `airlean`,
    favela, circuito con la de carreras, Médanos, valle, park). Contra el `baseline-exe` (MinGW, fuentes de la v0.2.8) también
    idéntico en `accel`, `airlean` y la favela a 40 y 100 s. Contra la **v0.2.8 publicada** (MSVC): idéntico en 10 y
    **distinto** en `accel`, `airlean` y la favela: ver la trampa del compilador, abajo;
  - el surco no maneja la moto (`pruebas/`, `--map prueba/plaza_tierra`, con y sin `--ruts`): `accel` 3.26 s a 60 km/h en las
    dos, 95.5 km/h a los 5 s; `brakestraight` beta máx 4.6 / 4.2°, sin caídas; `circle8` 6.99 / 7.08 m y `circle12`
    17.14 / 17.23 m (+1.3 y +0.5%), sin caídas. Con la v0.3.1 y `--ruts` (el "antes"): 3.76 s y 80.5 km/h, caída frenando a
    los 2 s y caída a los 5.3 s en los dos círculos;
  - capturas (1280x720): menú con "v0.3.2", Ajustes > Gráficos ("Alto (auto)"), los 4 presets en la pista, seis mapas, una
    corrida `--ruts` con el surco y el polvo, boxes del circuito sin pasto: sin errores de GL en los logs (sólo el aviso de
    fuente de raylib);
  - migración: un `preferencias.ini` viejo (sombras 3, sol 60, niebla 190, volumen 40, nombre, moto) queda en
    `preferencias.ini.v1.bak` idéntico byte a byte, los gráficos pasan a Alto, el volumen, el nombre y la moto quedan y el
    menú avisa "Renovamos los gráficos"; la red de seguridad de fps midió 5.9 ms contra 5.6 del monitor y no bajó;
  - red (`tools/red.py`): nuevo contra nuevo, nuevo (carrera) anfitrión con v0.2.8 (trial) cliente, v0.2.8 anfitrión con nuevo
    cliente, y nuevo con la v0.3.1 y con la v0.3.0: cada uno ve la moto del otro, ping 10-18 ms, corrección 2-9 cm.
- **Revisión por lectura para MSVC y Mac** (sin compilador de ninguno de los dos): `g++ -std=c++17 -Wall -Wextra -Wpedantic
  -Wnarrowing -fsyntax-only` sobre todos los `src/*.cpp` (sin `gnu++`, con `-isystem` para raylib y Jolt): 0 avisos (nada de VLAs,
  inicializadores designados, extensiones ni estrechamientos que MSVC y clang rechazan); un script que busca cada `std::` y
  función de libc y comprueba que su cabecera esté incluida directo o por un `.h` del proyecto: sólo faltaba `<algorithm>` en
  `SoilChecks.cpp` (`std::max`; agregado; el resto de lo que marca ya estaba igual en la v0.2.8, que compila con MSVC y
  clang); ninguna cadena cruda de los shaders pasa de 5.8 KB (MSVC corta los literales de más de 16 KB); sin `M_PI`,
  `windows.h` fuera de `Net.cpp` (con `NOMINMAX`), `dynamic_cast` ni `typeid` (Mac va con `-fno-rtti`); los bindings
  estructurados están en `for` y no se capturan en lambdas.

1. Versión: cuando el usuario pide "compilá", el paquete sale con la anterior + 0.0.1 sin preguntar (si
   da un nombre, ése). `src/Version.h`; el protocolo de red sube sólo si cambió el formato de los paquetes.
2. Compilación aparte con `tools\compilar.bat build-release release` (`-DMOTOSIM_STATIC_RUNTIME=ON`;
   recompilar todo); `dumpbin /dependents` tiene que listar sólo DLLs de Windows.
3. Carpeta `dist/MotoSim-vX/`:
   - `MotoSim.exe` y `tuning.ini`;
   - `mods/`, sin `pruebas/`;
   - `MODDING.md`;
   - el modelo del piloto: `Low_Poly_Motorcyclist_3_rigged.gltf` con `Low_Poly_Motorcyclist_3_rigged_deps/`
     (el 2 no va en el paquete; queda en el proyecto de respaldo, a pedido del usuario: no borrarlo);
   - `LEEME.txt`: el de la raíz del repo (el de la versión anterior con las novedades nuevas arriba), que el script
     escribe en UTF-8 con BOM y CRLF. Verificarlo con Python: el `grep -c $'\r'` de Git Bash da 0 aunque haya CRLF.

   **Sin `preferencias.ini`**: el juego lo guarda al lado del exe (`GetApplicationDirectory`), sólo con
   ventana; una prueba con ventana desde esa carpeta lo crea (pasó dos veces). Las capturas de
   comparación, con el exe del paquete corriendo desde otra carpeta, o borrarlo después. En la v0.2.6
   apareció uno antes de las capturas y no se supo de dónde (probado: ninguna corrida sin ventana lo
   crea, ni en red): **revisar justo antes del zip**.
4. Probar desde la carpeta del paquete:
   - `--test bikestats` (sin errores de mods);
   - vueltas del bot (motocross 1:08.73 / 1:08.42) y los demás mapas;
   - una captura del menú (la versión);
   - un par en red con motos distintas (`--bike`); si el protocolo no cambió, también contra el
     paquete anterior, en los dos sentidos.

   Lo que dice el LEEME se vuelve a medir con la versión final: en la v0.2.5 la trial ya no subía el
   escalón de 0.5 m a fondo (sólo levantando la rueda) y hubo que cambiar el texto. En la v0.2.6, el
   derrape lento no sale "a paso de hombre" (a ~11 km/h la cola sale 4-8°, salvo la 2T) sino a ~15 km/h,
   y la escadaria a 30-32 km/h termina en caída (ver [FISICA.md](FISICA.md)).
5. El zip: `python tools/package_release.py <exe>` (carpeta y zip juntos; antes era `Compress-Archive`). Revisar la lista de archivos del zip.
6. **Publicarlo en GitHub Releases** (la sección de descargas del repo; el README apunta a
   `releases/latest`):
   - después de mergear el PR de la versión, la etiqueta en el commit del paquete:
     `git tag -a vX <commit> -m "MotoSim vX"` y `git push origin vX`;
   - en `https://github.com/Lukto02/MotoSIM/releases/new?tag=vX`: título "MotoSim vX", descripción
     con "Cómo jugar" (bajar el zip, descomprimir, ejecutar; requisitos; con qué versiones juega en red)
     y las novedades del LEEME, y el zip adjunto;
   - "Latest" sólo para la más nueva: al publicar una vieja, marcar "None" (GitHub trae "Latest"
     marcado por defecto y se la sacaría a la nueva);
   - con el navegador, el zip se sube con la herramienta de archivos al `<input type="file">` escondido,
     no al botón (el botón abre el selector de Windows). Esa herramienta acepta hasta 10 MB: la v0.2.6
     y las anteriores (~17 MB, con el modelo 2 del piloto) no se pudieron subir así; hay que
     arrastrarlas a mano a la página de la release;
   - al subir el archivo, la página pasa a ser un borrador (`releases/edit/untagged-...`): revisar que
     el título, la etiqueta y la marca de "Latest" sigan bien antes de "Publish release";
   - comprobar con `curl -s https://api.github.com/repos/Lukto02/MotoSIM/releases/latest`.
   Publicadas: v0.2.7 y v0.2.8 (etiquetas v0.2.6, v0.2.7, v0.2.8). La v0.3.2 sale por el workflow
   `.github/workflows/release.yml`: al llegar a `main` un cambio de `src/Version.h` compila con MSVC (`windows-latest`),
   corre `soilcheck` (motocross, `--flat`, circuito) y el bot de 150 s, arma el paquete con `package_release.py`, y crea la
   release con `RELEASE_NOTES.md` de cuerpo y el zip (falla si la etiqueta ya existe). Lo que hay que revisar en la pestaña
   Actions es que ese job y el `build` de cada push estén en verde.
7. **Limpiar las ramas**: mergeado el PR, borrar su rama acá y en GitHub (`git push origin --delete <rama>`,
   `git branch -d <rama>`), y las de los worktrees de los agentes. Lo normal es que quede sólo `main`: cada
   versión sigue a mano por su etiqueta y su release.

Paquetes: v0.2.5 (protocolo 7), v0.2.6 (protocolo 7, juega con la v0.2.5; carrera en tierra y
frenando, golpes fuertes, derrape lento, escaleras y cantos, parpadeos, cámara del selector),
**v0.2.7** (protocolo 7, juega con la v0.2.5 y la v0.2.6; 5.4 MB): Los Médanos, el piloto nuevo (modelo 3 con
dedos), la de carreras frenando y doblando, sin trompo frenando la motocross, la 2T y la trial, suspensión
más viva en la motocross y la 2T, choques en red (quién se cae), reaparecer con R, sacudón y motion blur
apagables. Al probar el paquete apareció un `preferencias.ini` del usuario (había abierto el juego desde la
carpeta del paquete): no se borra; el zip se arma desde una copia sin él.
**v0.2.8** (protocolo 7, juega con la v0.2.5-v0.2.7; 5.4 MB): menú Ajustes y Controles, HUD más limpio, Los
Médanos con saltos encadenados, el Parque rediseñado, el piloto que no se mete en la moto, la Trilheira sin
trompo, la 2T con motor de dos tiempos, la Carrera pesada en el aire, sin parpadeos en el autódromo y la
favela, profundidad de 24 bits.
**v0.3.2** (protocolo 7, juega con la v0.2.5-v0.2.8; 7.1 MB; junta la v0.3.0 y la v0.3.1, que no se publicaron): suelo de
tierra deformable con colisión (surcos, terrones, pasto), el surco que no maneja la moto, sombras en dos cascadas, post-proceso
limpio, presets Bajo/Medio/Alto/Ultra con autodetección, Ajustes en tres páginas, pasto sin saltos, looks de los mapas.

- **Trampas de las pruebas del paquete**:
  - **Un exe MSVC y uno MinGW no dan igual después de una caída.** El paquete publicado (v0.2.8, 3.8 MB) es de MSVC y los de
    esta PC (7 MB) de MinGW: `regresion.sh` da idéntico en todo lo que no termina en caída, pero `accel` (cae a los ~5 s),
    `airlean` y la favela difieren **después de la caída** (en `accel`, `crash=1` desde t=4.5 y el primer `t=` distinto es 5.51;
    el bot de motocross y sus vueltas, idénticos). Contra un exe MinGW hecho con las mismas fuentes que la v0.2.8 (`baseline-exe`)
    dan idénticas: no es un cambio de física. Para regresar contra un paquete de MSVC, mirar hasta el `crash=`; para
    comparar de verdad, contra un MinGW.
  - **Un `&` al final de `a && b && ( ... ) &` manda TODA la cadena al fondo**: un `rm -f $DIR/*` corrió en paralelo con las
    corridas y borró un archivo de salida recién creado (parecía que `soilcheck` no imprimía nada). Poner el `&` sólo
    dentro del paréntesis, o correr en serie.
  - **Rutas de Git Bash a un Python de Windows**: `python -c "...r'/c/Users/...'"` escribe en `C:\c\Users\...` (creó una
    carpeta `C:\c`). Pasar a Python rutas `C:/...` o `C:\...`, o `Expand-Archive` de PowerShell.
  - `tools/package_release.py` se rehace entero cada vez (carpeta y zip): correrlo una vez, probar desde la carpeta
    (con `--noprefs` las capturas no dejan `preferencias.ini`; sin él, borrarlo) y correrlo otra vez al final.
  - `brakeslideN` toma N en **m/s**, no en km/h (`brakeslide10` entra a 36 km/h). El derrape lento se
    mide con `brakeslide2.5` a `brakeslide4.4` (9-16 km/h), mirando `beta` mientras `rb` > 0.5.
  - En `frenada280` el "SE CAYÓ" de `tools/frenada.py` es el muro: la recta de la prueba se termina a
    ~105 km/h, la moto sale al pasto con las lisas y pega a ~60 km/h. La frenada en sí: beta 0°, ~10 m/s².
  - El par en red desde un script (`tools/red.py` ya lo hace bien): la salida del anfitrión a un **archivo**. Con un pipe que nadie lee
    mientras corre el cliente, se llena, el anfitrión se traba en el `printf` y el cliente ve la "edad"
    de los datos crecer (parece un bug de red y no lo es).
  - **Sin ventana el modelo del piloto no se carga**: el ragdoll de `--headless` es el del piloto
    generado. Lo del ragdoll con el modelo se mide con ventana (ver [PILOTO.md](PILOTO.md), "Cómo se mide").
  - El id del parque es `sandbox/park`; con un id que no existe, `--map` cae en la pista de motocross
    sin avisar fuerte (las vueltas dan 1:08.37 y parece que anduvo).

## Red en una sola PC

Anfitrión y cliente sin ventana, cada uno con su bot; el cliente toma el código de invitación de la
salida del anfitrión. `python tools/red.py <exe anfitrión> <moto> <exe cliente> <moto>` hace todo y
resume lo que ve cada uno (ver [RED.md](RED.md)). Con motos distintas por jugador, la telemetría
imprime la moto y el estilo de cada remoto. Los choques entre motos, con `python tools/netchoque.py`
(tabla de arriba); con `--lag` y `--jitter`, como una partida por internet.
