# Multijugador (red local)

`Net.cpp` (socket UDP, direcciones, código de invitación) y `Multiplayer.cpp` (anfitrión/cliente,
paquetes, motos remotas). El funcionamiento visto desde el jugador está en el README ("Multijugador LAN").

## Protocolo

- Cada paquete lleva `MX` y el número de protocolo (`kProtocol` en `Multiplayer.cpp`). Distinto = "otra
  versión". **Sube cada vez que cambia el formato de un paquete** (y la versión del juego la elige el
  usuario). Historia (la "v0.3.1" de acá es la vieja, de antes de la v0.2.1, no la v0.3.1 de la serie nueva): 3 (v0.3.1), 4 (v0.2.1-v0.2.2, mapa), 5 (v0.2.3, cuerpo de costado), 6 (v0.2.4,
  mapa por nombre `mod/archivo`), 7 (v0.2.5, la moto de cada uno; la v0.2.6 sigue en 7 y juega con la
  v0.2.5: probado con anfitrión de una y cliente de la otra, en los dos sentidos, con motos distintas; la
  v0.2.7 y la v0.2.8 también siguen en 7 y juegan con las anteriores: la regla nueva de los choques la usa
  cada uno en su PC; la v0.3.0, la v0.3.1 y la v0.3.2 (suelo deformable, gráficos) tampoco tocaron el formato:
  siguen en 7, `Multiplayer.cpp` idéntico al de la v0.2.8, y la v0.3.2 conectó con la v0.2.8 en los dos sentidos, y con la v0.3.1
  (de anfitrión) y la v0.3.0 (de cliente), con `tools/red.py` y motos distintas).
- HELLO (cliente → anfitrión, a la IP del código y por broadcast) → WELCOME con el número de jugador.
  El HELLO lleva la moto del jugador; si cambia de moto conectado, se vuelve a mandar.
- Estado de cada moto 60 veces por segundo: posición, rotación, velocidades, suspensión, ruedas,
  dirección, piloto, motor, petardeos, banderas (`kRiderOn`, `kCrashed`, `kLimiter`, `kShifting`,
  `kFrontGround`, `kRearGround`), por rueda dónde apoya, carga y patinaje (huellas y polvo) y, caído, las
  11 partes del ragdoll. El anfitrión lo reenvía a los demás.
- **Al final del estado, el truco** (`trick`, `trickAmt` 0..255, dos bytes; octubre de 2026). Sin subir el protocolo:
  `ReadState` lee lo suyo y no mira lo que sobra, y el anfitrión reenvía el paquete tal cual, así que una versión que no
  los conoce los ignora y una nueva que no los recibe (el paquete termina antes) toma "sin truco" y deja `r.ok` como
  estaba. Probado: nueva contra v0.3.9-beta con `tools/red.py` en los dos sentidos, y anfitrión nuevo haciendo trucos
  (`--trucos nacnac`: el bot aprieta el botón en cada salto) con un invitado v0.3.9-beta. Con dos nuevas, la línea `net`
  del invitado muestra `truco=N NN%` del otro. **Para agregar algo más sin romper**: siempre al final y leído así.
- **Los trucos espejados** (v0.3.10: nac-nac a la derecha `NacNacRight` = 6, can-can a la izquierda `CanCanLeft` = 7):
  el primer byte lleva el truco que conoce la v0.3.9 (`Trick::Base`: el nac-nac o el can-can de siempre), el segundo
  cuánto, y un **tercer byte** el de verdad. La v0.3.9 lee dos y no mira el tercero: ve el truco sin espejar (mejor que
  nada). La nueva usa el tercero si viene y es de la misma familia (`Base(tercero) == primero`); si no viene (una v0.3.9),
  el primero. Mandar el id nuevo directo en el primer byte tampoco rompía a la v0.3.9 (lee `t < Trick::Count`, con su
  Count de 6, y si no, lo ignora), pero no mostraba nada. Probado (anfitrión con `--trucos nacnacder` o `cancanizq`,
  invitado `--test netduel`, línea `net`): nueva → nueva ve `truco=6` / `7`; nueva → v0.3.9 ve `truco=4`; v0.3.9 →
  nueva (`--trucos nacnac`) ve `truco=4`; sin errores en ningún sentido.
