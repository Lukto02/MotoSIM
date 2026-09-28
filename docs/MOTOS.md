# Motos: diseño y tuning

Cómo se arma una moto nueva sin romper las que hay, qué aprendimos en cada una y qué falta. Ver también
`MODDING.md` (formato de los archivos) y [FISICA.md](FISICA.md) (por qué la física es como es).

## Qué es una moto

Una moto son dos cosas separadas a propósito:

- **La física**: sólo números. `tuning.ini` (la base, que es la Motocross 450) más el `.ini` de la moto,
  que se lee después y pisa sólo sus claves. Ninguna pieza del dibujo cambia cómo anda.
- **El estilo** (`"style"` en el `.json`): qué piezas se dibujan, los colores de cada jugador, cómo va el
  piloto (manubrio, estriberas, cadera, hombros, rodillas) y dónde está la boca del escape (la llamarada
  de los petardeos). No toca la física.

```
mods/<mod>/bikes/<id>.json   nombre, style, description, tuning
mods/<mod>/bikes/<id>.ini    las claves de tuning.ini que cambian
```

**Regla dura: la Motocross 450 no cambia.** Toda moto nueva se hace con su `.ini` y su estilo; si hace
falta tocar el código común (BikePhysics, Engine, Bike), el camino de siempre tiene que dar exactamente
lo mismo (ver "Regresiones" en [PRUEBAS.md](PRUEBAS.md)).

## Las motos que hay

Fichas sacadas con `motocross.exe --headless --test bikestats` (las mismas cuentas que el menú; se
recalculan con F5):

| Moto | Estilo | hp | kg | km/h | 0-100 | g lat. | susp. | giro | Notas |
|---|---|---|---|---|---|---|---|---|---|
| `base/motocross` Motocross 450 | `mx` | 47 | 105 | 133 | 4.5 s | 0.97 | 310 mm | 1.8 m | la referencia: `tuning.ini`, más `motocross.ini` (la frenada con la S y la suspensión más viva al aterrizar) |
| `base/carrera` Carrera 1000 | `race` | 245 | 160 | 329 | 3.7 s* | 1.33 | 125 mm | 2.8 m | 4 cilindros, rodilla al piso |
| `base/trilheira` Trilheira 450 | `trail` | 64 | 120 | 121 | 4.3 s | 1.02 | 310 mm | 1.5 m | grau (ver README, Morro do Grau), escape aberto |
| `base/trial` Trial 300 | `trial` | 28 | 70 | 91 | - ** | 1.02 | 220 mm | 1.2 m | piloto de pie, 48° de manubrio |
| `sandbox/dostiempos` Dos tiempos 250 | `mx2t` | 46 | 100 | 140 | 4.5 s | 0.97 | 310 mm | 1.8 m | la de ejemplo del mod; motor que gira arriba |

\* La ficha limita el 0 a 100 a 0.8 g (wheelie); en la pista la de carreras hace 2.1-2.6 s (ver pendientes).
\** No llega a 100 (91 km/h al corte en 5ª). Hasta la v0.2.7 la ficha decía 5.8 s: la cuenta seguía empujando en 5ª
pasado el corte.

Medido andando (no la ficha), cada una contra una moto real del mismo tipo: ver "Las motos contra las de verdad".

Colores del jugador 1 (el que se ve solo): motocross roja 21, carreras roja, blanca y negra 27,
trilheira verde y amarilla 22, trial roja 3, dos tiempos amarilla con asiento azul 92. **Elegir el esquema 0 de una moto nueva distinto de los que
ya hay**: si no, en el selector parecen la misma.

## Estilos

`src/BikeStyles.h` (el enum, `Count` al final) y `src/BikeStyleDef.h` (la definición). Cada estilo es
una `BikeStyleDef`:

| Campo | Qué es |
|---|---|
| `name` | lo que va en `"style"` del `.json` |
| `genParts` | piezas iguales para todos los colores (basculante, amortiguador, horquilla de abajo, comando, ruedas, cubiertas: `BikeMesh::Id`) |
| `genLivery` | piezas pintadas de un esquema: `out[0]` guardabarros delantero, `out[1]` barras de la horquilla (van con la tija) |
| `genBody` | el chasis de un esquema para una pipa de dirección dada; se rearma si el tuning cambia la geometría (`UpdateBikeBody`) |
| `livery` | tabla de `kLiveries` (4) esquemas, uno por jugador: plástico, franja 1, franja 2, número |
| `barOffset`, `gripX` | centro del manubrio respecto de la pipa y medio ancho entre puños |
| `peg` | estribera izquierda (la derecha es espejo en X) |
| `hips`, `shoulders`, `knee` | pose neutra del piloto (sentado o de pie, según la moto) |
| `leanHipsZ`, `leanShouldersZ` | cuánto van cadera y hombros adelante/atrás con el piloto tirado |
| `stand` | 0..1 de pie siempre. **Ver trampa abajo: para una moto de pie, poner la pose de pie en hips/shoulders/knee y dejar `stand = 0`** |
| `tuck` | 0..1 agachado detrás del parabrisas a velocidad (de 8 a 25 m/s) |
| `hangOff` | 0..1 colgado en las curvas: de 12° a 45° de inclinación la cadera sale ~14 cm hacia adentro, el torso y el casco se meten y la rodilla de adentro baja casi al piso (la de carreras: 1; las demás 0, que deja la pose idéntica) |
| `exhaustTip`, `exhaustDir`, `openExhaust` | boca del escape (llamarada) y si petardea mucho |

