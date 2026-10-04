# Física: problemas resueltos y reglas

Cada entrada: qué pasaba, por qué, qué se hizo y cómo se comprobó. El funcionamiento general está en el
README ("Cómo funciona la física"); esto es la bitácora de lo que costó. El piloto y el ragdoll tienen
su propio documento: [PILOTO.md](PILOTO.md).

## Reglas

- **La física de la Motocross 450 no cambia.** Antes de dar por bueno un cambio en código común, la
  telemetría de las pruebas de siempre tiene que salir idéntica a la del último paquete (ver
  [PRUEBAS.md](PRUEBAS.md)). Lo que es de una sola moto va en su `.ini` o detrás de una clave que en
  `tuning.ini` vale lo de antes.
- **Idéntico quiere decir bit a bit.** Cambiar el orden de una suma de floats ya cambia los resultados
  (ver "Orden de las sumas").
- **Todo número nuevo, con un valor por defecto que reproduce lo de antes**, registrado en
  `BikeParams::Register` (Bike.cpp) y comentado en `tuning.ini`.

## Bitácora

### Freno de mano: doblando y clavando el trasero, la cola sale (a pedido; las de tierra, no la de carreras)
- **Pedido**: "el espacio o A en joystick para el freno de mano debería ser el freno trasero [...] quiero que cuando
  doblás y lo clavás la moto deslice un poco más de la cola". Espacio / A ya eran el freno trasero solo (`Game.cpp`).
  Después: "estos cambios que no afecten a la moto de carreras".
- **Cómo se midió**: `tools/derrape.py` (nuevo; desde `pruebas/`, `prueba/plaza_tierra`, con las rampas del teclado):
  acelera a 30/50/70 km/h, dobla 1 s con la D y clava el trasero sin soltar la D hasta 5 km/h (`clava`), o 1.2 s y sale
  con gas doblando (`clava_sale`), o soltando la D (`clava_suelta`), o derecho (`recto`).
- **Qué pasaba** (v0.3.6):
  - **450 y 600** (`BikePhysics031.cpp`): la trasera se trababa enseguida y la cola quedaba a 7-10° (mediana) casi
    hasta parar, con la moto acostada 52-59° yendo con la delantera sola (el camino giraba 26-51°). Debajo de ~20 km/h
    se cruzaba de golpe 54-75°. La 600 soltando a 70 km/h con gas se caía;
  - **2T, Trilheira, Trial** (`BikePhysics.cpp`): a 50-70 km/h la cola a 10-31° (mediana) y al final hasta 35-57°;
    a 30 km/h, 6-12°.
- **Por qué**:
  - la trasera trabada no sostiene nada de costado (en la 031 la elipse de agarre se la come: uso 150-200, fuerza
    lateral ~10 N), pero **tampoco empuja la cola**: nada la saca. La delantera agarra y el contravolante automático
    (`caster_align` 2, tope 30°) la mantiene alineada. Cuando el tope ya no alcanza (despacio), trompo;
  - el derrape de la física nueva (`slideIntent`: el piloto dosifica el pedal, sostiene el manubrio, `slide_pivot`) es
    para despacio: se apaga entre `slide_speed_max` − 4 y `slide_speed_max` (9-13 m/s). La 031 no tiene nada de eso.
- **Qué se hizo**: claves nuevas (`Bike::UpdateHandbrake` y `HandbrakeSteer`, en `BikePhysics.cpp`; las llaman las dos
  físicas, guardadas con `if`: con `handbrake_yaw` 0, el de `tuning.ini`, no se calcula nada):
  - **`handbrake_yaw`** (Nm por rad): con el trasero a más del 60-95% (el bot frena atrás hasta 0.6: no lo usa), sin el
    de adelante y doblando, un PD de guiñada lleva la cola a **`handbrake_angle`** / **`handbrake_angle_fast`** (24° hasta
    ~30 km/h, 14° desde ~70, con la dirección a fondo; con media dirección ~60%) y la frena si se pasa (no hay trompo);
  - la delantera sigue su camino (contravolante automático apagado) apuntada `slide_steer_in` (6°) hacia la curva: empuja
    hacia adentro y **cierra la curva** sin frenar de costado. En la 031, además, la moto va más derecha (`slide_upright`);
  - al soltar el freno o la D, durante **`handbrake_catch`** (0.6 s) el mismo PD ataja la cola hacia 0;
  - en la física nueva, debajo de `slide_speed_max` (donde manda el derrape lento de siempre) sólo saca la cola: no la
    frena ni toca el manubrio, así el cavalo de pau de la Trilheira (55-60° a ~10 km/h) sigue igual;
  - `handbrake_yaw` 5000 en `motocross.ini`, `motocross600.ini`, `dostiempos.ini`, `trilheira.ini` y `trial.ini`.
    La de carreras, nada (idéntica).
- **Probado y descartado** (en la 450):
  - el PD solo, con menos contravolante y el manubrio de siempre: la cola salía 16-28°, pero la delantera quedaba cruzada
    respecto de su camino, saturada (uso 5-9), y frenaba de costado: 0.75-0.85 g (el doble). Al soltar con gas, con la cola
    a ~28°, trompo de 140°: la trasera patinaba con el gas y el contravolante, topado en 30°, no alcanzaba (de ahí
    `handbrake_catch`);
  - la delantera siguiendo el camino de su eje con toda la velocidad angular: inclinada ~55°, el rolido mueve el eje de
    costado y el manubrio oscilaba ±40° a 5 Hz; la moto se caía hacia adentro. Se usa sólo la guiñada;
  - la delantera justo en su camino (sin los 6° hacia adentro): el camino se **abría** 12-32°. La trasera trabada frena a
    lo largo de la moto, que apunta hacia adentro: empuja para afuera;
  - un término con la velocidad de la cola en el manubrio (como el del derrape lento): oscilaba;
  - sin la excepción del derrape lento: la Trilheira a ~9 km/h pasaba de 58° a 26°.
- **Se comprobó** (`python ../tools/derrape.py <exe> --bike ...`; cola = mediana / máxima con más de 10 km/h; camino =
  cuánto giró la dirección de marcha hasta 5 km/h):

  | `clava` a 30 / 50 / 70 km/h | antes: cola | antes: camino, g | después: cola | después: camino, g |
  |---|---|---|---|---|
  | Motocross 450 | 9/54, 9/75, 7/73 | +33° 0.53, +26° 0.43, +34° 0.41 | 25/28, 28/30, 28/30 | +47° 0.48, +82° 0.41, +100° 0.41 |
  | Motocross 600 | 10/70, 7/74, 3/70 | +27° 0.50, +35° 0.41, +51° 0.42 | 27/28, 28/30, 26/30 | +63° 0.42, +100° 0.40, +116° 0.41 |
  | 2T | 6/22, 27/44, 10/44 | +8° 0.33, +52° 0.37, +57° 0.37 | 25/34, 30/46, 27/50 | +12° 0.35, +63° 0.39, +99° 0.44 |
  | Trilheira | 12/28, 31/50, 23/57 | +18° 0.35, +64° 0.43, +81° 0.44 | 24/30, 37/53, 29/60 | +7° 0.35, +61° 0.45, +91° 0.48 |
  | Trial | 8/16, 19/35, 11/37 | +13° 0.39, +36° 0.45, +42° 0.46 | 13/28, 28/42, 24/43 | +49° 0.53, +41° 0.47, +73° 0.52 |

  - por velocidad (450): ~29° entre 10 y 45 km/h, ~21° arriba de 45. Las máximas de 40-60° de la 2T, la Trilheira y la
    Trial son del final, debajo de ~30 km/h: el derrape lento de siempre (`slide_angle_max` 65 / 85);
  - `clava_sale` (1.2 s y sale con gas): la 450, cola 21-25° y camino +23/+34/+45° (antes 4-9° y +9/+12/+24°); sale
    derecha en todas, también la 600 a 70 km/h (antes se caía). Sin caídas en ninguna maniobra;
  - exagerando (cambiar de la D a la A clavando, desde 90 km/h hasta parar, toques cortos, en tierra y asfalto): cola
    máxima 19-34°, sin trompos ni caídas;
  - `recto` y `clava_suelta`: iguales (derecho no se cruza más);
  - la de carreras, idéntica (`brakeslide*` y `teclas:` en `plaza_asfalto_ancha`; el bot del circuito);
  - regresión contra la v0.3.6, idéntica: las 9 de siempre menos `brakeslide` (es el freno de mano: con
    `handbrake_yaw` 0 da idéntica, igual que `brakeslide2.5/3.5/b` en las 6 motos), más `cuerpo`, `wheelie`, `accel`,
    `frenada60`, `frenacurva60x1d-1a1`, `brakestraight`, `circle12`, `whip`, `flip` y `airrot` de las 6 motos, y los bots
    (motocross 450 y 600, 1:04.26 / 1:04.10; favela 300 s; circuito; parque; Los Médanos; valle).
- **Lecciones**:
  - **Una rueda trabada no saca la cola**: deja de sostenerla, pero si nada la empuja la moto sigue con la delantera, y
    el contravolante automático la mantiene derecha. Un "freno de mano" necesita que el piloto ponga la guiñada.
  - **La delantera cruzada frena**: si la moto gira y la delantera no sigue su camino, va saturada de costado y la moto
    se clava. Que siga su camino con unos grados hacia la curva da el giro sin la frenada.
  - **El manubrio que sigue un camino, sólo con la guiñada**: con la moto acostada el rolido mueve el eje de costado y
    realimenta el manubrio (oscilación). Con `-0.3·rate` de la cola, lo mismo.
  - **Atajar al soltar**: con la cola a ~30° y gas, la trasera patina y no agarra; el contravolante tiene tope de 30°.
  - **El bot frena atrás hasta 0.6**: un umbral de pedal arriba de eso (0.6-0.95) deja los bots bit a bit iguales.

### La de carreras: rework de la frenada (a pedido; sólo la Carrera)
- **Pedido**: "la moto de carreras frena poco, a veces cuando frenas queda doblando para un costado, etc. quizás tiene poco
  peso o algo así, pero necesita un rework para comportarse bien". Juega con teclado. Después: "agregá telemetría y probá en
  el juego todos los comportamientos: ir rápido, doblar y clavar frenos, clavar frenos recto, clavar frenos y luego doblar".
- **Cómo se midió** (sin ventana, `prueba/plaza_asfalto_ancha`): prueba nueva `teclas:GUION`, que maneja con las mismas
  rampas del teclado, y `tools/maniobras.py`, que corre diez maniobras a 100/150/200/250 km/h y resume cada frenada (línea
  `FRENADA`); `frenacurvaN...` con un resumen de "soltar la curva" (cuánto sigue girando el camino 0.5/1/2/3 s después, si
  queda inclinada para el otro lado) en un barrido de 90 a 250 km/h con la D al 70, 85 y 100% (82 casos).
- **Qué pasaba** (antes = v0.3.4):
  - **derecho** frenaba 1.0-1.07 g: 100 → 0 en 39.8 m, 200 → 0 en 140 m. Una deportiva de verdad: 1.0-1.2 g (35-40 m);
  - **doblar, soltar la D y clavar** (lo más común con teclado): la moto se iba **para el lado contrario** de la curva: el
    camino giraba 5-12° para afuera y terminaba 5-13 m corrida. Clavando 0.3 s después de soltar, casi nada;
  - **soltar una curva a fondo sin frenar**, a 150 y 180 km/h (2 de 42 casos): quedaba ~7 s derrapando con la cola afuera
    (deriva 7-8°, la trasera al 350% de su agarre), **inclinada 13° para el otro lado y doblando 75° para el lado de la
    curva a 1.7 g**. A 120, 140, 160 o 200, se enderezaba en 0.65 s;
  - con el teclado también se frena menos cuando la moto cambia de inclinación (`brake_transition_release` 0.6): clavar y
    doblar desde 100 km/h frenaba 0.91 g (43 m).