- **Los colores a gusto** (v0.3.10, menú Colores): después del tercer byte del truco, una marca (`kColorMark` 0xC7), las
  banderas (`kBikeColors` 1, `kSuitColors` 2) y, si hay alguna, cinco colores RGB (principal, secundario y detalles de la moto;
  principal y secundario del traje): 2 bytes sin colores, 17 con. Van en cada estado (60 por segundo, ~1 KB/s) y no en el
  HELLO ni en la lista: así el anfitrión no tiene que guardar nada (reenvía el paquete tal cual, también uno v0.3.9) y quien
  cambia de color en la partida se ve enseguida. La v0.3.9 lee hasta el segundo byte del truco y no mira el resto; la nueva
  lee los colores sólo si vino el tercer byte y la marca, y si no, dibuja como siempre (el esquema del número de jugador, el
  traje de la textura). Probado con `tools/red.py` (ahora acepta opciones de más en `MOTOSIM_RED_ANFITRION` /
  `MOTOSIM_RED_CLIENTE`, p. ej. `--colores ...`): nueva ↔ nueva, la línea `net` muestra `moto=181818,FAC812,28A0E6
  traje=...` del otro en los dos sentidos; nueva (anfitrión, con colores) ↔ v0.3.9 y v0.3.9 (anfitrión) ↔ nueva, sin errores
  y con las correcciones de siempre. Con ventana, el remoto blanco con el traje verde (`cap/col/redv_005.png`,
  `redv_remoto_zoom.png`). `kProtocol` sigue en 7.

- PING/PONG cada 0.5 s; ROSTER (lista de jugadores, con la moto de cada uno y el mapa) cada 1 s; 5 s sin
  noticias = desconectado. Nada necesita entrega confiable: todo se repite o se refresca.
- **Al final del ROSTER, el reloj del tráfico** (un `double`, octubre de 2026; sólo si el anfitrión está en un mapa con
  tráfico, la autopista). Sin subir el protocolo, como el truco: una versión que no lo conoce lee la lista y no mira lo que
  sobra; el invitado lo lee sólo si viene (si no, `HostClock` da false y su tráfico corre con su propio reloj). El invitado
  le suma media ida y vuelta y lo que pasó desde que llegó (`Multiplayer::HostClock`) y `Traffic::SyncClock` lo sigue: si
  está más de 2 s atrasado se pone al día de una (simula la base hasta ahí), si está más de 2 s adelantado la rehace desde
  el reloj 0, y si no corre hasta un 5% más rápido o más lento. Como la base del tráfico es determinista (ver MAPAS.md,
  "Autopista del Atlántico"), **los dos ven los mismos autos** salvo cerca de una moto, donde cada PC hace frenar y esperar a
  los autos por las motos que ve (la propia y las de los demás, con la predicción de siempre) y eso se cura solo cuando la
  moto se va. Probado: anfitrión en la autopista e invitado que entra 12 s después (headless, en una PC): la suma de la base
  a los 30 y 60 s del reloj sale igual en los dos (`TRAFICO base t=30.0 suma=a053b719`, `t=60.0 suma=8679b8e6`); nueva
  contra v0.3.9 con `tools/red.py` en los dos sentidos (en la motocross) sin errores. Los autos no viajan por la red.

## Motos remotas

- Cada PC simula sólo su moto. Las otras se dibujan donde se predice que están (la última foto más su
  velocidad por medio ping, hasta 0.25 s); la diferencia con lo que se mostraba se reparte en ~0.1 s.
  Medido en una PC: corrección media 5-9 cm a 12 m/s, ping 14-20 ms (se procesa por cuadro).
- Con su moto: `knownBike[]` guarda el id que mandó cada uno; si cambia, la remota se rearma con los
  `BikeParams` de esa moto (el resolver `mp.bikeParams` lo da Game). Si esta PC no tiene esa moto en
  `mods/`, se la ve con la propia.
- Cuerpo cinemático por remota (capa `REMOTE`, la forma de colisión de la moto) que va a su pose
  predicha con `MoveKinematic`: empuja con la velocidad justa. Quién se cae en un choque: ver "Choques
  entre motos" (de costado, de atrás o de arriba a más de 4 m/s; de frente, los dos, a más de 7).