Para agregar uno: sumarlo al enum antes de `Count`, escribir su `BikeStyleDef` (en `BikeStyles.cpp` o en
su sección de `BikeMeshes.cpp`, como `race` y `trial`), agregar el `case` en `GetBikeStyle` y su nombre
legible en `Game::StyleLabel` (lo que se lee en la lista de motos). `Maps.cpp` valida el nombre solo.
Los arreglos del renderer van por estilo (`[BikeStyle::Count]`): **después de tocar el enum, recompilar
todo** (ver [PRUEBAS.md](PRUEBAS.md): ninja no sigue bien los headers en esta PC).

Estilos que hay: `mx` (motocross), `trail` (trilheira), `race` (pista: carenado y piloto agachado),
`trial` (de pie, tanque chico, sin asiento de verdad) y `mx2t` (la de motocross con motor dos tiempos).

## Piezas (BikeMeshes.cpp)

- **Espacio de la moto**: +X izquierda, +Y arriba, +Z adelante, origen en el centro de masa. **El lado
  derecho es -X**: ahí van el escape y el embrague.
- **Herramientas** (arriba del archivo): `Parts` con `Rod` (caño recto), `Tube` (caño curvo por puntos),
  `RBox` (caja redondeada: el tamaño es el total, no la mitad), `DiscX` (disco en el plano YZ) y `Add`;
  `Sweep` (perfil barrido por un camino: `profile`, `scale(t)`, `color(t, j)`, `edges`, `transport`,
  `caps`) para tanques, asientos, escapes y vigas; `Panel` (placa con contorno en el plano (z, y) y
  espesor variable) con `Basis` para llevarla a un costado; `Digits` para los números.
- **Números**: se leen derechos de los dos lados (girados, no espejados; ver el lateral de la motocross).
- **Compartir un generador entre dos estilos** (como `mx` y `mx2t` en `BuildMxBody`): el camino del
  estilo viejo tiene que quedar idéntico; todo lo nuevo, dentro de `if (twoStroke)`.
- **Colores que se pierden**: una pieza del mismo gris que el chasis de aluminio no se ve (pasó con el
  caño de expansión de la 2T: se hizo de acero pavonado oscuro y el silenciador de carbono).
- **Discos y plataformas grandes**: el cilindro común tiene 16 caras; a más de ~1 m de radio se ve
  octogonal. Para eso está `Renderer::Disc` (72 caras).

## Pose del piloto

`Bike::RiderPoseLocal` arma la pose con la del estilo, el cuerpo (adelante/atrás/costados), el tuck y
la pata afuera. El modelo glTF la sigue por IK (ver [PILOTO.md](PILOTO.md)).

- **Trampa de `stand = 1`** (trial): lleva al piloto a una pose fija pensada para la moto casi vertical
  sobre la cola (wheelie pasado). Con la moto derecha queda hecho un bollo sobre el tanque. Para una moto
  que se anda de pie, la pose de pie va en `hips`/`shoulders`/`knee` y `stand` queda en 0 (la mezcla con
  la pose de wheelie vertical sigue funcionando sola).
- **Constantes globales del modelo**: `kForwardLean` y `kForwardHipsBack` (RiderModel.cpp) hacen que el
  modelo se agache sobre el manubrio al tirarse adelante; son iguales para todos los estilos.

## Tuning

Todas las claves están comentadas en `tuning.ini`, por grupos: masa, centro de masa, geometría,
suspensión, neumáticos, frenos, dirección y balance, aire, motor y caja. Las que más cambian una moto:

- **Carácter del motor**: `engine_torque_scale` (fuerza), `engine_rpm_scale` (estira la curva de torque
  en rpm: 1.5 = pico 50% más arriba), `engine_rev_limit`, `engine_clutch_rpm`, `engine_inertia`,
  `engine_brake`, `engine_final_drive` y `engine_gear1..5`. `engine_cylinders` y `engine_two_stroke`
  sólo cambian el sonido (ver [SONIDO.md](SONIDO.md)).
- **Caja automática**: sube a 9400 rpm y baja a 4800 (no baja si quedaría arriba de 8500), todo
  multiplicado por `engine_rpm_scale`. Con una curva estirada o encogida hay que tocar esa clave, no el
  corte, para que cambie donde corresponde.
- **Cubiertas por superficie**: `*_tire_loose_grip` (tierra y pasto) y `*_tire_paved_grip` (asfalto,
  cemento, objetos). Una lisa de carreras: 0.35 en la tierra.
- **Frenada**: `brake_align` (frenando fuerte, derecho o doblando, la moto apunta hacia donde va; derecho,
  además, la delantera sigue su camino; para motos que descargan mucho la trasera), `rear_lift_load` y
  `rear_lift_mitigation` (anti-levantamiento de la cola del ABS: más bajos, frena más cerca del stoppie).
  `brake_stand_up` (0.6 en `tuning.ini`) es cuánto se endereza frenando inclinada: para una moto de calle o
  de pista, bajo (la de carreras 0.2); si no, frenando no dobla. `brake_transition_release`: afloja la
  delantera mientras la moto cambia mucho de inclinación (para motos que descargan mucho la trasera).
  `brake_yaw_comp`: frenando inclinada, cancela el giro hacia afuera del freno en el contacto (si no, se abre).
  Probar las frenadas como con teclado (viniendo inclinada y soltando, tocando la dirección), no sólo derecho:
  ver `frenacurva` en [PRUEBAS.md](PRUEBAS.md).
