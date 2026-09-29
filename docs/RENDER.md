# Gráficos: problemas resueltos y reglas

Render en `Render.cpp` (shaders de luz, sombras, cielo, terreno, pasto, post-proceso). Las mallas de los
mapas se arman por código (`Favela.cpp`, `Circuit.cpp`, `BikeMeshes.cpp`).

## Reglas

- **Dos superficies en el mismo plano se pisan** (z-fighting): titilan, se ven rayas o bordes dentados
  que cambian al moverse la cámara. Una capa encima de otra (calcomanía, franja, cartel) va **separada**
  o en otro plano. Cuánto: la profundidad es de 24 bits (medido en esta PC, Intel, en la ventana y en la
  textura de la escena) y el plano cercano está en 0.08 m, así que el escalón de profundidad a z metros
  es z² / (0.08 · 2²⁴): 2 mm a 50 m, 7.5 mm a 100 m, 3 cm a 200 m. Hace falta 2-3 veces eso: **1 cm
  sirve hasta ~60 m, 3 cm hasta ~110 m, 6 cm hasta ~160 m**. En el piso, visto de costado, alcanza con
  menos (la separación a lo largo del rayo es mayor).
- **El caso que más pasa es una caja adentro de otra con una cara en el mismo plano**: el fondo oscuro
  del garaje con el frente en el plano del cuerpo, el muro blanco de la tribuna adentro del primer
  escalón, tres cajas del alero con el frente en el mismo plano, una franja corrida con un vector que no
  es perpendicular a la caja. Al armar una pieza con cajas, cada cara que se ve tiene que salir unos cm
  de las de al lado (o quedar adentro), y las puntas también (las de módulos vecinos se tocan).
- **Buscarlas con la herramienta**, no a ojo: `MOTOSIM_COPLANARES` (ver abajo) lista todas las del mapa.
- **Calcomanías espalda con espalda** (un cartel que se lee de los dos lados): cada una de una sola cara.
  `Renderer::DrawMeshTextured(..., twoSided)` dibuja las dos caras por defecto (el modelo del piloto lo
  necesita); para calcomanías, `twoSided = false`.
- **Módulos repetidos a lo largo de una curva** (garajes, tribunas, techos): tocándose justo, los vecinos
  quedan girados apenas y se solapan en una cuña con caras casi coplanares. Hay que dejar un hueco de
  unos centímetros o alternar la altura/profundidad entre vecinos.
- **Calcomanías de una cara** (favela): la cara de adelante es la que mira a `cross(derecha, arriba)`.
  La derecha del cartel tiene que ser la derecha de quien lo mira desde afuera; si no, mira a la pared
  y queda invisible (o espejado, de dos caras).
- **Algo fino pegado al suelo** (marcas de goma): la textura de huellas no sirve en los mapas grandes (un
  texel mide 34-50 cm en el autódromo y la plaza). Va como geometría apoyada en el terreno, sin escribir
  profundidad, unos mm arriba y corrida hacia la cámara a lo largo de la mirada en el shader (mismo píxel,
  otra profundidad); de lejos, ensanchada a ~1.5 px y aclarada en proporción (ver "Marcas de goma").
- Las sombras: dos cascadas (cercana 24 m y lejana 96 m, cada una en su textura; ver "Sombras en dos
  cascadas"), cada una con el foco enganchado a la grilla de sus texels (si no, los bordes titilan al
  moverse) y sesgo en metros del mundo (corrimiento por la normal y en profundidad, sin piso).

## Buscar superficies que se pisan

`MOTOSIM_COPLANARES=1` (o `=detalle`, que agrega los dos triángulos del par más grande de cada grupo)
hace que `Circuit::CreateMeshes` y `Favela::CreateMeshes` revisen sus mallas con `src/Coplanar.h`
antes de subirlas: todos los pares de triángulos mirando para el mismo lado (o cualquiera, si la capa
es de dos caras) a menos de 12 mm uno del otro y que se superponen más de 4 cm². Los agrupa por capa
y color, con el área, la separación mínima y máxima y un punto de ejemplo; los pares iguales (misma
capa, color, brillo y modo) no se ven y sólo se cuentan. En el circuito revisa además que ningún calco
del piso quede a menos de 4 mm del terreno. No cambia nada de lo que se dibuja. Hace falta ventana:

```
MOTOSIM_COPLANARES=detalle ./motocross.exe --size 640 360 --map base/circuito --test pose --time 0.3
```

- La separación "0.0-0.0 mm" es el mismo plano; "0.0-10 mm" suele ser una cuña (planos que se cruzan).
- Lo que dice no tiene en cuenta lo que tapa: una cara metida adentro de otra caja aparece igual. Mirar
  el ejemplo y descartarlo si está adentro de otra cosa.
- Para ubicarse en una posición `(x, z)`: `--test profile` da `s → x, z` cada 2 m, y `--spawn s`.

## Bitácora

### Marcas de goma en el asfalto (todas las motos, también las de los demás)
- **Pasaba**: en el asfalto (autódromo, favela, `prueba/plaza_asfalto`) cada rueda apoyada dejaba el mismo
  surco de tierra que en el barro: una banda marrón de 50-100 cm con borde claro y relieve, rodando normal
  y sin patinar (captura del paquete: una estela marrón borrosa detrás de la moto). No había marcas negras.
- **Por qué no alcanzaba la textura de huellas**: cubre todo el terreno con 4096²; en la plaza (2048 m) un
  texel mide 50 cm y en el autódromo (1380 m) 34 cm, y la huella de una cubierta mide 10-18. Dibujada ahí,
  una marca de goma sería otra banda borrosa de medio metro (en la motocross, 255 m, un texel mide 6 cm:
  por eso los surcos se ven bien en la tierra). Distinguir "goma" de "surco" en un canal no arregla el tamaño.
- **Qué se hizo**:
  - `TerrainDeformation`: en el asfalto (`PavedAmount`; entre 0.2 y 0.6 se apagan) no hay surcos; la tierra y
    el pasto siguen igual (con `PavedAmount` = 0 el trazo es el mismo de antes). Las marcas de goma son
    **tiras de cuadriláteros** apoyadas en el terreno (un anillo de 16384, 4 vértices cada uno: entra justo en
    índices de 16 bits; ~2 km de marcas, las nuevas pisan las más viejas). Un borde cada 12 cm, perpendicular
    a hacia dónde se movió la goma (derrapando no es hacia donde mira la moto), cada borde a la altura del
    terreno (sigue el peralte) + 4 mm. Sólo se suben a la GPU los cuadriláteros nuevos (`UpdateMeshBuffer`
    una vez por frame, en `Flush`).
  - Cuánto marca: por el **resbale de la goma** `hipot(ω·R − vLong, vLat)` (m/s), que ya llega en `WheelFx`
    (`spin`, `latVel`) también de los demás. El modelo de la cubierta es tipo "constraint": mientras le
    alcanza el agarre no resbala (rodando, ~0; acelerando a fondo, < 1 m/s; frenando con la S la de carreras,
    0.6), así que no hace falta un umbral fino: marca desde 1.5 m/s y del todo desde 9 (smoothstep), por la
    carga (0.3 + 0.7 · carga / 1300 N) y por lo pavimentado. Al límite (el patinaje del pico de agarre, 3-5
    m/s) queda una línea tenue; trabada o derrapando, oscura. Ancho: 12 cm la de adelante, 15 la de atrás
    (±15% con la carga y el resbale). Oscurece hasta el 73% por pasada y las pasadas se suman. Patinando sin
    avanzar (burnout, golpe de gas en la largada) deja manchas del largo de la huella.
  - Sobre una rampa, un escalón o una caja no marca (el contacto a más de 8 cm del terreno); las de los
    demás llegan siempre a la altura del terreno (no se sabe si están sobre un objeto).
  - Dibujo (`Renderer::DrawSkidMarks`, `kSkidVS`/`kSkidFS`): después de lo opaco del suelo, sin escribir
    profundidad, **mezcla multiplicativa** (`BLEND_MULTIPLIED` con el color en negro: destino · (1 − alfa)):
    oscurece igual al sol y a la sombra, y se va borrando con la niebla. Bordes suaves y vetas a lo largo.
  - **Sin z-fighting a ninguna distancia**: 4 mm arriba del terreno y, en el shader, el vértice se corre
    hacia la cámara **a lo largo de la mirada** 5 mm + 0.1% de la distancia (cae en el mismo píxel, sólo
    cambia la profundidad). Rasante, los 4 mm solos ya son mucha separación a lo largo del rayo (4 mm /
    sen del ángulo). Queda encima de las líneas pintadas y del asfalto de boxes (1 cm), como debe.
  - **De lejos, en vez de mipmaps**: una tira de 12 cm a 50 m, o vista de costado rasante, mide menos de un
    píxel y se rasteriza a los saltos (rayas que titilan). El shader mide cuántos píxeles mide media tira
    (`pixelAngle` = 2·tan(fovy/2) / alto de la escena) y la ensancha hasta ~1.5 px aclarándola en la misma
    proporción (en promedio oscurece lo mismo), hasta 16 veces.
- **Se comprobó** (Mac, capturas del paquete de referencia `build-mac` y del nuevo, mismas pruebas):
  derrape de costado con gas (`prueba/plaza_asfalto --bike base/carrera --test slide --view 200 25 0.8`):
  antes, banda marrón de ~70 cm; ahora, una marca negra de ~15 cm con vetas que sigue la curva, y donde la
  moto sale al pasto sigue el surco de siempre. Trasera trabada (`--bike base/motocross --test brakestraight`,
  en la plaza y en el autódromo, cerca y a ~40 m con `--view 160 10 6`): línea oscura y fina, continua de lejos.
  Frenando y doblando (`frenacurva120`): la de adelante al límite deja una línea tenue. Rodando normal
  (`--test cuerpo`): nada (antes, la estela marrón). Favela (bot): una marca corta donde la trasera patina al
  bajar la escalera, en vez del surco claro. La tierra (`base/motocross --test brakeslide`): los mismos
  surcos (la diferencia entre antes y después, 3.6% de píxeles > 24 niveles, es menor que entre dos
  corridas del mismo exe, 12.8%: pasto que se mueve, polvo). Anillo lleno a propósito (20000 tramos, da la
  vuelta): 50 líneas paralelas de 48 m, continuas de cerca a ~60 m y de costado rasante; 17.0 ms contra
  17.1 sin marcas (60 Hz con vsync; 64 k vértices contra los 793 k del autódromo). Autódromo con el bot, 30
  s: 17.1 ms los dos. Regresión idéntica (la lista de PRUEBAS.md y `frenada120` en la plaza): sin ventana no
  se llama.
