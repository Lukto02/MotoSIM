# Compilar y probar

## Compilar

- `build.bat` / `build/` son de otra PC (MinGW). En esta PC: `tools\compilar.bat [carpeta] [release]`
  (VS 2022 Build Tools: vcvars64 + CMake + Ninja, reusando las fuentes de raylib y Jolt ya bajadas en
  `build/_deps`, sin internet). Por defecto compila en `build-msvc`; los agentes que trabajan a la vez,
  cada uno en `build-msvc-<área>`. El exe queda en `<carpeta>\motocross.exe` con `tuning.ini` y `mods\`.
- **Ninja no sigue bien los headers acá** (el MSVC en castellano imprime "Nota: inclusión del archivo"
  y ninja no lo entiende). Después de tocar un `.h` que cambia el tamaño de una estructura (un enum con
  `Count`, un miembro nuevo), tocar los `.cpp` que lo incluyen o recompilar todo. Si no, se linkean
  objetos viejos y el juego se cierra sin avisar en el primer cuadro.
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
(hoy `dist/MotoSim-v0.2.7/MotoSim.exe`; el más nuevo es el de `ls -t dist`, ojo que la v0.3.1 es
anterior a las v0.2.x). El juego nuevo agrega ` scr=` al final de cada línea; el script
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
| la pista al revés desde S | `--spawn S --test subidaN` (N km/h, recto: p. ej. subir la escadaria con `--spawn 90`) |
| cuerpo en el aire | `--flat --drop 3 --test airrot` (atrás a fondo) o `airlean` (adelante y atrás cada 0.6 s) |
| frenada a fondo desde N km/h | `--map base/circuito --bike base/carrera --test frenadaN` y `python tools/frenada.py` |
| frenar y doblar a la vez desde N km/h | desde `pruebas/`: `--map prueba/plaza_asfalto --bike base/carrera --test frenacurvaN[xS][dD][aA][n]` (S: cuánto manubrio, 1 = la D, < 0 la A; dD: la dirección empieza D s después de frenar, D < 0 antes, viniendo inclinada; aA: la suelta a los A s; n: sin freno) y `python ../tools/frenacurva.py`. Como con teclado: `frenacurva150x0.6d-1.5a1.5` viene doblando y suelta para frenar, `frenacurva150x1d0.3a0.15` toca la D frenando |
| el freno de cada rueda en cada paso | `--wheel-log T0 T1`: además de lo de la rueda, una línea `freno` (vueltas, patinaje, si está retenida, freno, uso y agarre máximo) |
| bajar la escadaria a N km/h | `--map favela --spawn 68 --test bajadaN` (sigue la línea) y `python tools/escalera.py 74 88` |
| un salto a N km/h | `python tools/saltos.py <exe> sandbox/medanos 240 446 40,50,60 ambos --time 30`: aparece en S, entra al salto que despega en s = 446 sin tocar nada (`bajadaN`) y con el bot a N; aire, altura, vy al tocar, cabeceo y si se cae (ver [MAPAS.md](MAPAS.md), "Medir un salto") |
| cada rueda en cada paso | `--wheel-log T0 T1`: contra qué pega el rayo, normal, apoyo, compresión y fuerzas |
| columnas de la telemetría | `... --telemetry --telemetry-dt 0.05 \| python tools/telemetria.py t v pitch fN rN gnd s --s 70 95` |
| sonido | `--sound-test archivo.wav [2t\|raspado]` y `python tools/sonido.py` (ver [SONIDO.md](SONIDO.md)) |
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

- `--size W H --screenshot T archivo.png`: una captura a los T s de simulación.
- `--shots-every DT`: desde T, una cada DT (`archivo_000.png`, `archivo_001.png`...).
- `--nohud`, `--norider`, `--genrider` (piloto generado), `--menu bikes|maps|join|name|lobby` (el menú principal
  es el que aparece al arrancar, sin `--menu`).
- Cámara:
  - `--side --view 0 0 0.3`: costado **izquierdo**, fija (`--side` ignora el yaw);
  - `--view 90 15 0.5`: costado **derecho** desde un poco arriba (ahí va el escape);
  - `--view 270 15 0.5`: el izquierdo.

  El yaw de la órbita no es igual con distintos pitch: confirmar mirando dónde queda la trompa.
- `tools/grilla.py salida.png columnas escala [crop x0 y0 x1 y1] archivos...` arma una grilla numerada
  (para mirar una serie de golpe) y recorta si hace falta.

- **El bot con ventana es mucho menos estable que sin ventana** (puede irse 100 m afuera): las vueltas se
  validan sin ventana.

## Mapas de prueba

`pruebas/mods/prueba/`: mapas y motos que no van en el juego:
- circuito de obstáculos de trial y escalones;
- los mapas de siempre con otra moto;
- pistas de la de carreras (`pista_carrera`, `recta`);
- plazas llanas para medir una moto: `plaza_asfalto` (120 m de ancho), `plaza_asfalto_ancha` (400 m, 1.5 km derechos
  desde la largada) y `plaza_tierra` (400 m, 1.1 km derechos, con el agarre de la tierra de pista);
- copias del circuito con otro bot o motos de prueba (`pista`, `agarre`). El juego junta los `mods/` de la carpeta actual y los de la del
ejecutable, así que se usan corriendo desde `pruebas/`:

```
cd pruebas && <exe> --headless --map prueba/trial_obstaculos --bot --time 120
```

## Paquete para compartir

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
   - `LEEME.txt`: el anterior con las novedades arriba, en UTF-8 con BOM y CRLF. Verificarlo con
     Python: el `grep -c $'\r'` de Git Bash da 0 aunque haya CRLF.

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
5. `Compress-Archive` de la carpeta a `dist/MotoSim-vX.zip`.

Paquetes: v0.2.5 (protocolo 7), v0.2.6 (protocolo 7, juega con la v0.2.5; carrera en tierra y
frenando, golpes fuertes, derrape lento, escaleras y cantos, parpadeos, cámara del selector),
**v0.2.7** (protocolo 7, juega con la v0.2.5 y la v0.2.6; 5.4 MB): Los Médanos, el piloto nuevo (modelo 3 con
dedos), la de carreras frenando y doblando, sin trompo frenando la motocross, la 2T y la trial, suspensión
más viva en la motocross y la 2T, choques en red (quién se cae), reaparecer con R, sacudón y motion blur
apagables. Al probar el paquete apareció un `preferencias.ini` del usuario (había abierto el juego desde la
carpeta del paquete): no se borra; el zip se arma desde una copia sin él.

- **Trampas de las pruebas del paquete**:
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
