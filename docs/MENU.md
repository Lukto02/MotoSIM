# Menú e interfaz

## Criterios

- Todo el texto del juego en castellano rioplatense, corto y concreto.
- Se elige con flechas, mouse o joystick; lo marcado se ve siempre (franja que se desliza).
- El menú no para la carrera: la escena sigue de fondo con su propia cámara.
- Escalar todo con `scale` (la altura de la ventana) y probar a 1280×720: lo que entra ahí entra en
  cualquier pantalla.

## Cámara del menú (`Game::UpdateMenuCamera`)

- Plano de cine: bajo, teleobjetivo (fov 32°), a 6.6 m, girando despacio (0.09 rad/s) y con la altura
  que respira. La moto va a la derecha de la pantalla, donde no tapa las opciones.
- **Encuadre**: dónde cae la moto en la pantalla se pide como fracción desde el centro (`screenX`,
  `screenY`). El objetivo de la cámara se corre `fracción × tan(fov/2) × distancia` por los ejes de la
  cámara. Es más fácil de afinar que mover la cámara a mano.
- **Transiciones**: al abrir el menú arranca desde donde estaba la cámara del juego y se mezcla en
  1.1 s; al cerrar vuelve igual. Entre el menú principal y el selector de motos, los parámetros (fov,
  distancia, encuadre, altura, centro) se mezclan en 0.7 s (`showroomBlend`): nada salta.

## Selector de motos

- La lista sólo tiene motos. Al abrir queda marcada la que se usa (la del mapa si no se eligió otra) y
  lleva "EN USO". Elegir la del mapa vuelve a "cada mapa con su moto".
- La marcada gira sin piloto sobre una plataforma: un disco oscuro de 72 caras (`Renderer::Disc`; el
  cilindro común de 16 se ve octogonal a ese tamaño) con un aro del color del juego, apenas más ancho y
  más bajo que la tapa. Si el aro es un cilindro encima, tapa todo el disco. La plataforma crece al
  entrar y sube la moto con ella.
- Ficha abajo a la derecha, con barras relativas a la mejor moto en cada número. La moto se encuadra
  arriba (`screenY` 0.5 a 7.4 m) para que no quede detrás del panel.
- La moto de la vista previa es un `Bike` sólo visual (`InitVisual`), con las suspensiones al 65%.
- **La vitrina queda fija** donde estaba la moto al entrar (`showroomAnchor`). La carrera sigue de fondo y
  la moto propia puede seguir andando sola; antes la cámara y la plataforma la perseguían.
- La plataforma se apoya en lo más alto del suelo que tiene abajo y el pedestal oscuro baja 0.6 m (en una
  pendiente no queda enterrada ni flotando).

## Menú principal

- Título con sombra, raya que respira, "motos con física de verdad · versión".
- Tarjeta "AHORA": mapa (con su color), la moto y sus números, y los pilotos en red.
- Probar con una captura: el principal aparece al arrancar (`--size 1280 720 --screenshot 3 archivo.png`) y el
  selector con `--menu bikes`.
- Opciones: Jugar solo, Crear partida, Unirse, Mapa, Moto, Nombre, **Ajustes**, **Controles**, Salir. En red:
  Volver, Ver la partida, Mapa (anfitrión), Moto, Ajustes, Controles, Salir de la partida, Salir del juego.
- **Cada opción lleva una clave** (`MenuItem::key`) y las pantallas vuelven con `BackToMain("moto")`, no con un
  índice. Antes volvían con `menuSel = 4`, `6`...: al agregar el sacudón y el motion blur, salir de "Nombre"
  marcaba "Sacudón de cámara" (el 6 había quedado viejo), y en red cambia según seas anfitrión o invitado.
- **Navegación**: flechas (repiten al mantenerlas), W/S, mouse, la cruz o el **stick** del joystick (un paso
  al moverlo, y repite cada 0.12 s desde los 0.4 s). Vuelven: Esc, B, **Start** y el botón derecho del mouse
  (sin teclado no había cómo salir de Ajustes ni de Controles).
- **Start abre el menú** corriendo (antes pausaba y con el joystick solo no había forma de llegar al menú). La
  pausa quedó en la P.

## Ajustes (`Menu::Settings`, `SettingsItems`)

- Lo que estaba suelto en el menú (pantalla completa, sacudón, motion blur) más lo que el jugador puede querer
  cambiar, en grupos: IMAGEN (pantalla completa, motion blur, sacudón de cámara), SONIDO (volumen), EN
  PANTALLA (ayuda de teclas, datos técnicos) y MANEJO (caja, control de tracción).
- Cada fila: la llave para sí / no (pastilla del color del juego con la bolita a la derecha, o gris), "◀ opción ▶"
  o el volumen en diez segmentos. ← → cambian (en una llave, → prende y ← apaga: no la da vuelta), Enter o
  click la cambian; en el volumen Enter lo apaga y lo prende, y con el mouse ◀ ▶ o un punto de la barra.
  Los triángulos se dibujan (`DrawTriangle`): la fuente no trae ◀ ▶.
- **Debajo del título va lo que hace la marcada**, no un subtítulo fijo: así las 8 filas y los 4 grupos
  entran a 1280×720 (el alto de fila se ajusta, como en el principal).
- **Se guarda al cambiarlo, desde donde sea**: la misma clave cambia con su tecla (F11, M, T, F3, F6) y queda
  en `preferencias.ini`. Si falta una clave (preferencias de una versión anterior), queda el default, que es lo
  de antes: todo prendido, volumen al 100% (= el de siempre, `kVolume` 0.8 en `EngineSound.cpp`), caja
  automática, tracción prendida; la ayuda "al empezar" y los datos técnicos escondidos son lo nuevo.
- Claves: `pantalla_completa`, `sacudon_camara`, `motion_blur`, `sonido` (0 = apagado con M), `volumen` (0-100,
  de a 10), `ayuda_teclas` (`al_empezar`, `siempre`, `nunca`), `datos_tecnicos`, `caja_automatica`,
  `control_traccion`, `moto` y `nombre`. `LoadPrefs` lee línea por línea `clave = valor` en un mapa: el de antes
  buscaba cada clave con `find` en todo el texto, y una clave que es parte de otra o un comentario la agarraban.
- **La caja y la tracción sólo se aplican si maneja el jugador** (`PlayerDriving`: sin `--bot`, sin `--test`, con
  ventana): las pruebas y el bot ponen las suyas y las regresiones no dependen de `preferencias.ini`.