- **Frenando al límite sin trabar (marca tenue)**: la de carreras con la S no traba ninguna rueda (la
  delantera frena al 60% del agarre y la trasera casi sin peso no llega a trabarse; con sólo el freno de
  atrás a 32 km/h, tampoco), así que por el resbale no marcaba nada. A pedido, sin tocar la física:
  `BrakingEdge` (Game.cpp) hace la cuenta del chirrido de `FeedTireSound` (`gripUsage` recortado a 1.3, por
  la parte de la fuerza que frena; de costado pasado el 90%, a la mitad) pero desde 0.45 hasta 0.8, y
  `WheelContact` lo recibe como `edge`: marca hasta un tercio de una trabada y sólo con peso (desde 300 N;
  la trasera casi en el aire usa mucho agarre relativo pero no apoya). Sólo la moto propia: `gripUsage` no
  viaja por la red (las de los demás, sólo por el resbale). Medido en metros de marca tenue:
  `frenada120` con la carrera, ~31 m de la delantera; `frenacurva120x0f0.3` (frenada suave), nada; el bot
  en el autódromo, ~27 m por minuto en las frenadas fuertes. En la captura (`--view 180 35 2.5`), una
  línea gris fina detrás de la delantera, bastante más clara que una trabada.
- **Quedan**: con el gas contra el freno de adelante (`--test burnout`) la trasera no patina en el asfalto:
  la moto empuja la delantera trabada, que es la que marca. Las pruebas que aparecen con gas (la moto cae
  con la rueda girando) dejan una mancha en la largada. En el asfalto sigue saltando "tierra" (partículas
  de `EmitWheelEffects`, según `Dustiness`), y no hay humo de goma.

### Profundidad de 24 bits pedida, no elegida por el driver (escena y sombras)
- **Pasaba**: nada en esta PC, pero `LoadRenderTexture` y `rlLoadTextureDepth` de raylib piden la
  profundidad sin tamaño (`GL_DEPTH_COMPONENT`, "que elija el driver"). Con otra placa podía salir de 16
  bits: con el plano cercano en 0.08, el escalón a 100 m sería de ~2 m y parpadearía todo lo de lejos
  (y el mapa de sombras perdería precisión). El log de raylib ("Depth renderbuffer ... (32 bits)") no
  dice lo que dio: imprime el máximo que soporta la placa.
- **Qué se hizo** (`Render.cpp`): `LoadSceneTarget` arma la textura de la escena como `LoadRenderTexture`
  (color RGBA8 con rlgl) pero con un renderbuffer `GL_DEPTH_COMPONENT24`; el mapa de sombras se rehace
  con `glTexImage2D(..., GL_DEPTH_COMPONENT24, ...)` después de `rlLoadTextureDepth` (`MakeDepth24`).
  Las funciones de OpenGL 3.x core que rlgl no expone salen de `glfwGetProcAddress` (GLFW está adentro
  de raylib en Windows y en Mac); si no están, queda lo de raylib. `UnloadRenderTexture` la libera igual
  (`rlUnloadFramebuffer` borra el renderbuffer enganchado). Al crearlas, el log dice lo que quedó:
  `RENDER: escena de W x H, profundidad de N bits` y `RENDER: mapa de sombras de N bits`: **mirar esas
  líneas en la PC de otro si aparecen parpadeos**.
- **Trampa**: `rlFramebufferAttach` y `rlFramebufferComplete` dejan suelto el framebuffer; para preguntarle
  algo después (`glGetFramebufferAttachmentParameteriv`), `rlEnableFramebuffer` antes (si no, da 0).
- Las marcas de las ruedas (`TerrainDeformation`) siguen con `LoadRenderTexture`: se dibujan en 2D y su
  profundidad no se usa.
- **Se comprobó**: en esta PC (Intel) la escena da 24 bits y las sombras 24 (el driver ya daba 24). Capturas
  con el exe de antes de este cambio y el de después (garajes, recta a 230 m, pórtico con su sombra, largada de la favela):
  iguales a la vista; la diferencia entre el de antes y el de después (0.7-2.8% de píxeles > 24 niveles,
  en bordes) es la misma que entre dos corridas del mismo exe (1.3-2.5%: el grano del post-proceso y el
  cuadro exacto de la captura). Regresión idéntica.

### Seguían los parpadeos en los boxes, abajo de las tribunas y en los grafitis (circuito y favela)
- **Pasaba**: en v0.2.7, rayas blancas en las aberturas de los garajes, la franja de arriba de los boxes
  cortada en gris, blanco y el color del equipo (cambia cada cuadro al moverse), una banda rayada en la
  base de las tribunas, la banda blanca del air fence y la franja roja de los techos de las tribunas
  cortadas. En la favela, "VIDA LOKA" sobre un portón se leía "VIDA" (el resto lo tapaba el portón), la
  parte de arriba de los carteles de negocios quedaba detrás de la laje, y los grafitis de costado no se
  veían nunca.
- **Por qué**: el arreglo anterior (cajas 3 cm más cortas, techos alternados) atacaba los vecinos girados,
  pero la recta es derecha: los módulos no se solapan. Lo que se pisaba estaba **adentro de cada módulo**,
  con caras en el mismo plano. Medido con `MOTOSIM_COPLANARES`:
  - garaje: el fondo oscuro (`adentro del garaje`) con el frente justo en el del cuerpo (0 mm, 975 m² en
    toda la recta); el frente del alero, una tapa blanca y la franja del equipo en el mismo plano (0 mm);
    los pilares metidos 10 cm en el dintel con el frente en su plano;
  - tribuna: el muro blanco del frente, adentro del primer escalón (que es más alto) con el frente en el
    mismo plano (0 mm, 1490 m², la "banda rayada"); la franja roja 1 cm delante del techo y con las puntas
    en su plano; el techo sin cara de abajo (transparente desde la pista);
  - muro frente a la tribuna: tramos blancos y de cemento alternados que se solapan 4 cm en el mismo plano;
  - air fence: la banda blanca corrida con `outDir`, que no es perpendicular al tramo cuando la barrera
    se abre de golpe (hay tramos de 25 m casi perpendiculares a la pista): quedaba en la cara del colchón;
  - motorhomes cada 11 m que miden 12: las franjas de colores de dos vecinos en el mismo plano;
  - pianos que se cruzan, gomas con la tapa a la altura del borde del muro, la línea de trazos de boxes
    (los huecos eran asfalto 1 cm arriba del asfalto), carteles a 1 cm de su tablero;
  - favela: las calcomanías a 7 cm de la pared, **en el plano de las puertas** (7 cm) y 1 cm detrás de la
    laje (8 cm), 1 cm delante de las ventanas y detrás de los alféizares (11 cm); un grafiti podía salirse
    0.1 del ancho de la casa; la puerta se metía 4 cm detrás de la laje de abajo; el borde amarillo de los
    escalones con el frente en el plano del escalón; la ropa tendida en el plano del alambre. Los grafitis
    de costado tenían la derecha al revés (`sgn · Z`): miraban a la pared, y desde que las calcomanías son
    de una cara no se veían (antes, espejados).
- **Qué se hizo** (sólo mallas; la colisión no cambió):
  - `Circuit.cpp`: fondo del garaje 5 cm delante; alero más corto con la franja del equipo pegada delante
    (sin la tapa blanca); pilares hasta abajo del dintel; franja blanca del dintel 3 cm más larga; muro
    blanco de la tribuna 4 cm delante del escalón (y 5 cm más corto, adentro de las paredes de costado);
    techo con cara de abajo y franja roja 6 cm delante y 3 cm más larga; tramos blancos del muro 2 cm más
    gruesos y altos; banda del air fence 2 cm más ancha del colchón de los dos lados; franjas de los
    motorhomes que alternan 2 cm; pianos que alternan 8 mm; gomas 3 cm más bajas; sólo los trazos de la
    línea de boxes; carteles 3-4 cm delante; cuadros de la largada 1 cm sobre las líneas del borde; borde
    rojo del muro de boxes apoyado arriba y 3 cm más ancho; baranda del balcón 2 cm más corta.
  - `Favela.cpp`: carteles, grafitis y banderas a 10 cm de la pared (2-4 cm delante de puertas, ventanas,
    alféizares y lajes) y adentro del ancho de la casa; la bandera en el frente del primer piso aunque
    vuele; alféizares que salen 1 cm del vidrio (salían 5); puertas desde arriba de la laje; los grafitis
    de costado mirando para afuera y sólo si del costado no hay otra casa pegada (la grilla `occupied`);
    el aire acondicionado de la planta baja no se arma si hay cartel del negocio; borde amarillo 1.5 cm
    arriba y delante del escalón; ropa tendida a 1.5-2.5 cm del alambre. Los carteles siguen saliendo de
    la misma semilla: las mismas casas tienen los mismos carteles.
  - El plano cercano queda en 0.08: con la geometría arreglada, capturas de la recta a 230-500 m con 0.08
    y con 0.25 no se distinguen (lo que cambia de un cuadro a otro es aliasing).
- **Se comprobó**: la herramienta pasó de 396 a ~330 grupos en el circuito (los que quedan: el asfalto de
  boxes y sus líneas a 1 cm, en el piso; cuñas chicas entre tramos de barrera; caras metidas adentro de
  otra caja) y de 440 a 66 en la favela (cables y alambres finos contra paredes); ningún calco toca el
  terreno. Capturas antes (paquete v0.2.7) y después, del mismo lugar y en series de cuadros: garajes
  desde la grilla (`--view 90 2 5` y `100 8 2.2`), tribuna principal (`--view 270 2 5`), boxes desde 230
  m (`--spawn 560 --view 180 3 1.5 --shots-every 0.4`), tribuna de la horquilla (`--spawn 985 --view 90
  3 3`), favela en la largada, "BORRACHARIA 24H" (`--view 90 3 1.2`), "VIDA LOKA" sobre un portón
  (`--spawn 280 --view 90 3 1.6`) y un grafiti de costado (`--spawn 1025 --view 100 4 1.2`). 1052 casas;
  regresión idéntica.

