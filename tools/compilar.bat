@echo off
rem Compila el juego con Visual Studio 2022 Build Tools (MSVC + CMake + Ninja).
rem Uso: tools\compilar.bat [carpeta] [release]
rem   carpeta: dónde compilar (default build-msvc). Un nombre suelto va adentro de compilaciones\ (no va
rem            a git); una ruta absoluta, ahí. Cada agente que trabaje en paralelo usa la suya
rem            (build-msvc-motos, build-msvc-mapas...).
rem   release: runtime estático (el .exe para compartir no pide el Visual C++ Redistributable).
rem El exe queda en <carpeta>\motocross.exe, con tuning.ini y mods\ copiados al lado.
rem Ninja no sigue bien los headers con el MSVC en castellano: después de tocar un .h que cambia el
rem tamaño de algo, tocar los .cpp o recompilar todo (ver docs\PRUEBAS.md).
setlocal
set SRC=%~dp0..
for %%I in ("%SRC%") do set SRC=%%~fI
set OUT=%~1
if "%OUT%"=="" set OUT=build-msvc
if not "%OUT:~1,1%"==":" set OUT=%SRC%\compilaciones\%OUT%
set EXTRA=
if /i "%~2"=="release" set EXTRA=-DMOTOSIM_STATIC_RUNTIME=ON
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set CMAKE="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set NINJA=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe
rem Las fuentes de raylib y Jolt ya bajadas (compilaciones\build\_deps) se reusan: no hace falta internet.
set DEPS=
set D=%SRC%\compilaciones\build\_deps
if exist "%D%\raylib-src" set DEPS=-DFETCHCONTENT_SOURCE_DIR_RAYLIB="%D%\raylib-src" -DFETCHCONTENT_SOURCE_DIR_JOLTPHYSICS="%D%\joltphysics-src"
if not exist "%OUT%\build.ninja" (
  %CMAKE% -S "%SRC%" -B "%OUT%" -G Ninja -DCMAKE_MAKE_PROGRAM="%NINJA%" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl %EXTRA% %DEPS% || exit /b 1
)
%CMAKE% --build "%OUT%" || exit /b 1
