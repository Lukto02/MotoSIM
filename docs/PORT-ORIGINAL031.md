# Motocross y piloto originales sobre 0.3.2

Port local realizado el 3 de octubre de 2026. Base: `MotoSIM-main 3.2/MotoSIM-main`, versión 0.3.2.
Referencia: la **primera** v0.3.1, empaquetada el 26 de septiembre de 2026 a la 01:15 (Argentina),
anterior a v0.2.1. No confundir con la posterior v0.3.1 de suelo deformable.

## Recuperación

Los backups del historial tenían inconsistencias: varias revisiones de `BikePhysics.cpp` contenían
cambios posteriores a la fecha indicada por el snapshot. Se recuperó su lectura completa del
25/09 a las 13:12 UTC y se reprodujeron 23 modificaciones registradas hasta el empaquetado.
El registro de sustituciones exactas está en `recovery-audit.json`. No se ejecutaron los comandos
históricos: el recuperador interpretó solamente cadenas literales y reemplazos de texto.

La física resultante se conserva como `src/BikePhysics031.cpp`. El cuerpo de la rutina original
sólo cambia de nombre a `PrePhysics031` y añade el metadato `Wheel::onTerrain` que los efectos
visuales de 0.3.2 requieren. Esas asignaciones no cambian las fuerzas ni su orden.

El ejecutable original de referencia tiene SHA-256:
`DBBC74EBA1AA2531128A560A6D84127759315312C6E49174540F574FFE54DEEC`.

## Integración

- `original_031=1` en `mods/base/bikes/motocross.ini` selecciona física y pose originales.
  El perfil incluye todos los parámetros de la moto del `tuning.ini` distribuido con la referencia.
- La Motocross 450 usa suspensión, dirección, neumáticos, frenos, wheelie, whip, scrub y caídas
  de la rutina recuperada. No se añade el patín de colisión de cola de versiones posteriores.
- Las otras motos siguen usando la rutina `BikePhysics.cpp` de 0.3.2.
- `RiderPoseLocal031`, `RiderModel.*` y `Rider.*` restauran pose, IK, dedos y ragdoll antiguos.
  El personaje original se carga para todas las motos: `Low_Poly_Motorcyclist_2_rigged.gltf`
  y sus dependencias, copiados del paquete original sin cambios.
- La compatibilidad `JointPose` devuelve la pose original, como hacía el ragdoll de esa versión.
  El campo `report` sólo mantiene compatibilidad con las llamadas de telemetría nuevas.
- Se mantienen gráficos, interfaz, mapas y sistema de mods de 0.3.2.
- En la Motocross original los surcos dejan marcas visuales, sin excavar el piso físico. Así no
  se introducen cambios de manejo por la deformación añadida en versiones posteriores.
- Jolt sigue en 5.6.0, raylib en 5.5 y el runtime MSVC es estático. Se desactiva DX12 de Jolt,
  que no se usa en el juego, para evitar compilar shaders de pelo/tela en las dependencias.

## Comprobación

Compilación Release terminada. Comparación de 14 pruebas, con igual secuencia de controles y
directorio de ejecución propio para cada versión: idle, accel, wheelie, turn, sharpturn, brake,
braketurn, reverse, whip, whiphold, flip, frontflip, crashloop y 40 segundos de bot en pista.
Todas las líneas de telemetría compartida coinciden exactamente; se excluyen solamente los
campos nuevos `side`, `gB` y `scr`. El detalle está en `regresion-original031.json`.
Esto verifica los valores impresos; no es una afirmación sobre todos los estados posibles del juego.

Las pruebas nuevas `brakeslide` y `airlean` no son comparables por nombre con la referencia:
la vieja no ejecuta la misma secuencia de controles. Se usaron `braketurn` y `frontflip`.

Capturas inspeccionadas de la versión original y el port: pose lateral, whip y curva cerrada.
Ambas cargan el modelo original: 24 huesos, 10 huesos virtuales de dedos y 5901 vértices.
La iluminación y el escenario conservan el render de 0.3.2.

Los ejecutables, fuentes y assets de las versiones anteriores no se sobrescribieron.