- El piloto remoto: el mismo modelo (una copia por jugador), por IK o pegado a las partes del ragdoll que
  llegan (`RiderPartLocals` con `JointPose`, como lo armó su PC). La pose "de pie sobre la cola" sólo con
  la moto apoyada (banderas de ruedas en el suelo).

## Choques entre motos: quién se cae

Cada PC decide sólo por su piloto, con lo que ya recibe (el protocolo no cambió: sigue en 7). En
`Game::Step`, después de la física, con el contacto más fuerte del paso contra una remota
(`PhysicsWorld::LastBikeHit`: normal, punto y la velocidad propia en la normal), `JudgeRemoteHit`:

- **Cierre** = la velocidad propia menos la que **mandó** el otro (`Remote::PointVelocity`: su foto, sin
  las correcciones de la predicción; quieta si la foto pasó los 0.25 s y ya no se extrapola), en la normal.
- **Con la trompa** (el otro a menos de ~41° de la nariz, `kBikeHitFront` 0.75): te caés sólo de frente
  (su trompa también te apunta) o si el golpe lo trajo él (más del 45% del cierre), y a más de
  `crash_impact_speed` (7 m/s, el mismo que contra un muro). Si embestiste vos, te caés recién a 1.5 veces eso
  (`kBikeRamFactor`: 10.5 m/s, ~38 km/h de diferencia con la Motocross): la otra moto cede con el golpe y
  el que embiste se frena más o menos la mitad del cierre, así que aguanta más que contra un muro.
- **De costado, de atrás o de arriba**: te caés a más de 4 m/s (`kBikeHitFall`, ~15 km/h de diferencia),
  lo haya traído quien lo haya traído (en un roce fuerte de costado caen los dos).
- **Eco**: tras un golpe de más de 4 m/s con un jugador, los contactos con él del medio segundo siguiente
  no tiran a nadie (`kBikeHitEcho`).
- **De frente caen los dos**: si el otro se cae (bandera `kCrashed`) menos de 0.5 s después de un contacto
  trompa contra trompa con nosotros, caemos también.

Para que a uno lo tire un golpe fuerte, la versión nueva la necesita **el que recibe** (la de la v0.2.6
del otro no importa: manda lo mismo).

### Qué pasaba (hasta la v0.2.6)
"Mi amigo me impacta jugando online y no me caigo". La regla era: se acercaban a más de 6 m/s y el
otro traía más del 45%, las dos cosas medidas con la velocidad del cuerpo cinemático. Medido con la prueba
`netchoque` (abajo), **la detección andaba**: el que recibe siempre se lleva el golpe entero, también con
150-300 ms de ida y vuelta y jitter, con ventana, y con el paquete v0.2.6 como el que recibe (se cayó a 40
km/h de costado y de atrás). Lo que fallaba:

1. **Umbral alto**: 6 m/s en la normal son 22 km/h de diferencia. De costado a 15-20 km/h (4.5-5.9 m/s) o de
   atrás con 15-20 km/h más (4.1-5.9) no tiraba a nadie.
2. **"Lo trajo el otro" medido contra el suelo**: con las dos motos yendo para el mismo lado (lo normal en
   carrera), la normal del contacto es el costado de una, inclinado, y la velocidad hacia adelante de la
   otra se proyecta en ella: cada PC se veía como la que trajo el golpe y **no se caía ninguno**, a 45° y
   25 km/h cada uno (5.6 m/s) y a 45° y 40 km/h (7.6-7.9 m/s, en una de dos corridas).
3. **La velocidad del cuerpo cinemático lleva las correcciones**: una foto que llega atrasada (115 ms) lo
   hace correr para alcanzar la nueva. En un duelo de bots que se rozaban, un roce dio "el otro traía 20.8
   m/s" (iba a 14) y tiró al invitado. Y cerca del umbral, la misma prueba caía o no según la corrida
   (`atras-50-30`: 5.8 una vez, más de 6 otra).
4. **De frente dependía de quién lo veía primero**: la predicción (medio ping) adelanta un poco al otro, la
   PC que ve el golpe primero empuja su moto para atrás y a la otra le llega retrocediendo (cierre 0.6
   m/s): se caía uno solo (`frente-10`, `frente-30`, a veces). Con velocidades distintas (30 contra 15
   km/h), el rápido no llegaba al 45% y no caía.