- **El nombre se recuerda** (`nombre`): antes siempre arrancaba con el usuario de Windows. `--name` gana y no se
  guarda.
- El sacudón (`camera.shakeEnabled`) apaga los golpes de aterrizajes y choques (`AddShake`: el sacudón, el zoom
  hacia adentro y la caída de la cámara) y la vibración fina a alta velocidad. El motion blur (`motionBlur`),
  sólo el desenfoque con la velocidad; la aberración, la viñeta y el grano siguen. F7 sigue apagando el
  motion blur, la aberración, la viñeta y el grano (para probar, no se guarda); el estilo de color y el FXAA
  quedan siempre puestos.
- El sacudón al aterrizar (`EmitEffects`, cada rueda que aterriza fuerte) quedó al 70% de lo que era, a pedido
  ("que se sienta más smooth"): `AddShake(0.084 + 0.315·impacto)`. Como la vibración va con trauma², queda a
  la mitad; el zoom hacia adentro y la caída de la cámara, al 70%. Los choques no cambiaron.
- Probar: `--menu ajustes`; con otro `preferencias.ini` al lado del exe (uno viejo de la v0.2.7 y uno con todo
  cambiado) se comprobó que se lee bien (captura de Ajustes, del principal con el nombre y del HUD).

## Controles (`Menu::Controls`)

- Tres tablas (MANEJO, PILOTO, JUEGO: acción, tecla y botón del joystick) y PARA PROBAR (las F de física y
  tuning) en un párrafo que se corta en líneas del ancho de las tablas. Van sobre un panel oscuro redondeado:
  las tablas pasan por delante de la moto del fondo. Oscurecer toda la pantalla apagaba también el título.
- Es la referencia completa: la ayuda del HUD es sólo lo justo para manejar y dice "todas las teclas: Esc,
  Controles".

## HUD

- **Escala con el alto de la ventana** (`ui` = alto / 900, entre 0.9 y 2): antes era de tamaño fijo y en un
  monitor de 1440 o 4K en pantalla completa quedaba chiquito. Más chico que 0.9 (a 720, 0.8) la ayuda no se
  leía. `DrawSession` (el panel de la partida y los nombres) escala igual.
- **Texto con sombra** (negro al 50%, corrido 5% del tamaño): el gris claro de "Vuelta 1 · Última · Mejor" no se
  leía sobre el cielo ni sobre la arena de Los Médanos.
- **Datos técnicos** (arriba a la izquierda, en castellano: velocidad, motor, marcha, gas, tracción, suspensión,
  agarre, patinaje, cabeceo, inclinación, centro de masa y cuadros por segundo): eran siempre visibles, en inglés
  ("Speed, Front susp..."). Son para mirar la física: escondidos por defecto, T o Ajustes. El contador de
  cuadros de raylib (verde, arriba a la derecha) pasó a ser una línea del panel.
- **Ayuda de teclas** (abajo a la izquierda, `DrawHelp`): antes eran cuatro líneas largas siempre, sin fondo,
  que tapaban media pantalla y sobre la arena no se leían. Ahora un panel de dos columnas con lo justo (gas,
  frenos, doblar, cambios, cuerpo, reaparecer, menú, H) que aparece los primeros 16 s de manejo (se descuentan
  sólo corriendo, no en el menú ni en pausa) y se va en 1.5 s, y vuelve en pausa. H la muestra o la esconde; en
  Ajustes, "al empezar", "siempre" o "nunca". Escondida queda "H  ayuda de teclas" chiquito. Con el bot o una
  prueba no aparece (las capturas quedan limpias).
- **Pausa**: "Pausa" y "P para seguir", con la ayuda.
- **El cursor se esconde** corriendo si el mouse queda quieto 1.5 s (vuelve al moverlo, al arrastrar la cámara o
  en el menú).
- Mensajes: "Control de tracción prendido/apagado" (decía activado), "Sonido apagado (M lo prende)", "Caja manual
  (Q y E para los cambios)".

## Mapas libres (pista `"style": "guide"`, `FreeRide()`)

- **Sin cronómetro de vuelta**: la guía no es una vuelta (el tiempo y "Vuelta 1 · Última" no tenían sentido) y
  al cruzar la largada no sale "Vuelta N". Arriba al centro, donde va el cronómetro, los saltos: "1.4 s en el
  aire" mientras volás (pasados 0.7 s), "Salto 2.3 s" y "Mejor" al caer (si no te caíste). El bot sigue
  imprimiendo `LAP` (las vueltas del bot en Los Médanos se miden con eso).
- **Reaparecer donde quedaste** (`RespawnHere`, R del jugador): antes iba al punto de la guía más cercano, a
  veces a 100 m. Ahora desde el piloto (o la moto, si no se cayó), en anillos cada 1.5 m hasta 45 m, el punto
  más parejo del primer anillo que tenga uno con pendiente < 16% (la peor entre 8 direcciones a 1 y a 2.5 m:
  en la cara de un médano no se arranca) y donde lo primero de arriba para abajo sea el terreno (no un objeto),
  mirando para donde iba al caerse (la velocidad antes del golpe). Si para ahí se termina el mapa (la pared
  del borde), mirando al centro. Sin lugar en 45 m, a la guía.
- **El bot y las pruebas siguen con `RespawnNearest`** (las vueltas y las regresiones cuentan con eso);
  `--respawn-here` hace que las pruebas usen lo del jugador. Comprobado: `--test crashloop` en Los Médanos, 7
  reapariciones a 0-10 m de donde quedó el piloto, pendiente 0-15%; con ventana (serie de capturas) aparece al
  lado, mirando para adelante, y sigue. Regresión idéntica (con y sin mapa libre) contra la v0.2.7 y contra el
  exe de antes de estos cambios.
- En la pista (`track`, `street`), R sigue yendo a la pista.

## Caído

- El jugador no reaparece solo: mira la caída y sale con R (Y en el joystick). "Caída" enseguida y, a los
  0.8 s, un panel "Tocá R para reaparecer" (con "o Y en el joystick" si hay uno; en un mapa libre sin joystick,
  "acá cerca, mirando para donde ibas") que aparece de a poco.
- El bot, las pruebas (`--test`) y sin ventana reaparecen solos a los 3 s (las vueltas y las regresiones
  cuentan con eso) y ahí queda la línea chica de siempre. `--respawn-after S` fija los segundos para
  todos; `-1`, sólo con R (para capturar el cartel: `--test crashloop --respawn-after -1`). Ver
  `AutoRespawnAfter` en `Game.cpp`.