### El cartel "MORRO DO GRAU" titilaba y se veía espejado (favela)
- **Pasaba**: rayas sobre el fondo y, según el lado, el texto espejado mezclado ("MARG DO GRAU").
- **Por qué**: los banderines que cruzan la calle son dos calcomanías en el mismo punto, una mirando a
  cada lado. `DrawMeshTextured` apaga el descarte de caras traseras, así que desde cada lado se dibujaban
  las dos y se pisaban.
- **Qué se hizo**: las calcomanías de la favela se dibujan de una sola cara (`twoSided = false`).
- **Se comprobó**: captura de la largada (`--map favela --test pose`) antes y después.

### Bordes dentados en las franjas de los boxes y abajo de las tribunas (circuito)
- **Por qué** (supuesto): los garajes son módulos de 10 m (media longitud 5.0) sobre la curva suave de la
  recta: los vecinos, girados una nada, se solapaban en una cuña con caras casi en el mismo plano y de
  colores distintos. Los techos de las tribunas de las curvas se pasan 0.2 m del módulo y se solapaban
  con el del vecino a la misma altura.
- **Qué se hizo**: las cajas de cada garaje son 3 cm más cortas de cada lado; los techos de tribunas
  vecinas alternan 4 cm de altura.
- **No alcanzó**: la recta es derecha y la causa real eran caras en el mismo plano adentro de cada módulo
  (ver arriba, "Seguían los parpadeos"). Lección: medir la geometría antes de suponer la causa.

### Arena (`ground_textures: "sand"`, Los Médanos)
- **Qué se hizo**: una textura más, `GenSandTexture` (512², neutra clara con grano y ondas de viento que
  serpentean; en A la altura de las ondas, para el relieve de cerca). Con `sand` va en las dos capas del
  terreno (la de tierra y la de pasto) y `pavedTint` = 1: el color del vértice tiñe las dos. El tono lo
  pone `Terrain::CreateMeshes` (arena dorada con manchones de ~80 m). No se tocó ningún shader.
- **Trampas**: con el preset `sunset` las caras a la sombra salen violetas (el rebote del suelo es
  marrón rojizo y el cenit azul): en arena conviene `ground` color arena. El polvo de las ruedas (color
  de `Game.cpp`) es más oscuro que la arena y se ve como humo.
- **Se comprobó**: capturas en Los Médanos (58-60 fps con el mapa de 1032² muestras); los demás mapas
  no cambian (la textura sólo se usa con `sand`; regresión idéntica).

## Sombras en dos cascadas (septiembre de 2026)

- **Pasaba**: sombra de la moto hecha una mancha (ruedas ilegibles), sombra del pilar del pórtico separada de
  su base, y una franja negra cortada a 50-60 m en el motocross; las dunas de Los Médanos sin sombra de lejos.
  Suavidad 0 pixelaba y 200 daba tablero.
- **Por qué**:
  1. Sesgo en profundidad normalizada y con piso: `(0.00012 + 0.0005·(1−ndl)) · max(0.5, texel/3.1 cm)` sobre
     un volumen de 219 m (2.6 a 14 cm a 32 m de radio, el doble a 64 m). Crecía con el radio y no con el texel:
     la sombra fina se despegaba del pie de las cosas (peter-panning) o desaparecía.
  2. Un solo mapa cuadrado con radio variable no da detalle y cobertura a la vez: 2048 sobre 64 m son 6.3
     cm por texel (mancha), y achicar el radio corta las sombras grandes de golpe (el fundido era el 5% del
     borde). La franja negra del motocross **es el borde del mapa de sombras**, no el descarte de casters.
  3. La suavidad escalaba la separación del filtro de 16 lecturas: 0 daba escalones, 200 saltaba de a texels
     enteros y dibujaba un tablero.
- **Qué se hizo** (`Render.cpp`: `kShadow`, `UpdateGraphics`, `BeginShadowPass`, `InShadowPrism`, `SetCommon`;
  `Game.cpp`: `Draw`, `DrawScene`):
  - **Dos cascadas ortográficas, una textura y un FBO por cascada** (`shadowMap`, `shadowMap1`; nada de atlas).
    Cercana: radio `shadowNear` (24), todo lo que se mueve, centrada en la moto + 0.25·R hacia donde mira la
    cámara. Lejana: radio `shadowFar` (96, 0 = sin lejana), sólo el mundo quieto (terreno, props fijos,
    circuito, favela; nunca la moto propia, el piloto, los demás jugadores, las motos estacionadas ni el
    pasto), resolución `min(cercana, 3072)`, centrada en la moto + 0.2·R1. Planos de luz 1 a 350 m con la
    cámara de la luz a 150 m del foco (antes 1 a 220 y 100).
  - **Caché de la lejana** (`Game::Draw`): se rehace sólo si el centro deseado se movió más de 0.2·R1 (19 m con
    96), a lo sumo una vez cada 3 cuadros, o si cambió `Renderer::ShadowFarRevision()`. Esa revisión sube al
    cambiar de mapa o de sol (`SetLook`), la resolución, el radio de la lejana o al crearla. Los props sueltos
    (dinámicos) no van en la lejana: su sombra quedaría vieja en la caché.
  - **Sesgo en unidades de mundo** (`wt` = metros por texel de cada cascada, uniform vec2): corrimiento por la
    normal `wt·(0.35 + sinT)` y sesgo en profundidad `wt·(0.5 + 0.5·min(tanT, 4)) / 349`, sin piso. Si aparece
    acné, subir primero el corrimiento por la normal.
  - **Filtro**: `sampler2DShadow` (comparación por hardware, cada lectura ya es un 2x2 bilineal) con
    `GL_TEXTURE_COMPARE_MODE/FUNC`, filtro lineal y `CLAMP_TO_EDGE` pedidos con `glTexParameteri` sacado de
    `glfwGetProcAddress` (como `MakeDepth24`). `shadowTaps` = 5 (centro ×2 + cruz) o 9 (grilla 3x3), separadas 1
    texel · suavidad (60-130, default 100). **Plan B**: si no carga `glTexParameteri` (o `glGetTexParameteriv`),
    los shaders se arman con `#define SHADOW_MANUAL` (sampler2D y comparación bilineal a mano, 5 lecturas de 4).
  - **Fundidos**: cercana a lejana en el 8% del lado del mapa cercano; lejana a luz plena en el 25% del lado de
    la lejana; con una sola cascada (sin lejana), a luz plena en el 15%.
  - **Un slot de textura por cascada** en los cuatro materiales receptores (`lit`, `terrainShader`,
    `grassShader`, `debrisShader`): `MATERIAL_MAP_OCCLUSION` para la cercana y `MATERIAL_MAP_EMISSION`
    (`shadowMap1`) para la lejana (sin lejana lleva la misma textura). Cada sampler se fija a su unidad en `Init`
    aunque no haya textura: con las sombras apagadas, un `sampler2DShadow` que comparte unidad con el
    `sampler2D` de `texture0` hace fallar todos los dibujos (GL_INVALID_OPERATION).
  - **Descarte de casters por prisma de la luz** (`Renderer::InShadowPrism(lo, hi)`): la caja proyectada sobre
    los ejes derecha y arriba de la luz contra el lado del mapa de la cascada, en vez del círculo 1.5R+10. Se
    usa en `Terrain::Draw` (alturas mínima y máxima por chunk, ±1.5 m, calculadas una vez por mapa y borradas
    en `Unload`), `Props::Draw`, `Circuit::DrawShadows` y `Favela::DrawShadows` (bloque con y en [-10, 90]):
    **son mallas compartidas con el agente de mapas**; las firmas de `Circuit::DrawShadows`,
    `Favela::DrawShadows` y `Props::Draw` cambiaron (ya no reciben foco y radio). Es una mejora de exactitud y
    de costo, y **no** explica la franja negra.
  - Los ajustes: `Graphics::shadowNear/shadowFar/shadowTaps` (`sombras_distancia`, `sombras_lejos`,
    `sombras_filtro` en el ini; `shadowDistance` queda por compatibilidad y no se usa). En el menú de Ajustes ->
    Gráficos, las filas "Sombras" (paquete de un preset) y, con "Opciones avanzadas": Detalle de sombras (1024 / 2048 /
    4096), Radio cercano (16-40 en pasos de 4), Distancia de sombras lejana (sin lejana, o 50-200 de a 10 más el 48 y el
    96 de Bajo y Alto), Filtro (5 / 9 lecturas) y Suavidad (60-130 en pasos de 10); la Intensidad (40-100) está en la
    página Imagen (avanzadas). Ver "Presets de calidad" y [MENU.md](MENU.md).
