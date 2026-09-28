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

## Opciones de la cámara y la imagen

- En el menú principal y en el de la partida en red, debajo de "Pantalla completa":
  - **Sacudón de cámara** (`camera.shakeEnabled`): apaga los golpes de aterrizajes y choques (`AddShake`: el
    sacudón, el zoom hacia adentro y la caída de la cámara) y la vibración fina a alta velocidad;
  - **Motion blur** (`motionBlur`): apaga sólo el desenfoque con la velocidad; la aberración, la viñeta y el
    grano siguen. F7 sigue apagando todo el post-proceso.
- El sacudón al aterrizar (`EmitEffects`, cada rueda que aterriza fuerte) quedó al 70% de lo que era, a pedido
  ("que se sienta más smooth"): `AddShake(0.084 + 0.315·impacto)`. Como la vibración va con trauma², queda a
  la mitad; el zoom hacia adentro y la caída de la cámara, al 70%. Los choques no cambiaron.
- Los dos prendidos por defecto. Se guardan en `preferencias.ini` (`sacudon_camara`, `motion_blur`); si faltan
  las claves (preferencias de una versión anterior), queda el default.
- Con dos opciones más, el menú principal sigue entrando a 1280×720 (el alto de cada línea se ajusta solo).

## Caído

- El jugador no reaparece solo: mira la caída y sale con R (Y en el joystick). "Caída" enseguida y, a los
  0.8 s, un panel "Tocá R para reaparecer" (con "o Y en el joystick" si hay uno) que aparece de a poco.
- El bot, las pruebas (`--test`) y sin ventana reaparecen solos a los 3 s (las vueltas y las regresiones
  cuentan con eso) y ahí queda la línea chica de siempre. `--respawn-after S` fija los segundos para
  todos; `-1`, sólo con R (para capturar el cartel: `--test crashloop --respawn-after -1`). Ver
  `AutoRespawnAfter` en `Game.cpp`.
- **La moto elegida se guarda sólo desde el menú**: `--bike` (pruebas) cambia la moto por esa vez. Antes se
  guardaba en `preferencias.ini` y la moto de una prueba (`--bike base/carrera`) quedaba para todos los mapas,
  hasta en la pista de motocross. `savedBike` es la de preferencias; `chosenBike`, la de ahora.
- Ya existía un "Caída / R / Y para reaparecer" chico en `DrawHUD`: el cartel nuevo lo reemplaza (no dos
  textos uno arriba del otro). Buscar antes lo que ya dibuja el HUD.