- **La moto elegida se guarda sólo desde el menú**: `--bike` (pruebas) cambia la moto por esa vez. Antes se
  guardaba en `preferencias.ini` y la moto de una prueba (`--bike base/carrera`) quedaba para todos los mapas,
  hasta en la pista de motocross. `savedBike` es la de preferencias; `chosenBike`, la de ahora.
- Ya existía un "Caída / R / Y para reaparecer" chico en `DrawHUD`: el cartel nuevo lo reemplaza (no dos
  textos uno arriba del otro). Buscar antes lo que ya dibuja el HUD.

## Controles gráficos y desplazamiento (septiembre de 2026)

*(Esta sección es de la primera versión de Ajustes, la lista plana: lo vigente, en tres páginas y con nueve filas por vez, está en
"Ajustes en tres páginas...", más abajo. Los rangos y valores de acá abajo son los de entonces.)*

Ajustes muestraba ocho filas por vez, con contador de rango, grupos y ayuda de la opción seleccionada.
Flechas/joystick y rueda recorren la lista; las flechas de cada opción funcionan también con mouse
(izquierda reduce, derecha aumenta). La rueda se procesa después del hover, para que mover el mouse
al desplazar no anule el desplazamiento. Las zonas de click conservan el índice absoluto de la opción;
las filas ocultas tienen rectángulos vacíos. La barra de volumen sólo publica sus zonas si está seleccionada.

Nuevas claves en `preferencias.ini`: `sombras_calidad` (0–3), `sombras_distancia` (16–40 m, radio de la
cascada cercana), `sombras_lejos` (0 = sin lejana, o 50–200 m), `sombras_filtro` (5 o 9),
`sombras_suavidad` (60–130), `sombras_intensidad` (40–100), `luz_sol` (70–130),
`luz_ambiente` (60–140), `exposicion` (70–130), `niebla` (25–150), `fxaa` (0/1),
`vineta` (0–60), `grano` (0–40), `aberracion` (0–50). Los porcentajes son relativos a cada efecto.
(Los rangos son los vigentes al leer el ini; en la primera versión eran luz 0–200, ambiente 20–200, exposición 50–150, niebla 0–200 y
viñeta, grano y aberración 0–100. Restablecer gráficos y la lista plana también son de antes: ver "Ajustes en tres páginas, presets de calidad y autodetección", al final.)
Se verificaron cambio de calidad con mouse, desplazamiento a efectos, guardado y recarga de Ultra,
y capturas a 1280×720 y 960×540. Las pruebas usan una copia del ejecutable con preferencias propias
en `build/visual-checks/run/`, sin reemplazar las preferencias del jugador.

Pasto agrega distancia (0–120 m), viento y desplazamiento (0–200%). Tierra agrega deformación
apagada/visual/física local, blandura (0–100%), profundidad (5–35 cm), relieve fino (0–200%),
partículas (0–150%) y restaurar suelo. Restaurar borra también huellas y partículas.
Claves persistidas: `pasto_distancia`, `pasto_viento`, `pasto_desplazamiento`, `tierra_deformacion`,
`tierra_blandura`, `tierra_profundidad`, `tierra_relieve`, `tierra_particulas`.
Por defecto: distancia del pasto 70 m (la de Alto; 45 m en la primera versión), viento/desplazamiento 100%, física local, blandura 65%,
profundidad 24 cm.
F8 alterna física/visual y guarda la elección. En red la deformación es sólo visual.

## Ajustes en tres páginas, presets de calidad y autodetección (septiembre de 2026)

**Lo de arriba sobre la lista plana, "Restablecer gráficos" y los rangos viejos quedó reemplazado por esto.**

- **Qué pasaba**: Ajustes era una lista plana de 30 filas (sombras, luz, pasto, tierra, imagen, sonido...) sin
  presets: cada jugador armaba su calidad a ojo, los sliders permitían combinaciones rotas (ambiente 20% con sol 60%,
  niebla 190%), "Restablecer gráficos" también pisaba la física del suelo (`soilMode`, blandura, profundidad) y un
  jugador nuevo arrancaba igual en una notebook integrada que en una RTX.