- **Se comprobó** (RTX 5070, exe MinGW, capturas con el exe de antes y el nuevo en la misma pose, sin
  `preferencias.ini`):
  - motocross `--view 200 12 3`: la franja oscura de la loma sigue continua hasta el fondo (antes se cortaba
    contra el borde del mapa); la sombra del pilar del pórtico queda pegada a la base, también con el sol a 17.5°
    (mapas de prueba con `sun_dir [-0.955, 0.30, -0.10]`: motocross, favela y circuito).
  - Ruedas: con 2048 a 24 m (2.3 cm por texel) se leen los aros; con 4096 a 24 m (1.2 cm), los rayos y el
    guardabarros. Con 1024 son una mancha.
  - Favela a 25° y a 17.5°: sin acné en paredes, lajes ni calle; las sombras de los cables y los bordes de las
    casas salen más nítidas que antes. Los Médanos (`--view 0/180/90`): sin cortes, con la sombra de las dunas
    grandes hasta el fondo. Suavidad 60 y 130 con 5 y con 9 lecturas: sin tablero (60 con 5 lecturas se ve
    apenas festoneado; 130 con 5 lecturas, algo fantasma).
  - Serie manejando (bot y `--test accel`, cada 0.15-0.2 s, motocross, circuito y motocross con sol bajo): las
    sombras de lo quieto no titilan y el fundido entre cascadas no se ve.
  - Sombras apagadas, 1024, 2048, 4096, lejana 200, y el ini extremo del jugador (`sombras_distancia = 64`,
    suavidad 100): sin errores de GL en el log. Plan B (`glTexParameteri` forzado a fallar con una variable
    temporal, ya sacada): el log avisa, los shaders compilan y la captura del motocross es igual a la del
    camino normal.
  - fps sin vsync a 1920x1080 (con un getenv temporal, ya sacado), ms por cuadro medio, moto quieta / bot 25 s:
    circuito 2.8 (sin sombras) / 3.0 (una cascada de 32 m) / 3.2 (24 + 96); favela 2.0 / 2.5 / 2.5; motocross
    1.5 / 2.2 / 2.2. Picos de 4-5.6 ms, ningún tirón. Bajo (una cascada de 24 m, 5 lecturas) da lo mismo que
    una de 32 m con 9 en esta GPU (2.1 ms motocross): el costo de Bajo y Medio hay que medirlo en una
    integrada.
  - Telemetría del bot headless (motocross, circuito, favela) idéntica a la del exe anterior (`diff` sin
    diferencias salvo la ruta): no se tocó nada de física. No había paquete en `dist/` para `regresion.sh`.
- **Lecciones**: (1) la sombra cercana necesita el sesgo en metros del mundo (texel de cada cascada), sin piso ni
  profundidad normalizada; (2) la suavidad es la separación de la grilla del filtro, y 0 y 200 rompen (rango
  60-130); (3) la franja negra del motocross es el corte del borde del mapa de sombras, no el descarte de
  casters; (4) un mapa único con radio variable no da detalle y cobertura a la vez: hacen falta cascadas
  (cercana fina + lejana en caché); (5) un `sampler2DShadow` sin textura enlazada comparte la unidad 0 con
  `texture0` y rompe el dibujado: fijar cada sampler a su unidad; (6) `tools\compilar.bat` es LF con acentos y
  el cmd de Windows lo interpreta mal: si falla con "no se reconoce Compila...", correrlo desde una copia CRLF.

## Imagen: post-proceso, ambiente, asfalto, escala de render y anisotropía (septiembre de 2026)

Pulido de la imagen final sin tocar física ni mapas (`Render.cpp`, `Render.h`; un cambio de una línea en `Game.cpp`
y en `Circuit.cpp` / `Favela.cpp`). Todo medido con exe MinGW de antes y de después, mismas poses, sin
`preferencias.ini`, 1280x720 sin HUD (`--test pose`, y el bot a los 12 s en el circuito para la velocidad).

- **Bordes con escalones rosa y verde y halos magenta con el motion blur.**
  - *Por qué*: `kPostFS` mezclaba siempre el 85% de R y B con una muestra **cruda** de la escena, aunque la aberración
    valiera 0. El FXAA solo suavizaba G: los bordes con contraste quedaban con escalones de color en los otros dos
    canales, y con el motion blur cada copia desfasada dejaba un halo.
  - *Qué se hizo*: el FXAA y el motion blur (ahora 12 taps, sin jitter) son una función (`Scene`); la aberración sólo
    corre si es > 0 y toma R y B de `Scene(uv ± c·e)`, o sea por el mismo camino (costo triple del post sólo ahí).
    `Game.cpp`: `speedBlur` máximo 0.035 → 0.025.
  - *Se comprobó*: píxeles magenta (R-G > 25 y B-G > 25, muestreo 1/4) en el bot del circuito a los 12 s: **2112 →
    80** (con aberración 100: 87); el criterio de la especificación era ~330 o menos (2871 en otro cuadro). En
    la recta, el alambrado, los pinos y la línea blanca del borde salen limpios (recorte a 2x, antes y después).
- **Estilo de color (`graphics.colorStyle`: 0 natural, 1 vívido, 2 suave; default 1) siempre puesto.**
  - *Por qué*: ACES por canal sin compensación desatura: sin blancos, cielo gris azulado, sombras azul oscuro.
  - *Qué se hizo*: en el post, sobre la imagen ya tonemapeada y antes de la viñeta y el grano: nitidez con el
    promedio de las 4 diagonales del FXAA (`± 0.06`, se apaga con el motion blur: no hay nada que afilar),
    exposición, saturación, contraste (curva S `mix(c, c²(3-2c), k)`), y al final un dither triangular de ±1 nivel
    (`(hash(p) + hash(p+7) - 1) / 255`). Vívido 1.05 / 1.08 / 0.25 / 0.4; natural 1 / 1 / 0 / 0.2; suave 1 / 1.05 /
    0.10 / 0. **F7 ya no lo apaga** (sigue apagando viñeta, grano, motion blur y aberración); el HUD se dibuja
    después del post (`DrawPost` termina antes de `DrawHUD`): no recibe el estilo.
  - *Se comprobó*: luminancia p5 / p50 / p95 antes → después: motocross con cielo 0.23 / 0.51 / 0.75 → 0.30 / 0.55 /
    **0.81** (natural: 0.75); circuito 0.11 / 0.38 / 0.69 → 0.12 / 0.44 / **0.75**, con píxeles blancos. Cielo
    (mx_low) de (160, 181, 201) a (171, 196, 219). Banding del cielo: racha media de un mismo valor en una columna
    de 70 filas, 6.7 / 10.2 / 23.3 (R, G, B) → 5.5 / 6.9 / 8.0.
  - Con el look de día no se sube exposición ni saturación por otro lado: ya la sube el estilo (acumular los dos
    quema el verde).
- **`Ambient()` reescrita** (`kCommon`): cielo (cenit·0.8 + horizonte·0.35) desaturado un 30%, ganancia 1.25 y un
  relleno de lado (`0.30 · (1 - |n.y|) · max(dot(n, -sol_xz), 0)`), sin ×1.3 extra en `SetCommon`. *Por qué*: el
  ambiente era un degradé azul parejo: las caras en sombra salían azul oscuro y las paredes opuestas al sol,
  negras. *Se comprobó*: asfalto en sombra del circuito de (20, 28, 43) a (28, 34, 48), saturación 0.52 → 0.43
  (todavía algo azul: ese valor es de un negro casi puro). Favela (look actual): luminancia media 0.316 → 0.378.
- **Terreno** (`kTerrainFS`): (1) los taludes empinados suman `0.5 · (1 - smoothstep(0.55, 0.95, n.y)) · Ambient(arriba)`
  (una pared de la pista queda sin cielo y se veía como una franja apagada); (2) brillo rasante del asfalto: reflejo
  Fresnel del horizonte, más fuerte cuanto más oscuro el asfalto, con el uniform `sheenOn` (1 sólo con
  `ground_textures` circuit o street; `pavedTint` también vale 1 en arena y ahí sería un espejo de cielo en la
  duna). Se ve en la recta del circuito como un brillo claro lejos (rasante). No se tocó el especular de `kLitFS`.
- **`renderScale` (75-100, default 100)**: la textura de la escena mide `renderScale`% del framebuffer (ancho y alto),
  el post la estira a la ventana con el filtro bilineal que ya tiene, y el HUD y los menús se dibujan después, a
  resolución nativa. `pixelAngle` (marcas de goma) y `resolution` del post salen del tamaño de la textura, no de la
  ventana. *Se comprobó*: 75 → escena de 960x540, 85 → 1088x612, 100 → 1280x720 (log `RENDER: escena de`), imagen
  sana; a 1080p en esta GPU el costo casi no cambia (3.0 contra 3.1 ms en el circuito: el límite es la CPU, no
  hay medición en una integrada).
- **Anisotropía (`graphics.aniso`: 1, 4, 8 o 16; default 8)**: `Renderer::UpdateGraphics` la aplica a las texturas
  del suelo (`detail`, `dirtTex`, `grassTex`, `pavedTex`, `soilTex`, `sandTex`) y `Renderer::ApplyAniso` a los atlas
  de `Circuit` y `Favela` (reaplican al ver otra `AnisoRevision()` o al armar un atlas nuevo, en `Draw`).
  - **Trampa**: `rlTextureParameters` **vuelve la anisotropía a 1 en cada llamada**, también con `SetTextureWrap` o
    `SetTextureFilter`: el `TEXTURE_FILTER_ANISOTROPIC_8X` que ponía `UploadTileable` antes del wrap no valía
    nada (el suelo estaba en 1x). La anisotropía tiene que ser lo último que se le pide a la textura (se sacó de
    `UploadTileable`).
  - *Se comprobó*: circuito, `--view 200 8 1.2`, 1x contra 16x: el pasto y el asfalto lejanos y el texto del cartel
    "SIERRA DE LOS VIENTOS" (atlas) salen mucho más nítidos; el brillo del atlas no cambia (cartel 139.8 / 144.9 /
    167.8 en 1x contra 138.8 / 142.8 / 165.8 en 16x: 1-2 niveles, por el desenfoque con el fondo azul; tribuna igual).
    4x y 16x sin errores en el log.
- **Sin `FLAG_MSAA_4X_HINT` en la ventana** (`Game.cpp`): la escena ya se dibuja en una textura sin MSAA (la suaviza el
  FXAA); sólo lo usaban el HUD y los menús. Recortes a 4x del HUD (panel de marcha) y de la fila marcada del menú
  (bordes redondeados), antes y después: sin diferencia que se note, así que queda sin MSAA.
- **fps** (sin vsync, 1920x1080, `--telemetry`, moto quieta 6 s / bot 25 s, ms por cuadro medio; con un `getenv`
  temporal ya sacado): circuito 3.1 → 3.1 / bot 3.1 → 3.2; favela 2.4 → 2.5; motocross 2.1 → 2.1. Con aberración
  100 en el circuito 3.2. Sin tirones.
