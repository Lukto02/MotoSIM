#!/bin/bash
# Regresión: corre cada prueba sin ventana con el ejecutable nuevo y con el de referencia (el último
# paquete) y compara la telemetría. La física de la Motocross 450 tiene que dar idéntica.
#
# Uso: tools/regresion.sh <exe nuevo> <exe de referencia> "args de la prueba 1" "args de la prueba 2" ...
#   p. ej. tools/regresion.sh build-msvc/motocross.exe dist/MotoSim-v0.2.4/MotoSim.exe "--flat --test brakeslide --time 8"
# Cada exe corre desde su carpeta (ahí están su tuning.ini y su mods/). El juego nuevo agrega " scr=..."
# al final de cada línea de telemetría (velocidad de raspado de la cola); se saca antes de comparar.
NEW=$(realpath "$1"); OLD=$(realpath "$2"); shift 2
run() { local exe=$1; shift; (cd "$(dirname "$exe")" && "./$(basename "$exe")" "$@" 2>&1); }
status=0
for args in "$@"; do
  a=$(run "$OLD" --headless $args --telemetry --telemetry-dt 0.5 | grep -E "^t=" | sed -E 's/ scr= *[-0-9.]+$//' | md5sum)
  b=$(run "$NEW" --headless $args --telemetry --telemetry-dt 0.5 | grep -E "^t=" | sed -E 's/ scr= *[-0-9.]+$//' | md5sum)
  if [ "$a" = "$b" ]; then echo "idéntico: $args"; else echo "DISTINTO: $args"; status=1; fi
done
exit $status