- **Qué se hizo** (`Game::SettingsItems`, `DrawMenu`, `HandleMenuKeys`, `LoadPrefs`, `SavePrefs`, `Renderer::ApplyPreset`):
  - **Tres páginas** con una primera fila `Sección: ◀ Gráficos · Imagen · Juego y sonido ▶` (`Setting::Kind::Tabs`, sin
    grupo): ← → o Enter cambian de página, el click en el nombre la abre, LB y RB del joystick también desde cualquier
    fila. Al cambiar, la marcada vuelve a la primera fila (`SetSettingsPage`). `SettingsItems()` se arma cada cuadro con
    `settingsPage` y `settingsAdvanced`; las zonas de click siguen siendo por índice absoluto (`menuRects[k]`).
  - **Gráficos** (calidad, lo que cuesta rendimiento): Calidad gráfica (Bajo / Medio / Alto / Ultra, "Personalizado"
    automático), Sombras (Apagadas / Bajas / Medias / Altas / Ultra: el paquete de sombras del preset homónimo),
    Distancia de dibujado (Corta / Media / Larga / Muy larga: pasto + props + gente de tribuna juntos; "Personalizada" si
    no coincide), Pasto (Apagado / Poco / Normal / Denso = densidad 0 / 60 / 85 / 100), Filtro de texturas, Resolución de
    imagen (75 / 85 / 100%), FXAA, Pantalla completa, Restablecer calidad (el preset autodetectado, con el nombre de la
    placa en la pista) y "Opciones avanzadas" (interruptor: detalle, radio cercano, distancia lejana, filtro y suavidad
    de sombras, distancia del pasto, relieve fino, partículas de rueda).
  - **Imagen** (estilo; no cambia el preset): Estilo de color (Natural / Vívido / Suave), Brillo 70-130, Niebla (Mínima 30 /
    Ligera 60 / Normal 100 / Densa 150), Viñeta (Apagada / Suave / Media / Fuerte = 0 / 20 / 40 / 60), Grano 0-40,
    Aberración 0-50, Motion blur, Sacudón de cámara, Restablecer imagen; avanzadas (mismo interruptor): Luz del sol
    70-130, Luz ambiental 60-140, Intensidad de sombra 40-100, Viento del pasto 0-150.
  - **Juego y sonido**: Volumen, Ayuda de teclas, Datos técnicos, Caja, Control de tracción, Stick derecho (JOYSTICK) y el grupo TERRENO
    (Deformación, Blandura, Profundidad máxima, Restaurar suelo, **Restablecer terreno**). Es lo único que toca la física
    del suelo: ninguna fila de Gráficos ni de Imagen la cambia, y los presets tampoco (`soilMode/soilSoftness/soilDepth`).
  - **"Personalizado" automático**: cada setter de calidad llama a `QualityChanged()` (`preset = Renderer::MatchPreset(g)`,
    `customBase` = el último preset aplicado, `grafico_auto = 0`, guarda). Sobre "Personalizado", ← → o Enter vuelven a
    `customBase`; entre presets, ← → y también Enter o el click suben o bajan sin dar la vuelta (tope en Bajo y Ultra: un click perdido
    en el medio de la fila no baja de Ultra a Bajo). Si tocando filas las claves vuelven a coincidir con un
    preset, la fila vuelve sola a su nombre. Los ajustes de estilo no cuentan (`MatchPreset` sólo mira calidad).
  - **Opciones avanzadas de Gráficos** (interruptor): Detalle de sombras (1024 / 2048 / 4096), Radio cercano (16-40, de a 4),
    **Distancia de sombras lejana** (sin lejana, o 50-200 de a 10, más el 48 y el 96 para poder llegar a mano a Bajo y Alto, y el 128
    del Ultra de antes; Medio es 80 y Ultra 140, que ya caen en la grilla; al
    cambiarla el radio cercano se acomoda solo a `12 + lejana/8` de a 4 m entre 16 y 32, que da 20 con 48, 24 con 80 y con 96 y 28 con 140, y
    después se puede ajustar a mano), **Filtro de sombras** (5 / 9 lecturas), Suavidad (60-130), Distancia del pasto (0-120),
    Relieve fino y Partículas de rueda. Llevar a mano esas filas a los valores de un preset lo deja en ese preset (la fila
    "Sombras" lee "Altas"), no en "Personalizado".
  - Las filas de varias opciones con nombre dan la vuelta con Enter o click (salvo Calidad gráfica); los números (`range`) no dan la vuelta con
    las flechas y Enter los sube y vuelve al mínimo. Un valor del ini que no es uno de los niveles muestra el más cercano
    (la viñeta 15 de fábrica se ve "Suave").
  - **DrawMenu**: la pista de la marcada va en hasta dos líneas con el espacio de las dos siempre reservado (los presets
    tienen frases de 130 caracteres y las filas no tienen que saltar al recorrerlas); filas sin grupo no llevan
    encabezado; nueve filas visibles (`visibleRows`); el aviso del juego (`message`) también se ve con el menú abierto
    (tarjeta arriba a la derecha): "Renovamos los gráficos..." aparece en el menú principal al arrancar.
- **preferencias.ini** (`graficos_version = 2`): claves nuevas `grafico_preset` (`bajo|medio|alto|ultra|personalizado`),
  `grafico_base` (el preset al que vuelve Personalizado; extra a la especificación, sin ella no se sabría con qué
  preset se había partido), `grafico_auto`, `sombras_lejos`, `sombras_filtro`, `pasto_densidad`, `resolucion_render`,
  `filtro_texturas`, `distancia_props`, `distancia_gente`, `estilo_color` (`natural|vivido|suave`). `SavePrefs` sigue
  escribiendo las claves viejas (`sombras_calidad`, `pasto_distancia`...) para poder volver a una versión anterior.
  - **Lectura**: (1) un preset con nombre manda y se ignoran las claves sueltas de calidad; (2) el estilo se lee siempre y
    se recorta a los rangos del menú (sol 70-130, ambiente 60-140, exposición 70-130, niebla 25-150, viñeta 0-60,
    grano 0-40, aberración 0-50, intensidad 40-100, viento 0-150, suavidad 60-130); (3) `personalizado` (o sin clave):
    las sueltas, recortadas. La física del suelo, el sonido, la moto y el nombre se leen siempre.
  - **Migración**: un archivo sin `graficos_version` (v0.2.8 y anteriores) se copia a `preferencias.ini.v1.bak` (o `.bak2`...
    si ya había una), se conserva todo lo no gráfico, lo gráfico vuelve al preset de la placa con el estilo en sus
    defaults, se reescribe con la versión 2 y el primer cuadro dice "Renovamos los gráficos: quedaron en Alto.
    Ajustes → Gráficos." (`startNotice`: `LoadMap` pisa el mensaje, por eso se muestra después de armar el mapa).
- **Autodetección de la placa** (`src/GpuPreset.h`, `Renderer::GpuDescription`): `glGetString(GL_VENDOR/GL_RENDERER)` con
  `glfwGetProcAddress` después de `InitWindow`, antes de leer el archivo. Sin archivo, o con uno viejo, el preset sale de
  ahí con `grafico_auto = 1`. Bajo: Intel HD/UHD, Iris viejo, Vega y "Radeon(TM) Graphics" integradas, 610M, GeForce MX y
  GT, GTX 4xx-7xx, llvmpipe / SwiftShader / GDI; Medio: Iris Xe/Plus, Arc A3xx e integrada, Apple M base, GTX 1050-1060 y
  16xx, RX 4xx/5xx, Radeon 660M-890M; Alto: RTX, GTX 1070+, RX 5000+, Vega 56/64, Arc A5xx/A7xx/B5xx, Apple Pro/Max/Ultra;
  lo que no se reconoce, Medio. Ultra no sale nunca solo. La función no depende de raylib: se probó con 70 cadenas reales
  (NVIDIA, Intel, AMD, Mesa, ANGLE y las de Apple `Apple M1`, `Apple M2 Pro`, `Apple M3 Max`...: no hay Mac a mano, se
  comprobó por lectura de las cadenas). Esta PC (RTX 5070): Alto.
- **Red de seguridad de fps** (`Game::PerfWatch`): sólo con `grafico_auto = 1`, jugando uno (no el bot ni pruebas ni capturas): 5 s de
  manejo salteados (carga de shaders) y 12 s medidos con el menú cerrado, sin pausa, con la ventana enfocada y sin contar
  cuadros de más de 0.25 s; si el cuadro medio pasa 1.35 veces el período del monitor baja un escalón, avisa y guarda.
  Una vez por preset. Tocar cualquier ajuste de calidad la apaga para siempre (`grafico_auto = 0`).
