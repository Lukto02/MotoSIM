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

### Cambiar de lado: la moto no tenía peso (a pedido, v0.4.1; todas las motos, también la 450)
- **Pedido**: "con joystick, al cambiar de dirección repetidamente con alguna moto, es como que la moto no tiene peso y salta de
  un lado al otro. Revisá las motos y fijate que cambien de dirección realísticamente tanto con joystick como con teclado, es
  como si se inclinaran demasiado rápido". Autorizado a cambiar todas las motos, también la 450.
- **Cómo se midió**: prueba nueva `slalomN[fF][xS][cC][k]` (Game.cpp: llega a N km/h, la sostiene 1.5 s y tira la dirección de
  lado a lado F veces por segundo, C ciclos; sin `k` de golpe como el stick, con `k` por las rampas de la D y la A) y
  `tools/slalom.py` (por cada cambio: rolido máximo, aceleración de rolido de la derivada de `wr` con una fila por paso,
  cuánto tarda en cruzar el 0 y en llegar al 90% del recorrido, cuánto se pasa, y la oscilación: ± y centro);
  `tools/rolido.py` barre motos × velocidades × modos (`cambio`: un cambio entero cada 2 s con el stick; `cambiok`: con el
  teclado; `s1`/`s2`: slalom a 1 y 2 Hz; `s1k`; `medio`: el stick a medias). Las de tierra en `prueba/plaza_tierra`, las de
  calle en `prueba/plaza_asfalto_ancha`, de 30 a 200 km/h. El joystick no se puede enchufar acá: el stick es la dirección
  de golpe (`ReadPlayerInput` la pasa sin rampa), que es lo que hace `slalomN` sin `k`.
- **Qué pasaba** (v0.4.0, cambio entero de lado a lado, ±46-55°):
  - con el stick las de tierra rolaban **190-220°/s** con **3000-7600°/s²** de aceleración de rolido y la Carrera
    **300-430°/s** con **10 000-19 000°/s²**; la Naked **450-550°/s** con **16 000-23 000°/s²** y **se caía en casi cada
    cambio** (a 30, 60, 100 y 150 km/h): al cambiar de lado la moto **se levantaba del piso** (delantera y trasera a
    10 000 N un paso y a 0 el siguiente), volaba rolando a 9 rad/s y caía acostada a 90°: "salta de un lado al otro";
  - con el teclado (la rampa de la D, 0.45 s de un lado al otro) la mitad: 140-150°/s y ~900°/s² las de tierra, 180°/s la
    Carrera, 230°/s la Naked. O sea que el stick y el teclado daban motos distintas;
  - con el stick a medias (`medio`), las de tierra se pasaban de la inclinación 8-10°.
- **Por qué**: el balance es un PD que lleva el rolido a la inclinación pedida (`leanTarget`), y esa inclinación sale del
  manubrio tal cual: con el stick salta de +50° a −50° en un cuadro. El PD mete enseguida su torque máximo
  (`balance_max_torque`): 2600 Nm sobre 26 kg·m² son 100 rad/s² (5700°/s²) en la 450; la Naked, con el balance subido para
  tirarse rápido (K 12 000, tope 10 000 Nm sobre 24 kg·m²), 417 rad/s². Y el torque rota el cuerpo alrededor de su centro de
  masa, no de la línea de las ruedas: a esa aceleración las ruedas barren de costado y se clavan en el piso (por eso la
  Naked salía despedida). Una moto de verdad no puede: cambia de inclinación **contravirando** (el manubrio corre el contacto
  de costado y el peso hace el resto), así que el rolido acelera como mucho lo que da el agarre usado para eso, y el
  giroscópico de las ruedas pide cada vez más fuerza en el manubrio para el mismo rolido a más velocidad.
- **Contra las de verdad** (órdenes de magnitud de memoria, no buscados; como en "Velocidad de giro"): en la calle se rola
  20-40°/s, manejando rápido 40-70°/s, y en la pista, en una chicana, 80-120°/s de pico (de 45-55° de un lado al otro en
  ~0.8-1.2 s); la aceleración de rolido de un contraviraje con todo el agarre es a lo sumo ~a/h (aceleración lateral sobre la
  altura del centro de masa): 9.5-12 m/s² / 0.6 m ≈ 15-20 rad/s² (900-1100°/s²), y un piloto usa menos. A más velocidad cuesta
  más (el par en el manubrio para un rolido dado crece con la velocidad: el máximo cae ~1/v arriba de ~100 km/h). Tirarse a la
  curva (de derecho a acostada): 0.5-1 s.
- **Qué se hizo** (claves nuevas; en `tuning.ini` valen lo de antes, guardadas con `if`; en las dos físicas):
  - **`lean_rate` / `lean_accel` / `lean_rate_speed`** (`Bike::ShapeLean`, BikePhysics.cpp): la inclinación que pide el
    manubrio pasa por una referencia con peso. Es un sistema de 2º orden **críticamente amortiguado** (arranca, acelera y frena
    el rolido sin pasarse) cuya frecuencia baja cuando falta mucho, lo justo para que la aceleración al arrancar sea
    `lean_accel` (× la curva que puede hacer el manubrio a esa velocidad y en ese suelo sobre `max_lateral_accel`: a paso de
    hombre o con poco agarre, menos; entre 0.25 y 1.5), con frecuencia máxima 12 rad/s para los cambios chicos, el rolido
    hasta `lean_rate / √(1 + (v / lean_rate_speed)²)` y antes un filtro de 0.05 s para el escalón del stick. Un cambio chico
    tarda poco y uno grande más, igual con el stick que con el teclado;
  - el balance **amortigua el rolido respecto de la referencia** (`rollRate − leanRefRate`, en vez de todo el rolido) y le
    suma el torque de acelerar el rolido como ella (`inertia_roll × leanRefAcc`): sin eso la moto iba ~0.1 s atrás de lo
    pedido y la amortiguación frenaba justo lo que se pedía. Los recortes de la inclinación pedida (frenar enderezando,
    derrapar, el freno de mano) siguen al instante como antes, después del filtro (ver "Probado y descartado");
  - sólo con **las dos ruedas en el piso** (o fuera hace menos de 0.15 s: un bache). En el aire, en una rueda o caída, la
    pedida tal cual y el balance de antes;
  - **`stoppie_upright`**: apoyada sólo adelante y de trompa (8-20° de nariz abajo), lo que pide el manubrio no la acuesta
    (sin la trasera, acostarla sólo la hace girar sobre la delantera). Sólo en la Trial (abajo);
  - valores: las de tierra (450, 600, 2T, Trilheira) 2.2 rad/s, 16 rad/s², 25 m/s; la Trial 2.4 / 16 / 20 (liviana y lenta) y
    `stoppie_upright` 1; la Carrera 2.2 / 16 / 32; la Naked 2.3 / 18 / 30 (un poco más viva que la Carrera, por el manubrio
    ancho) y **el balance de la Carrera** (6000 / 1600 / 6000 en vez de 12 000 / 1800 / 10 000: lo que la hace cambiar de lado
    ahora es `lean_rate`, y con su balance de antes el slalom daba casi lo mismo: 112 contra 104°/s y 1.03 s a 100 km/h).
- **Probado y descartado**:
  - **"frenar justo para llegar"** (acelerar al máximo y frenar con la curva de frenado, tiempo óptimo): de un lado al otro
    perfecto, pero con el stick de lado a lado más rápido de lo que la moto puede seguir **se iba corriendo para un lado**
    (a 1 Hz, oscilando alrededor de +30°): el primer medio ciclo arranca de derecha y como nunca llega, nunca se compensa. Lo
    mismo con un tope duro de la aceleración sobre un 2º orden fijo, y con la frecuencia programada a 1.4 o 2 veces la justa
    (+13 a +25° de centro en 3 ciclos). Con la justa (la que quedó) se centra sola en ~2 ciclos;
  - **un 2º orden lineal solo** (sin topes, siempre centrado): todos los cambios tardan lo mismo, chicos o grandes (0.86 s
    para 9° o para 92°), y las correcciones chicas se sienten lentas; para un cambio grande en ~1 s pasa los 150°/s de pico;
  - **el filtro después de los recortes** (frenar enderezando y derrapar también con peso, más consistente): el bot de la
    Trilheira en la favela se caía 3-10 veces cada 15 min (0-1 con el filtro antes) y la 450 daba 1:05.08 en la pista;
    **la amortiguación respecto de la referencia recortada** (con el filtro antes): `alreves.py` de la 450 vuelve a 1 de
    129, pero la Trilheira en la favela se cae en 22 de 64 vueltas (con lo que quedó, 9; antes del cambio, 1 de 48);
  - **la ayuda en una rueda**: con la referencia también apoyada sólo adelante, la Trial que aterrizaba del doble de la pista
    de motocross en stoppie seguía rolando a la par de la referencia (9-12 caídas cada 15 min); sólo con las dos ruedas no
    alcanzó: el bot pedía 17-50° con la moto apoyada en la delantera de trompa (de ahí `stoppie_upright`). `stoppie_upright`
    en todas: la 450 en la favela pasaba de 0 a 1-4 caídas (en la curva de s≈258-266 no podía doblar de trompa); quedó en la
    Trial;
  - **para la 2T en la S de la largada de la favela** (abajo): rolido más rápido (2.6-6 rad/s, 20-100 rad/s²), sin el filtro
    de 0.05 s, sin el torque de anticipación, sin la amortiguación respecto de la referencia, `lean_load_flat`, sin
    `balance_fast`, y un bot que anticipa (apunta desde donde va a estar en 0.1-0.3 s): nada la deja como antes. Hasta con la
    referencia casi instantánea (6 rad/s, 100 rad/s², sin el filtro de 0.05 s) se cae 24 de 40 veces.
- **Se comprobó** (antes = v0.4.0; con el stick, un cambio entero de lado a lado, ±46-55°; entre paréntesis, con el teclado):

  | moto | km/h | rolido máx. (°/s) antes → después | aceleración de rolido máx. (°/s²) antes → después | 90% del cambio (s) antes → después |
  |---|---|---|---|---|
  | 450 | 30 / 60 / 100 | 222 / 198 / 204 (152 / 146 / 148) → **120 / 121 / 104** (105 / 113 / 101) | 3800 / 5000 / 5000 (840-990) → **620 / 1210 / 1120** (520-810) | 0.77 / 0.77 / 0.88 (1.11 / 1.06 / 1.11) → **1.00 / 0.96 / 1.10** (1.20 / 1.13 / 1.21) |
  | 600 | 30 / 60 / 100 | 221 / 197 / 203 → **120 / 121 / 105** | 3300 / 2900 / 3500 → **620 / 810 / 960** | 0.78 / 0.77 / 0.94 → **1.01 / 0.96 / 1.10** |
  | 2T | 30 / 60 / 100 | 205 / 186 / 189 → **115 / 119 / 105** | 3200 / 3000 / 3100 → **650 / 830 / 940** | 0.79 / 0.79 / 0.90 → **0.99 / 0.94 / 1.09** |
  | Trilheira | 30 / 60 / 100 | 205 / 190 / 194 → **117 / 122 / 109** | 3700 / 3200 / 3300 → **690 / 1020 / 990** | 0.81 / 0.78 / 0.88 → **1.03 / 0.95 / 1.11** |
  | Trial | 30 / 60 / 70 | 214 / 193 / 195 → **117 / 117 / 111** | 6500 / 7600 / 5500 → **610 / 1250 / 870** | 0.61 / 0.71 / 0.73 → **0.99 / 0.96 / 0.99** |
  | Carrera | 30 / 60 / 100 / 150 / 200 | 299 / 318 / 326 / 335 / 431 (178-180) → **117 / 118 / 103 / 86 / 76** (105 / 108 / 97 / 85 / 74) | 10 700-18 900 (1400-2100) → **980-1530** (760-1400) | 0.47 / 0.48 / 0.51 / 0.82 / 0.73 (1.01-1.07) → **1.03 / 1.02 / 1.10 / 1.29 / 1.54** (1.17-1.62) |
  | Naked | 30 / 60 / 100 / 150 | 451 / 523 / 535 / 547, **se caía en cada uno** (215-232) → **121 / 122 / 104 / 86** (107 / 112 / 100 / 86) | 16 000-23 000 (2000-3500) → **1180-1460** (840-1440) | 0.57-0.60 (0.72-0.76) → **0.95 / 0.94 / 1.03 / 1.22** (1.10-1.32) |

  - **ya no salta**: la Naked a 100 km/h, cambiando de lado dos veces, estuvo 546 pasos sin las dos ruedas en el piso y con
    11 000 N en cada una; ahora ninguno y 2350 N como máximo;
  - el stick y el teclado ahora dan casi lo mismo (el teclado 0.1-0.2 s más, por su rampa); se pasa de la inclinación 1-4°
    (la 450 con el stick a medias, 8-10° → 2-5°; la Carrera y la Naked 0.5-2°); con el stick de lado a lado a 1 Hz, ±22-28°
    alrededor de ~+11° los primeros 2 ciclos y después centrado; a 2 Hz, ±5-10°. La aceleración de rolido medida (hasta
    ~1500°/s²) pasa la de la referencia (16-18 rad/s², 920-1030°/s²) en los picos de las cubiertas, no en la tendencia;
  - tirarse a la curva (`giroN`, 90% de la inclinación, stick / teclado): 450 0.66 / 0.79 → 0.74 / 0.81 s, Trial 0.66 / 0.81
    → 0.69 / 0.76, Carrera 0.50 / 0.64 → 0.69 / 0.76, Naked 0.31 / 0.46 → 0.63 / 0.71 (de verdad: 0.5-1 s); la curva sostenida,
    igual (la misma inclinación, radio y g); `circle12` 55° máx. → 50° (no se pasa), los mismos 50.1° sostenidos;
  - bots, 900 s (vueltas / caídas; antes → después): 450 en la pista 13/0 → 13/0, **1:04.41 / 1:04.24 → 1:04.51 / 1:04.33**;
    600 13/0 → 13/0, **1:04.23 / 1:04.34 → 1:04.64 / 1:04.45**; Trial 13/0 → 13/0 (1:04.94 → 1:04.89); 2T 13/0 → 13/0 (1:04.28
    → 1:04.33); Trilheira 13/0 → 13/0 (1:05.07 → 1:04.77); la 450 en la favela 9/0 → 9/0 en 4 muestras (**1:36.71 → 1:36.98**);
    la Carrera en el circuito 6/0 → 6/0 (**2:10.86 / 2:09.19 → 2:11.28 / 2:09.60**); la Naked en el circuito 6/0 → 6/0 (2:14.46
    → 2:15.21); park 17/0 (0:50.87 / 0:50.90 → 0:50.91 / 0:50.98); valle 21/0 (0:41.01 → 0:41.09); autopista con tráfico
    idéntica (6:50.40 la Naked, 6:50.48 la Carrera); Los Médanos sin caídas; la Carrera en la favela 1-5 → 0-3 caídas cada
    15 min, la Trial en la favela 4 → 5 (las de siempre, s≈253-267);
  - `alreves.py`: la Carrera 1 de 215 → 0, la Naked 1 de 129 → 0, la Trilheira en tierra 61 → 60 de 129 (los de siempre), la
    450 en tierra 1 → 4 de 129 (soltar la D y clavar a 60-90 km/h: 0.3 s, justo el umbral, inclinada 9-15° con el camino
    girando 6-9°/s para el otro lado; el camino total queda en ±2°);
  - el whip (`whipkick50`: la 2T despega a 38° con −1.67 rad/s de guiñada, igual; la 450 a 40° en vez de 45°), el wheelie, el
    grau de la Trial y la Trilheira (`graucurva`), el mortal (`flip`), `airrot`, el freno de mano (`derrape.py`: la misma cola,
    2-5° menos de inclinación máxima) y los trucos del bot (`--trucos`: las mismas caídas en los saltos chicos), iguales;
  - `tools/regresion.sh` contra la v0.4.0: con los `.ini` de la v0.4.0 (las claves en 0) **idéntico** en 17 pruebas (las 9 de
    siempre, `accel`, la Naked en la autopista, la Carrera en el circuito, la Trial, la 450 en la favela, la 600, el park y la
    Trilheira en la favela). Con los nuevos, distinta a propósito en las que doblan (`brakeslide`, `cuerpo`, `circle12`, el bot,
    `flip`); en las derechas (`wheelie`, `brakestraight`, `airrot`) sólo en los centésimos; `whip` idéntica.
