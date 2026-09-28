# MotoSim

Sandbox de física de motos en C++17 con raylib 5.5 (render, input, audio) y Jolt 5.6 (física).
Comentarios del código, textos del juego y documentación de desarrollo en castellano rioplatense;
`MODDING.md` (para quien arma mods) en inglés.

## Antes de trabajar

- Leer el documento de `docs/` que corresponde a lo que se va a tocar (índice en `docs/README.md`):
  motos → `docs/MOTOS.md`, física → `docs/FISICA.md`, piloto/ragdoll → `docs/PILOTO.md`, mapas →
  `docs/MAPAS.md`, pruebas → `docs/PRUEBAS.md`, menú → `docs/MENU.md`, sonido → `docs/SONIDO.md`, gráficos → `docs/RENDER.md`. Ahí
  están las trampas conocidas.

## Reglas

- **La física de la Motocross 450 no cambia.** Lo nuevo va en el `.ini` de cada moto o detrás de una
  clave cuyo default reproduce lo de antes; comprobar con `tools/regresion.sh` contra el último paquete
  (`dist/`) que la telemetría da idéntica (ver `docs/PRUEBAS.md`).
- **Documentar lo aprendido en el mismo cambio**: todo problema no obvio resuelto (física, tuning,
  diseño de una moto o un mapa, trampas de Jolt/raylib, criterios de interfaz) va al documento de
  `docs/` que corresponde, con qué pasaba → por qué → qué se hizo → cómo se comprobó. Los agentes
  también: sus lecciones van a esos archivos o en una sección "lecciones" de su informe.
- Los nombres de versión los elige el usuario (`src/Version.h`); el protocolo de red (`kProtocol` en
  `Multiplayer.cpp`) sube cuando cambia el formato de los paquetes.
- Las pruebas visuales del piloto, con el modelo glTF (el principal).

## Dónde está cada cosa

- Estructura del código: README ("Estructura").
- Motos: `mods/<mod>/bikes/*.json` + `.ini`; estilos en `src/BikeStyles.*` y piezas en
  `src/BikeMeshes.cpp` (una sección por estilo).
- Mapas: `mods/<mod>/maps/*.json`; los de prueba en `pruebas/mods/prueba/` (no se empaquetan).
- Herramientas: `tools/` (regresión, grilla de capturas).