### Por qué 4 m/s
Roces medidos: duelo de bots en la largada 2.8-2.95 m/s (3.2 con 40 ms de demora y 40 de jitter);
cerrarse 15° a 30-40 km/h 2.0-3.2; de costado a 10 km/h 3.2 (3.6 con la de carreras). Golpes que tienen
que tirar: de costado a 15 km/h 4.5-4.9; de atrás con 15 km/h más 4.1-4.8. El umbral queda 1 m/s arriba
del roce más fuerte medido. Con la trompa, el del muro: una moto que viene de frente es un muro que viene
(de frente a 8+8 km/h, 5.5 m/s, no cae nadie; a 12+12, 7.4, caen los dos).

### Cómo se comprobó
`python tools/netchoque.py compilaciones/build-msvc-red/motocross.exe compilaciones/build-msvc-red/motocross.exe caso...` (motocross,
LAN de una PC, ~15 ms de ida y vuelta). La última columna es lo que decidía la regla vieja con los mismos
contactos (la telemetría imprime, entre corchetes, la medición vieja).

| caso | cierre m/s | recibe | embiste | v0.2.6 |
|---|---|---|---|---|
| `lado-10` (de costado a 10, el otro quieto) | 3.2 | no | no | no |
| `lado-15` / `lado-20` | 4.5 / 5.9 | **cae** | no | no / en el borde |
| `lado-40-30` (a 40 contra uno a 30) | 11.3 | **cae** | no | cae |
| `atras-10` / `atras-15` | 3.2 / 4.5 | no / **cae** | no | no |
| `atras-40-30` / `atras-45-30` (10 y 15 km/h más) | 2.7 / 4.1 | no / **cae** | no | no |
| `atras-60-30` | 8.1 | **cae** | no | cae |
| `frente-8` / `frente-12` (los dos a esa) | 5.5 / 7.4 | no / **cae** | no / **cae** | no / caían |
| `frente-30-15` | 13.1 | **cae** | **cae** | sólo el lento |
| `roce-40` (se cierra 15°) | 2.8-3.0 | no | no | no |
| `roce-50` / `roce-60` | 3.5-4.0 / 4.1-4.3 | en el borde / **cae** | no / **cae** | no |
| `ang45-25-25` | 5.3-5.5 | **cae** | **cae** | ninguno |
| `ang45-40-40` | 7.6-8.3 | **cae** | no (pegó con la trompa) | a veces ninguno |
| `ang30-50-50` | 7.0-7.3 | **cae** | **cae** | según la corrida |

- Igual con `--lag 40 --jitter 40` (150-300 ms de ida y vuelta), con el anfitrión con ventana
  (`--ventana`) y con la de carreras y la de trial (`--bike`).
- Tres duelos de bots de 70 s (`netduel`, uno con demora y jitter): ninguna caída, roce máximo 3.2 m/s.
- La motocross sola, idéntica a la v0.2.6 (`tools/regresion.sh`, las cinco pruebas), vueltas 1:08.37 /
  1:08.51; conecta con la v0.2.6 en los dos sentidos (`tools/red.py`, trial contra carreras).

### Trampas
- **Cada PC tiene su versión del choque.** Al que recibe le llega siempre el golpe entero: el cuerpo del
  otro está donde estaba hace L (la latencia) más su velocidad por la edad, así que llega a tocarte antes
  de que llegue la foto en que el otro ya chocó (en T + L − edad, antes de T + L). El que embiste, en
  cambio, a veces ve al otro ya alejándose (su PC lo empujó primero) y apenas lo toca: es normal, y por eso
  "de frente, los dos" necesita la regla de la bandera `kCrashed`.
- **Eco**: el golpe empuja a la moto propia; esa moto le llega al otro por la red y, en un choque en
  ángulo, le vuelve a pegar ~0.2 s después (a 45° y 25 km/h tiraba también al que había embestido).
- **El cuerpo cinemático no es el otro**: para medir, su foto (`PointVelocity`); para empujar, el cuerpo.
- En la prueba, el que embiste se ubica a partir de dónde ve al otro: contra una versión sin la prueba (que
  se queda quieta en la largada) también sirve (así se probó la v0.2.6 como la que recibe).

