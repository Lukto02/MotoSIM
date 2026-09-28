# Documentación de desarrollo

Lo que fuimos aprendiendo al hacer MotoSim, para no trabarnos dos veces con el mismo problema y tener
puntos de referencia. El `README.md` de la raíz cuenta **qué hace** el juego; `MODDING.md` es para quien
arma mapas y motos sin tocar código. Estos documentos cuentan **cómo se hace y por qué así**: decisiones,
trampas, números que funcionaron y cómo probarlo.

| Documento | De qué trata |
|---|---|
| [MOTOS.md](MOTOS.md) | Diseñar y afinar una moto: estilos, piezas, colores, pose del piloto, tuning, fichas, pendientes |
| [FISICA.md](FISICA.md) | Problemas de física resueltos (causa, arreglo, cómo se verificó) y reglas para no romper nada |
| [PILOTO.md](PILOTO.md) | El piloto: modelo glTF, IK sobre la moto, ragdoll, articulaciones de Jolt |
| [MAPAS.md](MAPAS.md) | Mapas y terreno: tamaños, generadores, circuitos, el bot en cada mapa |
| [PRUEBAS.md](PRUEBAS.md) | Cómo compilar y probar: regresiones, pruebas sin ventana, capturas, red |
| [MENU.md](MENU.md) | Menú e interfaz: cámara del menú, selector de motos, criterios visuales |
| [SONIDO.md](SONIDO.md) | Sonido procedural: motor 4T y 2T, derrape, raspado; cómo medirlo sin escuchar |
| [RENDER.md](RENDER.md) | Gráficos: superficies que se pisan (z-fighting), calcomanías, sombras |
| [RED.md](RED.md) | Multijugador en red local: protocolo, qué viaja, remotas, cómo probar con dos instancias |

## La regla

Quien resuelve algo que no era obvio (un bug de física, un número de tuning que costó encontrar, una
trampa de Jolt o de raylib, un criterio de diseño) lo anota en el documento que corresponde **en el
mismo cambio**. Vale para cualquier sesión y para cualquier agente: al terminar, además de su informe,
deja sus lecciones en estos archivos (o en su informe, para que las pase quien lo lanzó).

Cada entrada, corta: **qué pasaba → por qué → qué se hizo → cómo se comprobó** (con el comando o el
número). Si algo queda pendiente, va en la lista de pendientes del documento, con lo que ya se sabe.

## Los agentes expertos

En `.claude/agents/` hay un agente de Claude Code por área. Cada uno trae el contexto del proyecto, las
reglas, lo que sabemos de su área, las trampas conocidas y cómo probar, así no hay que prepararlo cada
vez. Cada uno lee su documento antes de empezar y anota ahí lo que aprende; este documento y los de
cada área son su memoria, así que **lo que se aprende va a `docs/`, no al archivo del agente**. El
archivo del agente se actualiza sólo cuando cambia algo de fondo (una herramienta nueva, una regla, un
archivo que se movió).

| Agente | Área | Su documento |
|---|---|---|
| `motosim-motos` | Motos nuevas y cambios: estilo, piezas, colores, pose, tuning, ficha | MOTOS.md |
| `motosim-mapas` | Mapas nuevos y cambios: terreno, circuitos, favela, objetos, el bot | MAPAS.md |
| `motosim-graficos` | Render: shaders, luz y sombras, mallas, calcomanías, z-fighting, rendimiento | RENDER.md |
| `motosim-fisica` | Ruedas, contactos, agarre, frenadas, derrapes, choques, Jolt | FISICA.md |
| `motosim-piloto` | Modelo glTF, IK, pose, ragdoll y sus articulaciones | PILOTO.md |
| `motosim-sonido` | Motor 4T/2T, petardeos, derrape, raspado | SONIDO.md |
| `motosim-interfaz` | Menú, selector, vitrina, HUD, cámara del menú | MENU.md |
| `motosim-red` | Protocolo, remotas, sincronización, pruebas en LAN | RED.md |
| `motosim-paquete` | Compilar la versión, armar el zip y verificar todo | PRUEBAS.md |

Para que varios trabajen a la vez, cada uno compila en su propia carpeta
(`tools\compilar.bat build-msvc-<área>`, que reusa las dependencias ya bajadas de `build\_deps`). Si dos
tienen que tocar los mismos archivos (por ejemplo `Game.cpp`), conviene que vayan de a uno, o cada uno
en su worktree de git, y después juntar los cambios. Ninguno hace commits: eso lo hace quien los lanzó,
después de revisar el informe.