- **Pendientes**:
  - **la 2T en la favela** (no es su mapa): 0 → 8-13 caídas cada 15 min (4 muestras), casi todas en la S de la largada
    (s≈1215 → 25): el bot llega a un lomo acostado ~50° y al aterrizar tiene que pasar a −40°; con el rolido con peso no llega y
    pega en una casa en s≈18-24. Con distintas velocidades del bot (`--bot-lat` 5-7), antes 3 de 21 y ahora 7-12 de 21: el
    lugar ya estaba al límite. Es el plan del bot en la favela (MAPAS) o su ley (persecución pura, sin anticipar:
    `Game::BotInput`); en la pista, el park y el valle la 2T no se cae;
  - **la Naked en la pista de motocross** (no es su mapa): sin caídas, pero 9-11 vueltas en 15 min (1:10-1:44) contra 13
    (1:05.6-1:06.2): a veces cae corta del doble de s≈425 y sale despacio del pozo;
  - la Trilheira en la favela: 0 → 0-1 caídas cada 15 min (9 en 64 vueltas con la masa corrida; antes 1 de 48), en la pared
    de s≈253 (la de siempre);
  - la 450 en `alreves.py` (arriba): 3 casos más, en el umbral.
- **Lecciones**:
  - **Una inclinación pedida que salta es un torque que salta**: el balance no tenía la culpa, sino lo que se le pedía: con el
    stick, de un lado al otro en un cuadro. Y el torque del balance gira la moto alrededor de su centro de masa: a cientos de
    rad/s² las ruedas barren de costado y la levantan del piso. Lo físico es limitar lo pedido (el contraviraje), no ablandar
    el balance (con menos K la moto sostiene menos y se pasa).
  - **Medir con una fila por paso**: la aceleración de rolido sale de la derivada de `wr`; con la telemetría cada 0.05 s se ve
    el rolido máximo pero no el golpe.
  - **Un limitador "óptimo" no se centra**: acelerar al tope y frenar justo, o cualquier sistema saturado, con un pedido que va
    y viene más rápido de lo que puede seguir, se corre para el lado del primer medio ciclo y no vuelve. Un 2º orden casi
    lineal (la frecuencia programada para no pasar la aceleración máxima) sí: probar siempre el slalom rápido, no sólo el
    cambio entero.
  - **La amortiguación tiene que ser respecto de lo que se mueve**: con una referencia que se mueve, amortiguar todo el rolido
    frena lo que se pide; respecto de la referencia, la moto la sigue. Si después se recorta la inclinación (frenando), la
    referencia ya no es la que se busca: hay que decidir qué ve cada término (acá, la del manubrio: la que menos cambió al bot).
  - **Lo que vale con las dos ruedas no vale en una**: el contraviraje es con las dos en el piso; en una (stoppie, wheelie,
    aterrizaje de trompa) la ayuda tiene que ser la de antes. Y apoyada sólo adelante, acostarla no dobla: la hace girar.
  - **Para comparar caídas de bot, muestras independientes de verdad**: arrancar el bot desde 40 `s` distintos no sirve (en
    100 m todos convergen a la misma línea y el resultado es uno solo repetido); barrer `--bot-lat` (otra velocidad, otra
    línea) y correr la masa ±0.01-0.04 kg sí.

### La 450 y la 600: el trompo frenando con la S (a pedido, v0.3.10; cambia la física de la 450)
- **Pedido**: "arreglá el trompo de la 450 y la 600": frenando fuerte con la S y un poco de dirección hacían un trompo (igual en
  la v0.3.8). Que frenar fuerte doblando abra la curva o haga un derrape controlable, y que la frenada recta y la sensación de
  la 450 se mantengan.
- **Cómo se midió**: `tools/alreves.py` en `prueba/plaza_tierra` y `prueba/plaza_asfalto` a 60, 90 y 120 km/h (129 casos: el
  stick fijo de 0.06 a 1 y los guiones de teclado), contando además los casos con la deriva máxima de más de 30° (sin los del
  freno de mano, `trasero*`, que derrapan a propósito); `frenacurva90xS` con `--flat`; las frenadas rectas `frenadaN`
  (`tools/frenada.py`); la telemetría cada 0.02 s.
- **Qué pasaba** (`frenacurva90x0.1`, la 450 en `plaza_tierra`): a los 0.3 s de clavar la S la trasera se levanta (stoppie,
  `rN` 0, cabeceo −12°); apoyada sólo en la delantera la moto gira sola (la deriva pasa de −5 a −22° en 0.45 s; el
  `stoppie_yaw_damping` sólo frena la velocidad de giro, no la corrige); la trasera vuelve a apoyar cruzada 24°, el
  contravolante automático (`caster_align`) clava el manubrio a −31° y la cola sigue hasta 150°. `alreves.py`: la 450 88 de
  129 casos malos en la tierra y 84 en el asfalto (86 de 108 con más de 30° de deriva, hasta 164°); la 600, 83 y 82 (81, hasta
  162°). Con la S, 140-168° en `frenacurva90x0.1`; con el teclado, soltando la D y clavando, 100-150°.
- **Por qué**: lo mismo que "La motocross: frenando fuerte hacía un trompo" (abajo): con la trasera descargada frenar es
  inestable de guiñada. Se había arreglado con `brake_align`, pero la física 031 (`original_031`, la 450 y la 600 desde la
  v0.3.1 recuperada) no lo tiene.
- **Qué se hizo**: `brake_align` portado a `BikePhysics031.cpp`, igual que en `BikePhysics.cpp` (la moto vuelve a apuntar hacia
  donde va frenando fuerte, sólo el giro que sobra respecto del camino; frenando derecho la delantera sigue su camino y no
  contravolantea), detrás de `brake_align > 0` (la 031 no lo leía: sin la clave, idéntica). El costado para medir la deriva y el
  camino de la delantera es el perpendicular al rumbo (lo de `flat_side_exact`, ver "Frenando y doblando rápido..."; en la 031
  es código nuevo, no hay un "antes"). `motocross.ini` y `motocross600.ini`: `brake_align` 4, `brake_align_torque` 2000, como
  las otras de tierra.
- **Se comprobó** (con lo de "En wheelie doblando la moto iba de costado", abajo, también prendido):
  - `alreves.py`: **la 450 88 → 1 de 129 en la tierra y 84 → 1 en el asfalto** (el que queda: soltar la D y clavar sólo el
    trasero a 120 km/h, −4.7° de camino, igual que antes: el freno de mano); deriva de más de 30°: 86 → 0, la máxima 7.5°. **La
    600, 83 → 5 y 82 → 5** (los del trasero solo, iguales que antes, y uno nuevo: a 60 km/h, con gas y la D 1.5 s antes de
    frenar, la 600 hace un wheelie alto doblando y al clavar el freno de mano se cruza; antes ese wheelie terminaba en caída);
  - `frenacurva90x0.1` con `--flat`: cola 168° → 0.4° (450) y 163° → 0.3° (600), y frena 0.67 g, lo mismo que derecho (antes
    0.79 g contaba el trompo); `x0.2`: camino −24° → +4°, 0.79 → 0.87 g; `x0.5`, 0.88 → 0.86 g y el camino +5° → +30°: dobla;
    con el teclado (la 450 en la tierra), clavando la S sin soltar la D, camino +64 a +86° con 5-7° de cola (antes −22 a +13°
    con 84-132°); soltando la D y clavando, se endereza y sigue derecho: camino −1 a +3°, cola 1-7° (antes −24 a +16° con
    12-153°);
  - la frenada recta, idéntica: `frenada60/90/120` en la tierra y el asfalto, 6.6-7.3 m/s², cola 0°, 77-94% en el aire;
  - `accel` (la frenada recta del final): antes la moto se cruzaba 15.6° (17.5° de máxima) al final; ahora 0°;
  - bots: la 450 en la pista **1:04.41 / 1:04.24, idéntica** (150 s; 900 s, 13 vueltas, iguales al centésimo); la 600
    **1:04.52 / 1:04.34 → 1:04.23 / 1:04.34**; la favela, 4 muestras de 45 min de la 450 (la masa corrida 0.01-0.04 kg) y 30 min
    de la 600: 0 caídas antes y después, las mismas vueltas;
  - `tools/regresion.sh` contra la v0.3.9: idénticas `brakeslide`, `cuerpo`, `flip`, `wheelie`, el bot de 40 y 150 s, `airrot`,
    `circle12` y `brakestraight` (el bot de la 450 en la pista nunca entra en lo nuevo: no frena tan fuerte con la delantera
    apoyada ni dobla en una rueda), y la 600 en `brakestraight` y `wheelie`; distintas a propósito `accel` (la frenada del final), `whip` (aterriza en la
    trasera cruzado: lo de abajo) y el bot de la 600. Sin las claves (los `.ini` de la v0.3.9), todo idéntico.
- **La Trilheira (probado, no se tocó)**: sus 61 de 129 casos (60/90/120 km/h; 38 de 86 a 60/90) son otra cosa: en el stoppie,
  soltando la D y clavando, queda 1-2 s inclinada 8-12° hacia la curva con el camino yendo para el otro lado a 5-15°/s. El
  `--wheel-log` muestra la trasera trabada y casi sin carga (130 N) derrapando de costado, la delantera apuntando 3.6° a la
  derecha del rumbo (lo que pide la inclinación) pero rodando 4° a la derecha (la moto cangrejea 3°): empuja a la izquierda y
  sostiene la inclinación. Su `brake_align_free` 0 (puesto por la favela) no deja que la delantera siga su camino.
  - `brake_align_free` 1 sola: 69 casos (peor); con `flat_side_exact` 1: **18** (la delantera libre necesita el costado
    exacto: en un stoppie, con cabeceo de −6 a −20°, el aplanado está torcido grados). Pero en la favela (6 muestras de 45 min,
    la masa corrida, `--map favela --bike ...`) las caídas pasan de 1-3 a 16-23, todas en la pared de s≈253;
  - `flat_side_exact` 1 sola: 67 casos y 13-20 caídas; la delantera libre con el costado exacto sólo para `brake_align` (una
    clave aparte, probada y sacada): 19 casos y 17-21 caídas; con eso y el alineado sólo frenando a más del 85%
    (`brake_align_from` 0.85, `full` 1): 15 casos y 10-12 caídas; con `flat_side_exact`: 15 y 6-9;
  - `brake_yaw_comp` 1 / 0.5: 70 / 66 casos: cancelar el giro que mete el freno en el contacto la pasa al otro lado (cruzada
    igual, inclinada para el otro lado).
  Lo que la arregla en la plaza (que la delantera siga su camino frenando fuerte) hace que el bot de la favela, que frena fuerte
  y dobla poco antes de la pared de s≈253, llegue más abierto. Queda pendiente: habría que mirar ese tramo del bot.
- **Lecciones**:
  - **Un arreglo de una física no está en la otra**: la 031 se recuperó de la v0.3.1, antes de `brake_align`; el `.ini` de la
    450 de la v0.2.7 tenía `brake_align` 4, el de la v0.3.1 no, y la 031 tampoco lo leía. Antes de buscar la causa de algo que
    "se había arreglado", fijarse si el código que lo arreglaba corre para esa moto.
  - **La "g" de una frenada con trompo engaña**: de costado la moto frena más (0.79 contra 0.67 g); comparar con la frenada
    recta, no con el número de antes.
  - **El bot de la favela de la Trilheira es la vara de cualquier cambio en la frenada fuerte**: en la plaza todo mejora y en la
    favela se cae 5-10 veces más; medirlo con varias muestras de 45 min (ver "La Trilheira: frenando fuerte hacía un trompo").

### En wheelie doblando la moto iba de costado (a pedido, v0.3.10; la 450, la 600, la 2T y la Trial)
- **Pedido**: "cuando hacés wheelie y doblás fuerte, no parecería que la moto dobla para donde la rueda apunta"; aclarado: la
  rueda de atrás, la que va apoyada: la trayectoria no coincide con hacia dónde apunta la cubierta, la moto parece ir de costado.
- **Cómo se midió**: con el teclado, `--flat --test teclas:W+15,Wv0.6,WvD3,W1` (arranca, se tira atrás con gas hasta levantar la
  rueda y dobla con la D 3 s), lo mismo desde 25 km/h y con la A desde 20. Un `printf` de prueba (sacado) en la cubierta trasera:
  el ángulo entre la velocidad del contacto y **la huella real de la rueda** (la recta del piso perpendicular a su eje,
  `n × eje`; es lo que se ve: un disco inclinado visto desde arriba es una elipse a lo largo de esa recta) y entre la velocidad y
  el rumbo de la moto proyectado en el piso, que es por donde la física hacía rodar la cubierta. Mediana y percentil 90 en los
  pasos con cabeceo de más de 8° e inclinación de más de 15°.
- **Qué pasaba** (deriva contra la huella, mediana / p90):

  | moto | desde 15 km/h, D | desde 25, D | desde 20, A |
  |---|---|---|---|
  | 2T | 21.0 / 23.1° | 19.2 / 21.9° | 20.5 / 22.6° |
  | Trial | 18.5 / 20.3° | 19.4 / 19.8° | 16.2 / 20.1° |
  | 450 | 15.2 / 20.9° (y trompo) | 13.6 / 17.0° (trompo, 79°) | 12.8 / 17.7° (trompo, 82°) |
  | 600 | 36.9 / 41.3°, caída | 30.3 / 37.6°, caída | 31.4 / 38.4°, caída |
  | Trilheira (grau) | 3.1 / 8.3° | 5.0 / 13.0° | |

  Contra el rumbo, en cambio, la 2T y la Trial daban 2°: para la física la cubierta rodaba casi derecha. (La columna `beta` de
  la telemetría, con los ejes 3D de la moto cabeceada, daba 15-19°: tampoco es la deriva de la rueda.)
- **Por qué**: la cubierta usaba como dirección de rodadura el eje de la moto proyectado en el piso
  (`fwd - n·(fwd·n)`). Es lo mismo que la huella sólo si la moto no está cabeceada respecto del piso o no está inclinada; en un
  wheelie inclinado se separan tan(cabeceo)·sen(inclinación): con 26° de cabeceo y 44° de inclinación, 23°. La física hacía
  rodar la cubierta por el rumbo (y `wheelie_turn`, en la 2T y la Trial con `flat_side_exact`, alineaba ese rumbo con el camino),
  así que la rueda que se ve iba 20° de costado. La 450 y la 600 (física 031) no tenían ni `wheelie_turn`: nada orientaba la
  moto y se cruzaban hasta el trompo. La Trilheira no: su grau alinea con el costado aplanado de la moto, que es perpendicular a
  la huella, así que la rueda que se ve rodaba derecha (y la física la veía cruzada 30-40°, patinando: en el grau no importa).
- **Qué se hizo** (`tire_track_exact`, 0 por defecto = como antes, guardada con `if`; `BikePhysics.cpp` y `BikePhysics031.cpp`):
  - en una rueda de verdad (la trasera apoyada y la delantera en el aire hace más de 0.25-0.4 s, como `wheelie_turn`) e
    inclinada (2-6° o más: derecha la huella y el rumbo coinciden), la cubierta trasera rueda por su huella (`left × n`); la mezcla
    entra y sale en 0.15 s (`trackExactW`);
  - `wheelie_turn` alinea la huella con el camino (la deriva medida contra el costado aplanado de la moto, como el grau);
  - en la física 031, con la clave, `wheelie_turn` y el equilibrio alrededor de la línea del piso en una rueda, como en
    `BikePhysics.cpp` (la 450 y la 600 heredan `wheelie_turn` 900 de `tuning.ini`; la 031 no lo leía);
  - `tire_track_exact` 1 en `motocross.ini`, `motocross600.ini`, `dostiempos.ini` y `trial.ini`. La Trilheira no: ya rodaba por
    su huella y con la clave se cae en el wheelie rápido doblando (la cubierta agarra de golpe lo que antes patinaba).
