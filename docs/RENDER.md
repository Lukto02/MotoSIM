# Gráficos: problemas resueltos y reglas

Render en `Render.cpp` (shaders de luz, sombras, cielo, terreno, pasto, post-proceso). Las mallas de los
mapas se arman por código (`Favela.cpp`, `Circuit.cpp`, `BikeMeshes.cpp`).

## Reglas

- **Dos superficies en el mismo plano se pisan** (z-fighting): titilan, se ven bordes dentados o se
  mezclan de lejos. Una capa encima de otra (calcomanía, franja, cartel) va **separada** (1-6 cm alcanza
  con el plano cercano en 0.08 m) o en otro plano.
- **Calcomanías espalda con espalda** (un cartel que se lee de los dos lados): cada una de una sola cara.
  `Renderer::DrawMeshTextured(..., twoSided)` dibuja las dos caras por defecto (el modelo del piloto lo
  necesita); para calcomanías, `twoSided = false`.
- **Módulos repetidos a lo largo de una curva** (garajes, tribunas, techos): tocándose justo, los vecinos
  quedan girados apenas y se solapan en una cuña con caras casi coplanares. Hay que dejar un hueco de
  unos centímetros o alternar la altura/profundidad entre vecinos.
- Las sombras: mapa de 2048² que cubre 64 m (3 cm por texel), con el foco enganchado a la grilla de
  texels (si no, los bordes titilan al moverse) y corrimiento por la normal de un texel.

## Bitácora

### El cartel "MORRO DO GRAU" titilaba y se veía espejado (favela)
- **Pasaba**: rayas sobre el fondo y, según el lado, el texto espejado mezclado ("MARG DO GRAU").
- **Por qué**: los banderines que cruzan la calle son dos calcomanías en el mismo punto, una mirando a
  cada lado. `DrawMeshTextured` apaga el descarte de caras traseras, así que desde cada lado se dibujaban
  las dos y se pisaban.
- **Qué se hizo**: las calcomanías de la favela se dibujan de una sola cara (`twoSided = false`).
- **Se comprobó**: captura de la largada (`--map favela --test pose`) antes y después.

### Bordes dentados en las franjas de los boxes y abajo de las tribunas (circuito)
- **Por qué**: los garajes son módulos de 10 m (media longitud 5.0) sobre la curva suave de la recta: los
  vecinos, girados una nada, se solapaban en una cuña con caras casi en el mismo plano y de colores
  distintos. Los techos de las tribunas de las curvas se pasan 0.2 m del módulo y se solapaban con el del
  vecino a la misma altura (desde la pista se ve la cara de abajo).
- **Qué se hizo**: las cajas de cada garaje son 3 cm más cortas de cada lado; los techos de tribunas
  vecinas alternan 4 cm de altura.
