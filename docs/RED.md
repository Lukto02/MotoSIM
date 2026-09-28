# Multijugador (red local)

`Net.cpp` (socket UDP, direcciones, código de invitación) y `Multiplayer.cpp` (anfitrión/cliente,
paquetes, motos remotas). El funcionamiento visto desde el jugador está en el README ("Multijugador LAN").

## Protocolo

- Cada paquete lleva `MX` y el número de protocolo (`kProtocol` en `Multiplayer.cpp`). Distinto = "otra
  versión". **Sube cada vez que cambia el formato de un paquete** (y la versión del juego la elige el
  usuario). Historia: 3 (v0.3.1), 4 (v0.2.1-v0.2.2, mapa), 5 (v0.2.3, cuerpo de costado), 6 (v0.2.4,
  mapa por nombre `mod/archivo`), 7 (v0.2.5, la moto de cada uno).
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
  predicha con `MoveKinematic`: empuja con la velocidad justa. Choques: si se acercaban a más de 6 m/s y
  lo trajo el otro, te caés; de frente fuerte, los dos.
- El piloto remoto: el mismo modelo (una copia por jugador), por IK o pegado a las partes del ragdoll que
  llegan (`RiderPartLocals` con `JointPose`, como lo armó su PC). La pose "de pie sobre la cola" sólo con
  la moto apoyada (banderas de ruedas en el suelo).

## Probar en una sola PC

```
motocross.exe --headless --host --bot --bike base/trial --test netduel --telemetry --time 30   (en segundo plano)
# tomar el código de invitación de su salida (lo imprime enseguida)
motocross.exe --headless --join CÓDIGO --bot --bike base/carrera --test netduel --telemetry --time 25
```

La telemetría de cada uno imprime a los demás con `moto=id(estilo)`, distancia, ping y corrección.
`netcrash` prueba el ragdoll remoto (con ventana). Sin ventana, en red, la simulación va a tiempo real.

## Trampas

- Todo lo que se genera en el mapa tiene que salir igual en todas las PCs (la favela usa semilla fija).
- Los objetos sueltos los simula cada PC por su cuenta (no viajan).
- Wifi de invitados o routers con aislamiento de clientes no dejan que las PCs se vean; el firewall de
  Windows pregunta la primera vez (red privada y pública).