- **Probado y descartado**: la huella exacta **siempre** (también con las dos ruedas y en la delantera): en la favela la 450, la
  600 y la 2T se caían en la escadaria (0 → 4, 0 → 5 y 0 → 6 caídas en 20-30 min; las normales de los escalones, de costado,
  giran la huella) y la 450 perdía 0.5 s por vuelta en la pista (los whoops). Y sin el umbral de inclinación cambiaban los
  floats de todos los wheelies derechos (el arranque de cada prueba): `cuerpo`, `circle12`, `brakestraight` daban distintas.
- **Se comprobó** (deriva contra la huella, mediana / p90):

  | moto | desde 15 km/h, D | desde 25, D | desde 20, A |
  |---|---|---|---|
  | 2T | 21.0 → 8.0 / 12.4° | 19.2 → 5.7 / 7.3° | 20.5 → 7.7 / 12.9° |
  | Trial | 18.5 → 2.7 / 3.3° | 19.4 → 2.6 / 2.7° | 16.2 → 2.6 / 3.0° |
  | 450 | 15.2 → 9.8° (el trompo sigue, después de apoyar: abajo) | 13.6 → 5.5 / 7.6°, sin trompo | 12.8 → 8.9 / 15.8°, sin trompo |
  | 600 | 36.9 → 7.5 / 33.3°, sin caída | 30.3 → 31.0°, trompo sin caída | 31.4 → 48.7°, trompo sin caída |

  - la inclinación medida sube de 43-46 a 51-56° (la moto dobla con la huella, el ángulo `roll` es el del eje levantado);
  - bots, sin caídas y las mismas vueltas: la 2T y la Trial en la pista (9 vueltas), el parque (11) y el valle (14), la Trial en
    el circuito de obstáculos; la favela 20-30 min: la 2T 0 → 0 caídas, la Trial 4 → 1, la 450 y la 600 0 → 0; la 450 en la
    pista idéntica (150 y 900 s);
  - `tools/regresion.sh` contra la v0.3.9 (con lo de la frenada, arriba): sólo cambia `whip` de la 450 (aterriza en la trasera
    cruzado 50° e inclinado 16° y rueda en una rueda más de 0.25 s: la guiñada de ese momento pasa de 3.1 a 2.7 rad/s; después,
    la misma maniobra con 0.2-0.3 km/h de diferencia); la 2T y la Trial, idénticas al exe de antes en `brakeslide`, `wheelie`, `circle12` y sus bots.
- **Pendiente**: a fondo en un wheelie doblando, la 450 y sobre todo la 600 patinan la trasera (`rSlip` 0.2-0.5, `rUse` 5-16) y
  se acuestan de más (la 600, a 55° de cabeceo, hasta 70°): se cruzan como un derrape con gas (antes la 600 se caía). Eso sí es
  "derrapar la cola con gas", pero llega solo con la W apretada; si molesta, el límite de wheelie o el control de tracción en
  una rueda.
- **Lecciones**:
  - **La dirección de rodadura de una rueda es perpendicular a su eje, no el eje de la moto proyectado**: coinciden con la moto
    sin cabecear respecto del piso; en un wheelie o un stoppie inclinados se separan tan(cabeceo)·sen(inclinación). Es la otra
    cara de "Aplanar un vector de la moto no da el perpendicular al rumbo" (arriba): la referencia correcta depende de qué se
    mide; para lo que se ve de la rueda, su huella.
  - **Medir lo que se ve**: la `beta` de la telemetría y la deriva de la física decían 2° y 17° del mismo instante; la rueda que
    se dibuja iba 20° de costado. Antes de tocar nada, calcular el ángulo con la geometría de lo que se dibuja.
  - **Lo nuevo, sólo donde hace falta**: la huella exacta siempre es más "correcta" pero cambia todo (las escaleras, los whoops);
    limitada a una rueda inclinada arregla el pedido y deja idénticas las pruebas de siempre.

### El manubrio dibujado saltaba 10-15° por cuadro (v0.3.10; sólo el dibujo)
- **Pasaba** (detector de saltos del piloto, ver [PILOTO.md](PILOTO.md), "Saltos de la pose"): manos y antebrazos 4-12 cm por
  cuadro cuando el manubrio saltaba (la Trial derrapando en el bot: `steer -5.6>-15.2>-24.7>-10.3`).
- **Por qué**: la dirección de la física (`steerBase`) es un seguidor con tope de velocidad (`MoveTowards`): la velocidad pasa de
  0 al tope en un paso, y el tope suma tasas (4 rad/s de base, +6 con el derrape lento, +6 con el freno de mano, +6 con la
  delantera libre de `brake_align`: hasta 22 rad/s, 10° por paso); el contravolante automático (`steerCaster`, 10 rad/s) cambia
  de objetivo cuando la trasera toca o se despega. Continua en posición, no en velocidad: en el bot de la Trial (60 s, cada
  0.01 s) la velocidad del manubrio cambia hasta 7.5°/cuadro de un cuadro al otro (4.2° el percentil 99.9).
- **Qué se hizo**: no se tocó la física (cambiaría todas las motos y la 450). Lo que se dibuja (el manubrio, la rueda delantera y
  las manos de la pose) sigue a `steerAngle` con un resorte crítico de 40 rad/s (`Bike::UpdateSteerShown`, `DrawSteer`), que
  `Game::Draw` avanza una vez por cuadro con el tiempo de simulación. Sin ventana, las otras motos (red) y la vitrina, el de la
  física. Simulado sobre la telemetría del bot de la Trial: 7.5 → 1.1°/cuadro de cambio de velocidad, atrás de la física como
  mucho 4.6°. Con 20-30 rad/s, 0.4-0.8° pero hasta 6° atrás.
- **Se comprobó**: la física idéntica (no la lee); el detector, en PILOTO.md.

### Frenando y doblando rápido, la moto doblaba para el otro lado (a pedido; la Carrera, la 2T y la Trial)
- **Pedido**: "a veces cuando frenás y doblás la moto dobla para el lado contrario en ciertas situaciones"; después: "suele pasar
  cuando vas muy rápido, y frenás y doblás al mismo tiempo, la moto termina doblando para el lado contrario del que está
  inclinada" (la de carreras; "pasa hace bastante").
- **Cómo se midió**: `tools/alreves.py` (nuevo, ver [PRUEBAS.md](PRUEBAS.md)): `frenacurvaNxS` (frena a fondo y dobla con el
  manubrio fijo en S, como un stick) de 100 a 280 km/h con S de 0.06 a 1, y guiones de teclado (`teclas:`) con la frenada partida
  en pasos de 0.1 s: la S sin soltar la D, soltándola, esperando 0.15-0.6 s y clavando, sólo el trasero (Espacio), la D a medias
  con toques, todo a la vez desde derecho; entrando (0.3 s de curva), a mitad (0.7) y adentro (1.5). En cada paso, cuánto giró el
  camino y la inclinación: "AL REVES" si el camino va para el otro lado de la D, "CRUZADA" si queda más de 0.3 s inclinada para un
  lado con el camino girando para el otro. Después la telemetría cada 0.02 s y `--wheel-log` (con un `printf` de prueba, sacado)
  en un caso.
- **Qué pasaba** (la Carrera de la v0.3.9; la v0.3.4, antes del rework, igual: no lo trajo el equilibrio por la carga):
  - frenando con poco manubrio a 150-280 km/h la moto se acostaba 12-23° para la derecha y el camino se iba para la izquierda:
    `frenacurva250x0.2`, camino −44° y 63 m corrida para afuera; `280x0.15`, −21° y 68 m; aun con 0.06, 19-25 m para afuera. En
    el estado de régimen (250x0.2): inclinada 22.5° a la derecha (pedía 12), manubrio 3° a la derecha, la delantera saturada
    empujando 2550 N a la **izquierda**, la trompa 4.6° a la izquierda del camino, girando a la izquierda a 1.2 g, 5 s;
  - con el teclado, la D a medias frenando desde 200-280: −5 a −11°; soltando la D y clavando sólo el trasero (Espacio) desde
    60-280: inclinada 11° a la izquierda y derrapando hacia la derecha a 46°/s durante 4-7 s (+90 a +230° de camino);
  - en total, 34 de 258 casos de `alreves.py` (60-280 km/h, `plaza_asfalto_ancha`); en tierra (lisas), 53 de 129.
- **Por qué** (el `--wheel-log` daba a la delantera una deriva de −0.3°, empujando a la izquierda, mientras el manubrio "libre"
  creía seguir su camino +0.035° hacia la derecha):
  - los ángulos alrededor de la vertical (la deriva que corrige `brake_align`, el camino que sigue la delantera libre de
    `brake_align`, el derrape lento, el grau, el peralte) se medían con `atan2(v · rightFlat, v · fwdFlat)`, y `rightFlat` era el
    costado de la moto con la Y en cero. Con la moto **cabeceada e inclinada a la vez** ese vector no es perpendicular a `fwdFlat`:
    queda torcido tan(cabeceo)·tan(inclinación). Frenando, la trompa se hunde (−2.5° en la Carrera, −20° en el stoppie de las de
    tierra) y con 10-25° de inclinación el error es 0.3-1° (en un stoppie, varios grados);
  - frenando fuerte con `brake_align`, la delantera "libre" (`brakeFree`) apunta a su camino más el manubrio que pide la curva. A
    250 km/h la curva pide décimas de grado de manubrio (`kLean`: 0.05-0.3°), así que el error manda: la delantera apunta para
    afuera de su camino y empuja para el otro lado. Esa fuerza, adelante del centro de masa, gira la trompa para afuera; con la
    trasera descargada nada la sostiene, `brake_align` sigue al camino (`pathRate`) y la cubierta empujando hacia afuera, debajo
    del centro de masa, acuesta la moto **más** hacia adentro: el balance la sostiene y queda el estado cruzado. Despacio, el
    manubrio que pide la curva (grados) tapa el error: por eso "cuando vas muy rápido";
  - el caso del trasero solo es otro: la trasera frenada no agarra de costado, al enderezarse la cola sale y las dos cubiertas
    saturadas sostienen el derrape (el estado de "soltar la curva sin frenar" de "La de carreras: rework de la frenada");
    `brake_align`, que lo cortaría, sólo miraba el freno de adelante.
- **Qué se hizo** (claves nuevas, 0 por defecto = como antes, guardadas con `if`; `BikePhysics.cpp`; la física 031 de la 450 y la
  600 no tiene `brake_align` ni la delantera libre):
  - **`flat_side_exact`**: el costado en el plano es el perpendicular a `fwdFlat` (`sideFlat`, una vez por paso, para todos esos
    usos). 1 en la Carrera, la 2T y la Trial;
  - **`brake_align_rear`**: `brake_align` también con el freno de atrás solo (`max(delantero, brake_align_rear × trasero)`; con la
    S, que frena atrás a la mitad, no cambia nada). 1 en la Carrera.
- **Probado y descartado**:
  - apagar de a una las claves de la frenada de la Carrera: con `brake_align` 0 nunca dobla al revés pero la cola sale 12-16° (lo
    que `brake_align` arregló); `brake_yaw_comp`, `brake_stand_up`, `brake_transition_release`, `lean_sweep_comp`,
    `balance_fast_*` y `lean_load` en 0 no lo cambian (el equilibrio por la carga no tenía que ver);
  - que la guiñada que busca `brake_align` nunca doble menos hacia el lado de la inclinación que lo que da esa inclinación: con
    S 0.1-0.15 seguía al revés y con la D a fondo derrapaba 28°;
  - `flat_side_exact` en la Trilheira: no mejora (38 → 42 casos; con `brake_align_free` 0 no suelta la delantera): lo suyo es
    otra cosa (abajo).
- **Se comprobó**:

  | Carrera, `frenacurvaNxS` (camino / costado) | S 0.06 | S 0.1 | S 0.15 | S 0.2 | S 0.3 | S 1 |
  |---|---|---|---|---|---|---|
  | 150 km/h, antes → después | +9° / −2 m → +18° / +2 m | +14 / −4 → +28 / +4 | +14 / −7 → +40 / +8 | +74 / +21 → +86 / +27 | +104 / +30 → +102 / +29 | +151 / +36 → +151 / +36 |
  | 200 km/h | +4 / −10 → +18 / +3 | +6 / −15 → +28 / +5 | −4 / −28 → +41 / +11 | −29 / −37 → +104 / +49 | +124 / +53 → +121 / +51 | +168 / +56 → +166 / +61 |
  | 250 km/h | −1 / −19 → +18 / +3 | 0 / −28 → +29 / +5 | −16 / −53 → +42 / +12 | −44 / −63 → +115 / +71 | +136 / +76 → +133 / +74 | +177 / +77 → +178 / +85 |
  | 280 km/h | −3 / −25 → +18 / +3 | −3 / −36 → +29 / +6 | −21 / −69 → +41 / +13 | −50 / −78 → +121 / +85 | +142 / +90 → +138 / +88 | +184 / +89 → +187 / +98 |

  - inclinada como mucho 3-12° con S 0.06-0.2 (antes 9-23°, más que lo pedido), deriva ≤ 6.6° (antes ≤ 6.4°), la misma g;
  - `alreves.py`, la Carrera en `plaza_asfalto_ancha` a 60-280: **34 → 2 de 258** (los dos que quedan: a 60 km/h, soltar la D,
    esperar 0.35 s y clavar: la moto se pasa a −7° al enderezarse y el camino la sigue −3°; y el trasero solo a 100 km/h, 0.3 s
    cruzada a 6° y 9°/s); el trasero solo desde 100: +190° → +12° de camino; en `plaza_tierra` (lisas) 53 → 18 de 129 (con la D
    a fondo en la tierra antes iba −19 a −26° al revés; ahora +24 a +31°). 2T: 43 → 5 de 86 (tierra) y 44 → 5 (asfalto), a 60-90;
    la Trial: 54 → 18 y 53 → 18;
  - `tools/maniobras.py` de la Carrera (100-250 km/h): igual salvo los décimos (derecho 1.12-1.23 g igual; doblando y clavando
    sin soltar 151-185°, antes 149-179°);
  - bots, 900 s: la Carrera en el circuito **idéntico** (2:10.86 / 2:09.19 / 2:09.19 / 2:09.21 / 2:09.66 / 2:09.66, 0 caídas), en
    la pista de motocross 10 → 11 vueltas sin caídas, en la favela 3 → 1 golpes (caótico); la 2T y la Trial con la clave en la
    pista, el parque, el valle, la favela (2T) y el circuito de obstáculos (Trial): las mismas vueltas, al centésimo, y 0 caídas;
  - sin las claves todo idéntico: `tools/regresion.sh` contra la v0.3.9 en las 9 de siempre más `accel` y la 600 (`brakestraight`
    y el bot), **idéntico**; la 2T, la Trilheira y la Trial sin la clave (`brakeslide`, `brakeslide2.5`, `cuerpo`, `whip`,
    `wheelie`, `circle12`, `brakestraight`, `accel`, `frenacurva90x0.3` y el bot de 60 s), idénticas al exe sin el cambio. Con la
    clave, en la 2T y la Trial cambian sólo `brakeslide` (cola máx. 32.2 → 35.0° la 2T, 34.4 → 34.7° la Trial), `brakeslide2.5`
    (42.7 → 42.9°, 60.0 → 60.4°) y `accel` (en los decimales); en la Carrera, `brakestraight`, `accel` y el bot de 60 s en los
    decimales; `circle12`, `wheelie` y `cuerpo`, idénticos.