- **Por qué**:
  - **para el otro lado**: el balance cambia la inclinación con un torque en el centro de masa, así que la moto rola
    alrededor del centro de masa y no de la línea de los contactos, como una de verdad. Al enderezarse rápido (50° → 10° en
    0.3 s) los contactos barren hacia afuera a ~0.65 m/s y las cubiertas, rígidas (`cornering_stiffness` 45), empujan para el
    otro lado (~900 N la delantera: `--wheel-log`, deriva -1° con el manubrio apuntando a la curva). Frenando, con la trasera
    casi sin carga, ese empuje gira la moto y el camino. En una moto de verdad es al revés: para enderezarse el piloto dobla
    más y el camino se cierra un poco antes de seguir derecho;
  - **el derrape sin frenar**: al enderezarse la moto rebota (sube el centro de masa y descarga las ruedas) y frena por el
    aire y el freno motor (~0.3 g a 150 km/h): la trasera, que llevaba ~1.5 de 1.67 de agarre de costado, se satura primero
    y la cola sale. Con las dos cubiertas saturadas empujando para el mismo lado la curva se sostiene sola (como un auto en
    drift neutro) y el balance, que pide 0° porque se soltó la D, la sostiene inclinada para el otro lado (13°: donde su
    torque iguala al de las cubiertas). Es un estado estable: no sale hasta que bajó 60 km/h;
  - **frena poco**: el límite era el stoppie (centro de masa a 0.62 m, 0.68 m detrás del contacto de adelante: ~1.1 g) y el
    freno de adelante (720 Nm en 0.30 m: ~1.04 g con 235 kg).
- **Qué se hizo**:
  - clave nueva **`lean_sweep_comp`** (BikePhysics.cpp; 0 en `tuning.ini` = como antes, guardada con `if`): mientras la moto
    se endereza (rolido y velocidad de rolido de signos opuestos), al contacto de cada cubierta se le resta la velocidad que
    le da el rolido alrededor del centro de masa (`(ω·fwd) fwd × (contacto − centro de masa)`): las cubiertas no ven el
    barrido. Tirándose a la curva no actúa: ese empuje hacia adentro ayuda a doblar (con la clave también al tirarse, clavar
    y doblar desde 150 dobla 97° en vez de 174°);
  - `carrera.ini`: `lean_sweep_comp` 1, `com_height` -0.06 (centro de masa efectivo a 0.56 m: stoppie a ~1.2 g),
    `front_brake_torque` 720 → 820, `rear_tire_lat_grip` 1.55 → 1.65 (la trasera con margen sobre la delantera, 1.60),
    `inertia_yaw` 52 → 60 y `brake_transition_release` 0.6 → 0.3;
  - **no se tocó el peso** (160 kg + piloto): en esta física las cubiertas empujan en proporción a la carga, así que con
    más masa (y las inercias, los resortes y los frenos escalados) la moto hace lo mismo pero acelera menos (probado con
    270 kg: 0-100 en 2.33 s contra 2.08, y frena y dobla igual). Lo que cambia el comportamiento son las proporciones: el
    centro de masa, la inercia de guiñada, el agarre de cada cubierta y los torques fijos de las ayudas. Tampoco la
    distancia entre ejes (1.42 m, la de una 1000; una 600 tiene 1.37-1.40): las piezas de `BikeMeshes.cpp` están armadas
    para esa geometría. Las inercias (cabeceo 62, guiñada 60, rolido 22 kg m²) están en el rango de una deportiva con el
    piloto agachado.
- **Probado y descartado**:
  - `yaw_align` (que `brake_align` actúe sin frenar si la deriva pasa de 3°): baja la deriva del derrape a 3.7° pero la moto
    sigue doblando 73° inclinada para el otro lado (lo que la sostiene es el agarre, no la deriva). Se sacó del código;
  - menos balance (`balance_k_high` 4000-4500): saca el derrape pero se tira a la curva más lento (0.35 / 0.72 s) y,
    soltando para frenar a 200, vuelve a irse 2-3° para afuera;
  - `rear_tire_lat_grip` 1.75: sin derrapes y con menos cola al tirarse (2-6° en vez de 2-8°), pero el bot de la favela se cae más en
    la curva de s≈252 (17-18 caídas en 30 min contra 7-12): con más agarre atrás la moto gira menos y pega en la pared. Con
    1.65 y sin la inercia de guiñada, 1 derrape de 42; con las dos, ninguno en 82;
  - sin `brake_yaw_comp` (con todo lo nuevo): doblando y clavando sin soltar se abre (de +163° a +22° y -12°): sigue haciendo
    falta. `brake_align` 4 / 1500 (los de antes): la cola sale 7-8°. `brake_transition_release` 0: frena más pero clavar y
    doblar dobla menos (88° contra 120° desde 100);
  - `lean_surface_grip` 1 encima de todo: el parque mejora (14 vueltas en 15 min contra 4) y la favela empeora (25-28
    caídas en 30 min contra 9-18, en las calles de tierra de s≈1060-1090), igual que antes. Sigue sin activar.