- **Pendiente de otras etapas**: `colorStyle`, `renderScale` y `aniso` todavía no tienen clave en `preferencias.ini`
  ni fila en el menú (van con los presets y el menú); los looks de favela y Médanos y los colores de vértice del
  terreno son de la fase de mapas. Con los valores del look de la especificación (favela: `ground [126,106,90]`,
  `exposure 1.0`...) la favela da luminancia media 0.44 y 39 píxeles mauve (contra 165 con el look actual); Médanos
  con su look nuevo pero la arena sin oscurecer da p5/p50/p95 0.54 / 0.80 / 0.88 (objetivo 0.35 / 0.66 / 0.75: falta
  el albedo de arena de `Terrain.cpp`); el verde del valle no cambia de tono con esto (tono medio 76.4° → 75.7°,
  saturación 0.59 → 0.67), así que el "lima" lo resuelve el color de vértice del pasto.
- **Lecciones**: (1) en el post, un `mix` contra la muestra cruda cancela el FXAA aunque el efecto valga 0: medir con
  el recuento de píxeles magenta; (2) ACES por canal sin compensación desatura: el estilo de color va en el post,
  no en los `MapLook`; (3) `rlTextureParameters` resetea la anisotropía en cada llamada: aplicarla al final;
  (4) el HUD y los menús no necesitan el MSAA de la ventana cuando la escena va a una textura; (5) en este shell
  `grep -c $'\r'` no cuenta los CR: para saber el fin de línea de un archivo, contar `\r\n` con Python (Render.cpp,
  Render.h, Game.cpp y estos docs son CRLF; Circuit y Favela, LF).

## Terreno, pasto y distancia de dibujado (septiembre de 2026)

Pulido del terreno sin tocar física (`Terrain.cpp`, `Render.cpp`, `Circuit.cpp`, `Favela.*`, `Props.cpp`, `Particles.*`,
`Game.cpp`: sólo la lista de pasto). No cambian `Ground()`, `edgeHeight`, el heightfield, las cajas `solid`, `ground_textures`
ni `soilMode/soilSoftness/soilDepth`. Medido en una RTX 5070, exe MinGW, 1920x1080 sin vsync (un `getenv` temporal, ya sacado),
comparado con un exe armado con las fuentes de antes de esta etapa; capturas con `--size 1280 720 --nohud --test pose`. Los ms
no se trasladan a una GPU integrada (no hay medición ahí).

- **El terreno del circuito costaba 2 ms de GPU aunque se mirara para otro lado (culling).**
  - *Por qué*: `Terrain::Draw` y `Favela::Draw` mandaban todos los bloques cada cuadro; sólo `Circuit::Draw` descartaba los que
    quedaban fuera de la vista, con un lambda propio.
  - *Qué se hizo*: el lambda pasó a `Renderer::BoxVisible(lo, hi)` (dentro de `BeginMode3D`; una caja queda afuera si sus 8
    esquinas caen del lado de afuera de un mismo plano del recorte; nunca descarta de más) y `Renderer::CameraPosition()`.
    `Terrain` guarda la altura mínima y máxima de cada chunk al armar las mallas (`Chunk::low/high`; antes se calculaban
    la primera vez que había sombras) y en la pasada principal (`radius <= 0`) descarta con ±2 m de margen vertical (los
    surcos bajan el suelo 35 cm como mucho; las baldosas finas de `TerrainSoil` se dibujan todas). `Favela` guarda la caja de
    cada bloque de 32 m (`Chunk::lo/hi`, de los vértices) y `Draw` la usa para sólidos, dos caras y calcomanías.
  - *Extras*: `Graphics::propsRadius` (0 = sin límite): `Props::Draw` de la pasada principal saltea lo que queda más lejos de la
    cámara (las sombras no). `Graphics::detailRadius` (default 350; antes 260 fijo): la gente de las tribunas del circuito se
    dibuja hasta ahí y en los últimos 40 m se hunde en las gradas achatándose contra la base del bloque, en vez de saltar
    (cajitas de 36 cm: ~1 px a 300 m). Los tres campos nuevos de `Graphics` (con `grassDensity`, 100) todavía no tienen clave en
    `preferencias.ini` ni fila en el menú (van con los presets).
  - *Se comprobó*: con una variable temporal (ya sacada) que, para cada chunk descartado, proyecta todos sus vértices con la
    matriz de la cámara: **0 vértices adentro del frustum** en 6 mapas x 4 giros de cámara, x 3 vistas extremas (`--view 0 60 30`,
    `90 80 12`, `45 -5 3`; hasta 200 mil chunks descartados por corrida) y 25 s de bot en cada mapa. Capturas con y sin culling
    (variable `MOTOSIM_NOCULL`, temporal) de motocross, circuito, favela y médanos con dos giros cada una: 0.01-0.5% de píxeles
    con diferencia > 24 niveles, lo mismo que entre dos corridas iguales (0.01-0.26%: grano y viento); ningún chunk desaparece.
    ms por cuadro medio / peor: circuito quieto 3.1 / 4.3 → **1.6 / 2.6**, bot (pasto 45) 3.0 → **1.4**; favela quieta 2.5 → 2.0,
    bot 2.4 → 1.9; médanos quieto 2.1 → **1.4**; motocross y valle igual (mapas chicos: se ve casi todo).
- **Pasto: manchas brillantes en las lomas, pop-in y tirones al rehacer la lista.**
  - *Por qué*: las matas se iluminan con normal (0,1,0) y en las lomas del borde (y en el valle y el park) se pegaban como manchas
    lima sobre un talud que se ve más oscuro; el raleo con la distancia se decidía en la CPU y cada 2 m entraban o salían 45-290
    matas de golpe; y la lista completa (160 mil candidatas con pasto a 100 m) se rehacía en la CPU: 3 ms con 45 m, 5 con 70,
    8-10 con 100 (medido), cada pocos metros.
  - *Qué se hizo*: (1) descarte por pendiente después de los filtros baratos: `density *= smoothstep(0.75, 0.90, normal.y)` con
    el mismo hash de raleo (las matas de siempre, menos las de los taludes; en arena sigue el filtro de antes). (2) Raleo sin
    saltos: `GrassTufts` deja el hash de raleo de cada mata en `m15` de su matriz (`kGrassVS` repone `m[3][3] = 1`) y el
    vertex shader calcula `keep = min(1, 900/d²) · grassDensity`, `lodK = smoothstep(hash, hash + 0.15, keep)` y escala la
    mata por `distanceFade · lodK · min(2, 1/sqrt(max(keep, 0.05)))` con la distancia de ese cuadro (las que sobran se achican
    hasta desaparecer; las que quedan crecen para que no se vea a puntitos). En la CPU pasan las que van a entrar antes de
    la próxima lista: `hash <= min(1, 900/max(1, d-10)²) · densidad + 0.15`. (3) La lista se rehace cada **6 m** (antes 2) con
    radio de lista `grassDistance + 10` (antes +3): `Game::Draw`. (4) **Caché por bloques**: `Terrain::GrassTufts` arma una
    sola vez por mapa cada bloque de 32x32 celdas de la grilla de matas (17.6 m: dónde caen, pendiente, tono, hash; ordenadas
    por hash) y cada lista junta los bloques que entran en el radio, cortando cada uno con el umbral de su punto más cercano.
    Tope de 500 bloques (se sueltan los que hace más que no se usan). Con el suelo deformado (`deformed` o baldosas finas)
    se vuelve a calcular la altura de cada mata de la lista; la caché se descarta en `Build`, `Unload` y `ResetDeformation`
    (una mata armada con el suelo deformado tendría otra altura al restaurarlo).
  - *Se comprobó*: matas nuevas y visibles por cada rehacer la lista (las que no estaban en la lista anterior y el shader
    dibuja con escala > 0.02; con el bot 25 s, 51-53 rehechas por corrida, motocross y valle con pasto 45, 70 y 100): **0 en las
    seis**. Costo de rehacer la lista, medio / máximo: 3.1 / 3.5 (45), 5.3 / 6.1 (70), 8.0 / 10.2 ms (100) antes de la caché, y
    **0.36 / 0.6, 0.43 / 1.0, 0.49 / 1.3 ms** con ella (la primera, en frío, no cuenta); mismas matas (13.5 mil con 45 m en el
    motocross). Cuadro más lento con pasto 100, bot 25 s: motocross 8.3 → 4.5 ms, valle 7.5 → 4.5 (sin tirones en ninguno; antes,
    picos de 8); medio 2.4 → 2.6 y 2.0 → 2.2 con 100 m (más matas para el vertex shader: cambiar el tope de crecimiento a 1.6 no
    lo bajó), igual con 45 y 70 m. `grassDensity` 0 / 60 / 100 en el valle: sin pasto / la mitad / lleno, sin saltos.
    Tope de crecimiento y ancho del fundido quedan en 2.0 y 0.15.
- **Colores del terreno: taludes rayados, pasto lima, arena casi blanca.**
  - *Por qué*: el ruido de color de vértice (`fine`) está pensado para el llano; en un talud el suelo se proyecta por (x, z) y
    el ruido se estira en rayas verticales. El pasto `{92,116,58}` / seco `{132,128,76}` daba verde lima bajo el estilo de color
    vívido, y la arena `{234,204,158}` tenía un albedo casi doble del natural (0.75 lineal).
  - *Qué se hizo* (`Terrain::CreateMeshes` y `GrassTufts`, mismos valores en las dos): pendiente en el vértice =
    `max(|H(x+1,z)-H(x-1,z)|, |H(x,z+1)-H(x,z-1)|) / 2 Cell`; con `steepT = smoothstep(0.35, 0.9, pendiente)` el color va hacia
    un tono medio del pasto (`mix(pasto, seco, 0.3)`, hasta el 85%; no en arena ni en calles) y el brillo de `0.92 + fine·0.16` a
    1.0. Las matas hacen la misma cuenta. Pasto `{80,112,62}`, seco `{124,124,80}` (constantes `kGrass`/`kGrassDry`); arena
    `mix({186,158,120}, {166,128,88})` y huella de pista `{132,104,74}`; matas secas de arena `{126,116,72}` / `{158,140,96}`.
    No se tocaron alturas, colisión, `sand`, `Dustiness` ni `SoilAmount`.
  - *Se comprobó*: capturas de antes y después de motocross, valle, park (`--view 200 12 3`, `180 5 1.7`, `200 25 12`): las
    paredes salen lisas, sin rayas verticales ni matas lima flotando en el borde; el verde del valle deja de virar a lima; la
    arena de Médanos pasa de blanquecina a dorada. Con el look de la especificación para Médanos (un `medanos_newlook.json`
    temporal, sin tocar los mapas) la luminancia p5 / p50 / p95 pasa de 0.59 / 0.82 / 0.90 (arena vieja) a **0.44 / 0.72 / 0.80**;
    el objetivo era 0.35 / 0.66 / 0.75: falta ajustar el look del mapa (±10% de `sun_color`, `ground` o `exposure`, fase de mapas).