- **`--gfx bajo|medio|alto|ultra`** aplica el preset después de leer el archivo y **no guarda nada** (mientras dura, `SavePrefs`
  no escribe); `--noprefs` ni lee ni escribe `preferencias.ini`. Ver PRUEBAS.md.
- **Se comprobó** (exe MinGW, 1280×720, capturas): sin archivo la placa sale Alto y no se crea el ini hasta tocar algo; las tres
  páginas con el teclado (`tools\teclas.ps1`: Sección ← →, filas, Enter en "Opciones avanzadas") y con el mouse; el cambio a
  Personalizado (Resolución 85%) con el ini en `grafico_preset = personalizado`, y la vuelta (← sobre Calidad, o dejar el
  valor otra vez en 100%) a `alto`; el ini extremo del jugador (`sombras_calidad = 3`, sol 60, ambiente 20, niebla 190,
  aberración 60, pasto 105) migra a Alto con el `.bak` idéntico byte a byte y el aviso en el principal; un ini viejo mínimo
  (v0.2.7) conserva volumen, motion blur y nombre; un ini nuevo con `grafico_preset = bajo` e ignora las claves sueltas
  (Calidad "Bajo", Sombras "Bajas", Distancia "Corta", Pasto "Poco", 4x, 75%); un `personalizado` con base `ultra` y estilo
  fuera de rango se recorta (sol 130, ambiente 60, exposición 70, niebla 150, viñeta 60, grano 40, aberración 50) y
  conserva `tierra_blandura`/`tierra_deformacion`.
- **Red de seguridad** (con un `getenv` temporal, ya sacado, que forzaba el umbral a 0): Alto -> Medio a los 17 s y Medio -> Bajo a los
  34 s (una vez por preset, cada uno con su 5 + 12 s), `grafico_auto = 1` y `grafico_preset = bajo` guardados, y el aviso "Bajamos
  los gráficos a Bajo para que ande más fluido. Ajustes → Gráficos." en el HUD. Sin forzar, esta PC (cuadro medio 6.5 ms, monitor
  5.6 ms) no baja.
- **Limpieza posterior** (exe MinGW final, `--noprefs --gfx`, teclas de verdad, 1280×720): `--gfx ultra --menu ajustes`, Enter dos veces
  sobre Calidad gráfica: sigue "Ultra" (captura); `--gfx medio`, Opciones avanzadas, Distancia lejana con → hasta 96 y Filtro en 9
  lecturas: la fila Sombras lee "Altas" (captura).
- **Lecciones**: (1) Enter y click no son "→": en una fila de varias opciones el click del medio de la fila tiene que
  hacer lo que Enter (subir y dar la vuelta), no `+1` sin vuelta, o queda pegado en el último (excepción: los presets de
  calidad, donde dar la vuelta de Ultra a Bajo por un click perdido es peor que quedar pegado: ahí Enter y click suben con
  tope); las filas avanzadas tienen que poder llegar a mano a los valores de los presets; (2) `LoadMap` llama a
  `ShowMessage` con el nombre del mapa y pisa cualquier aviso armado antes: guardar el aviso aparte y mostrarlo después;
  (3) un aviso en el HUD no se ve con el menú abierto y el juego arranca con el menú: `DrawMenu` tiene que dibujar el
  mensaje; (4) para probar el mouse de verdad con `PostMessage` a la ventana (`WM_MOUSEMOVE` + `WM_LBUTTONDOWN/UP`) hay
  que dividir las coordenadas por la escala de pantalla de Windows (aquí 125%: para clickear en (400, 297) de la captura,
  enviar (320, 238)); (5) en esta máquina no hay MSVC (`tools\compilar.bat` falla): se compiló con MinGW (WinLibs) en una
  carpeta aparte; (6) los scripts de edición con `\n` o `\x..` dentro de heredocs de bash se corrompen (el `\n` de un
  `printf` se convirtió en un salto de línea real): escribir el script con la herramienta de archivos.

## Cámara con el stick derecho y el cuerpo a la cruz (octubre de 2026)

> **El cuerpo de costado, sólo en la de carreras (v0.3.7, a pedido del usuario)**: en las otras no lo notó como una
> mejora y le complicaba el whip; en la de carreras sí cambia cómo dobla (colgarse hacia adentro), así que ahí queda.
> `Game::SideBody()` (estilo `race`) decide si la entrada llega (en las otras motos queda en 0) y si se muestran las
> filas de Controles y de la ayuda (H). En la de carreras, **por defecto el stick derecho es el cuerpo y no hay cámara
> con el joystick** (a pedido: "en la de carreras dejá por defecto la cámara apagada y el cuerpo con el stick derecho");
> el ajuste "Stick derecho en la Carrera: Cámara" (`stick_derecho_carrera` en preferencias.ini) pone la cámara en el
> stick y el cuerpo en la cruz. `Game::StickCamera()`: las otras motos, siempre cámara. Comprobado con ventana y
> `--pad "0.5:1,0"`: carrera `side=1` y órbita 0; con `--stick camara`, `side=0` y órbita 43°; motocross, órbita 43°.
> Sin ventana la entrada del jugador no llega (ReadPlayerInput necesita la ventana): estas pruebas van con ventana.
> Lo de abajo describe cómo era con el cuerpo en todas las motos.

- **Qué pasaba**: con el joystick no había cómo mirar alrededor (la órbita era sólo arrastrando el mouse) y el stick derecho
  movía el cuerpo de costado. El usuario pidió la cámara en el stick derecho, suave y que al soltarlo vuelva sola, el cuerpo de
  costado a la cruz y poder intercambiarlos desde Ajustes.