- **Pendientes** (no se tocaron; `alreves.py --map prueba/plaza_tierra --vel 60,90,120`):
  - **resuelto en la v0.3.10** (arriba, "La 450 y la 600: el trompo frenando con la S"). Era: **la 450 y la 600 (física 031) hacen un trompo frenando con la S y un poco de dirección**: `frenacurva90x0.1` cola a 146-149°,
    `x0.2` 155° (la v0.3.8 igual); con el teclado, soltando la D y clavando, 100-145°: 88 y 83 de 129 casos. Es "La motocross:
    frenando fuerte hacía un trompo" (abajo): se arregló con `brake_align` en la física de entonces y la 031 no lo tiene. Cambia
    la física de la 450: a pedido;
  - la Trilheira (38 de 86; lo probado en la v0.3.10, arriba, en la entrada de la 450 y la 600) y lo que queda de la Trial (18): en el stoppie, soltando la D y clavando, la moto queda 1-2 s inclinada
    8-12° hacia el lado de la curva con el camino yendo para el otro a 7-10°/s; la Trial frenando con la D, 0.4-1.3 s inclinada
    20-30° con −6 a −9°/s. No es el costado torcido (con la clave, igual): la delantera con la trasera en el aire;
  - el trasero solo (Espacio) soltando la D en la 2T, la Trial y la Trilheira: −4 a −8° de camino (el freno de mano).
- **Lecciones**:
  - **Aplanar un vector de la moto no da el perpendicular al rumbo**: con la moto cabeceada e inclinada, el costado aplanado queda
    torcido tan(cabeceo)·tan(inclinación). Para medir ángulos alrededor de la vertical, el perpendicular de `fwdFlat`
    (`(-z, 0, x)`), como ya hacían `HandbrakeSteer` y el freno de mano.
  - **Un error chico importa cuando lo que se pide es más chico**: a 250 km/h la curva pide décimas de grado de manubrio; un sesgo
    de medio grado en "su camino" invierte la fuerza de la delantera. Despacio no se ve. Barrer la velocidad hasta la máxima y el
    manubrio desde casi nada (0.06), no sólo la D a fondo.
  - **El `--wheel-log` contra lo que cree el código**: la deriva de la cubierta (`latV`) contra el ángulo con que el código apunta
    la rueda mostró la diferencia en un paso; antes de seguir apagando ayudas (se probaron siete claves), comparar las dos
    mediciones del mismo ángulo.
  - **Medir el síntoma que describe el usuario**: "dobla para el lado contrario del que está inclinada" es la inclinación contra
    hacia dónde gira el camino, paso a paso; el resumen de toda la frenada (camino total) escondía los 4-7 s cruzada del trasero
    solo, que terminaba girando para el lado "correcto".

### Salir del whip (a pedido: "cuesta salir del whip, inicia bien pero al intentar salir la moto no se endereza del todo")
- **Cómo se midió**: `whipkickN...rT` (nuevo en `Game.cpp`): el whip armado en el labio, suelto en el aire y desde los T s de
  vuelo el stick al otro lado **hasta tocar** (el `cT` de antes lo tira 0.35 s fijos). En `prueba/salto_whip` (50 km/h, ~1.05-1.12
  s de vuelo) temprano / mitad / tarde = `r0.2` / `r0.45` / `r0.7`; en la pista de motocross, el doble de s≈425 (`--spawn 360
  --test whipkick52rT`, ~1.1-1.3 s de vuelo) con `r0.25` / `r0.65` / `r0.9`. Por cada vuelo: rolido y `whipYaw` al tirar,
  cuánto se pasa al otro lado, cuándo entra en menos de 5° de los dos, con cuánto toca y la cola/rolido del segundo siguiente.
- **Qué pasaba** (v0.3.9-beta, `salto_whip`, toca: rolido / guiñada contra el vuelo):

  | moto | sin traerla | desde 0.2 s | desde la mitad (0.45 s) | desde 0.7 s |
  |---|---|---|---|---|
  | 2T | 36 / −21° | −15 / +30° | 1 / +13° | 16 / −4° |
  | Trilheira | 39 / −15° | −16 / +32° | 2 / +17° | 18 / +1° |
  | Trial | 41 / +3° | −17 / +54° (caída) | 0 / +38° | 19 / +21° |
  | 450 | 46 / −11° | −7 / +34° | 11 / +19° | 27 / +2° |
  | 600 | 48 / −11° | −12 / +39° | 7 / +22° | 24 / +7° |

  O sea: el rolido tardaba y la guiñada se pasaba al otro lado. Con el `c0.35` de siempre la 450 tocaba a 21° / +14°.
- **Por qué**: el contra-stick era la misma velocidad de giro que el whip pero al revés, **sobre el eje del whip** (adelante y
  abajo: rolido y guiñada por partes iguales). Un whip no es rolido y guiñada iguales: sale del labio con ~45° de rolido y
  12-25° de guiñada (en la Trial, el efecto giroscópico se come casi toda la guiñada: 2-5°). Girando sobre ese eje, para
  sacar los 45° de rolido mete 45° de guiñada: la guiñada pasa por cero y sigue al otro lado mientras al rolido todavía le
  falta. No era el tope de torque (350 Nm llegan) ni la amortiguación: era que un eje solo no corrige dos ángulos distintos.
  Y como el stick pedía una velocidad (no "derecha"), sostenerlo la pasaba al otro lado; soltarlo a tiempo era adivinar.
- **Qué se hizo** (`whip_air_return`, 0 = como antes, guardado con `if`; `WhipAirReturn` en `BikePhysics.cpp`, lo usan las
  dos físicas): con el stick al otro lado de para donde está cruzada (rolido − `whipYaw`, más de 6°), el piloto la trae
  derecha: busca una velocidad de rolido hacia la vertical y una de guiñada hacia donde vuela, cada una proporcional a lo
  que falta (`whip_air_return` 8 rad/s por rad, con el tope `whip_air_rate` × el stick: 1.8 rad/s a fondo) y la persigue
  con su propio torque (tope `whip_air_return_torque` 350 Nm en cada eje; la ganancia, la inercia de la moto sobre 0.05 s).
  Llega frenando, sin pasarse. Es un torque con tope, no se fija la velocidad: el giroscópico y lo que traiga siguen
  actuando. El estado (`whipReturn`, el lado del stick) queda hasta soltar el stick (menos de 0.25) o tocar el piso:
  sostenido la deja derecha; para un whip al otro lado, soltar y volver a tirar. Con el stick para el mismo lado, suelto, o
  con la moto derecha, todo como antes (la entrada y el "no vuelve solo" no cambian). Entre los 0.1 y 0.25 s de vuelo entra
  de a poco como el resto del `whip_air`. Valor: 8 en la 2T, la Trilheira, la Trial, la 450 y la 600.
- **Probado y descartado**: `whip_air_return` 6: la 450 desde la mitad tocaba a 2.8° (justo) y en 0.54 s; 10: se pasa 3° de
  guiñada en la 2T. 8: se pasa a lo sumo 1.3° en `salto_whip` y 2.6° de guiñada en la pista.
- **Se comprobó** (exe nuevo; toca: rolido / guiñada; entre paréntesis, cuánto tarda en quedar a menos de 5° de los dos):

  | `salto_whip` | desde 0.2 s | desde la mitad (0.45 s) | desde 0.7 s | `c0.35` (antes → después) |
  |---|---|---|---|---|
  | 2T | −0.3 / 0.0° (0.42 s) | 0.0 / +0.5° (0.42 s) | 6 / −1° | 12 / +5 → 2 / +4° |
  | Trilheira | −0.3 / 0.0° (0.45 s) | −0.3 / −0.1° (0.44 s) | 8 / −1° | 13 / +9 → 2 / +3° |
  | Trial | −0.5 / 0.0° (0.42 s) | −0.4 / −0.2° (0.42 s) | 11 / −1° | 14 / +28 → 3 / +1° |
  | 450 | −0.4 / 0.0° (0.52 s) | 1.3 / −0.1° (0.50 s) | 18 / −1° | 21 / +14 → 7 / +4° |
  | 600 | −0.1 / 0.0° (0.52 s) | −0.1 / −0.1° (0.52 s) | 12 / 0° | 21 / +14 → 8 / +6° |

  - lo que se pasa al otro lado: 0-1.3°, en la pista hasta 2.6° (antes 6-54° de guiñada); al otro lado
    (`whipkick50Lr0.45`) igual, simétrico; la cola en el segundo siguiente 1-3° (antes 13-38°);
  - la pista de motocross (doble de s≈425, mitad = `r0.65`): 2T 0.4 / +0.8° (antes 0.1 / +7.2°), Trilheira 2.8 / +1.7° (antes
    15 / +0.5°), Trial 4.0 / −0.3° (antes 12 / +11°; vuela 1.08 s), 450 0.0 / 0.0° (antes −15 / +22°: la 450 ahí sale con
    menos whip, 19°, y se pasaba). La 600 en ese salto no arma el whip (no sube la cara igual), sin cambios;
  - tarde (a 0.35-0.4 s de tocar) no alcanza: llega a 6-24° según la moto (el tope de 1.8 rad/s). Es a propósito: traerla
    lleva ~0.4-0.5 s;
  - la entrada, idéntica (md5 de la telemetría): `whipkick50`, `L`, `h`, `h0.3`, `a` y `n` en las 5 motos;
  - `tools/regresion.sh` contra la v0.3.9-beta, **idéntico** (con la clave prendida en la 450 y la 600) en las 9 de siempre
    más `whipL`, `whiphold`, `airlean`, `accel`, la 600 (bot y `whip`) y el `whip`/`whiphold` de la 2T, la Trial y la
    Trilheira; `prueba/ladera` y `prueba/bowl_pared` (`giro20`, `giro30`, `giro30x0.5`; 450, 2T y Trial) idénticos; bots de
    900 s idénticos a la v0.3.9-beta con las 5 motos en la pista (450 1:04.41 / 1:04.24, 600 1:04.52 / 1:04.34), la favela,
    el parque, el valle, Los Médanos y el circuito (el bot no tira el stick al otro lado en el aire).
- **Lecciones**:
  - **Un eje no corrige dos ángulos**: traer algo que tiene dos errores distintos (rolido 45°, guiñada 12-25°) girando sobre un
    eje fijo deja uno corto y pasa el otro. Cada ángulo con su objetivo y su torque.
  - **"Traerla" es un objetivo de ángulo, no una velocidad**: si el stick pide una velocidad, sostenerlo la pasa al otro lado
    y soltarlo a tiempo es adivinar. Una velocidad proporcional a lo que falta, con tope, llega frenando sola.
  - **El signo de `whipYaw` es + a la izquierda** (el comentario decía a la derecha): un whip a la derecha da rolido + y
    `whipYaw` −. Medirlo en la telemetría (`roll`, `wyaw`, `beta`) antes de combinar ángulos: con la suma al revés, el
    `whiphold` (rolido +41°, `whipYaw` −45°) se leía como contra-stick.
  - **Medir con el stick sostenido hasta tocar**: con un tiempo fijo (`c0.35`) el resultado depende de cuándo lo suelta la
    prueba y esconde si se pasa.

### La 450 y la 600 con el equilibrio por la carga y el whip (a pedido: cambia la física de la 450)
- **Pedido**: "seguí implementando en el resto de motos": el equilibrio por la carga (`lean_load`, `balance_fast_*`) y el
  whip de verdad (`whip_*`) también en la Motocross 450 y la 600 (física 031, `BikePhysics031.cpp`). Los valores son los de
  la 2T y la Trilheira (`motocross.ini`, `motocross600.ini`, al final). Prenderlos destapó tres problemas; los tres se
  arreglaron (abajo) y uno de ellos también era el pendiente de la Trial en la favela.
- **1. La escadaria de la favela** (la 450 con el bloque: 12-18 caídas cada 15 min de bot, y en una muestra 94):
  - **qué pasaba**: sin el bloque la 450 ya se caía en la escadaria **en cada pasada** (9-19 caídas cada 900 s en 5
    muestras con la masa corrida ±0.01-0.02 kg, todas en s≈83-92). Con el bloque, además, al caerse quedaba sobre la
    escalera y `RespawnNearest` la hacía reaparecer arriba (s=71, parada): desde ahí también se caía, y en una muestra
    reaparecía y se caía cada 5.5 s hasta el final;
  - **por qué**: no era `lean_load` leyendo los escalones (los contactos sobre objetos ya no cuentan) ni el whip (la intención
    queda en 0 en los escalones). Era lo de "Escaleras de la favela: la moto rebotaba para todos lados" (abajo), que la
    física 031 nunca recibió (no tiene las sondas de objetos): bajando, el rayo de la trasera, inclinado como el eje de la
    suspensión, pega en la contrahuella del escalón que acaba de dejar (una pared que mira hacia adelante) y la cubierta
    "tangente" a esa pared sale con 80 000-128 000 N: la velocidad salta de 19 a 26-33 km/h y la moto cae de trompa a
    −80°, rola y se cae. Que se cayera una vez o dos por vuelta dependía de dónde quedaba la moto caída (caótico); el bloque
    sólo cambió eso;
  - **probado y descartado**: que con la moto apoyada sólo en objetos `lean_load` se mida contra el mundo (un `weight` que va
    a 0): sacó el bucle infinito pero seguía cayéndose dos veces por vuelta (la primera pasada y la de reaparecer);
  - **qué se hizo**: `object_wall_deg` (sólo física 031; 0 = como antes, guardado con `if`): sobre objetos, una cara que mira
    hacia adelante (`n · fwd > 0`) más empinada que eso (65° en la 450 y la 600) no es apoyo: el rayo de esa rueda queda
    sin suelo ese paso y la rueda baja al escalón siguiente. Las caras que miran hacia la moto (un escalón subiendo) siguen
    siendo un tope: saltearlas todas hacía que, subiendo, el chasis pegara en el canto y Jolt lo tirara 3 m arriba (vy 8.7);
  - **se comprobó**: bot de la 450 en la favela, 900 s, 5 muestras: 9-19 caídas → **0** en todas (1:36.6-1:36.9 por vuelta;
    antes 1:38.8-1:39.4); la 600: 6-9 → 0-1 (la que queda, en la curva de s≈265); el bot desde 17 puntos antes de la
    escalera (s=40-72, 9 s): 13/17 caídas → 0/17 con las dos; `--spawn 68 --test bajadaN` a 10-25 km/h: trasera máx.
    80 000-128 000 N → 7 000-9 500 N, cabeceo −84..−87° → −31..−37°, salto de velocidad entre muestras 8-22 → 1-2 km/h;
    subiendo (`--spawn 90 --test subida8/15`), antes caída a 8 y 88 000 N en la delantera a 15, ahora las dos pasan (25 000 N:
    la moto se frena contra el escalón, como la otra física).
- **2. La cresta de `prueba/ladera` con la D a fondo** (`giro30`: la 450 con el bloque se caía a los 24.6 s):
  - **qué pasaba**: doblando a 30 km/h la moto pasa la cresta de arriba acostada 26° y vuela 0.9 s. Con `whip_air` el stick
    (que venía apretado desde el piso) pedía 1.8 rad/s sobre el eje del whip: en el aire rolaba de 26° a 87° y caía de costado.
    La 2T y la Trial pasaban la misma cresta de milagro (inclinadas 84-93° contra el suelo, cabeceo 61-69°);
  - **qué se hizo** (las dos físicas, sólo con `whip_air`; `WhipAirStick` en `BikePhysics.cpp`): un stick que ya estaba
    apretado al despegar no es un whip hasta que el piloto lo suelta (menos de 0.25) y lo vuelve a tirar, salvo que haya
    despegado con whip (`whipLaunch`, la intención en el labio). Y "en el aire el whip no vuelve solo" (el enderezado y la
    alineación de guiñada apagados mientras está cruzada) vale sólo para un vuelo del piloto (`whipRiderFlight`: despegó con
    whip o tiró el stick en el aire); un vuelo que no es whip (una cresta, un rebote después de aterrizar) se endereza como
    siempre;
  - **se comprobó**: `giro30` de la 450: toca a 13° de rolido y sigue (639° de giro, sin caída); la 2T y la Trial en la misma
    cresta: contra el suelo máx. 93/84° → 66/46°, cabeceo 61/69° → 49/47°; las pruebas del whip pensadas (`whipkick50`, `c0.35`,
    `h0.3`, `a`, `n`) dan **idénticas** (md5 de la telemetría) en la 2T, la Trilheira, la Trial y la 450; el `whip` con
    `--drop 1` cambia sólo después del primer aterrizaje (el rebote ya no es un vuelo del piloto: cola máx. 48.9 → 48.7°).
