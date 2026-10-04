#!/bin/bash
# Mac: configura (la primera vez descarga raylib y Jolt), compila y ejecuta el juego.
# Los argumentos pasan al juego, p. ej. ./build.sh --map favela
set -e
cd "$(dirname "$0")"
GEN=""
command -v ninja >/dev/null && GEN="-G Ninja"
cmake -S . -B compilaciones/build-mac $GEN -DCMAKE_BUILD_TYPE=Release
cmake --build compilaciones/build-mac --parallel
cd compilaciones/build-mac && exec ./motocross "$@"