### El que embiste también se cae (si el golpe es fuerte)
- **Pasaba**: el que pegaba con la trompa nunca se caía por la regla, ni a 60 km/h. El usuario lo pidió.
- **Qué se hizo**: `kBikeRamFactor` 1.5 (ver arriba).
- **Se comprobó** (`tools/netchoque.py`, LAN): de costado a 30 km/h (8.6 m/s) se cae sólo el que recibe; a 45
  (12.7), los dos; de atrás 60 contra 20 (10.9) y 80 contra 30 (13.5), los dos; de frente a 12 cada uno, los
  dos; roce a 40, ninguno. `ang45-40-40` (8.4-8.7 m/s) tiró a los dos: el otro trae parte del cierre (más
  del 45%), así que no cuenta como "embestiste vos" y vale el umbral del muro.
- Los umbrales son constantes de `Game.cpp`; podrían ser claves de la moto (`.ini`).

## Probar en una sola PC

```
motocross.exe --headless --host --bot --bike base/trial --test netduel --telemetry --time 30   (en segundo plano)
# tomar el código de invitación de su salida (lo imprime enseguida)
motocross.exe --headless --join CÓDIGO --bot --bike base/carrera --test netduel --telemetry --time 25
```

O todo junto: `python tools/red.py <exe anfitrión> <moto> <exe cliente> <moto>` (sirve para probar dos
versiones entre sí: el paquete nuevo contra el anterior). **Trampa**: si lo armás con un script, la
salida del anfitrión va a un archivo; con un pipe que nadie lee mientras corre el cliente, se llena, el
anfitrión se traba en el `printf` y el cliente ve crecer la "edad" de sus datos (parece un bug de red).

La telemetría de cada uno imprime a los demás con `moto=id(estilo)`, distancia, ping y corrección.
`netcrash` prueba el ragdoll remoto (con ventana). Sin ventana, en red, la simulación va a tiempo real.

Choques: `python tools/netchoque.py <exe anfitrión> <exe invitado> [--lag MS] [--jitter MS] [--bike m]
[--ventana] [--telemetria] caso...` con casos `TIPO-KMH[-KMH]` (`lado`, `atras`, `frente`, `roce`, `angN`;
el primer número es el que embiste, el segundo el que recibe): la prueba `--test netchoque-...` con
`--flat`, el anfitrión recibe y el invitado embiste, y a los 5 s cada uno imprime `NETCHOQUE ... SE CAYO`
o `no se cayo` con el choque más fuerte que midió. Cada línea `CHOQUE` dice el cierre, qué traía el otro,
de dónde vino (de costado, de atrás, de frente, con la trompa), el umbral y, entre corchetes, lo que medía
el cuerpo cinemático. `--net-lag MS` / `--net-jitter MS` (en el juego) demoran lo que llega, como por
internet: ida y vuelta ~2×MS.

## En pantalla

- **El código para invitar se ve sólo con el menú abierto** (Esc), en la tarjeta "AHORA" del anfitrión con "F10 lo copia"; F10 lo
  copia corriendo y con el menú abierto (a pedido del usuario, octubre de 2026). En el HUD, arriba a la derecha debajo del
  minimapa, quedan "EN RED · anfitrión / conectado / conectando" y los pilotos con su color y su ping; en el minimapa, una flecha
  del color de cada uno. Detalle y capturas en MENU.md ("Minimapa, línea ideal con frenada...").
- "Reiniciar carrera" (menú) y Retroceso reinician sólo la moto propia.

## Trampas

- Todo lo que se genera en el mapa tiene que salir igual en todas las PCs (la favela usa semilla fija). Lo que se mueve
  solo (el tráfico de la autopista) también, con un reloj compartido: sin `sin`/`cos`/`pow` de la biblioteca, sin nada que
  dependa de la moto propia en la parte compartida y con los órdenes desempatados (un `std::sort` con claves iguales no
  ordena igual en MSVC y en clang).
- Los objetos sueltos los simula cada PC por su cuenta (no viajan).
- Wifi de invitados o routers con aislamiento de clientes no dejan que las PCs se vean; el firewall de
  Windows pregunta la primera vez (red privada y pública).