- **3. La carga de más en suelo tendido** (el bot de la 450 en la pista: 1:05.19 / 1:05.03 contra 1:04.26 / 1:04.10, en 4
  muestras 1:04.99-1:05.27; y la Trial en la favela, el pendiente de "El equilibrio era un giroscopio"):
  - **qué pasaba**: la vuelta perdía 0.54 s en los whoops (s≈600-625: el bot entraba a 41 km/h y salía a 31, a fondo). Con
    velocidad fija (`--spawn 575 --test bajada41`) la moto pasa los whoops igual con y sin el bloque: es el ritmo del bot.
    La Trial se caía 4-7 veces cada 15 min (4 muestras) en la curva de s≈258: iba 2-3° más derecha (contra el mundo, 44-45°
    contra 46-47°), se abría, pasaba por un borde y pegaba contra una casa;
  - **por qué**: la inclinación buscada y la curva que da una inclinación usan la carga filtrada a 0.5 s (`f0n`). En suelo
    tendido esa carga de más son baches, whoops y compresiones: duran décimas y el filtro las sigue tarde (en los whoops la
    media real es 1.16 g y la filtrada 1.31 g; en la bajada de la Trial, 1.05-1.1 g). Con 1.09 g y la D a fondo, `atan2(a, f0n)`
    da 42.5° en vez de 45°;
  - **probado y descartado**: usar sólo el peso a lo largo de la normal para la inclinación (1:04.42 / 1:04.25 y la Trial sin
    caídas, pero en la pared de `bowl_pared` la moto se pasaba a 106° y daba la mitad de vueltas: ahí la carga es la curva
    del bowl y dura); el mínimo entre la carga filtrada a 0.5 s y a 0.1 s (la Trial 1-4 caídas, la 450 1:04.56 / 1:05.08);
    la carga en el aire yendo a 0 (nada); la fórmula del peralte de siempre para el tope de la curva (la Trial igual);
  - **qué se hizo**: `lean_load_flat` (0 = como antes, guardado con `if`): la carga de más cuenta para la inclinación sólo en
    un suelo empinado (de ~20° a ~40°, `nY` de 0.94 a 0.77: la pared del bowl, un peralte fuerte); en suelo tendido, el peso
    a lo largo de la normal. 1 en la 450, la 600 y la Trial. **No** en la 2T, la Trilheira ni la de carreras: con ella
    llegaban más rápido a la curva de s≈253 de la favela y pegaban en la pared (la 2T 0 → 4-7 caídas cada 15 min, la
    Trilheira 2-3 → 3-6, la de carreras 2-4 → 7-10 y en la pista de motocross 0-3 → 5-11);
  - **se comprobó**: la 450 en la pista 1:04.41 / 1:04.24 (4 muestras: 1:04.40-1:04.42 / 1:04.23-1:04.25); la Trial en la
    favela 4-7 → **0** caídas en 4 muestras de 900 s, y en la pista, el circuito de obstáculos, el parque y el valle igual que
    antes (13/0, 23/0, 17/0, 21/0); el bowl (`giro30`) sigue subiendo por la pared (la 450 927° de giro, rolido contra el mundo
    86°, cabeceo 82°).
- **Valores finales** (`motocross.ini` y `motocross600.ini`): `lean_load` 1, `lean_load_flat` 1, `balance_fast_k/d/lead`
  0.6 / 0.8 / 0.5, `whip_takeoff` 1, `whip_air` 1, `whip_air_rate` 1.8, `whip_air_damping` 200, `whip_land_crash_deg` 50,
  `object_wall_deg` 65. Se probó `whip_takeoff` 0.7-0.8 (con `lean_load_flat` la moto sale del labio 6° más acostada que
  la 2T: 44.8° contra 38.5°): 38-40° pero menos guiñada (−1.2 a −1.35 rad/s contra −1.64); quedó 1, como las otras.
- **Se comprobó (antes → después; antes = la v0.3.8, o el exe nuevo con los `.ini` de la v0.3.8, idéntico)**:
  - bots, 900 s (vueltas / caídas): 450 en la pista 13/0 → 13/0, **1:04.26 / 1:04.10 → 1:04.41 / 1:04.24**; 600 13/0 →
    13/0, **1:04.28 / 1:04.18 → 1:04.52 / 1:04.34**; favela (arriba); parque 17/0 → 17/0 las dos (450 0:51.11 / 0:51.16 →
    0:51.13 / 0:51.01); valle 21/0 → 21/0 (0:41.18 → 0:41.01); Los Médanos 0 caídas → 0;
  - `prueba/ladera` (450): `giro20` 454° → 1119°, `giro30` caída a los 19.5 s → 639° sin caída, `giro30x0.5` 588° → 594°;
    contra el suelo ≤ 56°. `prueba/bowl_pared`: `giro20` 1680° → 1658° (rolido 47° → 55°), `giro30` 1095° en el fondo a 58°
    → 927° subiendo por la pared. La 600 en `giro30` de la ladera y del bowl se cae a los 3-5 s **con y sin el bloque**:
    arranca a fondo en 1ª, hace un wheelie de 47° y se da vuelta (la prueba no es para ella);
  - en el plano (`plaza_tierra`, 90% de la inclinación con el stick y el teclado): 450 `giro40/70` 0.71/0.72 → 0.66/0.69 s,
    `k` 0.84/0.87 → 0.79/0.84 s; inclinación sostenida 47.9/46.4° → 49.1/49.6° con 0.91/0.80 → 0.93/0.82 g; la 600
    0.77/0.73 → 0.70/0.73 s. `--flat --test circle12`: 47.2° → 50.2° (lo pedido, 46°): el balance que acompaña sostiene
    menos, como en la 2T;
  - whip (`prueba/salto_whip`, 50 km/h, 450; la 600 parecida): derecho sube 1.68 m igual; con el stick en la cara despega a
    27.1° enderezándose (+0.81 rad/s, guiñada −0.54) → 44.8°, +0.45, −1.64 rad/s; sube 1.55 → 1.45 m (el scrub, 14% más bajo
    que derecho; la 600 2.03 → 1.57 m); toca −2.7° / −1.9° de cruzada (volvía sola) → 45.8° / +5.1°; trayéndola (`c0.35`)
    20.6°; sostenido 0.3 s, toca a 54° y la cola llega a 14° (serpentea, la 600 29°); sostenido todo el vuelo, caída;
  - `tools/regresion.sh` contra la v0.3.8: **distinta a propósito** en todas con el bloque (`wheelie`, `brakestraight`,
    `frenada60` y `airrot` sólo en los decimales: menos de 0.05 km/h y 0.05°; `brakeslide`, `cuerpo` y `circle12` en la
    inclinación, hasta 7-9°; `whip`, `flip`, `accel` y el bot, desde la caída de la moto al aparecer: en el aire actúa
    `whip_air`). Con los `.ini` de la 450 y la 600 de la v0.3.8 (las claves en 0), **idéntico** en las 9 de siempre más
    `accel`, `whiphold`, `airlean`, `frenada60`, la 600, el bot de la 450 en la favela (120 s), Los Médanos y la pista (150 s);
  - las otras motos (2T, Trilheira, Trial, carreras), con `WhipAirStick` (lo único que les cambia): pista 13-14/0, parque,
    valle y circuito de obstáculos sin caídas, como antes; favela 2T 0 en 4 muestras (antes 0), Trilheira 0-1 (antes 2-3), la
    de carreras idéntica (no tiene `whip_air`).
- **Pendientes**: la 600 en las pruebas `giroN` de la ladera y el bowl (wheelie al arrancar a fondo, de antes); reaparecer
  arriba de la escadaria, parada, sigue siendo un mal lugar para el bot con cualquier moto (`RespawnNearest`, Game.cpp; ya no
  se cae ahí, pero la regla es de la interfaz); la 2T, la Trilheira y la de carreras siguen pegando a veces en la pared de
  s≈253 de la favela (el plan del bot, ver MAPAS.md).
- **Lecciones**:
  - **Lo que una clave nueva "empeora" puede ser un bug viejo que cambió de lugar**: la 450 se caía en la escadaria en cada
    pasada desde siempre; el bloque sólo cambió dónde quedaba caída y con eso dónde reaparecía. Contar las caídas por pasada
    (y no por vuelta), con varias muestras, y mirar el primer golpe raro de la telemetría (100 000 N en una rueda) antes de
    culpar a lo nuevo.
  - **Una pared que no es apoyo depende de hacia dónde mira**: la contrahuella que la rueda deja atrás (mira hacia adelante)
    es un artefacto del rayo inclinado; la que tiene adelante (mira hacia la moto) es un tope de verdad. Sacar las dos dejaba
    al chasis pegando en el canto, y Jolt resuelve eso tirando la moto por el aire.
  - **Un stick que ya estaba apretado no es una orden nueva**: la intención del piloto en el aire es lo que hace en el aire;
    lo que traía del piso (doblando) no puede convertirse en un whip al despegar sin querer. Lo mismo para las ayudas: un
    vuelo que el piloto no armó se endereza como siempre.
  - **Una carga filtrada sólo vale para lo que dura**: la de la pared de un bowl dura segundos y decide el equilibrio; la de
    un bache dura décimas y el filtro la entrega cuando ya pasó. Separarlas por la pendiente del suelo (no por el valor)
    deja las dos cosas bien.
  - **Una mejora de una moto puede empeorar otra por el bot**: con la carga en suelo tendido, la 2T y la Trilheira iban más
    lentas por la favela y por eso no pegaban en la pared de s≈253. Medir cada moto donde corre antes de hacer común un
    arreglo, y dejarlo detrás de una clave si no le sirve a todas.

### El equilibrio era un giroscopio (a pedido; las motos de `BikePhysics.cpp`; la 450 y la 600, después: ver arriba)
- **Pedido**: "el equilibrio automático es buena asistencia pero cuando manejas en slopes inclinados se siente como un
  giroscopio, te intenta inclinar la moto y no permite hacer giros continuos [...]". El usuario quiere que todo se sienta
  "smooth y físico, no scripteado".
- **Cómo se midió**: mapas de prueba nuevos en `pruebas/mods/prueba/maps/`: `ladera` (un plano de tierra de 22°, 200 m, se
  larga en la mitad, de costado a la pendiente) y `bowl_pared` (un bowl de 25 m de radio con paredes que se empinan hasta
  ~70°). En los dos, `--test giro20 | giro30 | giro30x0.5` (velocidad fija y la dirección a fondo o a medias, 25 s) y un
  resumen de cuánto giró en total, cuánto se apartó la inclinación de la pedida (`|leanT − roll|`), el rolido máximo contra
  el mundo y contra el suelo (`psi`, línea `suelo` de la telemetría, abajo), el cabeceo máximo (cuánto sube por la pared) y si
  se cayó. En el plano, `giroN[k]` y `tools/giro.py` (lo que tarda en tirarse a la curva). Bots de 15-30 min antes y después,
  con la masa corrida ±0.01 kg donde había dudas (las caídas de los bots son caóticas: ver más abajo).
- **Qué pasaba** (v0.3.8):
  - el balance es un PD fuerte hacia `leanTarget`, y `leanTarget = atan(v²·k/g)` es la inclinación **contra la vertical del
    mundo**, con el tope `max_lean_deg` también contra el mundo, y el rolido que mide (`roll`) también. En plano está bien; en
    una ladera, doblando a 25 km/h con la dirección a fondo, la inclinación pedida saltaba de 25° a 55° (el tope) en cada
    vuelta, según para dónde apuntaba la moto: el peralte que calcula `bank_turn_gain` (la normal bajo el centro de masa,
    hasta ±30°) la hacía pedir de más subiendo y de menos bajando;
  - en la pared de un bowl la moto andando de costado a la pared necesita 60-90° contra el mundo (perpendicular a la pared):
    el balance la enderezaba "contra el mundo" y no subía; daba vueltas en el fondo a 47°;
  - en las laderas se caían: `giro20`/`giro30` con la 2T, la Trilheira, la Trial y la 450: 1 a 4 caídas de 12 corridas.
- **Por qué**: lo que hace que una moto quede "derecha" no es la vertical del mundo sino la suma del peso y lo que la empuja el
  suelo: la moto está en equilibrio cuando esa suma cae en su plano. En plano es la vertical más la fuerza de la curva
  (`atan(a/g)`, lo de siempre); en una ladera de costado, también la vertical (la moto queda inclinada respecto del suelo);
  en la pared de un bowl, el suelo la aprieta (la curva del bowl) y "derecha" es casi perpendicular a la pared.