- **Los "rectángulos oscuros de bordes rectos" de la tierra no eran de `TerrainSoil`: eran el polvo.**
  - *Por qué*: con las baldosas finas teñidas de magenta (temporal) en el motocross con `--ruts` (sin `--ruts` ni existen: la
    prueba de un bot o de un test no crea surcos físicos), se ven cuadrados de 4 m que no coinciden con lo oscuro, y el borde entre
    baldosa fina y chunk no se nota. Sacando el polvo (`MOTOSIM_NODUST`, temporal) desaparecen: eran las bocanadas de polvo
    (`Particles`, DUST). Cada una es un billboard vertical cuadrado, centrado en el suelo: el terreno lo corta por la mitad y queda
    un borde recto; el degradé radial de `GenImageGradientRadial` (blanco a transparente, sin premultiplicar) además oscurece el
    borde de la bocanada (el color viaja con el alfa), y con el color de polvo de `Game.cpp` (más oscuro que la tierra) parece
    humo. Las sombras apagadas no lo cambian porque nunca fue sombra.
  - *Qué se hizo* (`Particles.cpp`): cada bocanada guarda la altura del terreno de abajo (`Item::ground`, en `Update`) y se
    dibuja con el centro a `max(y, ground + 0.45·size)`: el borde de abajo del cuadrado, transparente en el degradé, queda
    apenas bajo el suelo y no hay corte. Sólo el dibujo (no cambia dónde nace ni cómo se mueve).
  - *Se comprobó*: motocross con sombras apagadas, `--test brakeslide` (`--view 200 40 3`) y el bot (`--view 200 35 5`), antes y
    después: los rectángulos con borde recto pasan a bocanadas redondas; vistas rasantes (`--view 120 8 3`, motocross y
    médanos): el polvo se ve natural.
- **Anotado, no resuelto**: matas de pasto sobre el piano (rojo y blanco) y el asfalto de boxes del circuito (la máscara de pista no
  las saca; lo que sobresale es la mata, no el terreno). El terrón/polvo del asfalto sigue saltando como tierra (ver "Marcas de goma").
- **Regresión**: `tools/regresion.sh` contra un exe armado con las fuentes de antes de esta etapa (no había `dist/`), 22 pruebas
  **idénticas**: la lista de PRUEBAS.md (motocross con y sin `--flat`, accel, brakeslide, wheelie, flip, whip), favela (1052 casas,
  48 postes, 132 cables), circuito (vueltas 1:08.73 y 1:08.42), médanos, park, valle, `prueba/plaza_asfalto` (`frenada120`,
  `frenacurva120`), `plaza_tierra`, `recta`, `park_tierra`, `medanos_grande` y `--ruts` con el bot y el brakeslide.
- **Lecciones**: (1) medir el costo de rehacer la lista de pasto por separado del cuadro: el promedio del cuadro no muestra un
  pico de 8 ms cada 6 m; conviene una caché de lo que no depende de la cámara (bloques) y ordenarla por el hash de raleo para
  cortar el recorrido; (2) para saber si el culling descarta de más, proyectar los vértices de lo descartado con la misma matriz y
  contar cuántos caen adentro (mejor que comparar capturas, que tienen ruido de grano y viento); (3) un billboard centrado en el
  suelo se corta contra el terreno y deja un borde recto: subir el centro a ~0.45·size; y una textura de degradé de blanco a
  `BLANK` sin premultiplicar oscurece el borde (el rgb baja con el alfa); (4) antes de creer que un artefacto es de una malla,
  apagar de a una las capas (polvo, baldosas finas teñidas, sombras): el "rectángulo de bordes rectos" de la especificación
  era una hipótesis equivocada; (5) las baldosas finas de `TerrainSoil` sólo existen con `--ruts` (o manejando con Deformación
  "Física" en el menú): una prueba de captura sin eso no las ejerce; (6) el suelo se proyecta por (x, z): en un talud, ni el
  ruido de color de vértice ni las matas de pasto de normal (0,1,0) se ven bien: hay que apagarlos con la pendiente.

## Ajustes gráficos en vivo (septiembre de 2026)

**Lo vigente** (después del pulido de septiembre de 2026): `Renderer::Graphics` guarda valores relativos al look del mapa, sin
modificar `MapLook` ni acumular multiplicadores al cambiar de mapa. Los ajustes viven en Ajustes, en tres páginas
(detalle en [MENU.md](MENU.md), "Ajustes en tres páginas"):

- **Gráficos** (calidad, lo que cuesta rendimiento; agrupada en los presets Bajo / Medio / Alto / Ultra, ver "Presets de
  calidad" más abajo): Calidad gráfica, Sombras (paquete de un preset), Distancia de dibujado (pasto + props + gente de
  tribuna), Pasto (densidad), Filtro de texturas, Resolución de imagen (75 / 85 / 100%), FXAA, Pantalla completa,
  Restablecer calidad y, en "Opciones avanzadas", el ajuste fino de sombras (detalle 1024 / 2048 / 4096, radio cercano
  16-40 m, distancia lejana, filtro de 5 o 9 lecturas, suavidad 60-130), la distancia del pasto (0-120 m), el relieve
  fino y las partículas de rueda.
- **Imagen** (estilo; no cambia el preset): estilo de color (Natural / Vívido / Suave), brillo 70-130, niebla (Mínima 30 /
  Ligera 60 / Normal 100 / Densa 150), viñeta (0 / 20 / 40 / 60), grano 0-40, aberración 0-50, motion blur, sacudón de
  cámara y, en avanzadas, luz del sol 70-130, luz ambiental 60-140, intensidad de sombra 40-100 y viento del pasto 0-150.
- **Juego y sonido**: volumen, ayuda, datos técnicos, caja, tracción y el grupo TERRENO (la física del suelo, que ni los
  presets ni las filas de Gráficos e Imagen tocan).

Los defaults de `Graphics` son los de Alto (sombras 2048 con radio cercano de 24 m y lejana de 96 m, filtro de 9 lecturas, pasto a
70 m, 16x, viñeta leve, sin grano ni aberración, estilo Vívido). F7 sigue siendo el interruptor temporal de efectos (viñeta,
grano, motion blur y aberración; el estilo de color y el FXAA quedan, ver "Imagen").

*Lo que sigue es la primera versión de estos ajustes (antes de los presets), con su cómo se comprobó, y se deja como bitácora:
eran sombras apagadas / 1024 / 2048 / 4096 con un único mapa de radio 16-64 m (32 por defecto), suavidad 0-200 y sol,
ambiente y niebla con rangos de 0 a 200. Esos rangos y el único mapa de sombras ya no existen.*

La resolución se cambia construyendo otro framebuffer antes de soltar el anterior y actualizando
las texturas de los materiales receptores (objetos, terreno, pasto, terrones). Si falla, se conserva la
calidad anterior y no se reintenta cada frame. Apagadas: no hay pasada de sombras ni muestreo en
el shader. El recorte de los objetos que proyectan sombras es ahora por el prisma de la luz de cada cascada (ver "Sombras en dos
cascadas"); la grilla estabilizadora y el sesgo se calculan con el tamaño real del texel de cada cascada. (El filtro de 16
muestras y la suavidad 0-200 de la primera versión se reemplazaron por el filtro por hardware de 5 o 9 lecturas.)

Comprobado en la primera versión: compilación MinGW Release, shaders GLSL compilados en GPU, cambio 2048→4096 desde el
menú sin reiniciar, persistencia al abrir de nuevo y capturas con sombras apagadas, bajas/duras a
64 m y Ultra/suaves a 16 m. Preferencias fuera de rango se recortan; texto inválido conserva defaults.
Capturas y logs locales: `build/visual-checks/`.

## Pasto y tierra deformable

El pasto permite radio 0–120 m desde la cámara (en Opciones avanzadas; los presets lo llevan a 25 / 45 / 70 / 100 m), densidad,
viento (0–150% en el menú) y desplazamiento (0–200%, sólo en el ini). Las posiciones
son estables en el mundo; la densidad disminuye lejos y las hojas desaparecen gradualmente.
Las ruedas doblan las puntas, las huellas las aplastan y las raíces siguen la altura deformada.
No se recalculan instancias cada frame: la lista se rehace cada 6 m de cámara, o al cambiar el terreno, el radio o la
densidad (ver "Terreno, pasto y distancia de dibujado").

La tierra combina geometría y colisión deformables con relieve fino de shader. Las ruedas compactan
por carga y excavan al patinar; parte del material forma bordes. El asfalto queda protegido.
La blandura también modifica el color húmedo y reduce el polvo. Los terrones son mallas instanciadas
iluminadas, con gravedad, rebote y reposo sobre el suelo; hay un límite compartido de 3000 partículas.
Reciben sombras; no proyectan sombras individuales.

La deformación física es local. En multijugador se restaura el terreno y sólo se dibujan huellas,
pasto doblado y partículas. La primera implementación usaba la grilla original; la revisión siguiente
reemplaza esa limitación por sectores finos.
Capturas de GPU y pruebas de extremos: `build/soil-checks/`.

### Revisión: surcos finos y material húmedo

La limitación anterior de resolución quedó reemplazada por `TerrainSoil.cpp`: muestras de 5 cm
en sectores pisados (100 veces más muestras por área que la grilla de 50 cm), con malla y colisión
del mismo relieve. Las huellas se interpolan cada 2.5 cm, con ancho de cubierta, compactación,
excavación por patinaje, variación de tacos y bordes de volumen acotado. El asfalto está excluido.

