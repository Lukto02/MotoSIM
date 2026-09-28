# Multijugador (red local)

`Net.cpp` (socket UDP, direcciones, código de invitación) y `Multiplayer.cpp` (anfitrión/cliente,
paquetes, motos remotas). El funcionamiento visto desde el jugador está en el README ("Multijugador LAN").

## Protocolo

- Cada paquete lleva `MX` y el número de protocolo (`kProtocol` en `Multiplayer.cpp`). Distinto = "otra
  versión". **Sube cada vez que cambia el formato de un paquete** (y la versión del juego la elige el
  usuario). Historia: 3 (v0.3.1), 4 (v0.2.1-v0.2.2, mapa), 5 (v0.2.3, cuerpo de costado), 6 (v0.2.4,
  mapa por nombre `mod/archivo`), 7 (v0.2.5, la moto de cada uno; la v0.2.6 sigue en 7 y juega con la
  v0.2.5: probado con anfitrión de una y cliente de la otra, en los dos sentidos, con motos distintas; la
  v0.2.7 también sigue en 7 y juega con las dos: la regla nueva de los choques la usa cada uno en su PC).
- HELLO (cliente → anfitrión, a la IP del código y por broadcast) → WELCOME con el número de jugador.
  El HELLO lleva la moto del jugador; si cambia de moto conectado, se vuelve a mandar.
- Estado de cada moto 60 veces por segundo: posición, rotación, velocidades, suspensión, ruedas,
  dirección, piloto, motor, petardeos, banderas (`kRiderOn`, `kCrashed`, `kLimiter`, `kShifting`,
  `kFrontGround`, `kRearGround`), por rueda dónde apoya, carga y patinaje (huellas y polvo) y, caído, las
  11 partes del ragdoll. El anfitrión lo reenvía a los demás.
- PING/PONG cada 0.5 s; ROSTER (lista de jugadores, con la moto de cada uno y el mapa) cada 1 s; 5 s sin
  noticias = desconectado. Nada necesita entrega confiable: todo se repite o se refresca.

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
`python tools/netchoque.py build-msvc-red/motocross.exe build-msvc-red/motocross.exe caso...` (motocross,
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

## Trampas

- Todo lo que se genera en el mapa tiene que salir igual en todas las PCs (la favela usa semilla fija).
- Los objetos sueltos los simula cada PC por su cuenta (no viajan).
- Wifi de invitados o routers con aislamiento de clientes no dejan que las PCs se vean; el firewall de
  Windows pregunta la primera vez (red privada y pública).
