# Compilar y probar

## Compilar

- `build.bat` / `build/` son de otra PC (MinGW). En esta PC se compila con VS 2022 BuildTools en una
  carpeta aparte (vcvars64 + CMake + Ninja, reusando las fuentes de raylib y Jolt ya bajadas en
  `build/_deps`); ver la configuración en el `.bat` de desarrollo de la sesión.
- **Ninja no sigue bien los headers acá** (el MSVC en castellano imprime "Nota: inclusión del archivo"
  y ninja no lo entiende). Después de tocar un `.h` que cambia el tamaño de una estructura (un enum con
  `Count`, un miembro nuevo), tocar los `.cpp` que lo incluyen o recompilar todo. Si no, se linkean
  objetos viejos y el juego se cierra sin avisar en el primer cuadro.
- El paquete para compartir es otra compilación (runtime estático); ver la nota de memoria del paquete.
- **Scripts con saltos de línea**: los scripts de Python que editan C++ se escriben con la herramienta
  de archivos, no en un heredoc de bash. En un heredoc, un `\n` dentro de un string de C++ terminó
  convertido en un salto de línea real (tres veces).
- **Rutas largas**: en una copia del proyecto muy adentro del scratchpad, un objeto de Jolt pasa los
  260 caracteres de Windows (error C1083). Se compila en una carpeta de nombre corto o con `subst W: <copia>`.

- **Fin de línea**: Python en Windows, abriendo en modo texto, escribe CRLF aunque el archivo fuera
  LF (pasó con una docena de archivos). Para editar sin cambiarlo: leer y escribir en binario, o
  `open(..., newline='')`. Chequeo: contar `\r\n` contra `\n` con Python.

## Regresiones: la motocross tiene que dar idéntica

`tools/regresion.sh <exe nuevo> <exe de referencia> "args" ...` corre cada prueba sin ventana con los
dos ejecutables y compara la telemetría (md5). La referencia es el último paquete
(`dist/MotoSim-v0.2.4/MotoSim.exe`). El juego nuevo agrega ` scr=` al final de cada línea; el script
lo saca.

```
tools/regresion.sh <nuevo> <viejo> "--flat --test brakeslide --time 8" "--flat --test cuerpo --time 12" \
  "--flat --drop 1 --test flip --time 6" "--flat --drop 1 --test whip --time 5" \
  "--flat --test wheelie --time 8" "--bot --time 40" "--test accel --time 12"
```

- Vueltas del bot en la pista de motocross (`--headless --bot --time 150`): **1:08.37 y 1:08.51**.
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
| caídas en serie | `--test crashloop [--respawn-after S]` |
| la pista al revés desde S | `--spawn S --test subidaN` (N km/h, recto: p. ej. subir la escadaria con `--spawn 90`) |
| cuerpo en el aire | `--flat --drop 3 --test airrot` (atrás a fondo) o `airlean` (adelante y atrás cada 0.6 s) |
| frenada a fondo desde N km/h | `--map base/circuito --bike base/carrera --test frenadaN` y `python tools/frenada.py` |
| bajar la escadaria a N km/h | `--map favela --spawn 68 --test bajadaN` (sigue la línea) y `python tools/escalera.py 74 88` |
| cada rueda en cada paso | `--wheel-log T0 T1`: contra qué pega el rayo, normal, apoyo, compresión y fuerzas |
| columnas de la telemetría | `... --telemetry --telemetry-dt 0.05 \| python tools/telemetria.py t v pitch fN rN gnd s --s 70 95` |
| sonido | `--sound-test archivo.wav [2t\|raspado]` y `python tools/sonido.py` (ver [SONIDO.md](SONIDO.md)) |
| el resto | la lista de `--test` está en el README ("Pruebas automáticas") |

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
- copias del circuito con otro bot o motos de prueba (`pista`, `agarre`). El juego junta los `mods/` de la carpeta actual y los de la del
ejecutable, así que se usan corriendo desde `pruebas/`:

```
cd pruebas && <exe> --headless --map prueba/trial_obstaculos --bot --time 120
```

## Paquete para compartir

1. El nombre de la versión lo elige el usuario (si dice "dale" sin nombre, la siguiente sub-versión, y se
   le avisa). `src/Version.h`; el protocolo de red sube sólo si cambió el formato de los paquetes.
2. Compilación aparte con `-DMOTOSIM_STATIC_RUNTIME=ON` (recompilar todo); `dumpbin /dependents` tiene
   que listar sólo DLLs de Windows.
3. Carpeta `dist/MotoSim-vX/`:
   - `MotoSim.exe` y `tuning.ini`;
   - `mods/`, sin `pruebas/`;
   - `MODDING.md`;
   - el modelo glTF con su `_deps/`;
   - `LEEME.txt`: el anterior con las novedades arriba, en UTF-8 con BOM y CRLF. Verificarlo con
     Python: el `grep -c $'\r'` de Git Bash da 0 aunque haya CRLF.

   **Sin `preferencias.ini`**: una prueba con ventana desde esa carpeta lo crea (pasó dos veces). Las
   capturas de comparación, con el exe del paquete corriendo desde otra carpeta, o borrarlo después.
4. Probar desde la carpeta del paquete:
   - `--test bikestats` (sin errores de mods);
   - vueltas del bot (motocross 1:08.37 / 1:08.51) y los demás mapas;
   - una captura del menú (la versión);
   - un par en red con motos distintas (`--bike`).

   Lo que dice el LEEME se vuelve a medir con la versión final: en la v0.2.5 la trial ya no subía el
   escalón de 0.5 m a fondo (sólo levantando la rueda) y hubo que cambiar el texto.
5. `Compress-Archive` de la carpeta a `dist/MotoSim-vX.zip`.

## Red en una sola PC

Anfitrión y cliente sin ventana, cada uno con su bot; el cliente toma el código de invitación de la
salida del anfitrión (ver la nota de memoria de pruebas de red). Con motos distintas por jugador, la
telemetría imprime la moto y el estilo de cada remoto.
