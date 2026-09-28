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
- Las sombras: mapa de 2048² que cubre 64 m (3 cm por texel), con el foco enganchado a la grilla de
  texels (si no, los bordes titilan al moverse) y corrimiento por la normal de un texel.

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