- **Derrape lento**: `slide_pivot` (freno trasero doblando debajo de ~13 km/h: el piloto empuja la cola).
- **Golpes**: `crash_impact_speed` y `crash_impact_vertical` (m/s contra algo fijo para salir despedido).
- **Wheelie**: `wheelie_turn`/`wheelie_align` (doblando en una rueda la moto gira a la par de su
  trayectoria; ver [FISICA.md](FISICA.md)) y el grau (`grau_*`, sólo la Trilheira).
- **Peso**: `mass` incluye los 75 kg del piloto (`Bike::kRiderMass`); `inertia_pitch/yaw/roll`.
- **Geometría**: `front_mount_*`, `rake_deg`, `rear_mount_*` (distancia entre ejes y horquilla).
- **Suspensión**: `*_travel`, `*_spring`, `*_damping`, `*_rebound_ratio`, `*_progressivity`,
  `*_bottoming_damping`, `*_max_damper_velocity` (tope del hidráulico: los golpes secos no patean).
- **Cubiertas**: `*_tire_long_grip`, `*_tire_lat_grip`, `*_tire_stiffness`, `*_tire_damping`,
  `*_tire_unsprung_mass`, `max_lateral_accel`.
- **Dirección**: `max_steer_deg`, `max_steer_high_speed_deg`, `steer_rate`, `min_turn_radius`,
  `balance_k_*`, `balance_d_*`, `lean_steer_speed_*`.

### La ficha del menú

`Game::ComputeStats` (Game.cpp) la saca de la física:

- **Potencia**: el máximo de torque × rpm de la curva, con `engine_torque_scale`.
- **Peso**: `mass` menos el piloto.
- **Velocidad máxima**: la menor entre la de la 5ª al corte y la que deja el aire con esa potencia.
- **0 a 100**: una cuenta en recta con cambios al 97% del corte, con el empuje limitado a
  `min(0.8, 0.9·rear_tire_long_grip)·m·g` (agarre y wheelie).
- **Agarre en curva**: `max_lateral_accel / g`.
- **Suspensión**: la suma de los recorridos.
- **Giro**: `min_turn_radius`.

Las barras del menú son relativas a la mejor moto en cada número.

## Cómo hacer una moto nueva (lista)

1. `.json` con `name`, `style`, `description` y `tuning`; `.ini` con sólo lo que cambia. El `.ini`
   comentado en castellano: qué se busca con cada grupo de números.
2. Estilo: reusar uno si alcanza; si no, uno nuevo (sección propia en `BikeMeshes.cpp`, esquema 0 con
   colores que no tenga otra moto).
3. Mirarla: capturas de costado y de 3/4 con y sin piloto (comandos en [PRUEBAS.md](PRUEBAS.md)),
   de los dos lados (el escape está a la derecha).
4. Andarla: `--test bikestats`; bot en la pista de motocross, la favela, el parque y el valle (vueltas y
   caídas); las pruebas de su especialidad (`accel`, `wheelie`, `brakeslide`, `circle*`, `grau*`...).
5. Comprobar que la Motocross 450 da idéntico (regresiones) y anotar en este archivo la ficha, los
   números que costaron y las trampas.

## Lo que aprendimos en cada moto

