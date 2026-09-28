# MotoSim

Sandbox de física de motos en C++17 con raylib 5.5 (render, input, audio) y Jolt 5.6 (física).
Comentarios del código, textos del juego y documentación de desarrollo en castellano rioplatense;
`MODDING.md` (para quien arma mods) en inglés.

## Antes de trabajar

- Leer el documento de `docs/` que corresponde a lo que se va a tocar (índice en `docs/README.md`):
  motos → `docs/MOTOS.md`, física → `docs/FISICA.md`, piloto/ragdoll → `docs/PILOTO.md`, mapas →
  `docs/MAPAS.md`, pruebas → `docs/PRUEBAS.md`, menú → `docs/MENU.md`, sonido → `docs/SONIDO.md`, gráficos → `docs/RENDER.md`,
  red → `docs/RED.md`. Ahí están las trampas conocidas.
- Compilar: `tools\compilar.bat [carpeta] [release]` (por defecto `build-msvc`, con VS 2022 Build Tools;
  reusa las dependencias de `build\_deps`). Después de tocar un `.h`, tocar los `.cpp` que lo incluyen.

## Agentes

En `.claude/agents/` hay un agente experto por área: `motosim-motos`, `motosim-mapas`,
`motosim-graficos`, `motosim-fisica`, `motosim-piloto`, `motosim-sonido`, `motosim-interfaz`,
`motosim-red` y `motosim-paquete` (armar y verificar una versión). Traen el contexto, las reglas y cómo
probar; para un trabajo de un área, lanzar el suyo en vez de uno genérico. Detalle en `docs/README.md`.
- Cada uno compila en `build-msvc-<área>`, así pueden trabajar a la vez; si dos tocan los mismos
  archivos, de a uno o cada uno en su worktree.
- No hacen commits; al terminar informan con una sección **Lecciones**. Quien los lanzó revisa que
  estén anotadas en `docs/` antes de commitear.
- Lo aprendido va a `docs/`; el archivo del agente se toca sólo si cambia algo de fondo (herramienta,
  regla, archivo movido).

## Reglas

- **La física de la Motocross 450 no cambia.** Lo nuevo va en el `.ini` de cada moto o detrás de una
  clave cuyo default reproduce lo de antes; comprobar con `tools/regresion.sh` contra el último paquete
  (`dist/`) que la telemetría da idéntica (ver `docs/PRUEBAS.md`).
- **Documentar lo aprendido en el mismo cambio**: todo problema no obvio resuelto (física, tuning,
  diseño de una moto o un mapa, trampas de Jolt/raylib, criterios de interfaz) va al documento de
  `docs/` que corresponde, con qué pasaba → por qué → qué se hizo → cómo se comprobó. Los agentes
  también: sus lecciones van a esos archivos o en una sección "lecciones" de su informe.
- **"Compilá" = paquete**: cuando el usuario pide compilar, se termina armando el paquete para compartir
  con la versión + 0.0.1 (`src/Version.h`: v0.2.6 → v0.2.7...), sin preguntar el nombre; si da uno, ése.
  Procedimiento en `docs/PRUEBAS.md` ("Paquete para compartir") o con el agente `motosim-paquete`. El
  protocolo de red (`kProtocol` en `Multiplayer.cpp`) sube sólo cuando cambia el formato de los paquetes.
- Las pruebas visuales del piloto, con el modelo glTF (el principal).

## Dónde está cada cosa

- Estructura del código: README ("Estructura").
- Motos: `mods/<mod>/bikes/*.json` + `.ini`; estilos en `src/BikeStyles.*` y piezas en
  `src/BikeMeshes.cpp` (una sección por estilo).
- Mapas: `mods/<mod>/maps/*.json`; los de prueba en `pruebas/mods/prueba/` (no se empaquetan).
- Herramientas: `tools/` (compilar, regresión, grilla de capturas, análisis de telemetría, sonido,
  frenadas y escaleras).