UV2 lleva profundidad real y presencia del sector fino; el shader usa eso para el barro húmedo,
el color y la reflexión suave del cielo, evitando que el atlas global dibuje surcos cuadrados.
Las normales suman la pendiente fina a la pendiente suavizada del terreno original. Los triángulos
gruesos se retiran donde se dibuja una malla fina: no hay superficies superpuestas.
La caché de GPU conserva como máximo 128 sectores cercanos y sube hasta ocho por frame; lejos
vuelve la malla base, pero las alturas y colisiones finas permanecen durante la partida.
Esto modela suelo deformable, no agua con dinámica de fluidos. En red sigue siendo sólo visual.
Captura sin partículas para inspeccionar el relieve: `build/soil-checks/fine-detail.png`.
Verificado en GPU, con pruebas de suelo en motocross plano y circuito (cero fallos), regresión
sin deformación idéntica y recorrido de 18 s. En esta PC: 6.6 ms medios por frame, máximo 35.9 ms,
seis frames mayores de 25 ms. El resultado depende del equipo y del mapa.

### Lo que se ve no cambia cuando la física deja de leer el surco entero

Las ruedas ya no leen la malla fina tal cual sino una versión suavizada, con tope de 2 cm y demora (`Terrain::RideHeight`,
ver [FISICA.md](FISICA.md), "El surco no tiene que manejar la moto"). La malla, las normales, el shader, las huellas, los
terrones, el polvo y los ajustes de Tierra siguen igual; `UpdateSoilMesh` y la colisión de Jolt no se tocaron. Lo único que
se nota: la moto apoya 2-3 cm más arriba del fondo de un surco hondo (capturas de una rueda en un surco de 24 cm, contra la
versión anterior, de costado, de atrás y en marcha: sigue viéndose dentro del surco, sin flotar ni enterrarse) y ya no
sube por las paredes ni rebota. Los bancos siguen en la malla y en la colisión, pero la rueda pasa por encima sin sentirlos.

## Presets de calidad Bajo / Medio / Alto / Ultra (septiembre de 2026)

Los ajustes de calidad de las etapas anteriores (sombras en dos cascadas, distancia y densidad del pasto, radio de props,
gente de tribuna, anisotropía, escala de la imagen) quedan agrupados en cuatro presets (`Renderer::ApplyPreset`,
`Renderer::MatchPreset`, tabla `kQualityPresets` en `Render.cpp`). El menú, las claves de `preferencias.ini`, la
autodetección de la placa y la red de seguridad de fps están en [MENU.md](MENU.md) ("Ajustes en tres páginas").

| Campo (`Renderer::Graphics`) | Bajo | Medio | Alto | Ultra |
|---|---|---|---|---|
| `shadowQuality` (resolución cercana) | 1024 | 2048 | 2048 | 4096 |
| `shadowNear` (radio cercano, m) / cm por texel | 20 / 3.9 | 24 / 2.3 | 24 / 2.3 | 28 / 1.4 |
| `shadowFar` (radio lejano, m; 0 = sin lejana) / cm por texel | 48 / 9.4 | 80 / 7.8 | 96 / 9.4 | 140 / 9.1 |
| `shadowTaps` (lecturas del filtro) | 5 | 5 | 9 | 9 |
| `grassDistance` (m) / `grassDensity` (%) | 25 / 60 | 45 / 85 | 70 / 100 | 120 / 100 |
| `propsRadius` (m; 0 = sin límite) / `detailRadius` (gente, m) | 250 / 260 | 350 / 300 | 500 / 350 | 0 / 450 |
| `aniso` / `renderScale` (%) | 4x / 75 | 8x / 100 | 16x / 100 | 16x / 100 |
| `soilDetail` / `soilParticles` / `grassDisplacement` / `fxaa` | 60 / 50 / 100 / sí | 100 / 80 / 100 / sí | 100 / 100 / 100 / sí | 100 / 100 / 100 / sí |

- **Sólo calidad**: lo que cuesta rendimiento. El estilo (sol, ambiente, exposición, niebla, intensidad y suavidad de la
  sombra, viñeta, grano, aberración, viento, estilo de color, motion blur) es del jugador y no cuenta para "Personalizado".
  `soilMode`, `soilSoftness` y `soilDepth` (alimentan las huellas físicas y la colisión), la pantalla completa y el sacudón
  no entran nunca. Los presets escriben sólo en `Renderer::Graphics`, jamás en `MapLook` ni en los JSON de los mapas.
  Los defaults de `Graphics` son los de Alto (distancia 70, props 500, gente 350, 16x, viñeta 15).
- Los ms de las cascadas están medidos sólo en una RTX 5070; Bajo y Medio son estimaciones (en una integrada no se
  midió) y la red de seguridad de fps los cubre. Costo relativo de las sombras respecto de antes: Bajo ≈ 0.3, Medio ≈ 0.9, Alto
  ≈ 1.2 con la lejana en caché, Ultra ≈ 2 a 3 (estimado; esas cifras son de antes de la "Revisión de los críticos": desde entonces Bajo y Medio también llevan una lejana corta).
- **Se comprobó** (RTX 5070, exe MinGW, sin `preferencias.ini`, `--noprefs --gfx`, 1280×720 sin HUD, `--test pose`), y **sólo esto**:
  los cuatro presets con **una sola vista** (`--view 200 12 3`) en **seis mapas** (motocross, circuito, favela y Los Médanos en la
  etapa de los presets; park y valle después, con el exe final): Bajo con el pasto más ralo y cerca y las sombras cortas, Medio con la
  sombra de la moto nítida, Alto y Ultra con las sombras lejanas (la loma del fondo del motocross); sin errores de GL en el log.
  **No se miraron** las otras dos vistas de la especificación (`180 5 1.7` y `200 25 12`) con cada preset, ni el bot a los 12 s ni el circuito
  a lo largo de la recta por preset (eso se miró sólo con Alto, ver "Sombras en dos cascadas" y "Looks por mapa"). Los ms de Bajo y Medio en
  una integrada tampoco se midieron.
- **Menú -> preset** (exe final, `--noprefs --gfx`, teclas de verdad): Enter sobre "Calidad gráfica" en Ultra se queda en Ultra (antes
  daba la vuelta a Bajo); partiendo de Medio, llevar "Distancia de sombras lejana" a 96 a mano (más el filtro de 9 lecturas) deja
  "Sombras: Altas" (el radio cercano queda en 24 m solo), o sea el paquete de Alto y no "Personalizadas".
- **fps** (RTX 5070, 1920×1080, sin vsync con un `getenv` temporal ya sacado, `--gfx`, `--telemetry`; ms por cuadro medio, moto
  quieta 6 s / bot 25 s): motocross Bajo 2.0 / 2.0, Medio 2.2 / 2.2, Alto 2.3 / 2.4, Ultra 2.4 / 2.6; circuito 1.5 / 1.2, 1.6 / 1.3,
  1.9 / 1.8, 2.1 / 2.0; favela 1.8 / 1.7, 2.0 / 1.9, 2.0 / 1.9, 2.1 / 2.0. Peor cuadro 5.1 ms (circuito, Ultra, bot), ningún tirón. La
  diferencia entre presets en esta GPU es chica (el límite es la CPU): Bajo y Medio hay que medirlos en una integrada.
- **Regresión**: telemetría del bot headless 40 s (motocross, circuito y favela) idéntica línea por línea contra el exe anterior a
  esta etapa (no había paquete en `dist/`); la etapa no toca física.
- **Lecciones**: (1) agrupar los ajustes de calidad en una tabla y comparar sólo esos campos permite que el menú diga
  "Personalizado" cuando algo se sale, sin que el estilo del jugador lo saque de su preset; (2) la autodetección tiene que ser
  una función pura de las cadenas de OpenGL (`GpuPreset.h`) para probarla con cadenas reales de otras placas sin tenerlas;
  (3) Enter y click en una fila de presets no dan la vuelta (Ultra -> Bajo sería un salto de costo enorme por un click perdido),
  y las filas avanzadas tienen que poder llegar a mano a los valores de los presets (la lista de la distancia lejana lleva el 96 y el
  48; el radio cercano acompaña como 12 + lejana/8, de a 4 m, que pasa justo por 48 -> 20, 80 -> 24, 96 -> 24 y 140 -> 28; la fórmula `lejana/4` de la
  especificación daba 32 para Ultra y nunca coincidía con su 28).
- **Revisión de portabilidad (Mac y MSVC), por lectura**: sin nada que corregir en el C++ nuevo: no hay bindings estructurados usados
  dentro de lambdas (`auto [it, fresh]` de `Terrain::GrassTufts` se usa fuera de la lambda `threshold`), ni desplazamientos de
  negativos (los hashes pasan por `uint32_t`), ni argumentos por defecto con structs anidados con inicializadores
  (`GpuDescription(int* = nullptr)`), ni cabeceras que sólo entren por casualidad (`<cstdint>`, `<memory>`, `<algorithm>`, `<cctype>`
  están donde se usan; `Render.h` toma `<string>` de `Maps.h`). Los escapes `\xE2\x86\x92` van siempre seguidos de espacio (un dígito
  hexa pegado se comería al escape). El GLSL usa sólo 3.30 (`sampler2DShadow`, `texture`, bucles con límites constantes). Único cambio:
  `/utf-8` en las opciones de MSVC (`CMakeLists.txt`), porque los fuentes son UTF-8 sin BOM y sin eso MSVC los lee con la página de
  códigos de la PC. No se pudo compilar con MSVC ni con Apple clang en esta máquina (sólo MinGW).

## Looks por mapa (septiembre de 2026)

