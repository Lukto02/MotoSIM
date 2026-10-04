cmake_minimum_required(VERSION 3.20)
project(Motocross LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)          # raylib/GLFW are C99-era code; avoid GCC 15+ defaulting to C23
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ---------------------------------------------------------------- raylib
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
FetchContent_Declare(raylib
    URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# ---------------------------------------------------------------- Jolt Physics
set(TARGET_UNIT_TESTS       OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD      OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES          OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER           OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS     OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE       OFF CACHE BOOL "" FORCE)
set(FLOATING_POINT_EXCEPTIONS_ENABLED   OFF CACHE BOOL "" FORCE)
# Jolt 5.6 compila shaders de cómputo por GPU (pelo/tela) si encuentra Vulkan o Metal; el juego no los
# usa y en Mac rompen el build (Vulkan de Homebrew sin dxc/glslang; Metal pide Xcode completo).
set(JPH_USE_VK  OFF CACHE BOOL "" FORCE)
set(JPH_USE_MTL OFF CACHE BOOL "" FORCE)
set(JPH_USE_DX12 OFF CACHE BOOL "" FORCE) # no usamos GPU de Jolt; evita escribir shaders en las dependencias
# Jolt usa por defecto el runtime estático de MSVC (/MT) y raylib + el juego el dinámico (/MD):
# mezclados no linkean. Para repartir el juego conviene el estático en todo (el .exe no pide
# instalar el "Visual C++ Redistributable" en otra PC): -DMOTOSIM_STATIC_RUNTIME=ON. Sin efecto en MinGW.
option(MOTOSIM_STATIC_RUNTIME "Runtime de MSVC estático (/MT) en el juego y las dependencias" OFF)
if(MOTOSIM_STATIC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set(USE_STATIC_MSVC_RUNTIME_LIBRARY ON CACHE BOOL "" FORCE)
else()
    set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
endif()
if(MINGW)
    # GCC en Windows no puede garantizar pila alineada a 32 bytes y genera stores AVX alineados
    # (vmovdqa ymm) sobre temporales de la pila -> crash. Jolt propaga sus flags de CPU a nuestro
    # código, así que en MinGW nos quedamos en SSE4.2 (sobra para un solo cuerpo rígido).
    foreach(opt USE_AVX USE_AVX2 USE_AVX512 USE_F16C USE_FMADD)
        set(${opt} OFF CACHE BOOL "" FORCE)
    endforeach()
endif()
FetchContent_Declare(JoltPhysics
    URL https://github.com/jrouwe/JoltPhysics/archive/refs/tags/v5.6.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR Build)

FetchContent_MakeAvailable(raylib JoltPhysics)

# ---------------------------------------------------------------- game
add_executable(motocross
    src/main.cpp
    src/Game.cpp
    src/Assists.cpp
    src/PhysicsWorld.cpp
    src/Render.cpp
    src/BikeMeshes.cpp
    src/Favela.cpp
    src/Tuning.cpp
    src/Bike.cpp
    src/BikePhysics.cpp
    src/BikePhysics031.cpp
    src/Engine.cpp
    src/EngineSound.cpp
    src/Suspension.cpp
    src/Tire.cpp
    src/Terrain.cpp
    src/TerrainSoil.cpp
    src/TerrainDeformation.cpp
    src/SoilChecks.cpp
    src/Track.cpp
    src/Camera.cpp
    src/Particles.cpp
    src/Rider.cpp
    src/RiderModel.cpp
    src/Net.cpp
    src/Multiplayer.cpp
    src/Json.cpp
    src/Maps.cpp
    src/Props.cpp
    src/BikeStyles.cpp
    src/Circuit.cpp
)
target_link_libraries(motocross PRIVATE raylib Jolt)
if(WIN32)
    target_link_libraries(motocross PRIVATE ws2_32)       # sockets (multijugador LAN)
endif()

if(MINGW)
    # Ejecutable autocontenido (sin DLLs de libstdc++/libgcc/winpthread) y sin símbolos en Release
    target_link_options(motocross PRIVATE -static $<$<CONFIG:Release>:-s>)
endif()
if(MSVC)
    # /utf-8: los fuentes son UTF-8 sin BOM (textos con tildes y escapes \xE2\x86\x92 de flechas); sin esto MSVC los lee con la
    # página de códigos de la PC (en una china o japonesa, un byte de una tilde puede comerse la comilla que cierra el texto).
    target_compile_options(motocross PRIVATE /W3 /permissive- /utf-8)
    target_compile_definitions(motocross PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(motocross PRIVATE -Wall -Wno-missing-field-initializers)
endif()
if(APPLE)
    # Jolt se compila sin RTTI; el linker de Apple pide igual el typeinfo de sus clases base cuando el
    # juego hereda de ellas (RiderCollisionFilter : JPH::GroupFilter). El juego no usa RTTI.
    target_compile_options(motocross PRIVATE -fno-rtti)
    # clang en ARM fusiona a*b+c en una FMA (otro redondeo) y MSVC en x64 no: alcanza para que la favela
    # ponga otras casas (1035 en vez de 1052), y en red Mac-Windows cada uno tendría otras paredes.
    # Jolt queda con lo suyo (-ffp-contract=on en ARM); apagarlo ahí tampoco iguala la telemetría.
    target_compile_options(motocross PRIVATE -ffp-contract=off)
endif()

# tuning.ini and the mods/ folder (maps, bikes and objects as text files; see MODDING.md) live next to
# the executable so they can be edited while the game runs (F5 reloads tuning.ini, saving a map file
# rebuilds that map, Shift+F5 rescans every mod)
add_custom_command(TARGET motocross POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/tuning.ini $<TARGET_FILE_DIR:motocross>/tuning.ini
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/mods $<TARGET_FILE_DIR:motocross>/mods)
