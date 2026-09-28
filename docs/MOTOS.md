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
| `base/motocross` Motocross 450 | `mx` | 47 | 105 | 133 | 4.5 s | 0.97 | 310 mm | 1.8 m | la referencia: `tuning.ini` tal cual |
| `base/carrera` Carrera 1000 | `race` | 245 | 160 | 329 | 3.7 s* | 1.33 | 125 mm | 2.8 m | 4 cilindros, rodilla al piso |
| `base/trilheira` Trilheira 450 | `trail` | 64 | 120 | 121 | 4.3 s | 1.02 | 310 mm | 1.5 m | grau (ver README, Morro do Grau), escape aberto |
| `base/trial` Trial 300 | `trial` | 28 | 70 | 91 | 5.8 s | 1.02 | 220 mm | 1.2 m | piloto de pie, 48° de manubrio |
| `sandbox/dostiempos` Dos tiempos 250 | `mx2t` | 44 | 100 | 140 | 4.6 s | 0.97 | 310 mm | 1.8 m | la de ejemplo del mod |

\* La ficha limita el 0 a 100 a 0.8 g (wheelie); en la pista la de carreras hace 2.3-2.6 s (ver pendientes).

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
- **Frenada**: `brake_align` (frenando fuerte y derecho la delantera sigue su camino y la moto vuelve a
  apuntar hacia donde va; para motos que descargan mucho la trasera), `rear_lift_load` y
  `rear_lift_mitigation` (anti-levantamiento de la cola del ABS).
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
  velocidad (`brake_align` 4, `rear_lift_load` 450, `rear_lift_mitigation` 0.35) y sin derrape lento
  (`slide_pivot` 0). Ver [FISICA.md](FISICA.md).

### Dos tiempos 250 (mx2t)
- Física: `dostiempos.ini` (liviana, menos torque abajo, corte a 11500, relación corta, casi sin freno
  motor). El estilo sólo cambia el dibujo.
- Qué hace que se lea como dos tiempos: el **caño de expansión** (sale del frente del cilindro, baja por
  delante del motor, la panza y el cono que sube por el costado hasta un silenciador corto y fino), la
  **tapa de cilindro chata** con la bujía arriba en vez de la tapa de válvulas alta, el carburador atrás
  del cilindro, y colores de los 90 con el **asiento del color de la franja** (amarilla con azul).
- Sonido de dos tiempos (`engine_two_stroke = 1`): ver [SONIDO.md](SONIDO.md).

## Pendientes conocidos

- **El 0 a 100 de la ficha** limita el empuje a 0.8 g para todas; debería salir de la geometría (centro
  de masa respecto de la rueda trasera): la de carreras aguanta más.
- **El control de tracción no mira la inclinación**: a fondo acostada la trasera se va (el bot se abre
  hasta 20 m a la salida de algunas curvas). Un corte que dependa de la inclinación lo arreglaría.
- **La motocross frenando a fondo con la S** de 90-120 km/h todavía gira: la de carreras se arregló con
  `brake_align` (la delantera libre, ver [FISICA.md](FISICA.md)); en la motocross está en 0 para no
  cambiar su física.
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