- **Qué se hizo**:
  - `ChaseCamera::StickOrbit(x, y, realDt)` (cada cuadro, antes de `Update`): el stick pide una **velocidad** de giro (no un
    ángulo). Detalle de la cámara en RENDER.md ("Órbita con el joystick").
  - `Game::ReadPad()` junta el stick derecho y la cruz del joystick (o del guion de `--pad`); `Game::PadLook()` decide qué mueve
    la cámara según el ajuste: con **Cámara** (default) el stick derecho con zona muerta **redonda** de 0.18 y la curva de
    `StickCurve` sobre el largo (`StickCurve2`: con una zona muerta por eje, en diagonal suave se perdía un eje); con **Cuerpo**,
    la cruz a 0.75 (← → gira, ↑ ↓ sube y baja). Con el menú abierto no pide nada (la cruz y los sticks son del menú).
  - **Cuerpo de costado con la cruz**: ← → de la cruz se suman a ← → del teclado en `ReadPlayerInput` (mismo `KeysDown`), así
    tienen la misma rampa de `KeyboardInput` (sube a 5/s, baja a 6/s): con una cruz digital no hay saltos de 0 a 1. Sólo con el
    menú cerrado. ↑ ↓ de la cruz no hacen nada en ese modo (el cuerpo adelante y atrás sigue en el stick izquierdo).
  - **Ajuste** "Stick derecho: Cámara / Cuerpo" en Juego y sonido, grupo JOYSTICK (Enter, click o ← → lo cambian), con la pista
    que dice dónde quedó lo otro. `preferencias.ini`: `stick_derecho = camara|cuerpo` (sin la clave, cámara). `--stick
    camara|cuerpo` lo cambia por esa vez (`rightStickCamera`; lo que se guarda es `prefStickCamera`).
  - **Controles** y la **ayuda de teclas (H)** siguen al ajuste: "Cuerpo a los costados ← → / cruz" o "/ stick der." y "Mirar
    alrededor: mouse / stick der." o "/ cruz" (fila nueva de la ayuda: la columna derecha tiene 6 y la izquierda 5; el panel
    toma la más alta). Las tablas de Controles dejaron de ser `static const` (cambian con el ajuste).
- **Se comprobó** (exe MSVC, 1280×720, `--pad` porque no hay joystick acá; capturas en
  `compilaciones/build-msvc-interfaz/cap/`):
  - `g_yaw90.png` (a fondo a la derecha 0.75 s: llega a 93° mirando de costado y vuelve, al 10% en ~1.3 s), `g_yaw180.png` (a la
    izquierda 1.45 s: 178°, la moto de frente, y vuelve por el lado corto), `g_pitch.png` (arriba 48° y abajo -15°, sin meterse en
    el suelo), `g_360.png` (2.6 s a fondo: la vuelta entera, y al soltar vuelve por los 24°-76° que quedaban, no desenrolla la vuelta).
  - Log `CAM` cuadro a cuadro: sin saltos; la velocidad de la órbita pasa de -161°/s a 0 en ~0.15 s al soltar y la vuelta arranca
    desde 0 subiendo de a 5-7°/s por cuadro (continua también en los cuadros de 0.2 s de las capturas).
  - Con `--stick cuerpo` la cruz mueve la cámara y el stick derecho no; con `--stick camara` la cruz mueve el cuerpo (`side=1.00`
    en la telemetría con la Carrera) y el stick no. **Ojo: la Motocross 450 y la 600 (`original_031 = 1`) no tienen cuerpo de
    costado**: su física 0.3.1 ignora `in.side` (la telemetría da `side=0` con cualquier entrada); para probarlo, otra moto.
  - Caído (ragdoll, `--test crashloop --respawn-after -1`), `--side` (gira desde la vista lateral y vuelve a ella) y `--view`
    (queda fija: el stick no la mueve).
  - El ajuste: `tools\teclas.ps1` en Juego y sonido, Enter sobre Stick derecho: la pista cambia, y en una copia del exe (en el
    scratchpad, no en la carpeta de compilación) queda `stick_derecho = cuerpo`; con ese archivo, Controles y la ayuda dicen "cruz"
    para la cámara y "stick der." para el cuerpo.
  - Regresión idéntica contra la v0.3.6 (las 7 de PRUEBAS.md).
- **Lecciones**: (1) una cámara con el stick se maneja por velocidad, con suavizado exponencial (`1 - exp(-k·dt)`) para que acelere
  y frene igual a cualquier fps; la vuelta, con la solución exacta del resorte críticamente amortiguado (no paso a paso); al pasar
  de una a otra, la velocidad que traía pasa al resorte (y al revés, al tomar el stick durante la vuelta) para que no haya tirones;
  (2) la zona muerta de un stick de cámara va redonda y un poco más grande que la de manejo: un stick que no vuelve a 0 trabaría la
  vuelta atrás; (3) la cruz digital para un eje analógico se pasa por la rampa del teclado, no se inventa otra; (4) antes de
  concluir que una entrada "no llega", mirar si la moto la usa (la física 0.3.1 de la Motocross no tiene cuerpo de costado).

## Minimapa, línea ideal con frenada, HUD en las esquinas, código en pausa y reiniciar (octubre de 2026)

Pedidos del usuario: "agrega minimapa, y si es posible linea ideal que se pone en rojo o verde cuando hay que frenar o estas
bien"; después, "la linea de carrera mostrara tambien el recorrido ideal de cada mapa verdad, para maximizar los apex", "en
mapas como favela o las dunas no hace falta la linea de pista, ya que no son circuitos", "arriba a la derecha el minimapa, y
arriba a la izquierda la vuelta, y tiempos de vuelta" (con "el tiempo de vuelta que quede arriba en el medio como esta"), "el
codigo online que se vea solo al poner pausa con esc" y "agrega un boton en pausa, para reiniciar la carrera". Código en
`src/Assists.cpp` (minimapa y línea, miembros de `Game`) y en `Game.cpp` (HUD, menú, ajustes, preferencias).

### Minimapa (`Game::BuildMiniMap`, `DrawMiniMap`)
- **Dónde**: arriba a la derecha, `kMiniMapSize` (196) × `ui` de lado, a 18 × `ui` de los bordes. Debajo, los pilotos en red (mismo
  ancho). Antes de que el usuario lo pidiera ahí estaba arriba del velocímetro.
- **Qué**: un relieve del terreno visto desde arriba en una textura de 512² armada una vez por mapa (sombreado con la pendiente
  exagerada ×3 y luz desde arriba a la izquierda, un poco más claro lo alto, y la pista o las calles más claras con
  `Terrain::TrackMask`: en la favela salen también las calles que cruzan), el trazado encima (borde oscuro y línea clara del 70%
  del ancho de la pista, mínimo 3 px), la largada (raya naranja), la moto propia (flecha naranja con borde) y las de los demás en
  red (flechas más chicas del color de su moto; si quedan afuera, contra el borde).
