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
  sólo el desenfoque con la velocidad; la aberración, la viñeta y el grano siguen. F7 sigue apagando todo el
  post-proceso (para probar, no se guarda).
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
