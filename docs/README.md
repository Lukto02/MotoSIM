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

## La regla

Quien resuelve algo que no era obvio (un bug de física, un número de tuning que costó encontrar, una
trampa de Jolt o de raylib, un criterio de diseño) lo anota en el documento que corresponde **en el
mismo cambio**. Vale para cualquier sesión y para cualquier agente: al terminar, además de su informe,
deja sus lecciones en estos archivos (o en su informe, para que las pase quien lo lanzó).

Cada entrada, corta: **qué pasaba → por qué → qué se hizo → cómo se comprobó** (con el comando o el
número). Si algo queda pendiente, va en la lista de pendientes del documento, con lo que ya se sabe.