- **Orientación** (ajuste "Minimapa" y tecla **N**, `minimapa = norte|gira|no`): **norte arriba** (por defecto) muestra la pista
  entera fija y la flecha se mueve: en estas pistas cerradas y chicas se ve dónde estás en la vuelta y lo que viene; hasta el
  autódromo (unos 900 m) entra entero y se lee. **Gira con la moto**: centrado en la moto (un poco abajo del centro, para ver más
  lo que viene), lo de adelante hacia arriba, una ventana de radio `0.3 × tamaño de la pista` (entre 60 y 200 m) y una "N" en el
  borde donde queda el norte; el rumbo se suaviza 0.12 s (la moto serpentea y el mapa entero temblaba). Apagado: nada.
- **Mapas libres** (Los Médanos): no hay trazado que mostrar (la guía es sólo para el bot), pero el relieve sí aporta: se ven las
  líneas de saltos, el serrucho, el cráter y la pared, y dónde estás entre ellos. Se encuadra el terreno entero.
- **Orientación del mundo**: el norte es +z y, visto desde arriba, +x queda a la **izquierda** (la cámara mirando a +z tiene +x a la
  izquierda). La convención de los `.json` ("90 = este, +x", imagen con el oeste a la izquierda) es la de un mapa espejado: el
  minimapa sigue a lo que se ve en el juego (doblar a la derecha gira la flecha a la derecha). Comprobado contra una vista cenital
  (`--view 0 89 60`): el trazado del minimapa es el de la escena girado, no espejado.
- **Costo**: el trazado se simplifica al armarlo (un punto cada 2 m en las curvas y cada 30 m en las rectas) y se dibuja como una
  tira con juntas en inglete en un solo `rlBegin(RL_TRIANGLES)`: **0.2 ms por cuadro**. La primera versión (DrawLineEx más un
  `DrawCircleV` en cada junta para que no quedaran muescas) costaba 1.5-1.9 ms en el autódromo: un círculo son 36 triángulos.

### Línea ideal y de frenada (`Game::BuildRacingLine`, `DrawBrakeLine`)
- **Sólo en los circuitos**: clave `"racing_line": true` del mapa (la pista de motocross y el autódromo; ver MODDING.md). La favela,
  Los Médanos, el valle y el parque no la tienen (el usuario: "no son circuitos"). Nunca con `"style": "guide"`.
- **La trayectoria** (afuera-adentro-afuera) se calcula al cargar el mapa con K1999 (el método de Rémi Coulom del bot de TORCS):
  sobre puntos cada 2 m del trazado, cada uno se corre de costado para que su curvatura sea el promedio de las de sus vecinos
  (ponderado por la distancia), con un paso de Newton y recortado al ancho de la pista menos 1.1 m al centro de la moto (más un
  margen que crece con la separación entre puntos); primero con puntos cada 128 m, después 64, 32... hasta 2 m (100·√paso pasadas
  cada vez), interpolando los del medio. La curvatura de la línea se mide con cuerdas de ±4 m y se promedia ±4 m (con 2 m sale
  ruidosa); todo se guarda por punto del trazado (`rlOffset`, `rlCurv`). Tarda unos ms.
- **Peraltes** (la pista de motocross): en un berm se dobla a fondo sólo donde el peralte bajo la moto pasa los 11° (MAPAS.md), o
  sea de `t = tan 11° · hw / bank` hacia afuera (`bank·t²` de costado). Ahí la línea no puede ir más adentro de `t + 0.08` (entra
  de a poco entre 0.6 y 1.4 m de peralte): el vértice queda en la cara del berm y no en el plano de adentro. Ojo con el signo:
  `Track::Bank > 0` es el berm del lado `−n` (n = (−tz, tx)), o sea el borde de adentro es el de `+n`; la primera vez quedó al
  revés y la línea se pegaba al borde de adentro de todas las curvas (se vio en la captura cenital).
- **Cuánto mejora** (una vuelta simulada: la velocidad de la curva `sqrt(bot_lateral / curvatura)`, acelerando a 3.5 m/s² y
  frenando a 6, tope 35 m/s): motocross 46.2 → 41.1 s (radio mínimo 13.0 → 22.6 m), autódromo 137.7 → 117.0 s (18.5 → 21.5 m;
  3757 → 3695 m de largo), y también en los que no la muestran: favela 74.7 → 70.4, valle 34.0 → 30.2, parque 36.6 → 31.8. Medir
  sólo el radio o `Σ 1/sqrt(curvatura)` sin aceleraciones engaña: con la aceleración lateral como único límite, una curva más
  abierta es más larga y "más lenta" (t ∝ √R); lo que gana la línea ideal es la velocidad mínima y las frenadas.
- **Los colores**: en cada punto adelante, la velocidad de la curva sobre la línea ideal con la aceleración lateral del mapa
  (`bot_lateral`, la misma del bot) y, de atrás para adelante, la más alta con la que se llega frenando a todas las que siguen,
  `sqrt(v² + 2·a·d)` con la distancia real entre puntos de la línea. Con la velocidad de ahora: hasta el 95% de esa, **verde**; al
  100%, **amarilla**; desde el 108%, **roja** (la naranja en el medio). El rojo arranca donde, yendo así, ya habría que estar
  frenando: al frenar retrocede, y al llegar a la moto, es "frená ya". **La desaceleración es 6 m/s²**, no la del bot (4, o 2.5
  en las calles): el bot frena tosco y temprano, y con 2.5 la recta del autódromo salía roja 900 m antes de la horquilla. Las motos
  frenan 0.7-1 g: 6 deja margen para reaccionar. El bot anda justo en el límite (su velocidad objetivo es esa misma con 4 m/s²),
  así que con él la línea se ve amarilla en las curvas; por eso el rojo empieza al 108% y no al 100%.
- **Cuánto se ve**: de 1.5 m adelante de la moto (aparece de a poco hasta los 6 m) hasta `25 + 3·v` m (~3 s; entre 35 y 180),
  desvaneciéndose en el último 45%. Se apaga lejos de la pista (de 2 a 10 m afuera del borde), caído, con el menú abierto, con F2
  y con `--nohud`. Para dónde va: con la velocidad, o parado hacia donde mira la moto (en contramano, la línea va para el otro lado).
- **Ajuste** "Línea de frenada" y tecla **L** (`linea_frenada`, prendida por defecto: el usuario la pidió, en los mapas que no son
  circuito no aparece igual, y se apaga con una tecla). En el suelo: ver RENDER.md ("Línea de frenada en el suelo").

