"""Arma y compila una copia del juego con la medición de holgura del piloto (RiderClearance).

Uso (desde la raíz del proyecto): python tools/holgura/armar.py [carpeta]   (por defecto compilaciones/build-msvc-holgura)

Copia src/, CMakeLists.txt, tuning.ini y mods/ a la carpeta, agrega RiderClearance.cpp/.h (de esta carpeta),
engancha la medición en Game::Draw (después de riderModel.Skin()) y compila con VS 2022 Build Tools, con las
fuentes de raylib y Jolt de compilaciones/build/_deps. El proyecto no se toca. El exe queda en <carpeta>/b/motocross.exe y
se corre desde la raíz del proyecto (ahí están el modelo del piloto y mods/). Ver docs/PILOTO.md, "Medir la holgura".
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(ROOT, sys.argv[1] if len(sys.argv) > 1 else os.path.join('compilaciones', 'build-msvc-holgura'))

HOOK = ('        {   // medición de la holgura del piloto (tools/holgura): no va en el juego\n'
        '            static const char* sweep = std::getenv("MOTOSIM_HOLGURA_BARRIDO");\n'
        '            static const bool perFrame = std::getenv("MOTOSIM_HOLGURA") != nullptr;\n'
        '            if (sweep) { RiderClearance::Sweep(bike, riderModel, renderer, sweep); std::fflush(stdout); std::exit(0); }\n'
        '            if (perFrame && bike.RiderOnBike()) RiderClearance::Frame(bike, riderModel, renderer, simTime);\n'
        '        }\n')


def patch(path, old, new):
    raw = open(path, 'rb').read().decode('utf-8')
    crlf = '\r\n' in raw
    s = raw.replace('\r\n', '\n')
    if new in s:
        return
    if s.count(old) != 1:
        raise SystemExit('%s: no encuentro (una sola vez) %r; ¿cambió el archivo?' % (path, old))
    s = s.replace(old, new, 1)
    open(path, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))


def main():
    os.makedirs(OUT, exist_ok=True)
    for d in ('src', 'mods'):
        dst = os.path.join(OUT, d)
        if os.path.exists(dst):
            shutil.rmtree(dst)
        shutil.copytree(os.path.join(ROOT, d), dst)
    for f in ('CMakeLists.txt', 'tuning.ini'):
        shutil.copy2(os.path.join(ROOT, f), os.path.join(OUT, f))
    for f in ('RiderClearance.cpp', 'RiderClearance.h'):
        shutil.copy2(os.path.join(HERE, f), os.path.join(OUT, 'src', f))
    game = os.path.join(OUT, 'src', 'Game.cpp')
    patch(game, '#include "Version.h"\n', '#include "Version.h"\n#include "RiderClearance.h"\n')
    patch(game, '        riderModel.Skin();\n', '        riderModel.Skin();\n' + HOOK)
    patch(os.path.join(OUT, 'CMakeLists.txt'), '    src/RiderModel.cpp\n', '    src/RiderModel.cpp\n    src/RiderClearance.cpp\n')

    deps = os.path.join(ROOT, 'compilaciones', 'build', '_deps')
    vs = r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'
    cmake = vs + r'\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    ninja = vs + r'\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
    b = os.path.join(OUT, 'b')
    lines = ['@echo off', 'call "%s\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul || exit /b 1' % vs]
    if not os.path.exists(os.path.join(b, 'build.ninja')):
        lines.append('"%s" -S "%s" -B "%s" -G Ninja -DCMAKE_MAKE_PROGRAM="%s" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl '
                     '-DCMAKE_CXX_COMPILER=cl -DFETCHCONTENT_SOURCE_DIR_RAYLIB="%s" -DFETCHCONTENT_SOURCE_DIR_JOLTPHYSICS="%s" || exit /b 1'
                     % (cmake, OUT, b, ninja, os.path.join(deps, 'raylib-src'), os.path.join(deps, 'joltphysics-src')))
    lines.append('"%s" --build "%s" || exit /b 1' % (cmake, b))
    bat = os.path.join(OUT, 'compilar_holgura.bat')
    open(bat, 'w').write('\r\n'.join(lines) + '\r\n')
    # Ninja no sigue bien los headers con el MSVC en castellano: se recompila lo que cambió de tamaño.
    for f in os.listdir(os.path.join(OUT, 'src')):
        if f.endswith('.cpp'):
            os.utime(os.path.join(OUT, 'src', f))
    r = subprocess.call(['cmd', '/c', bat])
    if r:
        raise SystemExit('no compiló')
    print('listo:', os.path.join(b, 'motocross.exe'))


if __name__ == '__main__':
    main()