### Trilheira 450 (trail)
El grau y los giros cerrados son sólo suyos (`trilheira.ini`). El detalle está en el README ("Freestyle
de favela"): relación corta (`engine_final_drive` 7.8), piloto bien atrás (`rider_shift` 0.30), embrague
que patina a 5500 rpm, `grau_*` para sostener el punto de equilibrio con gas y freno trasero, y
`grau_turn`/`grau_align` para que la trasera apunte hacia donde va doblando en una rueda.
- Frenada con la S (después de la v0.2.7): `brake_align` 4 / 2000 como las demás de tierra, pero sólo frenando muy fuerte
  (`brake_align_from` 0.55, `brake_align_full` 0.95) y sin soltar la delantera (`brake_align_free` 0); con los valores
  de la motocross el bot de la favela se caía el doble. Ver [FISICA.md](FISICA.md).
- La suspensión queda con la extensión de `tuning.ini` (3.0): más viva (1.7-2.0) rebota en la loma de la bajada de
  la favela (s≈241) y el bot se cae el doble o el triple en la curva de abajo.

### Trial 300 (trial)
- Motor: `engine_rpm_scale 0.75` + `engine_torque_scale 0.675` = ~28 hp con el pico de torque a 5250 rpm
  (lleno abajo). Relación cortísima: 1ª hasta ~33 km/h, 5ª ~90.
- Suspensión blanda y progresiva con `*_max_damper_velocity 4.5`: un canto o un tronco no patean.
- Cubiertas muy blandas de carcasa (60-70 kN/m): el canto de un escalón se hunde en la goma en vez de
  tirar la moto. Trasera con 1.40 de agarre longitudinal.
- Medido: 5 vueltas sin caerse al circuito de obstáculos (`pruebas/mods/prueba/maps/trial_obstaculos`,
  ~37.5 s; la motocross se cae 5-11 veces ahí) y sube escalones de 0.3 m andando. **El de 0.5 m, sólo
  levantando la rueda** (`--map prueba/escalon --spawn 14..20 --test grau`, desde `pruebas/`). Antes
  "subía" también a fondo sin levantarla: el rayo delantero, inclinado, veía la tapa del cajón y la
  apoyaba en un piso que todavía no tenía debajo. Desde que la cubierta apoya en los cantos (ver
  [FISICA.md](FISICA.md), escaleras) ya no: con 35 cm de radio, un escalón de 50 cm queda arriba del
  eje, como con una moto de verdad. **0.75 y 1 m no se pueden** (ver pendientes).
- El corte está en 9500 rpm (no 9000): antes la caja subía a 9400 fijos. Ahora los umbrales se escalan
  con `engine_rpm_scale` (0.75: sube a ~7050) y las vueltas dan igual (~37.4 s).
- La ficha decía 0-100 en 5.8 s, pero al corte en 5ª llega a 91 km/h: la cuenta de `Game::ComputeStats` seguía
  empujando pasado el corte. Ahora en 5ª al corte no empuja y el menú muestra "-" (`bikestats`: 99.0).

### Carrera 1000 (race)
Lo hizo un agente con su propia copia; su informe tiene todos los números (motor de 245 hp a 15300 rpm,
corte a 16500, 330 km/h, frenos, geometría). Lo que más importó:
- `cornering_stiffness` 14 → 45: con 14, acostada a más de 150 km/h casi no doblaba. El giro sale de la
  deriva de las cubiertas y, acostada, el contacto queda ~0.4 m al costado del centro de masa: el
  freno motor y los frenos la retorcían más que la dirección (60° a la izquierda, doblando a la derecha,
  50 m afuera de la pista).
- `caster_align` / `caster_deadzone_deg`: la zona muerta de la motocross (3°) se disparaba en curvas
  normales de 1.3 g y la abría; con 6° la curva andaba pero frenando quedaba cruzada 19°. Lo que anduvo:
  ganancia 2.5 (el manubrio realinea la moto de verdad) con zona muerta de 5.5°.
- `balance_k_high` / `balance_d_high` / `balance_max_torque` 6000 / 1600 / 6000: con los de la motocross
  el rolido se pasaba 15° a 250 km/h. **Los torques de amortiguación tienen que quedar por debajo de
  ~I/dt**: un `stoppie_yaw_damping` de 30000 explotó numéricamente.
- `front_brake_torque`: 1400 hacía un stoppie instantáneo a 330 km/h y después 90° de guiñada; 820
  todavía levantaba la trasera; quedó en 620. `rear_brake_torque` 250-300 trababa la trasera descargada
  (la S frena también la de atrás al 50%); 60 era poco para el bot. Quedó en 120.
- `max_lateral_accel` limita la inclinación pedida a atan(a/g): con 13, 53°. `max_lean_deg` 62 es margen.
- La pata afuera de la motocross se apaga con `leg_out_speed_*` = 200.
- **Lo que casi no cambió nada**: más `rear_tire_long_grip` (1.75), `tc_slip` 0.12 → 0.06 y 1ª/2ª más
  largas (además empeoraron el 0-100).
- Cómo se midió: una copia con telemetría extra y pruebas de arrancada en recta, frenadas a fondo desde
  velocidades fijas (como la tecla S), círculos a 15, 25 y 35 m/s y vueltas del bot; cada cambio, por
  todo el juego de pruebas.
- Piezas: las ruedas y cubiertas se escalan por radio / radio nominal (se arman a tamaño real y se
  escalan kR/R); los índices de las mallas son de 16 bits; todos los estilos comparten el pivote del
  basculante y los anclajes del amortiguador; el hueco de "guardabarros delantero" no sigue a la
  suspensión (el de carenado lo usa para el repartidor de freno y el guardabarros va en la horquilla).
- Las cubiertas lisas se dibujan con el radio de la banda igual al `roundness` de la física: el contacto
  en pantalla coincide con el de la física a cualquier inclinación.
- Después: lisas que patinan en la tierra (`*_tire_loose_grip` 0.35), frenada derecha a cualquier
  velocidad (`brake_align` 4) y sin derrape lento (`slide_pivot` 0). Y más tarde, porque frenaba poco y no
  doblaba frenando: `brake_stand_up` 0.2, `front_brake_torque` 720, `rear_lift_load` 300 y
  `rear_lift_mitigation` 0.25 (1-1.16 g en recta; S + D dobla), `brake_transition_release` 0.6 (soltando la
  curva para frenar se iba para el otro lado), y `brake_yaw_comp` 1 con `brake_align` 8 / `brake_align_torque`
  2000 (frenando inclinada sin soltar, se abría). Ver [FISICA.md](FISICA.md).
- En el aire (después de la v0.2.7): giraba igual que la motocross (un mortal atrás entero, whips de 31°). Con `air_pitch_rate`
  1.2, `air_pitch_torque` 250 y `air_body_tilt_deg` 15 el mortal se queda en ~160°, frenar un giro tarda 0.6 s y el
  whip es de 13°. El bot del autódromo no vuela: mismas vueltas.

### Dos tiempos 250 (mx2t)
- Física: `dostiempos.ini` (liviana, menos torque abajo, corte a 11500, relación corta, casi sin freno
  motor). El estilo sólo cambia el dibujo.
- Motor (después de la v0.2.7): con la curva de la 450 y 0.8 de torque era una 450 floja, no una 2T. `engine_rpm_scale` 1.15 y
  `engine_torque_scale` 0.72 la estiran en vueltas: 17% menos abajo, 24% más arriba, ~46 hp, cambia a ~10 800 rpm.
- Qué hace que se lea como dos tiempos: el **caño de expansión** (sale del frente del cilindro, baja por
  delante del motor, la panza y el cono que sube por el costado hasta un silenciador corto y fino), la
  **tapa de cilindro chata** con la bujía arriba en vez de la tapa de válvulas alta, el carburador atrás
  del cilindro, y colores de los 90 con el **asiento del color de la franja** (amarilla con azul).
- Sonido de dos tiempos (`engine_two_stroke = 1`): ver [SONIDO.md](SONIDO.md).

## Las motos contra las de verdad

Revisión de la física de las cinco contra una moto real genérica de su tipo (después de la v0.2.7). Los valores reales
son rangos conocidos de fichas de fábrica y pruebas de revistas, de memoria (no se buscó en internet): sirven de
orden de magnitud. El juego es arcade: un número lejos de lo real está bien si lo pide la jugabilidad; lo que se
corrigió fue lo que se sentía raro, lo que estaba lejos sin razón o lo que no distinguía a una moto de otra.

**Cómo se midió** (sin ventana, desde `pruebas/`; `plaza_tierra` y `plaza_asfalto_ancha` son llanas y de 400 m de
ancho, ver [PRUEBAS.md](PRUEBAS.md)):
- arrancada: `--test arranquea` (parado, W en rampa a 1 s, piloto adelante, caja automática); final: `--test
  frenada999` en `plaza_asfalto_ancha` (1.5 km derechos);
- frenada con la S: `frenada60` / `frenada100` en las dos plazas, y los casos de teclado (`frenacurva...`, ver [FISICA.md](FISICA.md));
- curvas: `circle12` / `circle20` / `circle30` (dirección a fondo sostenida), con la guiñada del mundo = `wy` /
  cos(inclinación);
- giro mínimo: `circle2`; sag: quieta 3 s en `--flat`; aterrizajes: `--flat --drop H` (1, 2.5 y 5 m);
- wheelie: `wheelie` (1ª a fondo tirado atrás) y `launch` (caja automática, piloto neutro: cabeceo en cada marcha);
- aire: `--drop 1 --test flip`, `--drop 3 --test airrot` (tiempo para frenar un giro de nariz de 2 rad/s), `whip`;
- derrapes: `brakeslide` (32 km/h) y `brakeslide4` (14 km/h) con el trasero, `slide` con gas;
- freno motor: `frenacurva80x0n` y `frenacurva40x0n` (llega y suelta todo), restando el aire y la rodadura.

### Motocross 450 (contra una CRF450R, KX450, YZ450F o 450 SX-F)

| Qué | Real | En el juego | |
|---|---|---|---|
| Potencia | 53-60 hp de fábrica (~50 en la rueda) | 47 hp | algo baja |
| Peso (sin piloto) | 102-112 kg | 105 kg | ok |
| Peso/potencia | 1.9-2.1 kg/hp | 2.2 kg/hp | ok |
| 0-50 / 0-100 | ~3.5-4.5 s a 100 (en tierra lo limitan el agarre y el wheelie) | 1.8 / 4.4 s | ok |
| Final | 140-150 km/h con la relación de fábrica | 132 km/h | algo baja |
| Frenada (S) | 0.6-0.8 g en tierra dura, la trasera liviana pero apoyada | 0.68-0.71 g **en stoppie a -21°, con la cola en el aire el 79-93% de la frenada** | ver pendientes |
| Lateral e inclinación | 0.6-0.8 g en tierra plana, 35-45° (más en los peraltes) | 0.87 g a 44 km/h y 0.80 a 72, 47° | generosa, arcade |
| Giro mínimo | ~2 m (42° de manubrio, 1.48 m entre ejes) | 1.8 m | ok |
| Suspensión y sag | 305-310 / 310-315 mm; sag de carrera ~100-105 mm atrás (33%) | 300 / 320 mm; sag 87 / 118 mm (29% / 37%) | ok |
| Wheelie | fácil en 1ª y 2ª; en 3ª con embrague o el cuerpo atrás | piloto neutro: 18° en 1ª y 2ª, 5-7° en 3ª a 5ª (limitador); tirado atrás en 1ª, 42° | ok |
| Aire | muy manejable: whips y scrubs, mortales | mortal atrás de 325° en 2.5 s (2.5 rad/s), frena un giro en 0.41 s, whip de 26° | ok |
| Caídas y aterrizajes | de 2-3 m al plano hace tope | de 1 m 90% adelante; de 2.5 m tope y vuelve en 0.24 s; de 5 m tope sin caerse; a plomo a más de 11 m/s (~6 m) el piloto sale despedido | ok |
| Derrapes | con el trasero y con gas, todo el tiempo | trasero: 32° a 32 km/h, 39° a 14 km/h; con gas ~6° (control de tracción) | ok |
| Freno motor | fuerte (monocilíndrico grande) | 0.07 g del motor de 80 a 48 km/h (0.16 en total), 0.10 de 40 a 24 (0.15) | ok |
| Caja | 5 marchas: 1ª ~60-70, 5ª ~145 km/h | 5 marchas: 52 / 71 / 89 / 108 / 133 km/h (cambia a 9400) | 1ª corta, ok |

No se tocó (a pedido: se siente bien). Lo único claramente raro es la frenada en stoppie: ver pendientes.

### Dos tiempos 250 (contra una YZ250, KTM 250 SX o TC 250)

| Qué | Real | En el juego | |
|---|---|---|---|
| Potencia | 45-50 hp de fábrica, pico a ~8500-9500 rpm | 46 hp (antes 44), pico de potencia a ~10300 | **ajustado** |
| Peso | 95-103 kg | 100 kg | ok |
| Peso/potencia | 2.0-2.2 kg/hp | 2.2 kg/hp | ok |
| 0-50 / 0-100 | como una 450, apenas más lenta | 1.7 / 4.5 s (antes 4.6) | ok |
| Final | 135-145 km/h | 139 km/h | ok |
| Frenada, lateral, giro, suspensión | como la 450 | como la motocross (0.67-0.72 g en stoppie; 0.88 g; 1.8 m; sag 31% / 38%, resortes más blandos) | ok |
| Wheelie | levanta de golpe cuando "entra en la pipa" | 18° en 1ª y 2ª con el piloto neutro, 5-6° después | ok |
| Aire y aterrizajes | la más fácil de revolear (liviana) | como la motocross (mortal de 328°, whip de 28°; de 2.5 m vuelve en 0.27 s) | ok |
| Derrapes | como la 450 | 31° / 39° con el trasero, 7° con gas | ok |
| Freno motor | casi nada: la gran diferencia con una 4T | 0.03 g del motor de 80 a 48 (un tercio de la 450), 0.05 de 40 a 24 (la mitad) | ok |
| Caja | 5 marchas, 1ª ~50-55 km/h, se estira hasta ~11 000 rpm | 55 / 75 / 94 / 113 / 140 km/h, cambia a ~10 800 (antes a 9400: 1ª hasta 52) | **ajustado** |

**Se ajustó el motor**: tenía la misma curva que la 450 con 20% menos de torque, o sea una 450 floja, no una 2T que
"se despierta arriba" (como dice su descripción). Con `engine_rpm_scale` 1.15 y `engine_torque_scale` 0.72 tiene un
17% menos de torque a 4000 rpm, lo mismo en el medio y un 24% más a 11 000; la caja automática cambia a ~10 800.
Medido: 0-100 4.6 → 4.5 s, el bot del parque 0:50.9 → 0:51.1 sin caídas.

### Trilheira 450 (contra una trail de calle brasileña, XRE 300 / Lander 250 / XR 250, y una enduro 450)

| Qué | Real | En el juego | |
|---|---|---|---|
| Potencia | trail de calle 21-26 hp; enduro 450 (CRF450X, 450 EXC) 45-55 hp | 64 hp | muy arriba, se deja |
| Peso | 135-150 kg / 110-125 kg | 120 kg | ok (enduro) |
| Peso/potencia | ~6 / 2.2-2.5 kg/hp | 1.9 kg/hp | la más brava de tierra |
| 0-50 / 0-100 | 8-10 s a 100 / ~4 s | 1.6 / 3.7 s en asfalto (la delantera apenas despegada el 36% del tiempo), 2.2 / 4.2 en tierra | rápida |
| Final | 130-140 / 150 km/h | 121 km/h (relación corta del grau) | se deja |
| Frenada (S) | 0.8-0.9 g en asfalto con ABS | 0.69-0.75 g en stoppie, como la motocross; **ya no hace trompo** | **ajustado** |
| Lateral | con mixtas en asfalto ~0.8-0.9 g | 0.91 g a 44 km/h, 0.81 a 72, 49° | ok |
| Giro mínimo | ~2.2-2.5 m | 1.55 m (45° de manubrio: zerinho, vueltas en U) | arcade, se deja |
| Suspensión y sag | trail 220-245 mm, blanda; enduro ~300 | 300 / 320 mm, sag 87 / 115 mm (29% / 36%) | ok |
| Wheelie | el grau | sin limitador; el grau se sostiene 8 s a 8-20 km/h; neutra, 8-10° en todas las marchas | es la gracia |
| Aire | más pesada, peor | como la motocross (mortal de 320°) | se deja |
| Aterrizajes | suspensión de trail, menos controlada | de 1 y 2.5 m vuelve en 0.6 s (extensión amortiguada 3 veces: "muerta"); de 5 m, 0.22 s | se deja (ver abajo) |
| Derrapes | de calle: la cola sale | 43° a 32 km/h, 47° a 14 km/h (cavalo de pau hasta 85°), con gas 5° | ok |
| Freno motor | monocilíndrico: como la 450 | 0.08 g del motor de 80 a 48, 0.10 de 40 a 24 | ok |
| Caja | 5-6 marchas, más largas | 50 / 68 / 86 / 103 / 121 km/h | corta a propósito |

**La potencia** (64 hp, +35% de torque que la motocross) se deja: el grau necesita fuerza en 1ª a 8-20 km/h y el
usuario pidió más en primera ("En primera le falta potencia", ver [FISICA.md](FISICA.md)). **La frenada**: con la S
a fondo desde 60-90 km/h la cola se cruzaba 47-151° (un trompo); ahora 0-29° en todos los casos de teclado. Detalle
y por qué el alineado va sólo frenando muy fuerte: [FISICA.md](FISICA.md), "La Trilheira: frenando fuerte hacía un
trompo". **La suspensión** queda "muerta" (3.0): con 1.7 o 2.0 rebota en la loma de la bajada de la favela y el bot
se cae el doble o el triple.

### Trial 300 (contra una TXT 300, Cota 301RR, TRRS One o ST 300)

| Qué | Real | En el juego | |
|---|---|---|---|
| Potencia | 20-30 hp | 28 hp | ok |
| Peso | 66-75 kg en seco | 70 kg | ok |
| Peso/potencia | 2.5-3 kg/hp | 2.5 kg/hp | ok |
| 0-50 / 0-100 | no importa | 1.9 s; no llega a 100 (la ficha decía 5.8 s: **corregida**, ahora "-") | ok |
| Final | 80-100 km/h en 6ª | 91 km/h | ok |
| Frenada | frenos fortísimos, corta y alta: el stoppie ("nose wheelie") es técnica | 0.64-0.68 g en stoppie | ok |
| Lateral | no importa | 0.86 / 0.78 g, 47° | ok |
| Giro mínimo | ~1.3-1.5 m | 1.2 m (48°) | ok |
| Suspensión y sag | 170-180 / 165-175 mm | 230 / 210 mm, sag 67 / 80 mm (29% / 38%) | +30%, por los cajones |
| Wheelie | levanta la delantera cuando quiere | 17° en 1ª y 2ª con el piloto neutro, 42° tirado atrás; sube el escalón de 0.5 m levantando la rueda | ok |
| Aire | no es para saltar, pero es liviana | como la motocross (mortal de 336°) | ok |
| Caídas y aterrizajes | cae de 2-3 m sobre la trasera | de 1 m casi hace tope adelante (98%) y vuelve en 0.19 s | ok |
| Derrapes | agarra todo | 29° / 36° con el trasero; con gas casi no patina (1.40 de agarre atrás) | ok |
| Freno motor | 2T con relación cortísima | 0.05 g del motor de 80 a 48, 0.07 de 40 a 24 | ok |
| Caja | 6 marchas: 1ª ~10-15 km/h, 6ª ~85-95 | 5 marchas: 24 / 32 / 43 / 55 / 91 km/h (cambia a 7050) | 1ª larga: el embrague patina hasta 3000 rpm |

No se tocó su física (sólo la ficha). La suspensión larga se deja: con la real (175 mm) las bajadas de un metro del
circuito de obstáculos harían tope.

### Carrera 1000 (contra una R1, ZX-10R, S1000RR o Panigale V4)

| Qué | Real | En el juego | |
|---|---|---|---|
| Potencia | de calle 200-215 hp; Superbike 230-240 | 245 hp | es la de carrera |
| Peso | 170-180 kg en seco (190-205 lleno); Superbike 168 mínimo | 160 kg | liviana, ok |
| Peso/potencia | ~0.85 / ~0.7 kg/hp | 0.65 kg/hp | ok |
| 0-100 / 0-200 | 2.6-3.1 s (limita el wheelie) / ~5.2-6 s | 2.1 s (la delantera apenas despegada el 71% del tiempo) / 4.75 s | rápida, arcade |
| Final | 299 limitada, ~310 libre; 320-330 en Superbike | 328 km/h | ok |
| Frenada | 1.1-1.3 g (limita el stoppie) | 0.96 g de 60, 1.01 de 100, 1.08 de 200, derecha, sin levantar la cola | ok |
| Lateral e inclinación | 1.3-1.5 g con lisas, 55-60° (MotoGP hasta 64°) | 1.25-1.30 g de 45 a 110 km/h, 52° | ok |
| Giro mínimo | 3.2-3.6 m (26-30° de manubrio) | 2.8 m (28°) | ok |
| Suspensión y sag | 120 / 120-130 mm, sag 30-35 mm | 120 / 130 mm, sag 36 / 38 mm | ok |
| Wheelie | antiwheelie | tirado atrás en 1ª, 1° | ok |
| Aire | pesada y corta: no es para saltar | mortal de 159° (se cae), frena un giro en 0.61 s, whip de 13° (antes 333°, 0.38 s y 31°: como la motocross) | **ajustado** |
| Caídas y aterrizajes | de un metro hace tope | de 1 m hace tope y las ruedas despegan 0.14 s | ok |
| Derrapes | entra cruzada con el embrague antirrebote | en tierra con lisas se cruza (con gas 160°); en asfalto el control de tracción no la deja | ok |
| Freno motor | controlado por la electrónica; en 1ª a 40 km/h se siente (~0.1 g) | 0.04 g del motor de 80 a 48, ~0 de 40 a 28 (1ª hasta 153 km/h) | bajo, se deja |
| Caja | 6 marchas, 1ª ~150-170 km/h | 5: 153 / 188 / 230 / 282 / 329 km/h | ok |

**Se ajustó el aire**: giraba y hacía whips igual que la motocross (hasta un mortal atrás completo); una de pista
de 235 kg con el piloto casi no se puede girar. `air_pitch_rate` 1.2, `air_pitch_torque` 250 y `air_body_tilt_deg`
15 (en `carrera.ini`). El bot del autódromo no vuela (0.2 s en 300 s, al aparecer): 2:11.65 / 2:09.83 sin caídas,
igual que antes.

### Lecciones de la revisión
- **Medir con la moto apoyada**: en `frenadaN` y `accel` la moto aparece en el aire con el gas a fondo; la rueda se
  pasa de vueltas y al tocar el piso la caja automática sube a 2ª a 4-5 km/h (la 2T y la Trilheira "arrancaban en
  2ª"). La arrancada se mide con `arranquea`, que espera 1 s.
- **`wy` de la telemetría es la guiñada en el eje de la moto**: acostada 47°, la del mundo es `wy / cos(47°)`. Sin
  eso, la motocross parecía doblar a 0.59 g con 47° de inclinación.
- **Los círculos rápidos se salen de las plazas de 120 m** (un radio de 30 m ya no entra al costado de la largada) y
  con lisas en el pasto la de carreras "no doblaba". Para eso están las de 400 m.
- **La `recta` de la de carreras no sirve para la final**: la largada está en la curva del óvalo y, sin tocar la
  dirección, la moto sale de la calle de 16 m al pasto. En `plaza_asfalto_ancha` llega a 328 km/h.
- **El `whip` de la Trial "se da vuelta"** (179°, caída): `--drop` la deja en 1ª a 50 km/h (su 1ª llega a 31 al corte), el
  freno motor pasado de vueltas frena la rueda en el aire y la reacción baja la trompa 40°. Es la prueba, no la moto.

## Pendientes conocidos

- **El 0 a 100 de la ficha** limita el empuje a 0.8 g para todas; debería salir de la geometría (centro
  de masa respecto de la rueda trasera): la de carreras aguanta más.
- **El control de tracción no mira la inclinación**: a fondo acostada la trasera se va (el bot se abre
  hasta 20 m a la salida de algunas curvas). Un corte que dependa de la inclinación lo arreglaría.
- **Con la S a fondo las de tierra frenan en stoppie**: la motocross, la 2T, la Trilheira y la trial van toda la
  frenada con la trompa a -20°/-21° y la cola en el aire el 77-93% del tiempo, a 0.64-0.75 g. El freno de adelante
  (950 Nm en una rueda de 0.35 m) da ~1.5 g, más que el límite de stoppie (~0.9 g con el centro de masa a 0.83 m), y
  lo que la sostiene es el anti-endo del ABS. Frenan *menos* que si no levantaran la cola, y la cola en el aire es lo
  que las hacía inestables (el trompo que arregla `brake_align`). Para la trial es técnica (nose wheelie); para las
  otras no. **Propuesta (no aplicada: la motocross no se toca sin pedido)**: en `motocross.ini` (y lo mismo en
  `dostiempos.ini`) `rear_lift_load = 700` y `rear_lift_mitigation = 0.6`. Medido en una copia: 0% de stoppie,
  0.81-0.85 g derecho (antes 0.68-0.72), la cola ≤17° en todos los casos de teclado, y los bots sin caídas
  (motocross 1:08.55 / 1:08.36, el valle 0:41.1-0:41.2, Los Médanos 1:26.7-1:26.9). En la Trilheira no sirve: en la
  bajada de la favela afloja el freno (la trasera va liviana por la pendiente) y el bot se cae más.
- **La curva de s≈253 de la favela**: el bot llega a ~43 km/h después de la loma de s≈241 (con la delantera sola en
  el piso, sin poder doblar) y pega contra la pared de afuera a ~28 km/h en casi todas las vueltas, con cualquier moto
  rápida (la motocross y la 2T también se caen ahí; la trial, que llega más despacio, no). Que cuente como caída
  depende de si el golpe pasa de 7 m/s. Es del bot o del mapa (ver [MAPAS.md](MAPAS.md)); mientras tanto, las caídas
  de la Trilheira en la favela se comparan con varias muestras largas (ver [PRUEBAS.md](PRUEBAS.md)).
- **La Trilheira es más potente que cualquier trail real** (64 hp contra 21-26 de una XRE 300 o 45-55 de una enduro
  450) y su final corta (121 km/h): se dejó por el grau. Si alguna vez se baja, que no pierda fuerza en 1ª.
- **El freno motor de la Carrera casi no existe abajo** (~0 a 40 km/h: su 1ª llega a 153): `engine_brake` es lineal
  con las rpm y vale 10 Nm al corte. Una de verdad en 1ª a 40 km/h frena ~0.1 g.
- **Piloto agachado**: los codos van a una distancia fija y los antebrazos se ven cortos.
- **Carenado y semimanubrios con rake fijo** (24°, `kRaceRake`): con otra geometría no coinciden.
- **Escalones verticales**: la rueda es un solo rayo por el eje de la suspensión; una pared vertical es
  pared o no existe hasta que el eje pasa el borde, y la caja fija del chasis se engancha. Para trial de
  verdad: contacto con forma barrida, caja del chasis por moto (`Bike::BuildShapes`) o un salto (hop).
- **Pose de pie por estilo**: `standHips`/`standShoulders`/`standKnee` para que `stand = 1` sirva.
- **Piezas pintadas de adelante con rake fijo**: `genLivery` no recibe la geometría; la máscara de la
  trial asume 23°. Pasarle el eje de la horquilla o rearmarlas como el chasis.
- **Sonido de dos tiempos**: `engine_cylinders` cuenta explosiones de un cuatro tiempos (una cada dos
  vueltas). Una 2T explota en cada vuelta: `engine_cylinders = 2` en su `.ini` lo imitaría (no está
  puesto: cambia el sonido de la moto de ejemplo).