- **Qué se hizo** (claves nuevas; en `tuning.ini` valen lo de antes y se guardan con `if`; `UpdateLoadFrame` en
  `BikePhysics.cpp`, lo usan las dos físicas):
  - **`lean_load`** (0..1): el marco del suelo que pisan las ruedas: la normal de los contactos (filtrada ~0.07 s) y la carga
    por kg a lo largo de ella (la fuerza que las ruedas le pasan al chasis, filtrada ~0.5 s y entre 0.9 × el peso a lo largo
    de la normal y 1.8 g). La inclinación se mide contra esa normal (`psi`). Lo que pide el manubrio en el plano
    (`leanTarget` de siempre, con todos sus retoques) se toma como una aceleración de costado `g·tan`, se le suma el peso a lo
    largo del costado del suelo (`f0r`) y la moto busca el plano de esa suma con la carga (`atan2(f0r + a, f0n)`), con el tope
    `max_lean_deg` **contra el suelo**. Lo mismo para el manubrio a velocidad (la curva que da esa inclinación con esa carga,
    `(f0n·tan psi − f0r)/v²`), para el tope de la curva pedida (`μ·g·nY − f0r` hacia el lado que dobla: doblar hacia arriba
    en una ladera cuesta más, hacia el fondo de un peralte menos) y para la caída por inclinación (`up · n` en vez de `up.y`).
    En plano da casi lo mismo que antes (en el plano de tierra, `giro40/70` igual al décimo de grado);
  - **`balance_fast_k` / `balance_fast_d` / `balance_fast_speed` / `balance_fast_lead`**: a más de ~20 km/h (desde la mitad de
    `balance_fast_speed` 5.5 m/s) K y D del balance se multiplican por esto (0.6 y 0.8), y la parte D amortigua el rolido
    respecto de una inclinación de referencia que sigue a la pedida a lo sumo a `balance_fast_lead` rad/s (0.5), no todo el
    rolido. La ayuda acompaña: la moto sigue más lo que le hacen el suelo y las cubiertas, y lo que pide el piloto la acuesta
    igual. Sólo **asentada** (las dos ruedas apoyadas hace 0.3 s), **cerca de lo pedido** (si se aparta más de 5-12°, la ayuda
    entera la ataja) y **sin el suelo empujando fuerte** (más de 1.2-1.5 g: la cara de un salto, un aterrizaje, una
    compresión; salvo que el piloto esté tirando la moto de costado, whip o scrub);
  - **`lean_load_world_deg`**: tope de la inclinación contra el mundo con `lean_load` (0 = sin tope);
  - los valores: `lean_load` 1 en la 2T, la Trilheira, la Trial y la de carreras; `balance_fast_*` en la 2T, la Trilheira y la de
    carreras (no en la Trial, abajo); `lean_load_world_deg` 62 en la de carreras. En `motocross.ini` y `motocross600.ini` el
    bloque estaba **comentado** (la física de la 450 no cambia sin pedido); después se prendió a pedido (ver "La 450 y la
    600 con el equilibrio por la carga y el whip").
- **Los casos que hubo que cuidar** (cada uno salió de una caída de bot y su telemetría):
  - **paredes**: una rueda que roza el costado de una casa en la favela da una normal casi horizontal (Y 0.09) y la moto, que
    sigue "a ese suelo", se acostaba contra la pared. Los contactos de más de 65° (normal Y < 0.42) no cuentan;
  - **objetos**: sobre cajas, troncos y escalones la normal es la del canto por el que rueda la cubierta, no un suelo para
    inclinarse; tampoco cuentan (en el aire o sin contactos que valgan, la normal vuelve a la del terreno bajo la moto a 2/s);
  - **de punta**: cayendo de trompa por una escalera (−70°) el costado del suelo sale de la trompa proyectada, casi nada; con la
    moto a más de ~30-50° del suelo (`|fwd · n|` de 0.5 a 0.75) se vuelve a medir contra el mundo;
  - **la carga para doblar**: el tope de la curva pedida usaba la carga; en la cara de un salto (2-4 g) el mismo stick del bot
    pedía 25% más manubrio y la Trial caía de los cajones del circuito de obstáculos 6° más de trompa (−37° de media contra −30°)
    y se iba por encima del manubrio. Ahora cuenta el peso a lo largo de la normal (lo del peralte ya está en `f0r`);
  - **la carga en los rollers**: filtrada a ~0.12 s iba de 0.9 a 2.2 g y con ella el manubrio (la de carreras se caía el doble en
    la pista de motocross); filtrada a ~0.5 s, no;
  - **la pared del peralte**: la de carreras, con lisas en la tierra (desliza siempre), subía por la cara de afuera del último
    peralte de la pista de motocross (44°) acostada 90° contra el mundo y al salir al plano se caía (900 s de bot: 19-21 caídas
    contra 9). Con `lean_load_world_deg` 62 (su `max_lean_deg`): 4;
  - **la Trial y `balance_fast`**: liviana (inercia de rolido 19), con el balance que acompaña despegaba del doble de la pista de
    motocross con 2° de rolido y el aterrizaje en stoppie, que ya era justo, terminaba en caída (1 a 13 cada 15 min, contra 0).
    Se queda sin `balance_fast` (con `lean_load` y el whip: 0 en 4 muestras de 15 min).
- **Probado y descartado**:
  - bajar K y D a velocidad sin más (0.5 / 0.7): se tiraba a la curva el doble de lento (90%: 0.71 → 1.16-1.73 s) y quedaba
    2-6° más acostada que lo pedido (las cubiertas devuelven sólo la mitad del rolido, `tire_roll_coupling` 0.5: con menos K
    sostiene menos). Con la D respecto de la inclinación pedida misma (la derivada del objetivo, con tope 3 rad/s): con el
    teclado, que sube en rampa, se tiraba en 0.34 s (antes 0.84) y con el stick, de golpe, igual que antes: distinto según el
    control. La referencia con velocidad máxima arregla eso; con 1.2-2.5 rad/s se tiraba en 0.3-0.45 s (más rápido que antes);
    0.5 deja lo de antes;
  - la ayuda reducida también en una rueda (stoppie al caer de un salto): la Trial rolaba hasta caerse (de ahí "asentada");
  - el tope de la curva con toda la carga (hasta 1.25 g): la Trial de los cajones (arriba);
  - en la Trial, la ayuda entera con el suelo empujando (para el doble): siguió cayéndose; lo que lo arregló fue sacarle
    `balance_fast`. Se quedó, sin efecto en la Trial, para las otras.
- **Se comprobó**:
  - `ladera` (25 s, giro total y caídas; antes → después): 2T `giro20` caída a los 19 s → 2271° sin caída, `giro30` 961° → 839°;
    Trilheira `giro30` y `giro30x0.5` caídas → 1147° y 586° sin caída; Trial `giro30` caída → 992° sin caída; la 450 con el
    bloque prendido `giro20` 550° → 1901°. La inclinación contra el suelo queda en ≤ 55-58° (el tope) mientras contra el mundo
    va de ~25° a ~77° según hacia dónde apunta, y la diferencia con lo pedido, 1-6° de media. La caída que queda (`giro30` de
    la 450 con el bloque; la 2T, en una versión intermedia) es en la cresta de arriba de la ladera: sale volando con la D a
    fondo, que en el aire ahora es un whip (abajo);
  - `bowl_pared`, `giro30`: sube por la pared tallando (cabeceo máx. 16-20° → 22-30°, rolido contra el mundo 57-61° → 76-91°,
    contra la pared ≤ 45-50°), sin caídas; antes daba vueltas en el fondo a 47°;
  - en plano (`plaza_tierra`, `giro40/70`, stick y teclado, 90%): 2T 0.71/0.73/0.84/0.86 s → 0.66/0.71/0.79/0.83 s, Trilheira
    0.72/0.74/0.87/0.89 → 0.69/0.71/0.82/0.86, carreras 0.67/0.67/0.79/0.79 → 0.62/0.62/0.74/0.74; la inclinación sostenida
    1-3° más (2T 47.9/46.4° → 49.0/49.4°) con la misma g lateral (0.90/0.78 → 0.92/0.79);
  - bots (antes → después; vueltas y caídas): 2T en la pista de motocross 900 s 14/0 → 13/0, parque 17/0 → 17/0, valle 14/0 →
    14/0, favela 900 s 8/11 → 8/0; Trilheira en la pista 13/0 → 13/0, en la favela 1800 s 18/3 → 17/5 (en la curva de s≈253: la
    misma de siempre, ver "La Trilheira: frenando fuerte hacía un trompo"; 3-9 caídas en 45 min sin cambios); Trial en la pista
    13/0 → 13/0, circuito de obstáculos 23/0 → 23/0 (y 0 en 4 muestras con la masa corrida), parque 11/0 → 11/0, valle 14/0 →
    14/0; carreras en el circuito 6/0 → 6/0, en la pista de motocross 11/9 → 10/4, en la favela 8/7 → 8/5. La 450 y la 600 con el
    bloque prendido: pista de motocross 900 s 13/0, igual que sin él;
  - **pendientes** (resueltos en "La 450 y la 600 con el equilibrio por la carga y el whip": `lean_load_flat` y
    `object_wall_deg`): la Trial en la favela (no es su mapa) se cae 2-6 veces cada 15 min en la curva de s≈258 contra 0 (3
    muestras de cada una): con `lean_load` pasa la curva de s≈253 sin pegar en la pared y más rápido, y se va contra una casa
    más abajo. La 450 con el bloque prendido en la favela se cae en la escadaria cada vez que pasa (la física 031 no tiene las
    sondas de objetos y ya se cae ahí una vez por vuelta sin él): para probarla, en la pista, Los Médanos o `prueba/ladera`;
  - la 450 y la 600 (claves apagadas): `tools/regresion.sh` idéntico contra la v0.3.8 (abajo, en "Un whip de verdad").
- **Lecciones**:
  - **"Derecha" es la suma del peso y el suelo, no la vertical**: un balance contra el mundo se siente un giroscopio en cuanto el
    suelo no es plano. Medir la inclinación contra la normal del suelo que se pisa y buscar el plano de peso + carga + la
    curva pedida da lo mismo en plano y lo que corresponde en laderas, peraltes y paredes.
  - **Todo lo que se mide del suelo necesita sus filtros de sentido común**: paredes (normal casi horizontal), cantos de
    objetos, la moto de punta, picos de carga. Cada uno apareció como una caída de bot distinta; buscar en la telemetría qué
    "suelo" estaba viendo la moto (la línea `suelo`: `psi`, `psiT`, `carga`, `nY`).
  - **La carga no es agarre para doblar**: el pico de la cara de un salto o de un aterrizaje dura décimas; usarla para cuánto
    se puede doblar cambia lo que hace el mismo stick en el peor momento.
  - **Bajar una ganancia no es "acompañar"**: menos K tira a la curva más lento y deja la moto más lejos de lo pedido; lo que
    se buscaba es que la ayuda no frene lo que el piloto pide (la D respecto de una referencia con velocidad máxima) y que
    ataje con todo cuando la moto se va (cerca de lo pedido, reducida; lejos, entera).
  - **Las caídas de bot en los lugares límite son caóticas**: un cambio en cualquier lado cambia por dónde llega. Antes de
    decidir, varias muestras con la masa corrida ±0.01 kg (mismas caídas en el mismo `s` en varias muestras = sistemático); y
    medir lo que se repite (la Trial: el cabeceo al caer del cajón en cada vuelta, −37° contra −30°).

### Un whip de verdad (a pedido; las de tierra de `BikePhysics.cpp`; la 450 y la 600, después: ver arriba)
- **Pedido** (plan aprobado): el whip se arma en el labio; en el aire el piloto lo dosifica con el cuerpo y lo tiene que traer
  antes de caer; aterrizar cruzado poco serpentea y mucho es caída; un scrub vuela más bajo; y el estado (ángulo de whip,
  despegue con whip) expuesto para el piloto animado y un sistema de trucos.
- **Cómo se midió**: `tools/whip.py` (nuevo: corre la prueba y resume cada vuelo: al despegar el rolido, su velocidad `wr`, la
  guiñada `wy` y cuánto sube con su velocidad vertical; en el aire el rolido y la cruzada (`beta`, la trompa contra hacia
  dónde vuela) máximos; al tocar, rolido y cruzada; y en el segundo siguiente la cola máxima: el serpenteo); la prueba nueva
  `whipkickN[L][n][a][hT][cT][sS]` en el mapa nuevo `prueba/salto_whip` (un doble: cara de ~28° hasta 2.2 m, pozo y bajada):
  llega a N km/h, tira el stick en la cara y en el aire lo suelta (o lo sostiene `hT`, lo tira al otro lado `cT`, sin nada en
  la cara `a`, a medias `sS`); y las de siempre `whip`, `whipL`, `whiphold` con `--drop 1`. Capturas con ventana
  (`--noprefs --size 1280 720 --screenshot T --shots-every 0.1`, de atrás y con `--side`).
- **Qué pasaba** (v0.3.8, 2T, 50 km/h):
  - con el stick en la cara la moto salía acostada 28° pero **enderezándose** (rolido +0.74 rad/s, guiñada −0.61) y en el aire
    volvía sola: tocaba a −2° y −1° de cruzada. Con el stick sólo en el aire (`whip`), 28° de rolido y 7° de cruzada, y al soltar
    volvía a 4°/−3°: una animación de ida y vuelta;
  - **por qué**: en el aire el stick corre el cuerpo (un resorte) y la moto recibe la reacción −I·α: gira mientras el cuerpo se
    mueve y vuelve cuando el cuerpo vuelve (momento angular conservado); encima el enderezado (`air_roll_assist`) y la
    alineación de guiñada (`air_yaw_align`) la traen. Nada en el labio le daba giro propio, y el scrub volaba igual (1.57 m con
    el stick en la cara contra 1.73 derecho, sólo por el camino que tuerce).
- **Qué se hizo** (claves nuevas, 0 = como antes, guardadas con `if`; las dos físicas):
  - **`whip_takeoff`**: en la cara de un salto (apoyada atrás, la trompa a más de 5°, subiendo a más de 1 m/s, más de ~22 km/h y
    **el labio empujando**: más de 1.4-1.8 g en las ruedas) el stick al costado (más de 0.2) es la intención de whip
    (`whipIntent`: sube en 0.15 s, se sostiene 0.25 s después del empujón, hasta despegar). El piloto patea la cola: el suelo
    empuja la trasera de costado con `whip_takeoff` × su carga × `v/12` (hasta lo que agarra la cubierta), una fuerza en el
    contacto, abajo y atrás del centro de masa, que gira la trompa y acuesta la moto hacia el lado del whip. Va aparte de la
    cubierta: en el labio la trasera, descargada y con gas, patina y su roce cinético la anulaba (se probó sumándola a la
    fuerza lateral de la cubierta: no cambiaba nada). Con la moto cargada en la cara las cubiertas la aguantan; cuenta sobre
    todo en el último instante, cuando se descarga;
  - **`whip_air`** (0..1): en el aire el whip no vuelve solo. El enderezado y la alineación de guiñada se apagan mientras la moto
    está cruzada (de 4° a 12° de guiñada contra el vuelo o de rolido); el cuerpo ya no empuja por reacción (se dibuja más
    derecho que la moto); el stick es una velocidad de giro sobre el **eje del whip** (adelante y abajo de la moto: rolido y
    guiñada para el mismo lado) que el torque persigue (`whip_air_rate` 1.8 rad/s, `whip_air_gain` 300, `whip_air_torque` 350
    Nm): para el mismo lado lo acentúa, para el otro lo frena y lo trae; suelto, `whip_air_damping` (200) lo frena sin traerlo.
    El giro de rolido y guiñada que no es del whip (el que mete el efecto giroscópico de las ruedas: la guiñada del whip las
    hace rolar para el otro lado y la moto se daba vuelta) se frena siempre con lo mismo. El stick entra entre 0.1 y 0.25 s de
    vuelo: un saltito doblando con la D apretada no es un whip;
  - **`whip_land_crash_deg`** (50): aterrizar con la moto cruzada más que eso (en el suelo que pisa, hacia dónde apunta contra
    hacia dónde va) es caída. Sin la regla, a 56-90° la moto no se caía: daba un trompo de 100-165° y seguía;
  - el estado, en todas las motos (aunque las claves estén en 0): `whipIntent`, `whipLaunch` (con cuánto despegó, hasta el
    próximo despegue), `whipYaw` (la trompa contra el vuelo, + a la izquierda: un whip a la derecha da rolido + y `whipYaw` −), `whipPeakYaw`/`whipPeakRoll` (lo máximo del
    vuelo) y `whipCrossedLanding`; en la telemetría, la línea `whip` (`wyaw wint wlaunch wpeak wpeakroll wcross`);
  - valores: 1 / 1 / 1.8 / 200 / 50 en la 2T, la Trilheira y la Trial; en la 450 y la 600, comentados (después, prendidos:
    ver "La 450 y la 600 con el equilibrio por la carga y el whip").
- **Probado y descartado**:
  - el stick en el aire como velocidad de giro con el suelto conservando todo: la moto seguía girando de guiñada hasta 120-160°;
    frenando sólo el eje del whip, el efecto giroscópico la daba vuelta (rolido −107°). `whip_air_damping` 80: el whip del labio
    abría más (−20° de cruzada) pero sostener el stick 0.3 s ya llegaba a 55°; 150-200 deja 20-25°;
  - `whip_air_rate` 2.5: con el `whip` de siempre (0.6 s de stick) se cruzaba 60-93°;
  - `whip_land_crash_deg` 45: un aterrizaje a 44° que la moto salvaba (la cola a 66° y vuelve) era caída;
  - la intención de whip sin mirar la carga: el bot de la Trilheira, doblando en las subidas de la favela (trompa arriba,
    subiendo), pateaba la cola y se iba contra las paredes: 24 caídas en 20 min contra 3. Una subida carga ~1 g; la cara de un
    salto a 45 km/h, 3-4 g.
- **Se comprobó** (`prueba/salto_whip`, 50 km/h; antes → después; ver la tabla de `tools/whip.py`):

  | 2T | despega: rolido, `wr`, guiñada `wy` | sube | en el aire: rolido / cruzada máx. | toca: rolido / cruzada | 1 s después: cola máx. |
  |---|---|---|---|---|---|
  | derecho (`n`) | 0° | 1.73 → 1.74 m | 0° / 0° | 0 / 0 | 0 |
  | stick en la cara y suelto (`whipkick50`) | 28°, +0.74, −0.61 → 38.5°, +0.03, −1.87 rad/s | 1.57 → 1.47 m | 31 / −2 → 39 / −23 | −2 / −1 → 36 / −5 | 0.6 → 13° |
  | ... y lo trae (`c0.35`) | igual | igual | 31 / −4 → 39 / −23 | −6 / −2 → 12 / +10 | 1.2 → 7° |
  | ... y lo sostiene 0.3 s (`h0.3`) | igual | igual | 57 / −7 → 46 / −27 | 1 / −5 → 44 / −11 | 4 → 18° |
  | sólo en el aire (`a`) | 0° | 1.73 → 1.74 m | 26 / −3 → 23 / −21 | 4 / 0 → 22 / −16 | 0 → 10° |
  | `whip` (`--drop 1`, 0.6 s de stick) | — | — | 28 / −7 → 35 / −37 | 4 / −3 → 35 / −27 | 5 → 49° (sin caída) |
  | `whiphold` | — | — | 28 / −7 → 50 / −47 | 17 / −6 → 49 / −53 | caída (más de 50° cruzada) |

  - la Trilheira, la Trial y la 450 con el bloque prendido, parecido: despegan con 31-39° y −1.2 a −1.7 rad/s de guiñada (antes
    27-29° y −0.5 a −1.5, enderezándose a +0.7-0.8 rad/s), el whip queda (toca a 31-39° sin traerlo, antes −1 a −3°), con el
    stick al otro lado 0.35 s lo traen (4-13° de rolido; la prueba lo tira un tiempo fijo y queda cruzada 12-25° para el otro
    lado) y suben 1.46-1.52 m contra 1.66-1.81 derecho (el scrub vuela 8-17% más bajo; antes, con el stick en la cara,
    1.53-1.57 m, porque el camino torcía; la Trial casi igual que antes: 1.53 → 1.52);
  - la Trial con el `whip` de 0.6 s de stick cae sobre la delantera a −50° de cabeceo cruzada 26° y acostada 42° (la prueba no
    maneja el cabeceo) y se cae; la 2T, la Trilheira y la 450 lo salvan (la cola a 49-53° y vuelve);
  - las capturas (de atrás y de costado) muestran la moto saliendo del labio acostada, cruzada en el aire con el piloto más
    derecho que ella, y traída antes de tocar;
  - bots: ver arriba ("El equilibrio era un giroscopio"); el bot no tira el stick en el aire y la intención no se dispara
    doblando en subidas (sólo con el labio empujando);
  - la 450 y la 600, con las claves en 0 (`tuning.ini`, `motocross.ini`, `motocross600.ini`): `tools/regresion.sh` contra la
    v0.3.8 idéntico en las 9 de siempre más `accel`, `whipL`, `whiphold`, `airlean`, `frenada60`, la 600 (bot, `whip`,
    `circle12`) y el bot de la 450 en la favela y en Los Médanos; el bot de la 450 en la pista, 1:04.26 / 1:04.10, y la 600
    1:04.28 / 1:04.18 (igual que el paquete).
- **Lecciones**:
  - **Una fuerza interna no cambia el giro**: el cuerpo moviéndose respecto de la moto sólo la rota mientras se mueve y la
    devuelve al volver (por eso el whip de antes era una animación). El giro propio sale del suelo: en el labio, una fuerza
    en el contacto. En el aire, "dosificar" es un torque que persigue una velocidad (como el cabeceo con el stick).
  - **En el labio la trasera patina**: descargada y con gas, el roce cinético reemplaza la fuerza lateral de la cubierta;
    algo que el piloto hace "con la cubierta" ahí hay que aplicarlo como fuerza aparte, con el tope de agarre.
  - **El efecto giroscópico convierte guiñada en rolido**: con las ayudas apagadas, un whip con guiñada termina con la moto
    dada vuelta si nadie frena el giro que no es del whip.
  - **Toda intención que se dispara por una condición geométrica la va a encontrar el bot en otro lado**: "trompa arriba y
    subiendo" es también una subida de la favela. Agregar la física del caso (el labio empuja 3-4 g) separa las dos.
  - **Una regla de caída necesita su umbral medido**: sin la regla, un aterrizaje cruzado de 60° era un trompo con el piloto
    arriba; con 45°, una salvada de 44° pasaba a caída. Barrer cuánto tiempo se sostiene el stick (`hT`) da la curva.

### La Trial salía despedida al caer alto (a pedido; sólo la Trial)
- **Pedido**: "la suspensión de la trial 300 me encanta [...] pero tiene un bug a veces cuando caes alto y en ciertas
  posiciones que la moto sale despedida para arriba". Después: "que amortigüe tanto como lo hace, que es perfecto, pero
  que vuelva a su posición suavemente, que no sea como un resorte".
- **Cómo se midió**: `--flat --bike base/trial --drop H --test teclas:v0.1,4` (la flecha abajo 0.1 s en el aire: cae de
  cola a ~25-35°; `^` cae de trompa; `v0.05` ~10°, `v0.2` ~53°) con la telemetría cada paso: velocidad vertical al
  tocar, la máxima hacia arriba después (`vy`), cuánto sube el chasis sobre el reposo (`h` 0.78), si las dos ruedas se
  despegan, la carga mínima de las cubiertas al volver y la compresión máxima. Para ver qué toca el chasis, un `printf`
  temporal en `ContactFlag::Note` (punto del contacto en el espacio de la moto y velocidad por la normal).
- **Qué pasaba** (antes → la causa):
  - **cayendo de cola** desde 3-8 m: la trasera sola no alcanza a frenar la caída (la Trial es blanda y su hidráulico se
    abre a 4.5 m/s: `*_max_damper_velocity`); la cubierta se aplasta ~16 cm y la esfera de tope de la rueda trasera
    (`SphereShape(0.17)` en el anclaje, `Bike::BuildShapes`) pega en el piso a 4.6-8.7 m/s. Jolt la frena en un paso como
    un cuerpo rígido (moto + piloto, inercia 44): el cabeceo pasa de -3 a -13 rad/s. La trompa pega después a 5-9 m/s con
    la moto girando y ese giro se vuelve velocidad hacia arriba (la delantera queda de pivote): de -1.8 a +2.4 m/s en un
    paso. Desde 6 m a 32°: sube 0.41 m sobre el reposo, 3.1 m/s, las dos ruedas en el aire 0.18 s y queda en una rueda;
  - **cayendo de trompa** (~-30°), lo mismo al revés: la esfera de adelante la hace girar y la cola pega;
  - **cayendo derecha** no hay cabeceo: la esfera de adelante pega igual (2.3 m/s desde 2 m, 8.8 desde 6) y frena sin
    despedirla. Pero al volver el chasis sube a 1.6-1.75 m/s con las cubiertas casi sin carga (130-250 N de 1420): el
    resorte progresivo guarda mucha fuerza en el último tramo y la extensión, amortiguada como cerca del sag
    (`rebound_ratio` 3.2: ~0.5 de la crítica al fondo), lo tira para arriba. Ese es el "resorte".
- **Qué se hizo** (claves nuevas; en `tuning.ini` valen lo de antes, guardadas con `if`):
  - **`*_rebound_bottoming`** (`Suspension.cpp`): la amortiguación de la extensión × (1 + K · smoothstep(`*_rebound_bottoming_from`
    = 0.5, 1, compresión)), **sólo con la cubierta apoyada**. La compresión no cambia. La Trial: 3 (hasta 4 veces al fondo).
    Sin la condición de apoyo, en el circuito de obstáculos la horquilla quedaba hundida después de pegar en el tronco, la
    moto bajaba del otro lado con la trompa más abajo (-30 a -41° en vez de -18 a -34) y el bot se caía una vez de cada
    siete vueltas; con la condición, la rueda en el aire se estira como siempre;
  - **`hard_landing_spin`** (`PhysicsWorld.cpp`, `ContactFlag::HardLanding`): en un contacto de la moto con algo fijo
    con la normal hacia arriba (> 0.5), acercándose a más de 2 m/s y cuyo empuje la haría girar **más** para el lado que
    ya gira, Jolt la ve con la inercia de giro dividida por esto (`mInvInertiaScale`). Los golpes que frenan el giro (la
    trompa que pega después, el aterrizaje derecho) no se tocan. Sólo con el piloto arriba (`PrePhysics` lo pone cada
    paso; sin piloto, 1). La idea: la mitad de la masa es el piloto, que no está atornillado; frena con las piernas en
    los posapiés, cerca del centro de masa, y casi no hace girar la moto. La Trial: 0.3 (0.5 dejaba 1.4-1.8 m/s en
    algunas caídas de cola; 0.25-0.3, ≤ 1.6).
  - **Probado y descartado**: subir sólo `*_rebound_ratio` (6, 10): baja el rebote derecho pero desde 6 m de cola sigue
    saliendo a 2.75 m/s (es el golpe rígido, no el resorte); `*_rebound_bottoming` 12: la rueda no llega a seguir el piso
    y se despega; escalar la inercia en **todos** los golpes contra el piso: arregla las caídas de cola pinchadas pero
    empeora las de ~10° (2.5-2.8 m/s): en el golpe de la trompa, con menos inercia, el giro se vuelve más velocidad
    hacia arriba. De ahí la regla de "sólo si la haría girar más".
- **Se comprobó** (`--flat --bike base/trial --drop H`, antes → después):

  | caída | vy máx. hacia arriba (m/s) | sube sobre el reposo | ruedas en el aire | carga mín. al volver | compresión / h mín. |
  |---|---|---|---|---|---|
  | derecha 1 m | 1.35 → 0.83 | 0 → 0 | no | 200 → 632 N | 0.98 / 0.58 igual |
  | derecha 2 m | 1.60 → 1.02 | 0 → 0 | no | 135 → 186 N | 1.00 / 0.53 igual |
  | derecha 4 m | 1.75 → 1.13 | 0 → 0 | no | 157 → 580 N | 1.00 / 0.49 igual |
  | derecha 6 m | 1.46 → 1.01 | 0 → 0 | no | 248 → 1020 N | 1.00 / 0.49 igual |
  | de cola 23°, 3 m | 2.21 → 1.47 | 7 → 0 cm | 0.03 s → no | 0 → 105 N | 0.99 / 0.50 → 0.51 |
  | de cola 11°, 5 m | 2.33 → 1.33 | 9 → 0 cm | 0.05 s → no | 0 → 270 N | 1.00 / 0.47 → 0.49 |
  | de cola 32°, 6 m | 3.09 → 1.15 | 41 → 0 cm | 0.18 s → no | 0 → 39 N | 0.92 / 0.56 → 0.62 |
  | de cola 37°, 8 m | 2.52 → 0.87 | 23 → 0 cm | 0.07 s → no | 0 → 420 N | 0.98 / 0.56 → 0.59 |
  | de cola 53°, 4 m | 1.90 → 0.83 | 16 → 0 cm | 0.05 s → no | 0 → 601 N | 0.92 / 0.66 → 0.69 |
  | de trompa -32°, 6 m | 2.15 → 0.23 | 33 → 0 cm | no | 49 → 1355 N | 1.00 / 0.76 → 0.74 |

  - aterrizando derecha, lo que tarda en llegar al fondo es igual (0.05-0.08 s) y en volver al reposo (a 1 cm) casi
    igual (0.39 → 0.41 s desde 0.5 m, 0.61 → 0.62 desde 2 m, 0.67 → 0.68 desde 3 m), sin pasarse: vuelve parejo en vez de
    tirar el chasis para arriba y flotar;
  - bots con la Trial (antes → después, sin caídas en ninguno): circuito de obstáculos 37.4-37.5 s → 37.4-37.5 (7
    vueltas), favela 1:36.0-1:36.3 → 1:36.0-1:36.2, motocross 1:04.6-1:04.7 → 1:04.4-1:04.5, `prueba/park` 51.5 → 51.3,
    `valle_trial` 41.2 → 41.2, `motocross_trial` 1:08.3-1:08.4 → 1:08.1-1:08.3;
    con la masa corrida ±0.01 kg (para salir de la trayectoria exacta), 600 s del circuito de obstáculos: 0 caídas antes
    y después;
  - las demás motos no tienen las claves: `tools/regresion.sh` idéntico contra la v0.3.7 en las 9 pruebas de siempre
    (más `accel` y una caída de cola desde 6 m) con la 450, y caídas, `flip` y bot con la Trilheira, la 2T, la de carreras
    y la 600. La 450 también hace el golpe de cola (desde 5 m a 9°, 1.9 m/s y las ruedas 0.03 s en el aire) pero más
    suave (más inercia: 1.63 m/s desde 6 m a 28°); se dejó como está.
- **Trampa** (`SuspensionParams`): `Bike.h` lo inicializa **por posición** (`front{0.30f, 6800.0f, ...}`). Un campo
  nuevo en el medio del struct corre todos los valores (el `bump_stop` caía en el campo nuevo) y cambiaba todas las
  motos sin tocar ningún `.ini`. Los campos nuevos van al final.

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
    ~30 km/h, 14° desde ~70, con la dirección a fondo; con media dirección ~60%; después, 24 y 24 en las de tierra) y la frena si se pasa (no hay trompo);
  - la delantera sigue su camino (contravolante automático apagado) apuntada `slide_steer_in` (6°) hacia la curva: empuja
    hacia adentro y **cierra la curva** sin frenar de costado. En la 031, además, la moto iba más derecha (`slide_upright`;
    después se cambió por `handbrake_lean`, abajo);
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
- **Después: mientras colea, la moto se enderezaba; y la cola entraba y salía de golpe** (a pedido: "el derrape está
  bueno, pero es difícil mantener la moto inclinada derrapando, tiende a enderezarse, quedaría mejor si queda inclinada para
  el lugar que estás doblando mientras colea"; probándolo: "no siento que se mantenga la inclinación de la moto al derrapar";
  y "agregá más suavidad en cuando se entra al derrape y cuando se sale del derrape, ahora mismo se siente muy snappy").
  - **Cómo se midió** (desde `pruebas/`, `prueba/plaza_tierra`, las cinco motos, con las rampas del teclado):
    - la inclinación (roll) en el tramo del freno de mano (desde 0.4 s después de clavar y con más de 10 km/h) contra la que
      traía y la que pide el balance (`leanT`), a 30/50/70/90 km/h: `clava` (D y Espacio hasta parar), `reclava` (suelta el
      Espacio 0.35 s y vuelve a clavar), `medias` (la D a medias: toques de 0.12 s cada 0.07 s, ~40-50% de dirección),
      `sale` (1.2 s y sale con gas), `toques` (tres toques de Espacio) y `cambia` (de la D a la A clavando); también en
      `plaza_asfalto_ancha`, `plaza_pasto` y `plaza_calle` a 40/80 km/h. Y cada 0.25 s del tramo (lo que se siente);
    - la suavidad, con `W+V,D1,D_1.5,WD2.5`: al entrar (0.8 s desde que clava) y al salir (1 s desde que suelta), el pico
      de la guiñada del mundo (`wy`/cos `roll`, °/s), el de su derivada (°/s², promediada en 3 pasos de 0.02 s) y el de la
      velocidad con que cambia la cola (`beta`, °/s);
    - la salida con gas, un barrido de 42 casos por moto (`W+V,D1,D_T,WD3`: V de 20 a 50 km/h, T de 0.6 a 2 s) contando
      trompos (cola de más de 90°).
  - **Qué pasaba** (v0.3.7):
    - la moto entraba a ~45° y al clavar bajaba a **18-20° de media (mínima 13°)** en la 450 y la 600, siempre; con media
      dirección, **7-9°** (casi derecha). En la 2T, la Trilheira y la Trial, 23-29° de media con mínimas de **4-8°**;
    - **al clavar**, la guiñada saltaba de ~30 a 135-330°/s en 0.1 s (2000-4300°/s²) y la cola salía a 135-255°/s; el
      manubrio pasaba de +17° a −12° en 0.15 s. **Al soltar**, la guiñada se daba vuelta (de −20 a +88°/s en 0.08 s:
      2300-2500°/s² en la 450) y la cola volvía a 195-325°/s.
  - **Por qué**:
    - el balance lleva la moto a `leanTarget` (la inclinación sigue a `leanT` ±2-5°), y durante el derrape `leanTarget`
      se achica: en la 031, `× (1 − slide_upright · handbrake)` = × 0.45 (de ~45° a ~20°); en la nueva,
      `× (1 − slide_upright · trabada)` con la trasera trabada (lo mismo). Encima, despacio `leanTarget` cae con v²;
    - el freno de mano se prendía y se apagaba de golpe: `handbrake` va de 0 a 1 entre el 60 y el 95% del pedal (con la
      rampa del teclado, 0.04 s), y con él el PD de la cola (hasta 3000 Nm hacia `handbrake_angle`), el manubrio que sigue
      su camino y el contravolante apagado. Al soltar, el atajo (`pd(0)`) empujaba con todo hacia derecho desde el primer
      paso y el manubrio y el contravolante volvían en un paso.
  - **Qué se hizo** (claves nuevas, 0 en `tuning.ini` = como antes, guardadas con `if`):
    - **`handbrake_lean`** (40 en `motocross.ini`, `motocross600.ini`, `trilheira.ini`, `trial.ini` y `dostiempos.ini`): con
      el freno de mano el piloto sostiene la moto inclinada hacia la curva (`hbLean`, en `UpdateHandbrake`): 40° con la
      dirección a fondo, ×0.6 con media dirección, **firme mientras dura el derrape** (no baja con la velocidad) y afloja
      recién entre ~11 y ~8 km/h (`Smoothstep(2.2, 3.2 m/s)`), cuando termina. `Bike::HandbrakeLean` (las dos físicas)
      lleva `leanTarget` hacia eso sin enderezarla nunca más que antes (toma la mayor hacia la curva). **Al soltar la
      levanta** (`slide_upright`, y vuelve a la normal en 2 × `handbrake_catch`): sale traccionando;
    - **`handbrake_ramp`** (0.4 s en las mismas cinco): `hbIn` sigue a `handbrake` a 1/0.4 por segundo. Al clavar, el
      ángulo que busca la cola sube con `hbIn` (sale en ~0.4-0.6 s, sin pasarse: el PD sigue entero). Al soltar, lo que
      busca el atajo baja a 0 en 0.4 s desde donde estaba (`hbCatchWant`) en vez de ir a 0 de golpe, y el manubrio que
      sigue su camino, el contravolante apagado, la inclinación sostenida y el levantarla pasan de a poco (`hbW` =
      la mayor entre `handbrake` y `hbIn`);
    - `handbrake_angle_fast` 24 (era 14) en las cinco: inclinada a 40-45° la cola quedaba en 15-19° a 70-90 km/h.
  - **Probado y descartado**:
    - llevar `leanTarget` a `hbLean` sin el máximo: la Trial a 30 km/h, que ya iba a ~50°, quedaba a 26°: la enderezaba;
    - la primera versión (38°, ×0.5 a ~10 km/h, `Smoothstep(3, 12 m/s)`): la media daba 30-35°, pero el usuario lo probó y
      no lo sintió. Cada 0.25 s desde que clava a 50 km/h: 41, 38.6, 38, 36.8, 34.6, 31.7, 28.4, 25.4, 22.8, 20.9°: el
      derrape frena ~0.4 g y en ~2 s ya estaba en 20-25°. Ahora: 43, 42, 40, 41, 42, 43, 43, 43, 43, 43° hasta ~12 km/h;
    - sostener la que traía al clavar (si era más): la moto queda 3-5° más acostada que lo pedido y al volver a clavar eso
      era lo nuevo a sostener: subía hasta 60-80° y se caía (11 caídas en 120 maniobras). Con un valor fijo, ninguna;
    - sin levantarla al salir: trompos saliendo con gas en el barrido (450 1, 600 6, 2T 5, Trilheira 13 de 42; la v0.3.7:
      0, 0, 0, 5). Levantándola 0.6 s: 0, 3, 0, 5; 1.2 s: 0, 0, 0, 3;
    - rampa también en el derrape lento (`slideIntent`) de la física nueva: casi no suavizaba y el cavalo de pau de la
      Trilheira bajaba de 57° a 46°. Sacarlo al soltar de a poco: nada. Que mande `HandbrakeSteer` también debajo de
      `slide_speed_max`: la cola de la Trilheira se iba a 76°;
    - `handbrake_ramp` 0.3 / 0.5: 0.3 suaviza menos al entrar (450 a 50: 969 contra 880°/s²); 0.5 casi igual que 0.4 y la
      cola tarda más (a 30 km/h el derrape dura ~1 s).
  - **Se comprobó**:

    | `clava` a 30 / 50 / 70 km/h | antes: incl media (mín.) | después: incl media (mín.) | antes: cola, camino, g | después: cola, camino, g |
    |---|---|---|---|---|
    | Motocross 450 | 20 (15), 19 (13), 18 (13) | 38 (38), 43 (40), 44 (42) | 25/28, 28/30, 28/30; +47/+82/+100; 0.48/0.41/0.41 | 14/24, 23/25, 23/24; +66/+88/+98; 0.55/0.40/0.39 |
    | Motocross 600 | 20 (13), 19 (13), 18 (13) | 42 (40), 45 (43), 46 (43) | 27/28, 28/30, 26/30; +63/+100/+116; 0.42/0.40/0.41 | 22/25, 22/24, 22/25; +76/+98/+108; 0.44/0.38/0.39 |
    | 2T | 29 (14), 26 (8), 26 (6) | 46 (43), 45 (42), 43 (41) | 25/34, 30/46, 27/50; +12/+63/+99; 0.35/0.39/0.44 | 22/29, 26/38, 26/42; +12/+53/+98; 0.34/0.37/0.42 |
    | Trilheira | 28 (17), 23 (6), 24 (4) | 45 (43), 46 (42), 45 (41) | 24/30, 37/53, 29/60; +7/+61/+91; 0.35/0.45/0.48 | 22/30, 28/44, 24/60; +23/+56/+78; 0.41/0.42/0.45 |
    | Trial | 50 (40), 27 (16), 28 (15) | 44 (41), 43 (41), 42 (40) | 13/28, 28/42, 24/43; +49/+41/+73; 0.53/0.47/0.52 | 14/37, 22/34, 22/35; +6/+35/+74; 0.43/0.47/0.52 |

    | suavidad (`W+V,D1,D_1.5,WD2.5`) a 30 / 50 / 70 km/h | antes: entra (guiñada °/s, °/s², cola °/s) | después: entra | antes: sale (°/s², cola °/s) | después: sale |
    |---|---|---|---|---|
    | Motocross 450 | 328/224/159; 4251/3099/2009; 255/180/135 | 185/116/83; 1386/916/730; 90/70/55 | 697/2479/2325; 325/285/195 | 687/747/1168; 275/90/100 |
    | Motocross 600 | 257/140/127; 3681/1737/1920; 190/115/100 | 125/73/70; 892/551/866; 65/55/55 | 1658/2296/2030; 295/195/150 | 1651/1151/1059; 95/90/95 |
    | 2T | 166/167/119; 2731/2111/1389; 150/115/90 | 141/108/76; 2440/1074/584; 125/75/50 | 844/1818/1716; 1090/250/135 | 811/1621/1143; 185/130/105 |
    | Trilheira | 211/114/110; 2344/2751/1201; 230/120/75 | 191/108/80; 1930/1886/751; 145/90/55 | 640/3136/2173; 480/310/180 | 602/1898/1167; 190/185/100 |
    | Trial | 199/186/139; 4319/3183/1907; 230/145/100 | 189/106/90; 3699/2004/862; 165/95/55 | 1556/2693/3571; 330/245/195 | 1525/1383/1502; 205/140/110 |

    - **cola sostenida** (mediana del tramo): 450 24/24/23 a 30-50/70 km/h y 22 a 90 (antes 24-29), 600 21-24 (antes 20-28),
      las otras 18-30. En la 450 a 30 km/h la mediana de todo el tramo baja (14°) porque la rampa ocupa 0.4 s de un derrape de
      ~1 s; el pico sigue en 24°;
    - la inclinación queda pareja de punta a punta (desvío ~1°), 2-5° arriba de los 40 pedidos; **media dirección**:
      450 de 9/8/7° a 30/29/29°, 600 de 9/7/6° a 30/29/26°, las otras de 14-25° a 25-34°; **soltar y volver a clavar**:
      antes 13° ↔ 42°, ahora 38-45° de media (mínima 28-43°);
    - la física nueva mejora menos a 30-50 km/h: ahí manda el derrape lento (`slideIntent`, debajo de `slide_speed_max`),
      con su manubrio que oscila ±15° a ~3 Hz (ya estaba; ver pendiente);
    - al soltar con gas sale limpia en todas; barrido de salida: trompos 0/0/0/6/0 de 42 (la v0.3.7: 0/0/0/5/0; los de la
      Trilheira, a 20-35 km/h, son de su derrape lento, en casos parecidos). La 600 hace un wheelie de ~47° saliendo con gas
      en los 42 casos, igual que en la v0.3.7;
    - exagerando (`cambia`, `toques`, `reclava`, desde 90 km/h, en tierra, asfalto, pasto y calle): ninguna caída ni trompo
      (270 maniobras);
    - el derrape lento (`brakeslide2.5`): la Trilheira 57° → 59°, la Trial 63° → 60°, la 2T 38° → 43°. Sin caídas;
    - sin las tres claves (`handbrake_lean`, `handbrake_ramp`, `handbrake_angle_fast` de `tuning.ini`) en las cinco motos,
      idéntico a la v0.3.7, también con el freno de mano (`brakeslide`, `brakeslide2.5/3.5/b` de las 6 motos y `teclas:`
      de `clava`, `clava_sale` y toques en la 450, la 600, la Trilheira y la Trial);
    - regresión contra la v0.3.7: las 9 de siempre idénticas menos `brakeslide` (usa el freno de mano); `cuerpo`,
      `wheelie`, `accel`, `frenada60`, `frenacurva60x1d-1a1`, `brakestraight`, `circle12`, `whip`, `flip` y `airrot` de las
      6 motos (60), idénticas; los bots idénticos (motocross 450 1:04.26 / 1:04.10, la 600, la 2T y la Trial en la pista de
      motocross, la Trilheira en la favela 300 s, el circuito, el parque, Los Médanos y el valle).
  - **Pendiente**: en la física nueva, entre ~20 y 45 km/h, el manubrio del derrape lento oscila (−15° ↔ −45° a ~3 Hz y la
    delantera empuja para los dos lados). Lo hace el camino de la delantera con toda la velocidad angular (la trampa de
    arriba), pero cambiarlo toca el derrape lento de todas las motos.
  - **Lecciones**:
    - **La inclinación la decide `leanTarget`, no la física del derrape**: el balance es fuerte y la moto se queda a ±2-5°
      de lo que pide. Si la moto "se endereza sola", buscar qué achica `leanTarget` (`slide_upright`, `brake_stand_up`, la
      caída con v² debajo de `min_turn_radius`), no el agarre.
    - **Lo que se siente es la evolución, no la media**: una media de 35° que arranca en 41 y termina en 20 se siente "se
      endereza". Medir cada 0.25 s del tramo; un derrape frena ~0.4 g, así que algo que dependa de la velocidad se va en
      1-2 s. Sostener = constante hasta que el derrape termina.
    - **"Snappy" se mide con la derivada**: el pico de la guiñada y, sobre todo, el de su derivada (°/s²) y el de la
      velocidad de la cola, en ventanas al entrar y al salir. Un interruptor de 0 a 1 (el pedal del 60 al 95%) prende todo
      en 0.04 s aunque el teclado tenga rampa.
    - **Rampa en lo que busca, no en la fuerza**: subir de a poco el ángulo buscado con el PD entero hace que la cola llegue
      sin pasarse; al soltar, bajar lo que busca el atajo desde donde está (y no atajar más flojo) la trae suave sin trompo.
    - **No tomar como referencia un estado que el cambio mismo modifica**: sostener "la que traía al clavar" se
      realimentaba hasta caerse.
    - **Una clave que "sostiene" algo tiene que ser un piso, no un reemplazo**: cambiar `leanTarget` por un valor fijo
      enderezaba a la Trial despacio. Tomar el mayor hacia la curva sólo agrega.
    - **Más inclinada al salir = trompo con gas**: con 40-45° despacio, el gas hace patinar la trasera. El piloto levanta
      la moto para traccionar; medirlo con un barrido de salidas (velocidad × tiempo clavado), no con una o dos.
    - **Medir con la dirección a medias y soltando/volviendo a clavar**: con media dirección el problema era peor (7-9°),
      y al soltar el Espacio la moto subía y bajaba 30° cada vez.
    - **Las pruebas con guion cortan con la velocidad de adelante**: con la cola a ~70° (la Trilheira en el pasto) `D_-5`
      termina a ~20 km/h reales y el resto es "soltó todo"; mirar `beta` antes de leer el final como una frenada.
    - **Los `.ini` de la carpeta de compilación se copian sólo al enlazar**: si no cambió ningún `.cpp`, compilar no los
      actualiza; para probar un valor de la fuente, copiarlo a mano.

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