- **Se comprobó** (`python tools/maniobras.py <exe> --vel 100,150,200,250` y el barrido de soltar la curva):

  | | antes | después | deportiva real (600-1000) |
  |---|---|---|---|
  | derecho 100 → 0 | 39.8 m, 1.01 g | 35.9 m, 1.12 g | 35-40 m, 1.0-1.2 g (limita el stoppie) |
  | derecho 150 / 200 / 250 → 0 | 83 / 140 / 205 m (1.05-1.12 g) | 75 / 128 / 187 m (1.16-1.23 g) | 1.1-1.2 g (con el aire, más arriba) |
  | doblar, soltar y clavar (100/150/200/250): camino después de soltar | -12 / -8 / -7 / -5° (para afuera), corrida 5-12 m | +9 / +3 / 0 / -1° (para el lado de la curva), 1-4 m | sigue un poco para el lado de la curva y endereza |
  | lo mismo con gas | -8 / -6 / -5 / -4°, 8-13 m | +2 / 0 / -1 / -2°, 0.6-5 m | |
  | g de esas frenadas | 0.92-1.11 | 1.05-1.24 | |
  | clavar y después doblar (100 / 150 / 200) | 0.91 / 0.96 / 1.02 g, dobla 106 / 150 / 163° | 1.11 / 1.11 / 1.15 g, dobla 109 / 148 / 165° | se puede doblar frenando (menos que sin frenar) |
  | doblando, clavar sin soltar la D | 1.03-1.17 g, dobla 147-182° | 1.14-1.29 g, dobla 152-179° | |
  | tocar la D frenando | -1 a -2°, 0.99-1.11 g | 0 a +5°, 1.11-1.23 g | |
  | soltar la curva sin frenar: se queda derrapando | 2 de 42 casos (75° y 13° para el otro lado) | 0 de 82 | se endereza en ~0.5-1 s |
  | deriva máxima frenando | 3.3-5.3° | 2.7-5.0° | |
  | 0-100 / 0-200 (`arranquea`) | 2.08 / 4.65 s | 2.08 / 4.48 s | 2.6-3.1 / 5.2-6 s |
  | giro a fondo 40-100 km/h (`giroNk`) | 1.26-1.30 g, 51-52°, entra en 0.67 s | igual | 1.2-1.4 g, 50-58° |

  - el bot del circuito: 2:11.41 / 2:09.73 / 2:09.73 / 2:09.76 y 0 caídas en 600 s (antes 2:11.65 / 2:09.83 / 2:09.84 /
    2:09.88, 0 caídas);
  - la favela, 30 min con la masa corrida ±0.01-0.02 kg (5 muestras de cada una): 9, 9, 11, 12 y 18 caídas (media 11.8)
    contra 7, 8, 11, 11 y 12 (media 9.8), casi todas en la pared de s≈252 (ver "La Trilheira: frenando fuerte hacía un
    trompo") y en s≈984, como antes: dentro del ruido;
  - el parque, 15 min: 4 y 13 vueltas con 1-2 caídas; antes se trababa (0 y 5 vueltas);
  - las otras motos, idénticas: 54 pruebas (`brakeslide`, `cuerpo`, `flip`, `whip`, `wheelie`, `accel`, `frenada60`,
    `frenacurva60x1d-1a1`, `launch` de la 450, la 600, la trial, la Trilheira y la 2T; los bots de la pista de motocross,
    la favela, Los Médanos, el valle y el parque; `giro`) contra el exe de la misma fuente sin el cambio, más
    `tools/regresion.sh` con las de siempre. El bot de la 450: 1:08.21 / 1:08.01.
- **Lecciones**:
  - **Probar como juega el usuario**: las pruebas de frenada eran derechas o con la dirección ya puesta; con teclado se
    viene doblando, se suelta la D y se clava. `teclas:` maneja por las mismas rampas del teclado y `FRENADA` resume cada
    frenada (también jugando con ventana y `--telemetry`).
  - **El rolido de este modelo es alrededor del centro de masa**: cualquier cambio rápido de inclinación arrastra los
    contactos de costado y las cubiertas responden. Al tirarse a la curva ayuda (empuja para adentro); al enderezarse
    empuja para afuera, que es lo contrario de una moto de verdad. `lean_sweep_comp` lo saca sólo al enderezarse.
  - **Con el balance artificial la moto puede quedar en un estado imposible**: inclinada para un lado y doblando para el
    otro, sostenida por el torque del balance contra el de las cubiertas. Buscar estados así comparando la inclinación con
    hacia dónde dobla (o `roll` contra `fLat`/`rLat`), no sólo la deriva.
  - **Un derrape que aparece en 2 de 42 casos es un borde**: la trasera estaba a ~1.0 de uso. Medir con barridos finos de
    velocidad y dirección, y con margen (82 casos), no con una o dos pruebas.
  - **El peso no es lo que parece**: con las fuerzas de las cubiertas proporcionales a la carga, más masa sola no cambia
    cómo frena ni cómo dobla; sólo cambian las proporciones (centro de masa, inercias, agarre de cada rueda, torques fijos).
  - **El bot de la favela juzga mal el agarre trasero**: con más agarre atrás gira menos en la bajada de s≈241-253 y pega
    en la pared. Compararlo con 5 muestras de 30 min y mirar dónde se cae.

### Velocidad de giro: revisión contra motos reales (v0.3.4, a pedido; no cambia ninguna moto)
- **Pedido**: "fijate velocidad de giro, en todas las motos y sobre todas las superficies, revisá si está correcta con
  contrapartes reales".
- **Cómo se midió**: prueba nueva `giroN` (Game.cpp: llega a N km/h con la caja automática y el piloto neutro, la
  sostiene 1.5 s y dobla a la derecha con la dirección a fondo, de golpe como el stick o con `k` en rampa como la D) y
  `tools/giro.py` (promedio de los últimos 3 s de un giro de ~15 s, más cuánto tarda la inclinación en llegar al 63% y al
  90% de la final). Desde `pruebas/` (mapas en `pruebas/mods/prueba/maps`): `plaza_tierra` (tierra de pista, suelo 1.0),
  `plaza_pasto` nueva (pasto, 0.82; una franja de pista de 3 m para la largada), `plaza_calle` nueva (calle de tierra de
  la favela, 0.84-0.95) y `plaza_asfalto_ancha` (1.08). La arena de Los Médanos agarra como la pista (1.0, `SurfaceGrip`
  no la distingue). Seis motos x cuatro suelos x 8 a 100 km/h, stick y teclado (~200 corridas).
- **Resultado en tierra de pista, dirección a fondo** (R = radio, °/s = guiñada del mundo, `wy / cos roll`):

  | moto | 8 km/h | 20 km/h | 40 km/h | 70 km/h | 100 km/h | tirarse a 40 km/h (63% / 90%): stick; teclado |
  |---|---|---|---|---|---|---|
  | Motocross 450 | R 1.8 m, 71°/s | R 3.0 m, 107°/s, 1.07 g, 45° | R 14 m, 45°/s, 0.91 g, 48° | R 48 m, 23°/s, 0.80 g, 46° | R 98 m, 16°/s, 0.78 g, 47° | 0.39 / 0.71 s; 0.54 / 0.84 s |
  | Motocross 600 | R 1.8 m | R 3.1 m, 1.07 g | R 14 m, 0.91 g, 48° | R 50 m, 0.78 g, 46° | R 104 m, 0.74 g, 47° | 0.42 / 0.77 s; 0.57 / 0.90 s |
  | Dos tiempos | R 1.8 m | R 3.1 m, 1.07 g | R 15 m, 0.90 g, 48° | R 50 m, 0.78 g, 46° | R 99 m, 0.78 g, 47° | 0.39 / 0.71 s; 0.54 / 0.84 s |
  | Trilheira | R 1.5 m, 85°/s | R 3.0 m, 109°/s, 1.11 g, 46° | R 14 m, 46°/s, 0.93 g, 50° | R 47 m, 24°/s, 0.82 g, 49° | R 99 m, 16°/s, 0.79 g, 50° | 0.42 / 0.72 s; 0.54 / 0.87 s |
  | Trial | R 1.2 m, 108°/s (a 11 km/h: 1.1 m, 162°/s) | R 3.3 m, 100°/s, 1.02 g, 44° | R 15 m, 43°/s, 0.87 g, 46° | R 48 m, 23°/s, 0.79 g, 48° | no llega (91 km/h) | 0.35 / 0.69 s; 0.51 / 0.83 s |
  | Carrera (asfalto) | R 2.8 m, 48°/s | R 2.6 m, 133°/s, 1.44 g, 48° (22 km/h) | R 10 m, 63°/s, 1.30 g, 51° | R 32 m, 36°/s, 1.27 g, 52° | R 64 m, 25°/s, 1.26 g, 52° | 0.28 / 0.55 s; 0.40 / 0.68 s |

- **Contra las de verdad** (órdenes de magnitud conocidos, no buscados: la inclinación de equilibrio es atan(a/g) más
  unos grados por el ancho de la cubierta; el agarre de un taco en tierra dura ~0.8-1.0, en pasto seco ~0.6-0.8, en arena
  suelta ~0.4-0.6; una cubierta deportiva en asfalto seco 1.1-1.4 (los registros de MotoGP dan 1.2-1.5 g); el radio
  mínimo es la distancia entre ejes / tan(tope de dirección); un piloto rápido se tira a la curva a 60-100°/s de rolido,
  o sea 0.5-1 s hasta acostarla):
  - **Motocross 450/600, 2T y Trilheira en tierra**: 0.78-0.93 g de 40 a 100 km/h (real 0.8-1.0) y radio mínimo 1.5-1.8 m
    (real ~1.8-2.2 m con 42-45°): **en rango**. Se acuestan 46-50°, 6-8° más de lo que pide atan(a/g) (0.80 g = 39°): el
    balance sostiene la inclinación pedida (`max_lateral_accel` con la pata afuera, ~47°) y las cubiertas, con deriva,
    doblan un poco menos. Generoso pero creíble. A 20 km/h llegan a 1.07-1.11 g (a esa velocidad dobla el manubrio, no la
    inclinación), apenas arriba.
  - **Trial**: radio 1.1-1.2 m a paso de hombre (real 1.3-1.5 m con 48°) y 108-162°/s: **en rango** (un poco más cerrada).
  - **Carrera en asfalto**: 1.26-1.30 g y 51-52° (real 1.2-1.4 g, 50-58°): **en rango**. A 22 km/h, 1.44 g en 2.6 m: más que
    su propio `max_lateral_accel` (a esa velocidad manda el manubrio); se deja.
  - **Tirarse a la curva**: 0.55-0.77 s hasta el 90% con el stick y 0.68-0.92 s con el teclado (real 0.5-1 s): **en rango**,
    igual a 40, 70 y 100 km/h; la de carreras es la más rápida, como tiene que ser.
  - **Las de tierra en pasto, calle de tierra y asfalto doblan igual que en la pista** (mismo radio y g a cada velocidad;
    a 40 km/h sólo cambia el uso del agarre: 0.71-0.77 en asfalto, 0.77-0.81 en pista, 0.81-0.85 en calle de tierra,
    0.86-0.91 en pasto). Lo que limita es `max_lateral_accel` (0.97 g), no el agarre: en pasto las cubiertas dan
    0.82 x 1.10-1.15 = 0.90-0.94 g, justo encima. Un taco en pasto seco real da ~0.6-0.8 g: **generoso**, pero no es de la
    moto sino del suelo (`Terrain::SurfaceGrip`, 0.82). En pasto a 20 km/h la trasera ya pasa el límite (uso 1.66) y doblan
    0.97 g en vez de 1.07. **La arena** (Los Médanos) agarra como la pista dura (1.0): una de verdad dobla ~0.4-0.6 g en
    arena suelta. Ver pendientes en [MOTOS.md](MOTOS.md).
  - **Carrera con lisas en tierra, pasto o calle de tierra**: dobla 0.37-0.47 g a 40-70 km/h (real 0.3-0.5 g: en rango) pero
    **acostada 56-57°**, con la trasera deslizando de costado (uso 15-27 veces su agarre), la cola 6-7° afuera y contravolante
    de 10°: un derrape permanente. Con 0.45 g una moto de verdad va a ~25°; acostada 56° se cae. Es lo único fuera de rango.
- **Por qué la de carreras se acuesta en la tierra**: el input pide una curvatura hasta `max_lateral_accel` (13 m/s² en la
  de carreras) sin mirar el suelo, y de ahí sale la inclinación pedida (`leanTarget`), que el balance sostiene. Con
  `*_tire_loose_grip` 0.35 las cubiertas dan 0.35 x 1.55-1.6 = 0.55 g, así que la moto se acuesta lo del asfalto y desliza.
- **Qué se hizo**: la clave `lean_surface_grip` (BikePhysics.cpp; 0 en `tuning.ini` = como antes, guardada con `if`). Con 1,
  si el agarre lateral de las cubiertas en ese suelo (`SurfaceGrip` x `*_tire_loose/paved_grip` x `*_tire_lat_grip`, en g,
  promedio de las ruedas apoyadas y filtrado ~0.25 s; cada rueda lo guarda en `Wheel::gripFactor`) es menor que
  `max_lateral_accel`, se pide eso. **Quedó sin activar** en `carrera.ini` (comentada, con los números):
  - con 1, en tierra se acuesta 27-28° y dobla 0.52-0.54 g (antes 56° y 0.44-0.47), en pasto 24° y 0.46 g, en calle 27°
    y 0.52 g (las cubiertas al límite, la cola 4.5-6° afuera); en asfalto, idéntica (el agarre pasa de `max_lateral_accel`);
  - bots de 30 min con la de carreras, 3 muestras de cada uno (la masa ±0.01 kg): pista de motocross de 13-17 caídas a 0-1;
    parque, de quedarse trabado (0-5 vueltas) a 7-18 vueltas con 2 caídas; **favela, de 7-12 caídas a 19-23**. En la favela
    se cae en las calles de tierra rectas de s≈1040-1091: con la trasera patinando al 35% (el control de tracción, en tierra
    con lisas) y menos inclinación, el bot (dirección proporcional, sin amortiguar) zigzaguea cada vez más (±26°) hasta pegar
    en una casa; también con `--bot-lat` 4.5 y 3.5. Antes el derrape acostado le daba más guiñada para corregir;
  - con 0.75 o 0.5 (o pidiendo el agarre del suelo sin el `lat_grip`: 0.45 g y 24°) la favela da igual o peor (15-21);
  - a las de tierra no les sirve: en pasto pediría 0.95 de lo de hoy. Lo que haría más resbaloso al pasto es `SurfaceGrip`.

  Queda para decidir si se activa (es la moto de asfalto: en la tierra se maneja más creíble, pero el bot de la favela se
  cae el doble).
- **Se comprobó** (sin la clave activa nada cambia): `tools/regresion.sh` contra el exe de la misma fuente sin el cambio,
  idéntico en 22 pruebas: las 9 de siempre, `accel`, el bot de motocross, la favela y Los Médanos con la 450, `circle12` de
  la 600, la trial, la Trilheira y la 2T, el bot de la 600, la Trilheira en la favela, la 2T en el parque, y la de carreras
  en el circuito, la favela y la pista de motocross (300 s) y en `plaza_tierra`. El bot de la 450: 1:08.21 / 1:08.01. Con la
  de carreras, 900 s de bot en el circuito, la pista de motocross, la favela y el parque: iguales al paquete v0.3.4 (circuito
  2:11.65 / 2:09.83 / 2:09.84 / 2:09.88 / 2:10.31 / 2:10.32 sin caídas; motocross 9 caídas; favela 9; parque trabado en s≈418).
- **Lecciones**:
  - El giro "a fondo" de esta física no depende del suelo: lo fija `max_lateral_accel` (más la pata afuera y los peraltes).
    El suelo sólo decide si las cubiertas alcanzan; si no alcanzan, la moto igual se acuesta lo pedido y desliza. Para
    ver el agarre de un suelo hay que mirar el uso de las cubiertas (`fUse`/`rUse`), no el radio.
  - La inclinación y la aceleración lateral no están atadas: el balance sostiene `leanTarget` con un torque. Comparar
    siempre `roll` con atan(g lateral); una diferencia grande (56° con 0.45 g) es un derrape sostenido.
  - `wy` es la guiñada en el eje de la moto: la del mundo es `wy / cos(roll)`.
  - El bot es un mal juez de una moto con poca autoridad de giro: su dirección es proporcional y sin amortiguación, y con
    menos guiñada zigzaguea. Para comparar caídas, varias muestras largas y mirar dónde (`s`) y por qué.
  - Un mapa de prueba "de pasto" necesita pista para la largada (la moto aparece sobre ella): una franja de 3 m alcanza,
    pero el giro arranca sobre la tierra; al salir al pasto la de carreras entra en un derrape que ya no suelta.

### En wheelie no se podía acelerar (v0.3.4, a pedido; cambia la 450)
- **Pasaba**: tirado atrás y con el gas a fondo, la moto subía la trompa y se quedaba ahí casi sin
  acelerar: en `--test wheelie` la 450 iba de 10 a 12 km/h en 2.5 s y la 600 de 14 a 19 km/h.
- **Por qué**: el limitador de wheelie (`wheelieCut`, en `BikePhysics.cpp` y `BikePhysics031.cpp`) evita
  dar la vuelta cortando el gas: en la zona permitida lo dejaba en 0.1 (450) o 0.16 (600). En 2ª y más
  marchas la zona empieza casi en 0° (`wheelie_gear_fade` 0.35), así que cualquier wheelie se quedaba sin gas.
- **Qué se hizo**: `wheelie_keep_throttle` (1 en `tuning.ini`; 0 = como antes). En un wheelie de verdad
  el limitador ya no corta el gas. En cambio anula el cabeceo que mete la tracción de la trasera (la
  fuerza en el contacto por debajo del centro de masa, multiplicada por `wheelieCut`), y la ayuda que baja
  la trompa (`wheelie_assist_torque`) queda igual. Así la moto acelera en una rueda y la rueda no sube más.
  - **Sólo a partir de `wheelie_keep_from_deg`/`full_deg` (5°/12°) de trompa arriba.** Con la trompa
    apenas despegada (un bache, la salida de una curva) sigue cortando el gas como antes. Sin ese filtro,
    en 15 min de bot la 450 se caía 9 veces en el circuito y 13 en la favela, contra 5 y 8 de antes.
  - **El valor tiene que ser 1.** Con 0.7 (una parte del gas cortado y una parte del cabeceo anulado), la
    450 y la 600 se daban vuelta: la tracción que queda sin compensar sigue levantando la trompa.
  - **La de carreras queda en 0** (`carrera.ini`): su anti-wheelie electrónico corta el gas a propósito.
    La trilheira tiene el limitador apagado (manda el grau), así que tampoco cambia.
- **Se comprobó**:
  - `--test wheelie` en 1ª: la 450 pasa de 13.5 a 54 km/h a 42° (hasta el corte de 1ª), la 600 de 14 a
    55 km/h a 55-58°, y la trial y la 2T igual, sin darse vuelta. Soltando el gas bajan solas.
  - Carrera y trilheira: telemetría idéntica a la v0.3.4 (`launch`, `wheelie`, bot en el circuito y la favela).
  - Bot de motocross: 450 1:08.21/1:08.01 (antes 1:08.45/1:08.00), 600 1:07.71/1:07.40; parque sin caídas.
  - Bot de 15 min, caídas: circuito 450 8 (antes 5) y 600 4 (3); favela 450 9 (8) y 600 6 (4). En la ventana
    de 4 s antes de cada caída no hay ningún wheelie. Casi todas son en las curvas que ya eran justas
    (circuito s≈1600, favela s≈80 y s≈258). Las de más vienen de que, al arrancar después de reaparecer, el
    bot sale más rápido y llega distinto a esas curvas.

### Motocross 600: corte de acelerador en wheelie (v0.3.4)
- **Pasaba**: la ayuda compartida de la 450 anticipaba 0.35 s y recortaba entre 12 y 27 grados; el limitador tambien reducia wheelie en marchas altas.
- **Que se hizo**: el perfil de la 450 queda intacto. La 600 usa 32/58 grados, 0.16 s de prediccion y asistencia de 800 Nm. En la 600, la influencia del cuerpo y el gas residual se ajustan por moto.
- **Estilo**: la 600 usa `mx600`: el chasis y la pose de la 450 con plasticos turquesa.
- **Se comprobo**: ver docs/VALIDACION-034.md.

### Cola que raspa en el wheelie pasado (v0.2.4)
- **Pasaba**: pasado el punto de equilibrio, al apoyar la cola la moto rebotaba "de más" y las
  colisiones parecían buggeadas.
- **Por qué**: pasados ~70° la rueda trasera "se perdía". Su rayo va por el eje de la suspensión, que
  ya estaba casi acostado; la cubierta se metía en el suelo y al reencontrarlo la escupía.
- **Qué se hizo**: un patín de cola (caño de aluminio bajo el guardabarros y caja de colisión
  `BoxShape(0.12)` en (0, 0.04, -0.90), marcada con `kTailSkidTag`). Con la moto a más de 40° y
  tocando el piso, el rayo de cada rueda llega más lejos cuanto más acostado queda el eje de la
  suspensión (`reach = max(1, 0.7 / eje hacia abajo)` en BikePhysics.cpp), así sigue encontrando el
  suelo debajo de la cubierta. Al raspar hay chispas y sonido (`Scrape` en PhysicsWorld: muchas en pavimento,
  objetos y casas, pocas en tierra) y el piloto va de pie. Detalle en el README ("Wheelie pasado").
- **Trampa 1**: una caja de cola **más chica que la parte más fina del chasis** (0.12) achicaba el
  "radio interior" que Jolt calcula para la forma compuesta. Eso cambia el CCD (`LinearCast`) de toda
  la moto, y las regresiones de la motocross dejaban de dar idénticas. Con medio lado 0.12 no cambia.
- **Trampa 2 (orden de las sumas)**: el largo del rayo se escribe
  `above + travel + radius*1.6 + (reach-1)*(...)`. Poniendo el término nuevo en otro lugar de la suma,
  los floats cambian en el último bit y la motocross deja de dar idéntica aunque `reach` valga 1.
- **Se comprobó**: pruebas `colazo`/`colazof` (la telemetría imprime `scr`, la velocidad de raspado);
  motocross idéntica a la v0.2.4.

### Doblar en una rueda: la trasera no apuntaba hacia donde iba (Trilheira)
- **Pasaba**: haciendo grau y doblando, la moto quedaba apuntando para afuera de la curva.
- **Por qué**: en una rueda, la inclinación se sostenía alrededor del eje levantado de la moto, no de
  la línea del piso por donde va. Eso metía guiñada.
- **Qué se hizo**: `grau_turn` (la cubierta trasera curva la trayectoria y la moto gira a la par) y
  `grau_align` (alinear con la dirección de avance). Resultado: ~6° de deriva a 21 km/h en vez de ~34°.
- **Se comprobó**: `--map favela --flat --test graucurva` y `graulentoc`.

### "En primera le falta potencia" (Trilheira)
- **Qué se hizo**: relación corta (`engine_final_drive` 7.8, como la corona grande del grau) y
  embrague que patina hasta 5500 rpm (`engine_clutch_rpm`): el motor queda arriba de vueltas al salir.
- **Se comprobó**: 0-50 km/h en 2.4 s sin ahogarse (`--test arranque`).

### Escaleras de la favela: la moto rebotaba para todos lados
- **Pasaba**: bajando la escadaria (12 escalones de 0.9 m con 27 cm de alto, y 29 cm contra el terreno
  abajo) la moto salía disparada. Bajando a 20 km/h la trasera hacía tope con 67 000 N en un paso, la
  velocidad saltaba de 18 a 33 km/h y la trompa bajaba 61°. El bot se caía ahí en cada vuelta.
- **Por qué** (con `--wheel-log`): la rueda es un rayo por el eje de la suspensión, y ese eje está
  inclinado: atrás hacia atrás, adelante hacia adelante (el rake). Apenas la trasera pasaba el borde, su
  rayo pegaba en la contrahuella del escalón que acababa de dejar: una pared con normal horizontal. La
  cubierta "tangente" a esa pared salía comprimida de más, con 105 700 N. Subiendo, el rayo delantero
  veía la pedada del escalón y la tomaba como un plano infinito que estaba debajo de la rueda, aunque la
  rueda estuviera todavía detrás del borde: 60 000 N y la moto volcaba para atrás.
- **Qué se hizo**: sobre objetos (no sobre el terreno) la cubierta tantea su propio perfil
  (`BikePhysics.cpp`, "Objetos"):
  - 7 rayos verticales repartidos bajo el círculo de la rueda (±0.84 del radio);
  - el piso bajo el eje es el apoyo plano;
  - donde dos sondas vecinas cambian de altura hay un canto: se lo ubica por bisección (5 rayos) y la
    cubierta rueda alrededor de él (el círculo de la rueda pasa por el canto, con la normal del canto
    al eje);
  - vale lo primero que toca al extenderse;
  - una diferencia de altura que explica la pendiente de la sonda vecina (una rampa) no es un canto.

  Si ningún rayo toca un objeto, todo queda como antes.
- **Se comprobó**:
  - motocross idéntica (su pista no tiene objetos) y 1:08.37 / 1:08.51;
  - bajando: sin golpes ni saltos de velocidad;
  - el bot en la favela: de 3 caídas en tres vueltas a 0 (1:37-1:38);
  - subiendo (`--spawn 90 --test subida8|15`): ya no vuelca. La delantera trepa el primer escalón y la
    moto queda frenada contra el segundo. Es realista: con una rueda de 35 cm, un escalón cuadrado de
    27-29 cm es casi una pared (el canto queda a ~78° de la vertical); hay que levantar la rueda.
  - **Efecto buscado en la trial**: antes subía el cajón de 0.5 m a fondo sin levantar la rueda (el
    mismo defecto: la trompa se apoyaba en la tapa del cajón antes de llegar). Ahora sólo lo sube
    levantando la rueda (`--test grau`); a fondo queda contra la cara.

### Doblando en wheelie la moto quedaba cruzada (todas menos la Trilheira)
- **Pasaba**: en una rueda y doblando, la motocross y la dos tiempos quedaban apuntando 20-25° para
  afuera de hacia dónde iban (`--map favela --flat --bike <moto> --test graucurva`, columna `beta`). La
  Trilheira no, porque tenía `grau_turn`. Desde que la moto elegida vale en todos los mapas se nota más.
- **Por qué**: la trasera inclinada curva la trayectoria, pero sin la delantera nada orienta la moto; el
  empuje, atrás del centro de masa, la cruza.
- **Qué se hizo**: `wheelie_turn`/`wheelie_align` (900 y 4 en `tuning.ini`) hacen lo mismo que el grau
  sólo mientras se dobla de verdad en una rueda: más de 0.3 s con la trasera sola en el piso, y
  inclinación o deriva de más de 2-6°. En ese caso la moto se inclina alrededor de la línea del piso y
  gira a la par de su trayectoria. Derecha, o un instante en una rueda (un salto), no cambia nada. Las
  motos con `grau_turn` siguen con lo suyo.
- **Se comprobó**: ~7° de deriva en todas (la Trilheira ~6.5°), y todas las pruebas de la motocross
  idénticas (wheelie derecho, saltos, vueltas del bot).

### Caja automática con rpm fijas
- **Pasaba**: subía a 9400 rpm y bajaba a 4800 en todas las motos. La de carreras (pico a 15300) subía
  muy abajo: 0-200 km/h en 7.2 s. La trial (corte a 9000) no subía nunca.
- **Qué se hizo**: los umbrales se multiplican por `engine_rpm_scale` (con 1 dan exactamente lo mismo).
- **Se comprobó**: la de carreras hace 0-200 en 5.1 s; la motocross, la Trilheira y la 2T no cambian.

### Escaleras, segunda vuelta: la rueda se trababa al costado
- **Pasaba**: bajando sin seguir la línea (la prueba `bajada` sin dirección), la moto se iba al borde y se
  quedaba trabada meciéndose, con picos de 20 000 a 64 000 N en la delantera.
- **Por qué**: entre la punta de los escalones (media calzada + 0.2 m) y el murito (su cara, + 0.33 m)
  quedaba una canaleta con el terreno en pendiente al fondo. Entrando de costado, las sondas de la cubierta
  encontraban de golpe la pedada 24 cm más arriba.
- **Qué se hizo**: los escalones llegan al murito (+ 0.36 m). Sobre objetos, el piso bajo la rueda sube
  como mucho `0.02 + 1.5·v·dt` por paso (lo que permite rodar trepando); viniendo del aire, sin límite.
- **Se comprobó** (`--spawn 68 --test bajadaN`, que sigue la línea con la dirección del bot, y
  `tools/escalera.py 74 88`), con la Trilheira, la motocross y la trial de 8 a 35 km/h:
  - fuerzas de 2 000 a 13 000 N;
  - saltos de velocidad ≤ 2 km/h;
  - ninguna trabada;
  - a 35 km/h vuela de escalón en escalón y cae de trompa, como una moto de verdad.
- **Revisado al armar la v0.2.6** (Trilheira, `bajadaN` con `--time 14`, mirando también después de la
  escalera): 10, 20, 25, 28 y 35 km/h sin caída; **a 30 y 32 km/h se cae al pie** (s ≈ 89-91). Pasa el
  primer escalón volando, la trompa baja a -57°, baja los escalones picando sólo con la delantera y se
  va tumbando de costado en el aire hasta -90°. No es un golpe (no aparece `GOLPE`). La prueba no tira
  el cuerpo atrás, que es lo que haría un piloto. Pendiente: ver si con el cuerpo atrás se salva, y si la
  caída de costado es real o viene de algún empuje lateral de las sondas en los cantos. El bot, a su
  velocidad, la baja sin caerse (0 caídas en 2 vueltas).

### La de carreras: frenando fuerte se cruzaba y hacía wobble
- **Pasaba** (`--map base/circuito --bike base/carrera --test frenadaN`): frenaba ~1 g, pero quedaba
  cruzada ~9° (a 120 km/h). A 250 km/h la cola saltaba (se levantaba y volvía) y el manubrio oscilaba
  entre 0 y -18°.
- **Por qué**: con la trasera casi sin carga (~250 N de 2500), la moto es inestable de guiñada, como un
  auto que frena con las de atrás trabadas. En cuanto giraba un poco, la delantera, con el manubrio
  "fijo", empujaba de costado y la hacía girar más; la inclinación crecía sola. En una moto de verdad la
  rueda delantera es libre: el trail la hace seguir su propio camino y no empuja de costado.
- **Qué se hizo**:
  - `brake_align` (0 en `tuning.ini`, 4 en la de carreras): frenando fuerte y sin pedir curva, la
    delantera sigue su camino y la moto vuelve a apuntar hacia donde va;
  - el anti-levantamiento de la cola del ABS con umbral y fuerza por moto (`rear_lift_load`, `rear_lift_mitigation`).
- **Se comprobó**: de 80 a 280 km/h, 0.0° de deriva, sin mover el manubrio, sin levantar la cola, a
  0.85-1 g (`tools/frenada.py`). La motocross, idéntica.
- **Pendiente**: la motocross (con `brake_align` 0) todavía gira frenando a fondo con la S de 90-120 km/h.

### La de carreras: frenaba poco y no doblaba frenando
- **Pasaba** (el usuario: "frena lento" y "aunque esté con ABS no puedo doblar ni un poco mientras freno"):
  - en recta frenaba 0.92 g a 120 km/h, 0.97 desde 200 y 1.05 desde 280 (una superbike con ABS: 1.1-1.2 g);
  - con S + D a 120 km/h la moto se inclinaba 26° (53° sin frenar), la cola salía ~8° de golpe y el
    camino giraba 9° en 1 s (sin frenar, 12°): durante casi un segundo iba cruzada sin doblar.
- **Por qué**:
  - **en recta el límite era el anti-levantamiento del ABS, no el agarre**: la delantera usaba el 55% de
    su agarre; con `rear_lift_load` 450 / `rear_lift_mitigation` 0.35 el ABS aflojaba mucho antes del
    stoppie. El límite de la geometría (centro de masa a 0.62 m, 0.68 m detrás del contacto de adelante) es
    ~1.1 g;
  - **doblando**, la Carrera heredaba de `tuning.ini` `brake_stand_up` 0.6: con la S a fondo la inclinación
    pedida quedaba en el 40%, y arriba de ~30 km/h se dobla inclinando;
  - **la cola**: con la delantera cargada y la trasera casi sin peso (~300 N), al tirarse a la curva la moto
    giraba más rápido que su camino y la trasera se iba de costado (no estaba trabada: patinaje -0.02, pero
    demanda lateral 4-6 veces su agarre). `brake_align`, que endereza frenando, se apagaba apenas se tocaba
    la dirección.
- **Qué se hizo** (sólo la Carrera: la motocross tiene `brake_align` 0 y sus claves no cambiaron):
  - `brake_align` sigue actuando doblando: frena sólo el giro que sobra respecto del camino (`pathRate`),
    así que no le quita curva; la delantera "libre" (`brakeFree`) sigue apagándose con la dirección;
  - `carrera.ini`: `brake_stand_up` 0.2, `front_brake_torque` 620 → 720, `rear_lift_load` 450 → 300 y
    `rear_lift_mitigation` 0.35 → 0.25.
- **Probado y descartado**:
  - alinear más fuerte (`brake_align` 8 o `brake_align_torque` 3000): oscila, a veces el camino vuelve para
    atrás;
  - aflojar la delantera según la inclinación (un "ABS de curva"): sólo le saca frenada, la cola sale igual;
  - menos freno trasero o menos freno motor: tampoco endereza (no era la trasera frenando), sólo frena menos.
- **Se comprobó** (`--map prueba/plaza_asfalto --bike base/carrera`, `tools/frenada.py` y `tools/frenacurva.py`):
  - en recta: 0.98 g desde 80, 1.01 desde 120, 1.09 desde 200 y 1.16 desde 280, sin cruzarse (beta 0°) ni
    levantar la cola (0-2% del tiempo);
  - S + D desde 120: 1.16 g, el camino gira 21° en 1 s y 56° en 2 s (antes 9° y 27°), inclinada 40°, la cola
    afuera ~6° (como entrar "cruzado" en una moto de carrera), sin caídas; desde 60: 45° en 1 s;
  - el bot del autódromo: 2:15 / 2:14 y 2 caídas en 300 s (antes 2:22 / 2:20 y 4);
  - la motocross, la favela, la trial y la 2T: idénticas; el bot de motocross 1:08.37 / 1:08.51.
- **Trampa de la prueba**: la primera plaza de asfalto tenía la largada donde la curva de la vuelta todavía
  torcía la línea (8.6°): yendo derecho 450 m la moto se corría 68 m y salía del asfalto. Parecía que el
  freno "se iba" a 95 km/h (uso de agarre de la delantera 5.3); el `--wheel-log` (que ahora imprime también
  una línea `freno` por rueda: vueltas, patinaje, si está retenida, freno, uso y agarre máximo) mostró que
  el agarre disponible caía con la misma carga: era la superficie. Las pruebas de frenada, en una recta
  larga y derecha; revisar con `--test profile` dónde es asfalto. Lo mismo pasa en el autódromo en
  `frenada280`: al final la recta de la prueba se termina y sale al pasto.

### La de carreras: soltando la curva para frenar se iba para el otro lado
- **Pasaba** (el usuario, con teclado: "cuando clavo los frenos se dobla la moto sola para la derecha o la
  izquierda, se pierde demasiado el control"). Las pruebas de frenada eran siempre con la moto derecha; con
  teclado se frena viniendo inclinado y soltando la A/D. `frenacurva150x0.6d-1.5a1.5` (doblando a 36°, suelta
  la D y frena): la moto quedaba clavada a 13° de inclinación, la cola salía 16° y hasta parar el camino
  giraba 93° para el **otro** lado (a 250 km/h, 119°). Sin freno, soltando igual, se endereza en 0.5 s con la
  cola a 2°.
- **Por qué**: la inclinación la cambia un torque de balance en el centro de masa (`balanceK*`), y al
  enderezarse rápido (36° → 16° en 0.3 s) las cubiertas tienen que acompañar de costado. Frenando fuerte la
  trasera tiene ~300 N: no acompaña, se va de costado y la moto queda deslizando con las dos cubiertas
  saturadas (uso 3-7), sin poder enderezarse ni doblar.
- **Qué se hizo**: `brake_transition_release` (0 en `tuning.ini`, 0.6 en la de carreras): mientras lo que
  pide el piloto y la inclinación que tiene difieren mucho (de 4° a 20°), el ABS afloja la delantera hasta
  esa fracción, como el piloto que primero levanta la moto y después frena a fondo.
- **Probado y descartado**:
  - aflojar según la inclinación (`brake_lean_release`): la moto se endereza en 0.3 s y a 13° ya frenaba a
    fondo otra vez; no cambió nada;
  - un ABS "combinado" que afloja la rueda cuando su uso de agarre pasa de 1: doblando la trasera siempre
    está pasada (de costado), así que mataba la frenada (0.3 g).
  Se sacaron del código.
- **Se comprobó** (`--map prueba/plaza_asfalto --bike base/carrera`, desde 150 km/h):
  - soltando la curva para frenar, cola 4.9° y camino 8° hasta parar (antes 16° y 93°); a 80 km/h, 2.5° y
    11°; a 250, 3.7° y 5°;
  - tocando la D un instante frenando, 2.4°;
  - frenando derecho, igual (1.04 g);
  - frenando y doblando desde el principio, sigue doblando: 128° hasta parar, 1.04 g;
  - el bot del autódromo, 2:15 / 2:14 con 2 caídas en 300 s;
  - la motocross, idéntica.
- **Pendiente**: la motocross (abajo).

### La de carreras: frenando inclinada sin soltar la curva se abría
- **Pasaba** (el usuario: "anda bien si primero frenás y luego doblás, pero sigue pasando cuando estás doblando
  y frenás"). `frenacurva150x0.6d-1.5` (doblando a 38° y frenando sin soltar la D): la moto seguía inclinada
  pero el camino giraba 1° en 1 s y -14° hasta parar. Se abría de la curva, cangrejeando con la cola 6° afuera;
  con la D a 0.4, hasta se iba para el otro lado.
- **Por qué** (con las líneas `freno` del `--wheel-log`, que ahora traen también la velocidad de costado,
  la deriva, la fuerza lateral pedida y la que da, y la carga):
  - la trasera pasaba de ~1100 N a 245 N y se deslizaba de costado a 5 m/s;
  - la delantera, alineada con su camino por el contravolante automático, empujaba -110 N de costado (sin
    frenar, -1179 N);
  - la causa de fondo: inclinada, el contacto de las cubiertas queda hacia afuera del centro de masa
    (0.62 m × sen 36° ≈ 0.36 m), y la fuerza de frenado ahí hace girar la moto **hacia afuera** (~800 Nm a
    1 g). En una de verdad eso la endereza y la abre, y el piloto lo compensa con el manubrio; acá el
    balance sostiene la inclinación, así que la moto giraba para afuera sin enderezarse.
- **Qué se hizo**: `brake_yaw_comp` (0 en `tuning.ini`, 1 en la de carreras) cancela el giro que hacen las
  fuerzas de frenado en el contacto: el giro de la fuerza en `contactPoint` respecto del centro de masa, sólo
  cuando la fuerza frena. Derecha no hace nada (el contacto está en el eje). Con eso, `brake_align` 8 y
  `brake_align_torque` 2000 (antes 4 y 1500) sostienen la cola.
- **Probado y descartado**: que el ABS le exija más carga a la trasera según la inclinación
  (`rear_lift_lean_load`): frenaba la mitad y seguía sin doblar. Sacarle el freno trasero y el freno motor, peor
  (con la D a 0.4 se iba para el otro lado): eso mostró que había un empuje hacia afuera.
- **Se comprobó** (desde 150 km/h, `tools/frenacurva.py` y el resumen de situaciones de teclado):
  - doblando y frenando sin soltar: 125° hasta parar a 1.07 g, cola 6.7° (con la D a 0.3, 119°; desde
    100 km/h, 96°);
  - soltando para frenar: 1.5° y 6°;
  - tocando la D: 1.2°;
  - derecho, 1.04 g;
  - frenando y doblando desde el principio: 144°, cola 4.8°;
  - a 80 km/h, parecido; a 250, doblando y frenando sin soltar dobla (-34°) pero frena ~0.6 g (el ABS
    afloja con la trasera sin carga): a 250 no se puede frenar a fondo y doblar fuerte a la vez;
  - el bot del autódromo: 2:11.6 / 2:09.8 y **0 caídas** en 300 s (antes 2:15 con 2; con la v0.2.6, 2:21
    con 4);
  - la motocross y las demás, idénticas.
### La motocross: frenando fuerte hacía un trompo
- **Pasaba**: con la S a fondo desde ~100 km/h se cruzaba 140-167° (un trompo) en tierra plana y en asfalto,
  derecha, tocando la dirección, soltando la curva o doblando (`frenada100` y `frenacurva100...` con `--flat`).
  El usuario lo pidió ("dale aplicalo, pero intentá que no afecte el resto, esa moto se siente bastante bien").
- **Por qué**: lo mismo que la de carreras: con la trasera casi sin carga, frenar es inestable de guiñada.
- **Qué se hizo**: la motocross tiene ahora su `.ini` (`mods/base/bikes/motocross.ini`, que el `.json` nombra),
  con sólo `brake_align` 4 y `brake_align_torque` 2000. `tuning.ini` no se tocó, porque es la base de las
  demás motos (la 2T, la Trilheira y la Trial lo heredan). `brake_yaw_comp` ahora sólo actúa con el freno de
  adelante (antes también con el freno motor al soltar el gas inclinado, lo que habría cambiado cómo dobla).
- **Probado y descartado para la motocross**:
  - `brake_yaw_comp` 1: con esta geometría (y en tierra) doblando y frenando la cola salía 41-56°;
  - `brake_transition_release` 0.6: no mejora;
  - `brake_align` 8: igual o peor.
  `brake_stand_up` queda en 0.6: se endereza frenando como siempre.
- **Se comprobó** (con `--bike base/motocross`):
  - frenando derecho desde 60, 100 y 125 km/h, cola 0°, a ~0.66 g en tierra;
  - tocando la dirección, 3°; soltando la curva, 5° (asfalto 5°); doblando sin soltar, 6° (desde 60 km/h,
    12.5°); frenando y doblando, 4°;
  - las regresiones contra la v0.2.6 idénticas (`brakeslide`, `brakeslide2`, `brakeslide3.5`, `cuerpo`, `flip`,
    `whip`, `wheelie`, `bot`, `accel`, la favela, la trial y la 2T): nada de eso frena fuerte con la S;
  - el bot, 1:08.37 / 1:08.51.
  Las frenadas fuertes con la S de la motocross (`frenadaN`, `frenacurvaN` con `--flat`) difieren de la v0.2.6
  a propósito.
- **Trampa**: `--flat --test frenada125` con la motocross tarda ~11 s en llegar y en la frenada se termina el
  mapa plano: cae por el borde (inclinación 180°, cabeceo -89°, las dos ruedas en el aire). No es la moto.
- **Después, a pedido, las otras de tierra**:
  - **Dos tiempos** (`dostiempos.ini`): `brake_align` 4 / `brake_align_torque` 2000 y `*_rebound_ratio` 1.7.
    Frenando desde 90 km/h se cruzaba 104-165° y ahora 3-22°; aterrizando vuelve en 0.27-0.30 s (antes
    0.70-0.77). El bot del parque, igual (0:50.9) y sin caídas.
  - **Trial** (`trial.ini`): sólo la frenada (se cruzaba hasta 58° desde 60 km/h; ahora ~15-17°). Su
    suspensión ya era viva (vuelve en 0.19 s: amortiguación de base 650/700 con 3.2) y con 1.8 rebotaba de
    más y despegaba las ruedas 0.07 s. El circuito de obstáculos, igual (37.4 s) y sin caídas.
  - **Trilheira**: no se tocó entonces. Se cruzaba 110-151° frenando desde 90, y la frenada nueva lo arreglaba, pero en
    la favela (15 min de bot) las caídas pasaban de 2 a 4 (bajada de -25% que entra en curva, s≈255: el
    alineado frena el giro de entrada). Con `brake_transition_release` esas desaparecían, pero aparecían en
    s≈1217 (subida de 24% con curva que termina en una loma). La suspensión (2.0 o 2.5) también sumaba
    caídas en esa loma: se estira, la moto queda liviana en plena curva. Resuelto después de la v0.2.7 (abajo, "La
    Trilheira: frenando fuerte hacía un trompo"); 15 minutos de bot no alcanzaban para compararlo.

### La Trilheira: frenando fuerte hacía un trompo
- **Pasaba**: con la S a fondo desde 60-90 km/h la cola se cruzaba 47-151° (`--flat`) y, en las plazas, 140-146°
  tocando la D o soltando la curva a 90 km/h. El arreglo de las otras de tierra (`brake_align` 4 / 2000) lo curaba,
  pero parecía sumar caídas en la favela (arriba).
- **Por qué**:
  - el trompo, lo mismo que en las otras: con la S la cola va en el aire el 70-92% de la frenada (stoppie a -20°) y
    la moto es inestable de guiñada;
  - la favela: **el bot pega contra la pared de afuera de la curva de s≈253 en casi todas las vueltas, también con
    la moto tal cual** (7 golpes en 9 vueltas, perdiendo ~20 km/h de golpe; con la motocross y la 2T también). Llega
    a ~43 km/h después de la loma de s≈241 con sólo la delantera en el piso y no puede doblar. Que el golpe sea
    caída depende de si pasa de 7 m/s (`crash_impact_speed`), y la simulación es caótica: cualquier cambio da otra
    muestra. **Con 15 minutos (9 vueltas) no se puede comparar**: la moto sin cambios, con la masa corrida ±0.01 kg,
    da de 3 a 9 caídas en 45 minutos (media 5.8 en 9 muestras);
  - aun así el alineado de la motocross sí era peor (13 caídas en 45 min; 9 y 12 sin la delantera libre): en la
    bajada el bot frena al 45-80% (s≈236-252) y empieza a doblar con muy poco manubrio. El alineado actúa desde el
    20% de freno y frena ese giro de entrada, y la delantera "libre" (que sigue su camino) le anula el poco
    manubrio: entra más abierto y más inclinado.
- **Qué se hizo**: claves nuevas `brake_align_from` / `brake_align_full` (con cuánto freno de adelante empieza el
  alineado y con cuánto actúa entero; 0.2 / 0.6, lo de antes) y `brake_align_free` (cuánto se suelta la delantera
  frenando derecho; 1, lo de antes). La Trilheira: `brake_align` 4, `brake_align_torque` 2000, 0.55, 0.95 y 0. La S
  va al 100% y alinea entero; el bot, al 45-80%, casi nada.
- **Probado y descartado**:
  - `rear_lift_load` / `rear_lift_mitigation` (700 / 0.6, 900 / 0.8, 500 / 0.5): sacan el stoppie y frena más en el
    plano (0.81-0.89 g), pero en la bajada la trasera va liviana por la pendiente y el ABS afloja la delantera: de 3
    a 8 caídas en 15 min, todas en s≈253-258;
  - un "ABS trasero" que aflojara el de atrás frenando con las dos (la S traba la trasera con ~250 N): destrabada,
    la moto ya no se endereza por el derrape (`slide_upright`) y llega a la pared inclinada 39-41° en vez de 28°:
    17-18 caídas en 45 min. Se sacó del código;
  - `brake_align_torque` 1000: doblando y frenando la cola sale 30-41°;
  - la suspensión más viva (`*_rebound_ratio` 1.7 y 2.0): vuelve en 0.25 y 0.32 s al aterrizar (hoy 0.6), pero
    rebota en la loma de s≈241 y en la curva de abajo se cae el doble o el triple (9-12 y 12-19 caídas en 45 min).
    Queda en 3.0.
- **Se comprobó**:
  - las frenadas de teclado (`frenadaN`, `frenacurvaN` derecho, tocando la D, soltando la curva, doblando sin soltar
    y frenando y doblando; 60 y 90 km/h; `--flat`, `prueba/plaza_tierra` y `prueba/plaza_asfalto`): la cola 0-29°
    (antes hasta 151°), 0.69-0.90 g, sin caídas;
  - la favela: 6 muestras de 45 min (la moto y la moto con la masa o las inercias corridas un pelo) dan 3-8 caídas,
    media 5.8, igual que sin cambios; en 15 min, 1 caída (antes 2);
  - el grau (`graucurva`, `graulentoc`), el arranque, los derrapes, el wheelie, la escadaria (`bajada20`) y los
    aterrizajes, idénticos a la v0.2.7 (no usan el freno de adelante); la motocross y la trial, idénticas.
- **Cómo comparar caídas en la favela**: varias corridas de 45 min (`--time 2700`) de cada versión, cada una con una
  perturbación que no cambia nada de fondo (`mass` ±0.01 en una copia de la moto en `pruebas/`), y comparar las
  medias; mirar también los golpes (caídas bruscas de velocidad) y en qué s, no sólo las caídas.

### Revisión contra las motos de verdad: lo de física
La tabla de cada moto está en [MOTOS.md](MOTOS.md) ("Las motos contra las de verdad"). Lo que tiene que ver con la
física en sí:
- **Con la S a fondo las de tierra frenan en stoppie** (no se cambió): la trompa a -20°/-21° y la cola en el aire el
  77-93% de la frenada, a 0.64-0.75 g. Por qué: el freno de adelante de `tuning.ini` (950 Nm en 0.35 m) da ~1.5 g y
  el stoppie llega antes que el agarre (centro de masa a 0.83 m del piso y ~0.8 m detrás del contacto de adelante:
  ~0.9-1 g). Lo sostiene el anti-endo, que afloja el freno entre -12° y -28° de cabeceo, y `rear_lift_mitigation`
  (0.25) no alcanza para bajar la cola. Frena menos que si la cola apoyara, y la cola en el aire es la inestabilidad
  que después arregla `brake_align`. En una copia de la motocross, `rear_lift_load` 700 y `rear_lift_mitigation` 0.6
  frenan a 0.81-0.85 g sin stoppie (propuesta en [MOTOS.md](MOTOS.md), pendientes). La de carreras no lo tiene
  (720 Nm, centro de masa a 0.62 m: 0.96-1.08 g derecha).
- **En el aire todas giraban igual**: `air_pitch_rate` 2.5 rad/s y `air_pitch_torque` 450 son de `tuning.ini`, y con
  450 Nm cualquier inercia (44 a 74 kg m²) llega a 2.5 rad/s en 0.25-0.4 s de un vuelo de 1-3 s, así que la inercia
  casi no se nota: la de carreras hacía el mismo mortal que la de motocross (333° y 325°). Para que una moto sea torpe en el aire hay que bajar esas dos
  (la de carreras: 1.2 y 250), no subirle la inercia (que cambia los wheelies y las frenadas en el piso).
- **El freno motor es lineal con las rpm** (`engine_brake` Nm en el cigüeñal al corte): con una relación larga (la
  de carreras, 1ª hasta 153 km/h) abajo casi no frena. Medido restando el aire y la rodadura de `frenacurvaNx0n`:
  motocross 0.07-0.10 g, Trilheira 0.08-0.10, trial 0.05-0.07, 2T 0.03-0.05, carreras 0.04 a 80 km/h y ~0 a 40.

### La motocross: la suspensión se sentía muerta al aterrizar
- **Pasaba** (el usuario: "la suspensión debería sentirse un poco más springy al aterrizar"). Cayendo de
  2.5 m en plano se comprimía casi a tope (0.98) y volvía a su altura de reposo (33%) en 0.74 s, arrastrándose y
  sin pasarse nada.
- **Por qué**: `*_rebound_ratio` 3.0 en `tuning.ini`: la extensión amortiguada tres veces más que la compresión.
  Con el resorte de adelante (6800 N/m, ~90 kg por rueda) eso es ~1.9 veces la amortiguación crítica al
  extenderse: sobreamortiguada.
- **Qué se hizo**: en `motocross.ini` (no en `tuning.ini`, que es la base de las otras motos),
  `front_rebound_ratio` y `rear_rebound_ratio` 1.7.
- **Se comprobó** con un resumen de aterrizajes (`--flat --drop H --test idle`):
  - desde 1 y 2.5 m, vuelve en 0.28 y 0.24 s (antes 0.65 y 0.74), casi sin pasarse;
  - desde 5 m se estira casi del todo y las ruedas se despegan 0.02 s;
  - con 1.5, desde 5 m se estiraba del todo y se despegaba 0.07 s (patada); con 2.0, parecido a 1.7 pero más
    lento (0.37 s);
  - el bot: motocross 1:08.73 / 1:08.42 y Los Médanos 1:26.9 / 1:26.7, sin caídas; los mortales (`flip`,
    `frontflip` desde 5 m, el Gigante de Los Médanos) giran y caen igual;
  - las otras motos, idénticas.
- **Consecuencia**: contra la v0.2.6 la motocross da distinta en todas las pruebas; ver [PRUEBAS.md](PRUEBAS.md).
- **Después**: la 2T pasó a 1.7 (v0.2.7); la Trial tiene lo suyo (3.2 con la amortiguación baja: ya volvía en
  0.19 s); la Trilheira se queda en 3.0: más viva rebota en la loma de la bajada de la favela (ver "La Trilheira:
  frenando fuerte hacía un trompo").

### Golpe fuerte: el piloto sale despedido
- **Pasaba**: sólo había caída por inclinación. Contra un muro, el piloto salía recién cuando la moto se
  daba vuelta, y con la velocidad de después del golpe (casi quieto).
- **Qué se hizo**: `PhysicsWorld::LastImpact()` anota, de cada contacto nuevo de la moto (con su piloto)
  contra algo fijo, la velocidad con que se acercaban por la normal.
  - Umbrales: más de `crash_impact_speed` (7 m/s) contra algo de frente o de costado (normal casi
    horizontal), o de `crash_impact_vertical` (11 m/s) cayendo a plomo.
  - Pasado el umbral, caída: el ragdoll sale con la velocidad de antes del golpe y pasa por encima del
    manubrio o se estampa.
  - No cuentan la cola raspando ni los objetos sueltos; rozar un muro de costado tampoco (la velocidad
    hacia el muro es chica).
- **Se comprobó**:
  - la motocross da idéntica en todas las pruebas y vueltas;
  - la de carreras contra el muro del circuito a 16 m/s: el piloto sale volando;
  - el bot de la favela se estrella una vez contra una pared a 8.7 m/s.
- **Consecuencia**: el bot del circuito, que pegaba en las barreras y seguía, ahora se cae ahí (ver
  [MAPAS.md](MAPAS.md)).

### Derrape lento con el freno trasero (motocross)
- **Pasaba**: entrando despacio (9-15 km/h) y doblando con el trasero, la cola salía apenas 11-24° y la
  moto se frenaba antes de derrapar: no hay inercia para que la cola salga sola.
- **Qué se hizo**: `slide_pivot` (800 en `tuning.ini`, 0 en la de carreras): debajo de ~13 km/h, con el
  trasero y doblando, el piloto empuja la cola hacia afuera hasta el ángulo que busca.
- **Trampa**: la ventana de velocidad se mide con la velocidad real, no la de adelante. Derrapando con la
  cola a 33°, la de adelante es un 16% menor y el empuje se colaba al final del derrape rápido de siempre
  (la regresión `brakeslide` dejó de dar idéntica hasta cambiarlo).
- **Se comprobó**: cola afuera 22-40° de 9 a 16 km/h (`--flat --test brakeslideN`), sin caídas; el
  derrape de siempre (32 km/h) idéntico.
- **Medido de nuevo al armar la v0.2.6** (`brakeslide2.5`, `3.5` y `4.4`: N en m/s; `beta` máxima con
  `rb` > 0.5). A 14-17 km/h: Motocross 34-38°, 2T 33-38°, Trilheira 43-54°, Trial 21-56°, sin caídas.
  A ~11 km/h: sólo la 2T (39°); las demás 4-8°. O sea, anda de ~13 a 16 km/h, no a paso de hombre.
  La de carreras (`slide_pivot` 0) da 10-13° sola.

### Lisas en la tierra (la de carreras)
- **Pasaba**: el suelo tenía un solo factor de agarre para todas las cubiertas: la de carreras agarraba
  en la tierra casi como en el asfalto.
- **Qué se hizo**: cada cubierta multiplica el agarre del suelo según cuánto es pavimento:
  - `*_tire_loose_grip` en tierra y pasto (la de carreras: 0.35);
  - `*_tire_paved_grip` en asfalto, cemento y objetos;
  - `Terrain::PavedAmount`: en los mapas de calles, la calzada; en los de pista, nada.

  Con los dos en 1 ni se multiplica.
- **Se comprobó**: en un círculo sobre tierra la deriva pasa de 2.5° a 6.9° y la vuelta a la pista de
  motocross de 1:04.7 a 1:07.3 (con 0.45; después se bajó a 0.35); en el asfalto del circuito el
  pavimento vale 1 en toda la calzada (`--test profile` lo imprime).

### Ragdoll que se doblaba para cualquier lado
- Ver [PILOTO.md](PILOTO.md): los ejes del swing-twist estaban cruzados, y después los límites no
  aguantaban los golpes (hasta 50° de más). Se arregló con bisagras de Jolt en rodillas y codos, más
  pasadas del solver y las articulaciones puestas donde las tiene el modelo.

## Notas de Jolt

- **SwingTwistConstraint**: espacio del constraint X = twist, Y = normal (plane × twist), Z = plane.
  El giro alrededor de **Z** (el eje "plane") lo limita `mNormalHalfConeAngle`, y el giro alrededor de
  **Y** lo limita `mPlaneHalfConeAngle`. Los nombres confunden: se leen al revés de lo que parece.
  Los límites son simétricos; para un rango asimétrico, el eje twist del padre se centra a mitad del
  rango. `GetRotationInConstraintSpace()` + `Quat::GetSwingTwist` da lo mismo que usa Jolt para medir,
  **con el signo normalizado** (si w < 0, negar: si no, un giro de 0° se lee 360°).
- **SwingTwist al crearlo en una pose que no es la de referencia**: el eje "plane" del cuerpo 2 tiene
  que ser el del cuerpo 1 llevado por el arco más corto de un eje twist al otro
  (`Quat::sFromTo(t1, t2) * plane1`). Proyectarlo (`plane1` perpendicular a `t2`) deja un giro sobre el
  hueso que no está en la pose, y con arcos grandes pasa del límite: el solver lo corrige de un tirón
  (80° de giro en los hombros del ragdoll, ver [PILOTO.md](PILOTO.md)). El cono es una elipse en
  `(sin(swingY/2), sin(swingZ/2))` con semiejes `sin(mPlaneHalfConeAngle/2)` y `sin(mNormalHalfConeAngle/2)`:
  para que la pose de partida entre, se chequea ahí, no cada ángulo por separado.
- **HingeConstraint**: rango asimétrico real (`mLimitsMin` en [-π, 0], `mLimitsMax` en [0, π]). El
  ángulo 0 es cuando `mNormalAxis1` y `mNormalAxis2` coinciden; crece con la mano derecha alrededor de
  `mHingeAxis1`. Para que el ángulo actual sea el de la pose, `mNormalAxis2` es la dirección actual.
- **Pasadas del solver por constraint**: `mNumVelocityStepsOverride` / `mNumPositionStepsOverride`.
  Rigen para toda la isla (se usa el máximo), así que sólo cuestan cuando el ragdoll existe.
- **Partes chicas y rápidas**: `EMotionQuality::LinearCast`, para que no atraviesen el suelo.
- **Cuerpos**: el mundo admite hasta 8192 (la favela tiene ~1050 casas con colisión; el circuito ~2950
  cajas).
- **Rayos que empiezan adentro de un cuerpo**: Jolt devuelve un choque en la fracción 0. Las sondas de
  la cubierta descartan esos (`mFraction > 0`): un rayo que arranca adentro de un escalón es una pared.

## Terreno

- El tamaño y la resolución son por mapa (`terrain.size`, `terrain.resolution`). La cantidad de
  muestras por lado, N, se redondea a múltiplo de 8 y va de 64 a 2048 (`Terrain::Build`).
- La simulación va a 120 Hz con 1 collision step.
## Suelo deformable: verificación

`motocross.exe --headless --flat --test soilcheck` verifica compactación, excavación por patinaje,
conservación acotada de volumen, profundidad, bordes, contacto Jolt, restauración, independencia
60/120 Hz, ruedas en el aire y teletransportes. Repetir con `--map base/circuito` verifica asfalto.
Ambas variantes pasaron; error altura/colisión medido menor a 0.003 m.

Las alturas pendientes se publican juntas a malla y colisión cada 0.06 s. El primer surco crea
un heightfield con margen para elevar bordes; el collider original se conserva para restaurarlo.
El 60% del volumen retirado como máximo se deposita en los bordes; el resto representa
compactación y material expulsado. La profundidad depende de la resolución original del mapa.
`PressWheel` usa los ajustes de Tierra; `rut_dig_rate` y `rut_max_depth` quedan sólo para `DigRut` legado.
Juego manual: física local por defecto. Bot/pruebas/headless: activarla con `--ruts`.
La telemetría de aceleración de cuatro segundos sin `--ruts` coincidió exactamente con el exe anterior.
Las ruedas no leen este relieve tal cual sino una versión suavizada (ver "El surco no tiene que manejar la moto").

### Suelo fino por sectores

El sistema actual guarda sólo las muestras modificadas en una grilla local de 5 cm. Al deformar
por primera vez, el mismo BodyID recibe un compuesto de heightfields: sectores originales de
64 celdas, subdivididos sólo donde hace falta en sectores de hasta 4 m con colisión fina.
Los bordes comparten las mismas muestras globales; cada heightfield se rellena con valores
sin colisión fuera de su tamaño útil para satisfacer el múltiplo de cuatro de Jolt.
Los cambios ocurren entre pasos físicos. Restaurar recupera el collider original y libera los sectores.

La suma de bits de SubShapeID de los dos niveles y las celdas queda limitada a 32: en mapas
enormes de mods se reduce la subdivisión si hace falta. Los mapas motocross y circuito mantienen
5 cm. No se altera el terreno original ni la física cuando no se activa la deformación.
`soilcheck` incluye 625 rayos distribuidos alrededor de una costura, comparación de la altura
con Jolt, ancho de huella, volumen, restauración, 60/120 Hz, teletransportes y pavimento.

### v0.3.0: la excavación se frenaba en su propio surco

`PressSoil` rechazaba pendientes con normal Y menor a 0.55, pero medía la normal ya deformada.
El borde de una huella profunda pasaba ese umbral y bloqueaba nuevas pasadas en tierra excavable.
Ahora el filtro de taludes mide el terreno base. `soilcheck` busca tierra de pista, crea un surco
profundo y vuelve a presionar su borde: fallaba antes del cambio y pasa después.

Además, el apoyo de la rueda conserva `onTerrain` desde la consulta de suspensión. La excavación
y los efectos locales usan ese dato, en vez de inferirlo por una diferencia de altura menor a 8 cm:
el contacto tangente de una cubierta sobre una huella curva puede superar esa diferencia.
Los apoyos sobre cajas y rampas siguen excluidos. El dato no se transmite por red; protocolo 7 intacto.

### v0.3.1: huellas al rodar sin patinaje

La compactación quieta estaba limitada a 2.5 cm antes de blandura/superficie y avanzaba a sólo
0.04 m/s: en una pasada de pocos milisegundos casi no se notaba. `PressWheel` ahora calcula la
velocidad recorrida (excluye teletransportes) y `PressSoil` suma compactación proporcional a la
distancia. El límite rodando aumenta a 10 cm antes de blandura/superficie; parado conserva el
límite de 2.5 cm. Derrapar sigue agregando excavación hasta la profundidad elegida.
Con 65% de blandura, 1100 N y sin patinaje: 47.7 mm en pista y 21.5 mm fuera de ella por pasada
a 18 y 36 km/h. Verificado a 60/120 Hz con `soilcheck`. No depende de subir el preset ni de
borrar las preferencias anteriores; blandura cero sigue desactivándolo.

### El surco no tiene que manejar la moto (suelo deformable)

Pedido: "que la deformación de tierra no afecte tanto al grip y la conducción, porque se maneja raro cuando se
deforma; visualmente me encanta". Es un cambio de física pedido a propósito, pero **sólo con la deformación
física prendida** (`--ruts`, o Deformación "Física" manejando uno mismo): sin ella la regresión da idéntica.

**Qué pasaba** (`--map prueba/plaza_tierra`, desde `pruebas/`, con `--ruts` contra sin surcos; motocross):

| prueba | sin surcos | con surcos (antes) |
|---|---|---|
| `accel`: t a 60 km/h / velocidad a los 5 s / metros a los 5.5 s | 3.03 s / 95.5 / 75.0 | 3.71 s / 80.5 / 57.5 (rebotes, trasera en el aire 5%) |
| `brakestraight` (freno trasero a 32 km/h) | frena derecho | se da vuelta a los 3 s (caída) |
| `circleN`, N = 6 a 16 (dirección a fondo a los 4 s, roll ~47°) | radio 3.6 a 32.9 m, 0 caídas | **caída a los 5-6 s en todas** (al salir del surco recto de la arrancada) |
| `bajada45` cruzando un surco de 14 cm a 10, 25, 60 y 90° | derecho | caída antes de llegar |
| bot en la pista de motocross, 10 vueltas | 1:08.29 - 1:08.73 | 1:43, 1:57, 2:23, 2:10, 2:06 (caídas y reapariciones) |
| bot en Los Médanos, 600 s | 6 vueltas de 97.7 - 98.0 s | ni una vuelta |

**Por qué** (medido con `--wheel-log`, no supuesto):
- La malla, la colisión de Jolt y lo que leía `BikePhysics` (`terrain.Height`/`Normal`, 3 sitios por rueda más
  la normal bajo el centro de masa) eran el mismo relieve de 5 cm: surco de hasta 24 cm de hondo y 20 de ancho
  con paredes casi verticales, más bancos de 8 cm. La cubierta se apoya en un plano tangente y veía normales
  como `(-0.55, 0.83, 0.01)` (empuje de costado de miles de N: el surco "encarrila" y al doblar la rueda
  trepa la pared) y alturas del punto de contacto que cambian 5-10 cm en un par de commits.
- Aunque las paredes no estuvieran, la cubierta es un resorte de 400 kN/m (1 mm = 400 N; la carga estática son
  2-3 mm) con 3500 N·s/m al comprimirse y **20000 al descomprimirse**, y la rueda pesa 11-14 kg: si el suelo se
  aleja a más de ~5 cm/s (`carga / 20000`) la fuerza da 0, y rebota a ~27 Hz con casi nada de amortiguación
  (2%). Una huella nueva baja el suelo bajo la cubierta 5-7 cm en 4 pasos (2 m/s) y su rugosidad (tacos de
  10 cm, ruido de a mm por nodos, y la carga que fluctúa y se graba en la profundidad de la vuelta siguiente:
  un lazo) la hacía rebotar (`cub` de 0 a 8000 N), y una moto al límite en una curva se caía.
- No era la colisión de Jolt: con los bancos fuera de ella (o dentro) la moto se maneja igual una vez
  arreglado lo anterior (medido: radios idénticos a 0.05 m), así que no se tocó.

**Qué se hizo** (`Terrain::RideHeight/RideNormal/BankNormal/StepRide`, `TerrainSoil.cpp`; lo usa sólo `BikePhysics`):
la rueda no lee el relieve de la malla sino una versión suya, y **la malla, el shader, las huellas, los
terrones, el polvo y los ajustes de Tierra no cambian** (ni `PressSoil`, ni la colisión de Jolt, ni el protocolo):
1. Sólo cuenta lo hundido: los bancos no los siente (`max(0, -altura)`).
2. Se promedia con un núcleo gaussiano de 9x9 puntos, sigma `rut_ride_blur` = 25 cm: el surco es una
   depresión suave y ancha, sin paredes ni rugosidad.
3. Tope blando `rut_ride_depth` = 2 cm (`tanh`): la rueda se hunde como mucho eso, aunque el surco tenga 24.
4. Con demora: el suelo de las ruedas (`soilRide`, una copia de `soilHeights`) sigue a la malla a
   `rut_ride_sink` = 2 cm/s como mucho (`StepRide`, una vez por paso desde `TerrainDeformation::CommitRuts`):
   pasando encima no se siente cómo se cava; la trasera siente la huella de la delantera, y parada, la moto se
   hunde de a poco.
5. La normal se saca por diferencias finitas a ±10 cm de ese suelo (la derivada exacta de la suma de muestras
   lineales salta en cada borde de triángulo y devolvió las caídas), y la del centro de masa (peralte para el
   límite de la curva, `bank_turn_gain`) es la del terreno sin surcos (`BankNormal`): un surco no es un peralte.
6. En las sondas de los objetos (escaleras, rampas) la normal del terreno se calcula sólo si hay algún objeto
   (era cara con el surco suavizado; sin surcos da lo mismo).

`rut_ride = 0` devuelve todo lo de antes (la rueda ve la malla tal cual). Con `soilHeights` vacío (sin surcos) o
`rut_ride = 0`, `RideHeight` y `RideNormal` dan exactamente `Height` y `Normal` (bit a bit); comprobado: con
`rut_ride = 0` `circle12 --ruts` vuelve a caerse a los 5 s y `accel --ruts` da 3.71 s a 60 km/h y 57.49 m, como antes.
Claves (comentadas con su valor por defecto) en `tuning.ini` y en la tabla de `MODDING.md`.

**Cómo se comprobó** (con y sin `--ruts`, misma moto y mapa; R = radio, de `v / (wy / cos roll)` en la vuelta
8-60 s; "surcos previos" = N pasadas patinando hechas antes de la prueba, sólo con una copia de medición, no
está en el código):
- `circleN` 60 s, N = 6, 8, 10, 12, 14, 16: 0 caídas (antes 6 de 6); radio contra sin surcos +3.0, +1.4, +0.8,
  +0.7, -0.3, -0.5 %; sin ratos en el aire.
- `accel`: 3.06 s a 60 km/h (3.03), 74.94 m a los 5.5 s (75.01), frenada 57.42 m (57.47); con 6 pasadas previas de
  surco de 14 cm, 74.98 / 57.47; con 20 pasadas patinando (24 cm), 75.04 m y 56.54 m (-1.6%).
- `brakestraight` y `brakeslide` con surcos: igual que sin ellos (giro del derrape -84.0° contra -83.6°; con 6
  pasadas previas debajo, -103°: la moto cruza el surco de costado).
- `bajada45` cruzando el surco a 10, 25, 60 y 90°: 187.9 m en 16 s como sin surco, balanceo máximo 0.2°.
- Otras motos (`circle6`, `circle10` de la de carreras, la Trilheira y la trial): radio +0 a +4%, 0 caídas.
- Bot, motocross, 10 vueltas con `--ruts` (surco de hasta 15.6 cm): 1:08.34 - 1:08.93 (sin surcos 1:08.29 -
  1:08.73). Bot en Los Médanos, 6 vueltas: 97.47 - 98.00 s (97.74 - 98.02).
- Sin surcos, `tools/regresion.sh` contra el exe anterior: idéntico en las 9 pruebas de la lista de siempre
  y en el bot de 150 s, `accel`, `airlean`, `crashloop`, la favela, el circuito con la de carreras, Médanos y el
  Valle. `--test soilcheck` (`--flat`, `base/circuito`, `base/motocross`, `sandbox/medanos`, `base/favela`): 0
  fallos; se le agregaron chequeos: las ruedas hunden como mucho `rut_ride_depth`, no sienten los bancos (16-37
  mm en la malla), su normal no se inclina en el surco (0.9995 contra 0.4-0.7 la de la malla) y sin surcos ven
  exactamente `Height` y `Normal`. El resto de la prueba no cambió (la colisión sigue igual a la malla).
- Costo: `RideDelta` 7 us por llamada, ~20 llamadas por paso con surcos (~2% de un cuadro); 5.6 ms por cuadro
  sin `--ruts` y 5.8 con (5.7 con la versión anterior).
- Lo que se ve: capturas de la rueda en un surco de 24 cm (20 pasadas) contra la versión anterior, lateral,
  de atrás y en marcha: la rueda queda en el surco (2-3 cm más arriba de donde se hundía antes), sin flotar ni
  enterrarse.

**Qué mide cada parte** (`circleN` 6-16 con la versión final, quitando una cosa a la vez): sin demora
(`rut_ride_sink = 0`) no cae pero rebota (aire 0.4-4%, roll hasta 51°); sin tope (`rut_ride_depth = 0`) 1
caída de 6 y radio +8.9%; sin las dos, 6 caídas; con sigma 15 cm en vez de 25, 0 caídas pero +4.5% en el
círculo más chico (25 cm da +3.0%). Al principio, con sigma de 10 cm (7x7 puntos) seguía cayendo aun con el
tope de 3 cm y la demora de 2 cm/s, y con 15 cm sin tope caían 4 de 6.

**Lecciones**
- La cubierta de esta moto es un sensor de altura de 400 N/mm: lo que se le muestre como suelo tiene que ser
  liso a nivel de mm y no alejarse a más de ~5 cm/s. Un relieve "que se ve bien" a 5 cm de resolución no sirve
  para la física de la rueda, aunque no tenga paredes.
- `circleN` (dirección a fondo, roll de 47°) es el detector: al límite de agarre cualquier rebote es una caída.
  Sirve para probar cualquier cosa que toque el suelo que ven las ruedas.
- Probar de a una cosa: la primera hipótesis (paredes y normales) sólo arregló la línea recta; los círculos
  necesitaron el tope y la demora. Y la colisión de los bancos, que parecía culpable, no lo era.
- Trampas al editar: un `\n` dentro de un heredoc de bash o de Python llega como salto de línea de verdad
  (usar la herramienta de archivos); tras tocar `Terrain.h`, tocar todos los `.cpp` antes de compilar.
- `StepRide` se llama desde `CommitRuts`: quien use `PressSoil` sin él (como `soilcheck`) tiene que llamarlo.

**Pendiente / dudoso**: los surcos ya no "encarrilan" la moto ni la frenan (era lo que se pidió); si se quisiera
un poco de eso, subir `rut_ride_depth` (2 cm da +3% de radio al límite; 4.5 cm, +6%). El suelo de las ruedas
sólo existe con la deformación física prendida; no viaja por red. No se compiló en Mac (código C++17 estándar).
