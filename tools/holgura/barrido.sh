#!/bin/bash
# Barrido de poses del piloto en cada moto, midiendo cuánto se mete en ella (con ventana: sin ventana el modelo
# no se carga). Uso, desde la raíz del proyecto:
#   bash tools/holgura/barrido.sh <exe de armar.py> <carpeta de salida> <etiqueta> [motos...]
# Deja <carpeta>/<etiqueta>_<moto>.csv (una fila por pose) y .txt (lo peor de cada grupo). Tarda unos minutos por
# moto (la de carreras, más). Después: python tools/holgura/tabla.py <carpeta> <etiqueta> [<otra etiqueta>].
EXE="$1"; OUT="$2"; TAG="$3"; shift 3
BIKES="$@"
[ -z "$BIKES" ] && BIKES="base/motocross base/carrera base/trial base/trilheira sandbox/dostiempos"
mkdir -p "$OUT"
for b in $BIKES; do
  n=$(basename $b)
  MOTOSIM_HOLGURA_BARRIDO="$OUT/${TAG}_$n.csv" "$EXE" --flat --bike $b --test pose --size 640 360 --nohud --time 5 > "$OUT/${TAG}_$n.txt" 2>&1
  echo "$n listo"
done