Ya con el estilo de color del post y el `Ambient()` nuevo, se recalibraron los `look` de sunset y la niebla base de cada
mapa (los valores, el antes y después y las cuentas están en [MAPAS.md](MAPAS.md), "Looks por mapa después del pulido
gráfico"). Lo que importa para el render:

- **`exposure` de los `look` de atardecer**: 1.0 en la favela y 0.9 en Los Médanos (antes 1.05): el estilo vívido ya suma
  5%. Sunset con las cifras viejas sobre el estilo nuevo daba una favela mauve y arena casi blanca (Médanos p50 0.72 con los
  valores de la especificación, 0.68 con `exposure` 0.9).
- **Niebla**: motocross 0.0034, park 0.0023 (0.0026 en la primera pasada), favela 0.0024, Médanos 0.0016, circuito 0.001, valle 0.0032. Los niveles de niebla
  del menú (30 / 60 / 100 / 150%) multiplican eso: sin una base comparable, "Normal" valía distinto en cada mapa.
- **Medir un `look` sin tocar el proyecto**: el juego toma cada mapa de la primera carpeta `mods/` que lo tenga (la actual,
  después la del exe), así que un `mods/<mod>/maps/<id>.json` en la carpeta de trabajo pisa el mapa sólo para esa corrida.
  Con `--noprefs --gfx alto|medio` y la misma pose queda una comparación limpia (los cambios de `look` no dependen del preset
  de calidad: Alto y Medio dieron la misma luminancia en Médanos y en la favela).

## Revisión de los críticos (septiembre de 2026)

Después de los presets, críticos visuales miraron los seis mapas con los cuatro presets. Cada hallazgo se reprodujo con una
captura antes de tocar nada (exe anterior contra el nuevo, misma pose, `--noprefs --gfx`, 1280×720 sin HUD); lo que sigue es lo
que era real, lo que se arregló y lo que se descartó con evidencia.

- **Matas de pasto casi negras a la sombra** (motocross y valle, Alto y Ultra; la sombra del pilar del pórtico, el pie de la
  loma).
  - *Por qué*: `kGrassFS` sólo tenía ambiente por `ao` (0.55 en la base de la hoja) sobre una textura de hoja que en la base
    vale 0.34-0.48 (sRGB). El terreno a la sombra suma además el relleno de taludes; la mata no. En la sombra la base
    quedaba con menos de la mitad de luz que el suelo de al lado.
  - *Qué se hizo*: `ao` de 0.8 a 1.0 y el ambiente de la mata sube hasta 1.5 veces cuando está a la sombra
    (`Ambient(n) * ao * (1 + 0.5 * (1 - sh))`). A pleno sol no cambia (ahí manda el sol).
  - *Se comprobó*: Ultra `--view 200 12 3` de motocross, franja de sombra al pie de la loma, 8% más oscuro de los píxeles: (26, 40, 27)
    antes, (30, 55, 30) ahora, con el suelo a la sombra en (34, 70, 33). Al pie del pilar, las matas pasaron de manchas de tinta a
    verde oscuro legible. El ao solo (0.8) movía apenas 5 niveles: la base de la hoja era el problema, no el `ao`.
- **Pasto atravesando la calle de boxes del circuito** (todos los presets; también el asfalto del paddock).
  - *Por qué*: la calle de boxes es una malla de asfalto (`band()` de `Circuit.cpp`) sobre un terreno que es pasto, y
    `Terrain::GrassBlock` sólo excluye lo que marca la máscara de pista. No se pintó la máscara: `TrackMask` también
    alimenta `SurfaceGrip` (agarre) y `Dustiness` (polvo), o sea la física.
  - *Qué se hizo*: `Terrain::ExcludeGrass(quad, margen)` marca en una grilla de 1 m propia (`noGrass`, sólo para las matas;
    se limpia en `Build` y `Unload`) los cuadriláteros del asfalto de boxes (entrada, salida, calle, paddock) con 1.2 m
    de margen; `Circuit::Generate` la llena desde un lambda `noGrass` junto a cada `band`. No toca la máscara, el agarre ni
    el polvo (regresión idéntica).
  - *Se comprobó*: `--map base/circuito --test pose --view 160 25 6` Alto: antes, cientos de matas sobre la calle; ahora
    asfalto limpio con el pasto normal a los costados.
- **Bajo y Medio con la sombra cortada** (sin cascada lejana: la sombra de la tribuna termina en una recta a 20-24 m, las
  paredes de las tribunas quedan blancas, en la favela la calle a más de 24 m sale al sol).
  - *Qué se hizo*: los cuatro presets llevan cascada lejana (Bajo 48 m, Medio 80 m, Alto 96, Ultra 140); con 1024 la de Bajo da
    9.4 cm por texel, con 2048 la de Medio 7.8 cm. La lejana está en caché (se rehace cada varios metros) y sólo la leen los
    píxeles fuera de la cercana, así que el costo es una pasada de mundo quieto cada tanto y no una segunda pasada por cuadro;
    aun así es **estimación**: en una integrada no se midió. Medio en 80 y no en 64 porque el radio cercano acompaña como
    `12 + lejana/8` de a 4 m (64 daba 20 y Medio es 24); la lista de "Distancia de sombras lejana" de las avanzadas suma el 48.
  - *Se comprobó*: circuito `--view 200 25 3.5` Bajo y favela `--view 200 25 12` Bajo y Medio, antes contra ahora: las sombras
    y el sombreado de las paredes coinciden con Alto. fps (1920×1080, bot 20 s, circuito, con vsync a 175 Hz, o sea limitado por el
    monitor): 5.6-5.8 ms medios y peor cuadro 7.2-7.7 ms, igual que antes en los cuatro presets, 0 tirones; **no dice nada del costo real**.
- **Ultra casi igual a Alto** (~1% de los píxeles cambia más de 24 niveles en las vistas fijas).
  - *Por qué*: 1.4 cm por texel en la cercana y pasto a 100 m ya están en el techo de lo que se ve a la distancia del juego;
    no hay nada grande que falte. Subir la lejana a 160 m (lo que sugería el crítico) daría 10.4 cm por texel con 3072, peor
    que la de Alto (9.4): más alcance a costa de nitidez.
  - *Qué se hizo*: pasto a 120 m y lejana a 140 m (9.1 cm por texel, casi la de Alto, y 9% más de alcance que el Ultra de antes, de
    128 m). Alto contra Ultra
    sigue en ~1% (motocross 1.4, circuito 0.9, favela 1.0): la diferencia se ve al manejar (pasto y sombras a lo lejos), no en un cuadro
    fijo. **Descartado**: supermuestreo (renderScale 125%) y 16 lecturas de filtro: cambian el rango del menú y de
    `resolucion_render`, y el bajado bilineal de una imagen de 125% no se probó; es una decisión de producto (y de costo de
    GPU, 1.56 veces los píxeles), no un arreglo.
- **Línea oscura al pie de los taludes con la cascada lejana** (valle y Médanos, Alto y Ultra): **no es un error de sesgo, es la
  sombra real de la pared**.
  - *Cómo se descartó*: (1) el mapa de sombras devuelve 0 en la franja, con la normal del suelo hacia arriba, medido pintando la
    visibilidad; (2) el sesgo de la lejana ×2 y ×8 (profundidad y normal) no la saca (con ×8 hasta engorda por el corrimiento);
    (3) la misma franja sale en la cascada cercana de 40 m a 2 cm por texel; (4) el filtro más ancho (2.5 texels) no la borra y
    sí mete bandas de muaré, así que no se dejó; (5) el exe anterior no la mostraba por el sesgo enorme (despegaba todas las sombras
    finas), no porque no existiera. El suelo apenas afuera del pie de una pared con pendiente un poco mayor que el sol queda a la
    sombra del labio de arriba; se ve fina porque está de costado. Queda anotada como conocida.
- **Escalón horizontal de brillo en la favela** (Alto): **no es una cascada**. En la captura del crítico el cielo también cambia en el
  mismo renglón (los 480 renglones de arriba, con el cielo, salen 17% más brillantes que en el cuadro del mismo instante de la
  simulación (los primeros 40 renglones, 162 contra 138) y de ahí para abajo da igual); una cascada no toca el cielo. No se reprodujo en 3 corridas más con la
  ventana minimizada como las de los críticos (cociente 1.006-1.021 arriba y abajo): artefacto de captura de una ventana minimizada. No se
  cambió el fundido.
- **Favela: suelo al sol lavado y sombra roja oscura**: subjetivo, y el rango de ±10% no lo mueve. Se probó sun_color -10% y
  ground +10%: la calle al sol pasó de (235, 208, 158) a (232, 203, 153) y el suelo a la sombra no cambió (usa el cielo, no
  `ground`); la calle al sol está en el hombro de ACES. Bajar de verdad haría falta ~-25%, fuera del rango permitido. Se dejó como estaba.
- **Pared lejana de park lavada y lisa**.
  - *Por qué*: lomas empinadas con el color baked y `shade` 1.0 (para sacar las rayas verticales) quedan sin variación, y la
    niebla de 0.0026 lava a 250 m un 34%.
  - *Qué se hizo*: en `kTerrainFS`, sobre pasto empinado (`wall`: pendiente por `1 - t`), manchas de macro-variación tomadas a lo
    largo de la curva de nivel y a lo alto (no en x, z: por eso no vuelven las rayas verticales), más claras/secas de a parches; y
    niebla de park 0.0023 (antes 0.0026, primera pasada; 0.0028 originalmente).
  - *Se comprobó*: park `--view 180 12 3` y vista por defecto: la loma tiene manchones suaves sin rayas; Médanos (sin pasto) da
    igual que antes (diferencia media 0.39 de 255 con el 0.04% de píxeles a más de 24 niveles, por el pasto); valle sin
    cambios visibles.
- **Regresión de física**: telemetría del bot headless 40 s (motocross, circuito y favela) idéntica línea por línea contra el exe de
  antes de todo el pulido (la única diferencia es que el exe viejo no conoce `--noprefs`). Nada de esto toca física: la
  grilla de `ExcludeGrass` sólo la lee `GrassBlock`.
- **Lecciones**: (1) una mata con textura oscura en la base necesita relleno a la sombra propio: comparar la sombra de la
  mata contra la del suelo de al lado, no contra la mata al sol; (2) el asfalto que una malla pone sobre el pasto tiene que
  avisarle al pasto por un camino propio, no por la máscara de pista, porque esa máscara también es agarre y polvo; (3) antes de
  arreglar una línea de sombra con más sesgo, mirar la visibilidad cruda y probar el mismo punto en la otra cascada: si sale
  igual, es geometría; (4) un cambio de brillo que incluye el cielo no es de sombras: comparar contra un cuadro de la misma
  corrida antes de culpar a una cascada; (5) `look` dentro de ±10% no mueve una imagen que ya está en el hombro del tonemap: si
  hace falta más, la palanca es el grade o la exposición, no `sun_color`; (6) una cascada lejana barata (48 m a 1024) en Bajo
  arregla el corte y el sombreado plano de un solo golpe, pero hay que medirla en una integrada.