### Trucos: el cuerpo mueve la moto (octubre de 2026)
- **Pedido**: superman y los demás trucos con un botón, "físico, no scripteado" (botón, poses y pruebas en
  [PILOTO.md](PILOTO.md), "Trucos en el aire").
- **Qué se hizo** (`Bike::UpdateTrick` en BikePhysics.cpp, en las dos físicas): el piloto son 11 masas (las del ragdoll,
  75.6 kg) en la pose neutra del estilo con el truco a `x` (0..1, un resorte crítico de `trick_speed`; `Bike::TrickBody`); la
  moto sola es el resto de la masa, puesta donde el centro de masa de todo con la pose neutra es el del cuerpo de Jolt.
  Lo que el cuerpo suma al momento angular (marco de la moto, respecto del centro de masa de todo, que se corre `c(x)`):
  `H = ΔI(x)·ω + G(x)·x'` (G: el momento del movimiento del cuerpo por unidad de x'). Torque sobre la moto:
  `-(dΔI/dx·x'·ω + dG/dx·x'² + G·x'' + ω × H)` (`trickTorque`), con las derivadas en x por diferencias. Además:
  - **La inercia del cuerpo estirado vale para todo**: `(I0 + ΔI)·ω' = torques`; el cuerpo de Jolt tiene I0, así que todo el
    torque del paso se multiplica por `I0·(I0 + ΔI)⁻¹` (`trickScale`) mientras dura el truco. Sin esto, el empuje de la rueda
    frenándose en el aire (la que gira la trompa abajo en todo vuelo) giraba la moto estirada como si no lo estuviera y, al
    volver, seguía girando un 35% más rápido que sin el truco (medido con superman: la diferencia de cabeceo crecía 1°
    cada 0.1 s después de terminar; con esto queda fija en +3°).
  - **El centro de masa de todo se corre** (`trickCom`, sumado a `comShiftWorld`): las fuerzas del suelo al tocar
    giran respecto de él.
  - En el truco el piloto casi no endereza la moto en el aire (el `hold` del enderezado y la guiñada, × (1 - 0.5·x)).
  - Tocar el piso a medio volver (`TrickTouch`): más de `trick_crash` (0.4) es caída; más de `trick_hit` (0.12),
    `trickHitAmount` afloja el balance en el piso (K -70%, D -50%) y se va en 0.6 s.
- **Se comprobó**: con el truco y sin él (el mismo lanzamiento, `--flat --drop 3 --test trucosuperman` contra
  `trucosupermanp9`), cabeceo y rolido cada 0.1 s: superman, la trompa +4.5° al estirarse y +3° al terminar; heel
  clicker +3.4 / +0.4; no-hander -2.1 / +1.4; nac-nac rolido +2.1° al salir y -3.0° al volver; can-can -2.0° / +3.0°.
  Nada corre sin el botón (todo detrás de `trickAmt > 0` o `in.trick`): regresión idéntica y bots iguales (PILOTO.md).
- **v0.3.10**: la pose del truco (`ApplyTrickPose`, la que usa `TrickBody`) cambió para sacar tirones del dibujo (el tope del
  alcance de la pierna suave y el pie del nac-nac por atrás del guardabarros; ver [PILOTO.md](PILOTO.md), "Saltos de la pose"):
  la reacción sobre la moto cambia un poco (la 450: cabeceo neto del heel clicker +3.9 → +7.0°, del nac-nac +5.1 → +9.8°; los
  tiempos de volver no cambian y tocar el piso con el truco entero sigue siendo caída).

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
