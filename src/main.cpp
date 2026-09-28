#include "Game.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// Uso normal: motocross.exe
// Pruebas:    motocross.exe --headless --bot --time 90 --telemetry
//             motocross.exe --test accel --telemetry --screenshot 5 shot.png
int main(int argc, char** argv)
{
    GameOptions opt;
    for (int i = 1; i < argc; ++i) {
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (!std::strcmp(argv[i], "--headless")) opt.headless = true;
        else if (!std::strcmp(argv[i], "--bot")) opt.bot = true;
        else if (!std::strcmp(argv[i], "--debug")) opt.debugVectors = true;
        else if (!std::strcmp(argv[i], "--side")) opt.sideCamera = true;
        else if (!std::strcmp(argv[i], "--ruts")) opt.ruts = true;
        else if (!std::strcmp(argv[i], "--flat")) opt.flat = true;
        else if (!std::strcmp(argv[i], "--norider")) opt.hideRider = true;
        else if (!std::strcmp(argv[i], "--nohud")) opt.hideHud = true;
        else if (!std::strcmp(argv[i], "--respawn-after")) {
            opt.respawnAfter = (float)std::atof(next());
            opt.respawnAfterSet = true;
        }
        else if (!std::strcmp(argv[i], "--wheel-log")) {
            opt.wheelLogFrom = (float)std::atof(next());
            opt.wheelLogTo = (float)std::atof(next());
        }
        else if (!std::strcmp(argv[i], "--genrider")) opt.generatedRider = true;   // el piloto generado (F9)
        else if (!std::strcmp(argv[i], "--host")) opt.host = true;                  // crea una partida LAN
        else if (!std::strcmp(argv[i], "--join")) opt.join = next();               // se une con el código
        else if (!std::strcmp(argv[i], "--name")) opt.name = next();
        else if (!std::strcmp(argv[i], "--port")) opt.port = std::atoi(next());
        else if (!std::strcmp(argv[i], "--net-lag")) opt.netLag = (float)std::atof(next());   // ms (pruebas de red)
        else if (!std::strcmp(argv[i], "--net-jitter")) opt.netJitter = (float)std::atof(next());
        else if (!std::strcmp(argv[i], "--menu")) opt.menuScreen = next();          // pruebas: join, name, lobby, maps
        else if (!std::strcmp(argv[i], "--bike")) opt.bike = next();                // mod/moto
        else if (!std::strcmp(argv[i], "--map")) opt.map = next();                  // motocross | favela | mod/mapa
        else if (!std::strcmp(argv[i], "--sound-test")) {        // WAV de prueba del sonido: [2t|raspado]
            const char* path = next();
            const char* mode = i + 1 < argc && argv[i + 1][0] != '-' ? argv[++i] : "";
            return EngineSound::RenderTest(path, mode) ? 0 : 1;
        }
        else if (!std::strcmp(argv[i], "--drop")) opt.dropHeight = (float)std::atof(next());
        else if (!std::strcmp(argv[i], "--bot-lat")) opt.botLateralAccel = (float)std::atof(next());
        else if (!std::strcmp(argv[i], "--telemetry")) opt.telemetry = true;
        else if (!std::strcmp(argv[i], "--telemetry-dt")) opt.telemetryInterval = (float)std::atof(next());
        else if (!std::strcmp(argv[i], "--test")) opt.test = next();
        else if (!std::strcmp(argv[i], "--time")) opt.quitAfter = (float)std::atof(next());
        else if (!std::strcmp(argv[i], "--spawn")) opt.spawnS = (float)std::atof(next());
        else if (!std::strcmp(argv[i], "--view")) {        // yaw y pitch (grados) y zoom de la cámara, fijos
            opt.viewYaw = (float)std::atof(next());
            opt.viewPitch = (float)std::atof(next());
            opt.viewZoom = (float)std::atof(next());
        }
        else if (!std::strcmp(argv[i], "--fullscreen")) opt.fullscreen = 1;
        else if (!std::strcmp(argv[i], "--windowed")) opt.fullscreen = 0;
        else if (!std::strcmp(argv[i], "--size")) {
            opt.width = std::atoi(next());
            opt.height = std::atoi(next());
            opt.sizeGiven = true;
        } else if (!std::strcmp(argv[i], "--shots-every")) {
            opt.screenshotEvery = (float)std::atof(next());
        } else if (!std::strcmp(argv[i], "--screenshot")) {
            opt.screenshotAt = (float)std::atof(next());
            if (i + 1 < argc && argv[i + 1][0] != '-') opt.screenshotFile = argv[++i];
        } else {
            std::printf("argumento desconocido: %s\n", argv[i]);
        }
    }
    Game game(opt);
    return game.Run();
}