### HUD en las esquinas, código en pausa
- **Arriba a la izquierda**, sobre un panel como el del minimapa (sin él, sobre el cielo o los boxes del autódromo el gris no se
  leía): "Vuelta N" y debajo "Última" y "Mejor" (en gris hasta que haya una vuelta). Antes era una
  línea chica debajo del cronómetro. **El cronómetro de la vuelta sigue arriba al centro**, como pidió. En un mapa libre, arriba
  al centro siguen los saltos y la esquina queda vacía. Los datos técnicos (T) bajan a 112 × `ui` para quedar debajo.
- **Arriba a la derecha**: el minimapa y, debajo y del mismo ancho, "EN RED · anfitrión / conectado / conectando" y los pilotos
  con su color y su ping. **El código para invitar ya no está en el HUD**: se ve sólo en el menú (Esc), en la tarjeta "AHORA" del
  anfitrión ("Código para invitar · F10 lo copia" y el código grande). F10 lo copia corriendo y también con el menú abierto
  (`HandleMenuKeys`, `Menu::Main`).
- Lo demás no cambió: el velocímetro abajo a la derecha, la ayuda de teclas abajo a la izquierda, los avisos arriba al centro.

### Reiniciar carrera (`Game::RestartRace`)
- En el menú (Esc), debajo de "Seguir corriendo" (sólo después de haber corrido) o de "Volver a la carrera" (en red): **"Reiniciar
  carrera"** ("Volver al inicio" en un mapa libre). Hace lo de Retroceso / Back (que ahora llama a lo mismo): la moto a la
  largada, el cronómetro, el número de vuelta y la "Última" desde cero, la **Mejor queda** (y el mejor salto), los objetos sueltos
  a su lugar, y cierra el menú. En red, sólo la moto propia (cada PC mueve sus objetos).

### Preferencias y pruebas
- `minimapa = norte|gira|no` (sin la clave: norte) y `linea_frenada = 1|0` (sin la clave: 1). Se guardan desde Ajustes o con N y L.
- Con el bot o una prueba no aparecen (como la ayuda de teclas: capturas limpias). Para las capturas: `--minimapa norte|gira|no` y
  `--linea si|no|toda` los fuerzan por esa vez (no se guardan); `toda` dibuja la vuelta entera, ancha y sin desvanecer, para mirarla
  desde arriba (`--test idle --spawn S --view 0 89 14`).
- Las teclas nuevas en `tools/teclas.ps1`: N, L y F10.

### Cómo se comprobó (exe MSVC, 1280×720; capturas en `compilaciones/build-msvc-interfaz/cap/mm/` y `cap/mm2/`)
- Cenitales de la línea ideal: `top_mx_190.png`, `top_mx_530.png`, `top_mx_700.png` (motocross: el vértice en la cara del berm),
  `top_circ_950.png` (la horquilla: entra abierta, toca el piano de adentro y sale abierta), `top_circ_1140.png`, `top_circ_1300.png`,
  `top_circ_1600.png`; `top_mx.png` la vuelta entera.
- Verde y rojo: `grilla_circ.png` (`--map base/circuito --test "teclas:W+230,W40" --spawn 420 --shots-every 1`): verde a 158-222
  km/h en la recta, roja a 242-284 llegando a la horquilla (la prueba no frena y se va contra la tribuna, como corresponde).
  `mx_norte_datos.png`: verde con la moto lenta.
- Minimapa: `mx_norte_datos.png` (con los datos técnicos abajo de la vuelta), `circ_gira.png`, `fav_gira.png` (gira, con la N),
  `med_norte.png` (Los Médanos, relieve solo).
- En red (anfitrión con ventana y un invitado sin ventana): `red_jugando.png` (la flecha azul del invitado en el minimapa, los
  pilotos debajo, sin código) y `red_menu.png` (Esc: el código en la tarjeta con "F10 lo copia", "Reiniciar carrera").
- `nohud.png` (`--nohud`: ni minimapa ni línea).
- Teclas de verdad (`tools\teclas.ps1`, en una copia del exe en el scratchpad con sus preferencias): N y L dejan
  `minimapa = gira` y `linea_frenada = 0` en el archivo y la captura muestra el minimapa girando sin línea; en Ajustes → Juego y
  sonido, las filas Minimapa y Línea de frenada; Controles con N y L (entra a 720); Esc → "Reiniciar carrera" → la moto en la
  largada con el cronómetro en "--:--.--" y la vuelta vuelve a arrancar al cruzar (`grilla_reinicio.png`, serie de capturas).
- Cuadros: 16.7 ms de media antes y después (vsync a 60 Hz) en el autódromo y en la motocross, con el bot. Medido por dentro: el
  minimapa 0.2 ms; la línea, 0.45 ms la primera vez que se vacía el lote de rlgl en el cuadro y 0.15 ms una segunda copia (ver
  RENDER.md: esa espera es la de la placa terminando lo anterior, no costo de la línea).
- Regresión idéntica contra la v0.3.7 (la lista de PRUEBAS.md más el bot en el autódromo y en la favela) con los datos de la v0.3.7;
  con los de la carpeta de trabajo, `brakeslide` da distinto por `handbrake_lean` de las motos de tierra (otro cambio en curso, no
  de la interfaz: con el exe nuevo y los `.ini` de la v0.3.7 da idéntico).

### Lecciones
- **Medir lo que gana una trayectoria con una vuelta simulada** (límite de curva, aceleración y frenada), no con la curvatura ni con
  `Σ ds / sqrt(a/κ)`: con la lateral como único límite, abrir la curva "empeora" el tiempo.
- **Probar el signo de los costados con una captura cenital** (`--linea toda --view 0 89 14`): el peralte, la normal de la pista y
  la izquierda del minimapa tienen cada uno su convención, y una al revés no rompe nada, sólo pone la línea del lado equivocado.
- **Un mapa visto desde arriba en este juego tiene +x a la izquierda** (el de los `.json` está espejado respecto de lo que se ve).
- **Pocas primitivas en el HUD**: un `DrawCircleV` son 36 triángulos; mil juntas redondas costaban más que todo lo demás del HUD.
  Una tira con juntas en inglete hace lo mismo con dos triángulos por tramo.
- **Lo nuevo del HUD se ve sólo jugando**: con el bot y las pruebas, apagado salvo que se pida (`--minimapa`, `--linea`), así las
  capturas de las otras áreas no cambian.
