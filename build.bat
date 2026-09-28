@echo off
rem Configura (la primera vez descarga raylib y Jolt), compila y ejecuta el prototipo.
setlocal
cd /d "%~dp0"
where ninja >nul 2>nul && set GEN=-G Ninja
cmake -S . -B build %GEN% -DCMAKE_BUILD_TYPE=Release || goto :error
cmake --build build --config Release || goto :error
if exist build\motocross.exe (
    pushd build & start "" motocross.exe & popd
) else if exist build\Release\motocross.exe (
    pushd build\Release & start "" motocross.exe & popd
)
exit /b 0
:error
echo.
echo La compilacion fallo.
pause
exit /b 1
