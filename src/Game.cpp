#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>

#include "Game.h"
#include "BikeStyleDef.h"
#include "Version.h"

#include "MathUtil.h"
#include "PhysicsWorld.h"
#include "rlgl.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

using JPH::Mat44;
using JPH::Quat;
using JPH::Vec3;

namespace {

// Colores de la interfaz.
const Color kAccent = {236, 96, 40, 255};
const Color kPanel = {12, 14, 18, 150};
const Color kText = {236, 238, 240, 255};
const Color kTextDim = {170, 176, 184, 255};

float StickCurve(float v, float deadzone)
{
    float a = std::fabs(v);
    if (a < deadzone) return 0.0f;
    a = (a - deadzone) / (1.0f - deadzone);
    return mu::Sign(v) * std::pow(std::min(a, 1.0f), 1.4f);
}

// Segundos caído hasta reaparecer solo, o < 0 si el jugador reaparece con R (ver GameOptions::respawnAfter).
float AutoRespawnAfter(const GameOptions& opt)
{
    if (opt.respawnAfterSet) return opt.respawnAfter;
    return opt.headless || opt.bot || !opt.test.empty() ? opt.respawnAfter : -1.0f;
}

std::string FormatTime(float t)
{
    if (t <= 0.0f) return "--:--.--";
    int minutes = (int)(t / 60.0f);
    float seconds = t - minutes * 60.0f;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d:%05.2f", minutes, seconds);
    return buf;
}

} // namespace

Game::Game(const GameOptions& options) : opt(options) {}

EngineSound::Character Game::SoundCharacter(const BikeParams& p)
{
    EngineSound::Character c;
    c.cylinders = p.engine.cylinders;
    c.twoStroke = p.engine.twoStroke > 0.5f;
    c.revLimit = p.engine.revLimit;
    return c;
}

Vec3 Game::ExhaustTip() const { return GetBikeStyle(bikeParams.visualStyle).exhaustTip; }
Vec3 Game::ExhaustDir() const { return GetBikeStyle(bikeParams.visualStyle).exhaustDir; }

Game::~Game() = default;

// ======================================================================================= ciclo
int Game::Run()
{
    if (!opt.headless) {
        SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
        InitWindow(opt.width, opt.height, "MotoSim " MOTOSIM_VERSION);
        SetExitKey(KEY_NULL);                                   // Esc abre el menú
        // En monitores chicos la ventana por defecto no entra: se ajusta al 85% del monitor.
        const int monitor = GetCurrentMonitor();
        const int mw = GetMonitorWidth(monitor), mh = GetMonitorHeight(monitor);
        if (mw > 0 && mh > 0 && (opt.width > mw - 40 || opt.height > mh - 80)) {
            SetWindowSize((int)(mw * 0.85f), (int)(mh * 0.85f));
            SetWindowPosition((int)(mw * 0.075f), (int)(mh * 0.06f));
        }
        int hz = GetMonitorRefreshRate(monitor);
        SetTargetFPS(hz > 0 ? hz : 60);
        LoadPrefs();
        const bool testing = opt.sizeGiven || opt.screenshotAt >= 0.0f || !opt.test.empty() || opt.bot;
        const bool want = opt.fullscreen >= 0 ? opt.fullscreen == 1 : (fullscreen && !testing);
        fullscreen = false;
        if (want) SetFullscreen(true, false);
    }
    Init();

    if (opt.headless) {
        const float limit = opt.quitAfter > 0.0f ? opt.quitAfter : 60.0f;
        const auto start = std::chrono::steady_clock::now();
        while (simTime < limit) {
            // En red, el mapa del anfitrión manda (con ventana lo hace Frame, con el aviso de carga).
            if (mp.GetMode() == Multiplayer::Mode::Client && mp.Connected() && !mp.HostMap().empty() && mp.HostMap() != current.id) {
                const int i = mods.FindMap(mp.HostMap());
                if (i >= 0) LoadMap(mods.Maps()[i]);
                else if (missingMap != mp.HostMap()) std::printf("falta el mapa %s\n", (missingMap = mp.HostMap()).c_str());
            }
            BikeInput in = opt.bot ? BotInput() : TestInput();
            pendingShiftUp |= in.shiftUp;
            pendingShiftDown |= in.shiftDown;
            Step(in);
            // En red, a tiempo real (si no, la simulación correría sola y los demás no la verían).
            if (mp.Active())
                while (std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count() < simTime)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    } else {
        int frames = 0, hitches = 0;
        double total = 0.0, worst = 0.0;
        while (!WindowShouldClose() && !quitRequested) {
            const float frameDt = GetFrameTime();
            Frame(frameDt);
            Draw();
            if (frames++ > 30) {                                // los primeros frames cargan cosas
                total += frameDt;
                worst = std::max(worst, (double)frameDt);
                hitches += frameDt > 0.025f ? 1 : 0;
            }
            if (opt.quitAfter > 0.0f && simTime >= opt.quitAfter) break;
        }
        if (opt.telemetry && frames > 31)
            std::printf("frames: %d, medio %.1f ms, peor %.1f ms, %d tirones (> 25 ms)\n", frames - 31, 1000.0 * total / (frames - 31),
                        1000.0 * worst, hitches);
    }

    Shutdown();
    return 0;
}

void Game::Init()
{
    bikeParams.Register(tuning);
    tuning.Add("rut_dig_rate", &deformation.digRate);
    tuning.Add("rut_max_depth", &deformation.maxDepth);

    if (!opt.headless) {
        rlSetClipPlanes(0.08, 900.0);
        renderer.Init();
        particles.Init(3000);
        sound.Init();
        // Modelo del piloto: el primero que se encuentre (carpeta actual, junto al ejecutable o arriba). El 3
        // (con dedos, riggeado con tools/riggear_piloto.py) es el principal; los otros, de respaldo.
        riderPaths.clear();
        for (const char* file : {"Low_Poly_Motorcyclist_3_rigged.gltf", "Low_Poly_Motorcyclist_2_rigged.gltf", "Low_Poly_Motorcycle_Racer_rigged.gltf"})
            for (const std::string& dir : {std::string(), std::string(GetApplicationDirectory()), std::string("../")})
                riderPaths.push_back(dir + file);
        riderModel.Load(riderPaths);

        // Fuentes del sistema si existen (UTF-8 con acentos); si no, la de raylib.
        std::vector<int> cps;
        for (int c = 32; c < 127; ++c) cps.push_back(c);
        for (int c : {0xE1, 0xE9, 0xED, 0xF3, 0xFA, 0xF1, 0xC1, 0xC9, 0xCD, 0xD3, 0xDA, 0xD1, 0xA1, 0xBF, 0xB0, 0xB7, 0x2190, 0x2191, 0x2192, 0x2193,
                      0xE7, 0xC7, 0xE3, 0xC3, 0xF5, 0xD5, 0xEA, 0xCA, 0xE2, 0xC2, 0xF4, 0xD4})   // + portugués: ç ã õ ê â ô
            cps.push_back(c);
        // Windows: Segoe UI + Consolas. Mac: Arial + Monaco (raylib no lee las .ttc de Helvetica y Menlo).
        struct FontSet { const char *bold, *regular, *mono; };
        const FontSet sets[] = {
            {"C:/Windows/Fonts/segoeuib.ttf", "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/consola.ttf"},
            {"/System/Library/Fonts/Supplemental/Arial Bold.ttf", "/System/Library/Fonts/Supplemental/Arial.ttf", "/System/Library/Fonts/Monaco.ttf"},
        };
        for (const FontSet& set : sets) {
            if (!FileExists(set.regular) || !FileExists(set.mono)) continue;
            font = LoadFontEx(set.bold, 64, cps.data(), (int)cps.size());
            if (font.texture.id == 0) font = LoadFontEx(set.regular, 64, cps.data(), (int)cps.size());
            mono = LoadFontEx(set.mono, 36, cps.data(), (int)cps.size());
            SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
            SetTextureFilter(mono.texture, TEXTURE_FILTER_BILINEAR);
            fontsLoaded = true;
            break;
        }
        if (!fontsLoaded) font = mono = GetFontDefault();
    }

    ScanMods();
    if (!opt.bike.empty()) chosenBike = opt.bike;
    showHud = !opt.hideHud;
    useRiderModel = !opt.generatedRider;
    mp.bikeParams = [this](const std::string& id) -> BikeParams* {
        BikeData* d = DataForBike(id);
        return d ? &d->params : nullptr;
    };
    int first = mods.FindMap(opt.map.empty() || opt.map == "mx" ? std::string("base/motocross") : opt.map);
    if (first < 0) {
        if (!opt.map.empty()) {
            std::printf("--map %s: no está. Mapas:", opt.map.c_str());
            for (const MapDef& m : mods.Maps()) std::printf(" %s", m.id.c_str());
            std::printf("\n");
        }
        first = 0;
    }
    LoadMap(mods.Maps()[first]);
    // Ajustes del jugador (preferencias.ini): la caja y el control de tracción, sólo si maneja él (el bot y las
    // pruebas, con los de siempre); el volumen, siempre.
    if (PlayerDriving()) {
        bike.engine.autoShift = prefAutoShift;
        bike.tractionControl = prefTraction;
    }
    ApplySound();
    if (!mods.Errors().empty() && !opt.headless) ShowMessage("Error en un mod: " + mods.Errors().front(), 10.0f);
    if (opt.spawnS >= 0.0f) Respawn(opt.spawnS);
    if (opt.test.rfind("subida", 0) == 0 && opt.spawnS >= 0.0f) {   // aparece dado vuelta: la pista al revés
        const TrackPoint p = track.At(opt.spawnS);
        bike.Reset(*physics, bike.Position(), std::atan2(p.tx, p.tz) + mu::kPi);
    }
    if (opt.test == "profile") {                     // perfil de la pista: altura y pendiente cada 2 m
        for (float s = 0.0f; s < track.Length(); s += 2.0f) {
            const TrackPoint a = track.At(s), b = track.At(s + 2.0f);
            const float ha = terrain.Height(a.x, a.z), hb = terrain.Height(b.x, b.z);
            std::printf("perfil s=%6.1f x=%6.1f z=%6.1f h=%5.1f pendiente=%+5.1f%% pavimento=%.2f (a 5 m del centro %.2f)\n", s, a.x, a.z, ha,
                        (hb - ha) / 2.0f * 100.0f, terrain.PavedAmount(a.x, a.z), terrain.PavedAmount(a.x + a.tz * 5.0f, a.z - a.tx * 5.0f));
        }
        opt.quitAfter = 0.001f;
    }
    if (opt.test == "bikestats") {                   // la ficha de cada moto (la del menú de motos)
        for (const BikeDef& b : mods.Bikes())
            if (BikeData* d = DataForBike(b.id)) {
                const BikeStats& st = d->stats;
                std::printf("moto %-20s %-6s %4.0f hp %4.0f kg %4.0f km/h 0-100 %4.1f s  %.2f g  %4.0f mm  giro %.1f m  (%s)\n", b.id.c_str(),
                            b.style.c_str(), st.hp, st.kg, st.topKmh, st.zeroTo100, st.latG, st.travelMm, st.turnRadius, b.name.c_str());
            }
        opt.quitAfter = 0.001f;
    }
    if (opt.dropHeight > 0.0f) {
        const Vec3 fwd = bike.Rotation() * Vec3::sAxisZ();
        bike.Reset(*physics, bike.Position() + Vec3(0.0f, opt.dropHeight, 0.0f), std::atan2(fwd.GetX(), fwd.GetZ()));
        bike.SetVelocity(*physics, fwd * 12.0f);
        // airrot: aparece girando de nariz a 2 rad/s para probar cuánto cuesta frenar la rotación.
        if (opt.test == "airrot" || opt.test == "airhold") {
            const Vec3 right = fwd.Cross(Vec3::sAxisY());
            bike.SetAngularVelocity(*physics, right * -2.0f);
        }
        // whip / whipL: sale como de un salto (14 m/s, 7 hacia arriba) con las ruedas girando.
        if (opt.test == "whip" || opt.test == "whipL" || opt.test == "whiphold") {
            bike.SetVelocity(*physics, fwd * 14.0f + Vec3(0.0f, 7.0f, 0.0f));
            for (Wheel& w : bike.wheels) w.omega = 14.0f / w.radius;
        }
        // flip / frontflip: sale disparada hacia arriba como de una rampa grande.
        if (opt.test == "flip" || opt.test == "frontflip") {
            bike.SetVelocity(*physics, fwd * 12.0f + Vec3(0.0f, 12.0f, 0.0f));
            flipRef = Vec3(fwd.GetX(), 0.0f, fwd.GetZ()).Normalized();
        }
    }
    // Los tests scripted cambian a mano (salvo accel; los launch la activan solos).
    if (!opt.bot && !opt.test.empty()) bike.engine.autoShift = opt.test == "accel";
    debugVectors = opt.debugVectors;
    if (opt.sideCamera) camera.mode = ChaseCamera::Mode::Side;
    if (opt.viewZoom > 0.0f) camera.FixView(mu::Rad(opt.viewYaw), mu::Rad(opt.viewPitch), opt.viewZoom);
    deformation.physicalRuts = opt.ruts;

    // Multijugador: nombre (el usuario de Windows si no se pasó --name) y, si se pidió, la partida.
    playerName = opt.name;
    if (playerName.empty()) playerName = savedName;             // el que se eligió en el menú (preferencias.ini)
    if (playerName.empty())
        if (const char* user = std::getenv("USERNAME")) playerName = user;
    if (playerName.empty())
        if (const char* user = std::getenv("USER")) playerName = user;
    if (playerName.empty()) playerName = "Piloto";
    if (playerName.size() > 16) playerName.resize(16);
    mp.SetRiderModel(riderPaths, !opt.headless);
    mp.lag = opt.netLag / 1000.0f;
    mp.jitter = opt.netJitter / 1000.0f;
    if (opt.host) StartHost();
    else if (!opt.join.empty()) StartJoin(opt.join);
    else if (!opt.headless && opt.test.empty() && !opt.bot) menu = Menu::Main;   // al abrir: jugar solo o en red
    if (!opt.headless && !opt.menuScreen.empty()) {                              // pruebas: una pantalla del menú
        if (opt.menuScreen == "join") {
            OpenMenu(Menu::Join);
            codeInput = "60N00V";
        } else if (opt.menuScreen == "name") {
            OpenMenu(Menu::Name);
            nameInput = playerName;
        } else if (opt.menuScreen == "lobby") {
            OpenMenu(Menu::Lobby);
        } else if (opt.menuScreen == "maps") {
            OpenMenu(Menu::Maps);
        } else if (opt.menuScreen == "bikes") {
            OpenBikeMenu();
        } else if (opt.menuScreen == "principal") {                              // con --host: el menú de la partida en red
            OpenMenu(Menu::Main);
        } else if (opt.menuScreen == "ajustes") {
            OpenMenu(Menu::Settings);
        } else if (opt.menuScreen == "controles") {
            OpenMenu(Menu::Controls);
        } else if (opt.menuScreen == "no") {                                     // como "Jugar solo": la ayuda del principio
            menu = Menu::None;
            raced = true;
        }
    }
}

void Game::LoadMap(const MapDef& def, bool keepPlace)
{
    current = def;                                // def puede ser de mods.Maps(), que se puede releer
    // La moto: la elegida en el menú (o --bike) si existe; si no, la del mapa.
    bikeId = !chosenBike.empty() && mods.Bike(chosenBike) ? chosenBike : current.bike;
    const BikeDef* bikeDef = mods.Bike(bikeId);
    if (!bikeDef && !bikeId.empty()) std::printf("mapa %s: no está la moto %s (queda la de motocross)\n", current.id.c_str(), bikeId.c_str());
    const int style = bikeDef ? std::max(0, BikeStyleFromName(bikeDef->style)) : 0;
    bikeTuning = bikeDef ? bikeDef->tuning : std::string();
    bikeName = bikeDef ? bikeDef->name : std::string("Motocross 450");
    const Vec3 oldPos = mapLoaded ? bike.Position() : Vec3::sZero();
    keepPlace = keepPlace && mapLoaded;

    // Lo que vive en la física vieja se saca antes de rehacerla.
    if (physics) {
        mp.ResetRemotes(*physics);
        ragdoll.Remove();
    }
    if (!opt.headless && mapLoaded) {
        terrain.Unload();
        favela.Unload();
        circuit.Unload();
        props.Unload();
    }
    physics.reset();
    physics = std::make_unique<PhysicsWorld>();

    // La moto del mapa: sus números salen de tuning.ini y del archivo de la moto (si tiene).
    bikeParams = BikeParams{};
    bikeParams.visualStyle = style;
    LoadTuning(false);

    track.Build(current);
    terrain.Build(track, current, opt.flat);
    terrain.CreateCollision(*physics);
    favela = Favela{};
    if (current.generator == "favela" && !opt.flat) {
        favela.Build(track, terrain);
        favela.CreateCollision(*physics);
    }
    circuit = Circuit{};
    if (current.generator == "circuit" && !opt.flat) {
        circuit.Build(track, terrain, current);
        circuit.CreateCollision(*physics);
    }
    props.Build(opt.flat ? std::vector<MapObject>{} : current.objects, terrain, *physics);
    if (!opt.headless) {
        terrain.CreateMeshes();
        if (favela.Active()) favela.CreateMeshes(font);
        if (circuit.Active()) circuit.CreateMeshes();
        props.CreateMeshes();
        BuildTrackDressing();
        deformation.Unload();
        deformation.Init(terrain);
        particles.Clear();
        renderer.SetLook(current.look);
        renderer.bikeStyle = style;
        grassCenter = {1e9f, 0.0f, 1e9f};
    }
    // Las motos estacionadas de la favela son siempre las del mapa, corra uno con la que corra.
    BikeData* mapBike = DataForBike(current.bike);
    parkedBike.InitVisual(mapBike ? mapBike->params : bikeParams);
    bike.Create(*physics, bikeParams, Vec3(0.0f, 5.0f, 0.0f), 0.0f);
    if (keepPlace) {                              // recarga del mismo mapa: donde estaba, sobre la pista
        trackIndex = -1;
        Respawn(track.Points()[track.Nearest(oldPos.GetX(), oldPos.GetZ())].s);
    } else {
        Respawn(track.startLine - 6.0f);
        lapStart = -1.0f;
        lastLap = bestLap = 0.0f;
        lapCount = 0;
        halfway = false;
        grauTime = lastGrau = bestGrau = grauShow = 0.0f;
        jumpAir = lastJump = bestJump = jumpShow = 0.0f;
    }
    mapLoaded = true;
    mapFileTime = GetFileModTime(current.file.c_str());
    missingMap.clear();
    mp.SetLocalMap(current.id);
    mp.SetLocalBike(bikeDef ? bikeId : std::string());
    if (!opt.headless && !keepPlace)
        ShowMessage(current.hint.empty() ? current.name : current.name + "  ·  " + current.hint, current.hint.empty() ? 3.0f : 6.0f);
}

void Game::ScanMods()
{
    // Carpetas mods/: la de donde se corre, la de al lado del ejecutable y la de arriba (al correr
    // desde la carpeta del build). Si un mapa está en dos, vale el de la primera.
    mods.Scan({"mods", std::string(GetApplicationDirectory()) + "mods", "../mods"});
}

void Game::ReloadMods(bool reloadMap)
{
    ScanMods();
    ReloadBikeData();
    const int i = mods.FindMap(current.id);
    if (reloadMap && i >= 0) LoadMap(mods.Maps()[i], true);
    if (opt.headless) return;
    if (!mods.Errors().empty()) ShowMessage("Error en un mod: " + mods.Errors().front(), 10.0f);
    else if (i < 0) ShowMessage("El mapa " + current.id + " ya no está en mods/ (sigue el que estaba)", 6.0f);
    else ShowMessage(current.name + " recargado  ·  " + std::to_string(mods.Maps().size()) + " mapas en mods/", 3.0f);
}

void Game::WatchMapFile(float dt)
{
    // Se guardó el archivo del mapa (desde un editor): se rearma ahí mismo, con la moto donde estaba.
    if (opt.headless || current.file.empty() || mapLoadIn >= 0) return;
    watchTimer += dt;
    if (watchTimer < 0.75f) return;
    watchTimer = 0.0f;
    const long long t = GetFileModTime(current.file.c_str());
    if (t == 0 || t == mapFileTime) return;
    mapFileTime = t;
    ReloadMods(true);
}

void Game::StartHost()
{
    mp.SetLocalMap(current.id);
    if (mp.Host(playerName, (uint16_t)opt.port)) {
        std::printf("Partida LAN creada. Código: %s\n", mp.InviteCode().c_str());
        for (const std::string& other : mp.OtherCodes()) std::printf("  otra red: %s\n", other.c_str());
        std::fflush(stdout);
        Respawn(track.startLine - 6.0f);
        lapStart = -1.0f;
    } else {
        std::printf("%s\n", mp.Status().c_str());
    }
    ShowMessage(mp.Status(), 4.0f);
    lastStatus = mp.Status();
}

void Game::StartJoin(const std::string& inviteCode)
{
    if (!mp.Join(inviteCode, playerName)) std::printf("%s\n", mp.Status().c_str());
    ShowMessage(mp.Status(), 4.0f);
    lastStatus = mp.Status();
}

void Game::Shutdown()
{
    mp.Leave(*physics);              // avisa a los demás; saca sus motos de la física y sus pilotos de la GPU
    if (opt.telemetry && props.DynamicCount() > 0) {
        float farthest = 0.0f;
        const int moved = props.Moved(*physics, farthest);
        std::printf("objetos sueltos: %d de %d se movieron (el que más, %.1f m)\n", moved, props.DynamicCount(), farthest);
    }
    if (deformation.physicalRuts) {
        int count = 0;
        float deepest = 0.0f;
        terrain.DeformationStats(count, deepest);
        std::printf("surcos: %d muestras deformadas, profundidad max %.1f cm\n", count, deepest * 100.0f);
    }
    if (!opt.headless) {
        terrain.Unload();
        favela.Unload();
        circuit.Unload();
        props.Unload();
        particles.Unload();
        sound.Shutdown();
        deformation.Unload();
        riderModel.Unload();
        renderer.Shutdown();
        if (fontsLoaded) {
            UnloadFont(font);
            UnloadFont(mono);
        }
        CloseWindow();
    }
    ragdoll.Remove();
    physics.reset();
}

void Game::Frame(float frameDt)
{
    const bool alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    if (IsKeyPressed(KEY_F11) || (alt && IsKeyPressed(KEY_ENTER))) {
        SetFullscreen(!fullscreen);
        ShowMessage(fullscreen ? "Pantalla completa  (F11 para volver a ventana)" : "Ventana  (F11 para pantalla completa)");
    }
    // Con el menú abierto las teclas son del menú y la moto queda en ralentí.
    if (menu != Menu::None) {
        HandleMenuKeys();
        camera.SetShift(0.0f);                          // (la cámara del menú es otra: UpdateMenuCamera)
    } else {
        HandleKeys();
        camera.SetShift(0.0f);
    }
    BikeInput in = opt.bot ? BotInput() : (!opt.test.empty() ? TestInput() : ReadPlayerInput(frameDt));
    if (menu != Menu::None && !opt.bot && opt.test.empty()) in = BikeInput{};
    pendingShiftUp |= in.shiftUp;
    pendingShiftDown |= in.shiftDown;

    // Mapa: el que elige el anfitrión manda para los que se unen.
    if (mp.GetMode() == Multiplayer::Mode::Client && mp.Connected() && !mp.HostMap().empty() && mp.HostMap() != current.id && mapLoadIn < 0) {
        const int i = mods.FindMap(mp.HostMap());
        if (i >= 0) {
            mapRequested = i;
            mapLoadIn = 2;
        } else if (missingMap != mp.HostMap()) {
            missingMap = mp.HostMap();
            ShowMessage("El anfitrión corre " + missingMap + " y no lo tenés: copiá ese mod a tu carpeta mods/", 8.0f);
        }
    }
    if (mapLoadIn >= 0 && --mapLoadIn < 0 && mapRequested >= 0 && mapRequested < (int)mods.Maps().size()) LoadMap(mods.Maps()[mapRequested]);
    WatchMapFile(frameDt);

    // Partida LAN: avisos de la conexión, y al entrar, a la largada (cada uno en su lugar).
    if (mp.Status() != lastStatus) {
        lastStatus = mp.Status();
        if (!lastStatus.empty()) ShowMessage(lastStatus, 4.0f);
    }
    if (mp.Connected() && !wasConnected && mp.GetMode() == Multiplayer::Mode::Client) {
        Respawn(track.startLine - 6.0f);
        lapStart = -1.0f;
    }
    wasConnected = mp.Connected();
    if (mp.Active()) slowMotion = paused = false;          // en red el tiempo es de todos

    float dt = std::min(frameDt, 0.1f) * (slowMotion ? 0.25f : 1.0f);
    if (paused) dt = 0.0f;
    accumulator += dt;
    while (accumulator >= kDt) {
        Step(in);
        accumulator -= kDt;
    }
    alpha = accumulator / kDt;

    particles.Update(dt, terrain);
    sound.Update(bike.engine.rpm, bike.throttle, bike.engine.limiter, bike.engine.shiftTimer > 0.0f, SoundCharacter(bikeParams));
    // Cubierta que patina: cuánto desliza el contacto (trabada, patinando o de costado) y con qué carga.
    {
        float skid = 0.0f, paved = 0.0f;
        for (const Wheel& w : bike.wheels) {
            if (!w.grounded || !bike.RiderOnBike()) continue;
            const float slideLong = w.longVel - w.omega * w.radius;
            const float slide = std::sqrt(slideLong * slideLong + w.latVel * w.latVel);
            const float k = mu::Smoothstep(1.2f, 7.0f, slide) * std::min(1.0f, w.normalForce / 1400.0f);
            if (k > skid) {
                skid = k;
                paved = terrain.Streets() ? terrain.TrackMask(w.contactPoint.GetX(), w.contactPoint.GetZ()) : 0.0f;
            }
        }
        EngineSound::SetSkid(skid, paved);
    }
    // Motores de los demás: las voces del sintetizador, más bajas cuanto más lejos.
    int voice = 1;
    const Vec3 ear = ToJph(camera.cam.position);
    for (int id = 0; id < Multiplayer::kMaxPlayers && voice < EngineSound::kVoices; ++id) {
        const Multiplayer::Remote& r = mp.Remotes()[id];
        if (!r.active || !r.hasState) continue;
        const float d = (r.DisplayPos() - ear).Length();
        const float gain = 0.9f / (1.0f + d / 7.0f) * mu::Smoothstep(160.0f, 60.0f, d);
        EngineSound::UpdateVoice(voice++, r.state.rpm, r.state.throttle, (r.state.flags & BikeState::kLimiter) != 0,
                                 (r.state.flags & BikeState::kShifting) != 0, gain,
                                 r.bike.Params() ? SoundCharacter(*r.bike.Params()) : EngineSound::Character{});
    }
    for (; voice < EngineSound::kVoices; ++voice) EngineSound::UpdateVoice(voice, 1800.0f, 0.0f, false, false, 0.0f);

    // Rumbo sacado del eje de las ruedas (no cambia con el cabeceo): en un flip, con la moto de
    // cabeza, la trompa apunta hacia atrás y la cámara se daría vuelta.
    const Quat rot = bike.RenderRotation(alpha);
    const Vec3 heading = Vec3::sAxisY().Cross(rot * -Vec3::sAxisX());
    if (ragdoll.Active()) {
        // Tras una caída la cámara sigue al piloto; sin rumbo propio conserva el que traía.
        camera.Update(dt, ToRl(ragdoll.Position(alpha)), Vector3{0.0f, 0.0f, 0.0f}, ToRl(ragdoll.Velocity()), terrain);
    } else {
        camera.Update(dt, ToRl(bike.RenderPosition(alpha)), ToRl(heading), ToRl(bike.Velocity()), terrain);
    }
    if (favela.Active()) {
        // En las calles angostas la cámara no se mete adentro de las casas: se acerca hasta la pared.
        const Vec3 from = (ragdoll.Active() ? ragdoll.Position(alpha) : bike.RenderPosition(alpha)) + Vec3(0.0f, 0.9f, 0.0f);
        const Vec3 to = ToJph(camera.cam.position);
        const JPH::RRayCast ray(from, to - from);
        JPH::RayCastResult hit;
        struct StaticNotTerrain : JPH::ObjectLayerFilter {
            bool ShouldCollide(JPH::ObjectLayer layer) const override { return layer == Layers::STATIC; }
        } staticOnly;
        const JPH::IgnoreSingleBodyFilter notTerrain(terrain.BodyID());
        if (physics->Query().CastRay(ray, hit, JPH::BroadPhaseLayerFilter(), staticOnly, notTerrain)) {
            const Vec3 p = from + (to - from) * std::max(0.0f, hit.mFraction - 0.35f / std::max((to - from).Length(), 0.1f));
            const Vector3 shift = Vector3Subtract(ToRl(p), camera.cam.position);
            camera.cam.position = ToRl(p);
            camera.cam.target = Vector3Add(camera.cam.target, Vector3Scale(shift, 0.3f));
        }
    }
    UpdateMenuCamera(frameDt);
    messageTime = std::max(0.0f, messageTime - frameDt);
    // La ayuda de teclas del principio: desde que se empieza a correr (se cierra el menú la primera vez), mientras
    // se corre (con el menú abierto o en pausa no se descuenta).
    if (helpTimer < 0.0f && menu == Menu::None && PlayerDriving()) helpTimer = 16.0f;
    if (helpTimer > 0.0f && menu == Menu::None && !paused) helpTimer = std::max(0.0f, helpTimer - frameDt);
    // Corriendo, el cursor se esconde si el mouse queda quieto (vuelve al moverlo, al arrastrar la cámara o en el menú).
    const Vector2 md = GetMouseDelta();
    mouseIdle = menu != Menu::None || md.x != 0.0f || md.y != 0.0f ? 0.0f : mouseIdle + frameDt;
    if ((mouseIdle > 1.5f) != IsCursorHidden()) {
        if (mouseIdle > 1.5f) HideCursor();
        else ShowCursor();
    }
}

// Choque con la moto de otro jugador: ¿se cae el piloto propio? Cada PC decide sólo el suyo, con lo que
// ya recibe (el protocolo no cambia). Ver docs/RED.md, "Choques entre motos".
namespace {
constexpr float kBikeHitFall = 4.0f;     // m/s en la normal: te pegan de costado, de atrás o de arriba
constexpr float kBikeHitFront = 0.75f;   // coseno: el otro está delante (a menos de ~41° de la trompa)
constexpr float kBikeHitEcho = 0.5f;     // s tras un golpe fuerte con el mismo jugador: es el eco (ida y vuelta)
constexpr float kBikeRamFactor = 1.5f;   // embestiste vos con la trompa: te caés pasado 1.5 × crash_impact_speed
struct RemoteHitJudge {
    float closing = 0.0f, fromOther = 0.0f, frontMe = 0.0f, frontOther = 0.0f, limit = 0.0f;
    bool headOn = false, fall = false;
    const char* Kind() const
    {
        if (headOn) return "de frente";
        if (frontMe > kBikeHitFront) return "con la trompa";
        if (frontMe < -kBikeHitFront) return "de atras";
        return "de costado";
    }
};
// frontalLimit: el de un golpe contra algo fijo (crash_impact_speed): de trompa contra la otra moto es
// como un muro que viene.
RemoteHitJudge JudgeRemoteHit(const PhysicsWorld::BikeHit& hit, const Multiplayer::Remote& other, Vec3 myForward, float frontalLimit)
{
    RemoteHitJudge j;
    const Vec3 n = hit.normal;               // de la moto propia hacia la otra
    // Con la velocidad que mandó el otro, no la del cuerpo cinemático: ésa suma las correcciones de la
    // predicción (una foto atrasada lo hace correr para alcanzar la nueva) y daba choques que no hubo.
    const float vo = other.PointVelocity(hit.point).Dot(n);
    j.closing = hit.mineAlongNormal - vo;
    j.fromOther = std::max(0.0f, -vo);
    j.frontMe = n.Dot(myForward);
    j.frontOther = -n.Dot(other.DisplayRot() * Vec3::sAxisZ());
    j.headOn = j.frontMe > kBikeHitFront && j.frontOther > kBikeHitFront;
    const bool withFront = j.frontMe > kBikeHitFront;
    // De costado, de atrás o de arriba te caés pasado el umbral, lo haya traído quien lo haya traído: en un
    // roce fuerte de costado caen los dos (se llevan el mismo golpe), y "quién lo trajo" medido contra el
    // suelo no sirve ahí (con las dos motos yendo para el mismo lado, cada PC se veía como la que lo trajo
    // y no caía ninguna). Con la trompa, de frente (los dos) o si te lo trajo el otro, como contra un muro
    // (crash_impact_speed). Si embestiste vos, la otra moto cede (te frenás más o menos la mitad del
    // cierre), así que recién a kBikeRamFactor veces eso: ~38 km/h de diferencia con la Motocross (el usuario:
    // si el golpe es fuerte, también se tiene que caer el que choca).
    const bool rammed = withFront && !j.headOn && j.fromOther <= 0.45f * j.closing;
    j.limit = !withFront ? kBikeHitFall : (rammed ? frontalLimit * kBikeRamFactor : frontalLimit);
    j.fall = j.limit > 0.0f && j.closing > j.limit;
    return j;
}
} // namespace

void Game::Step(BikeInput in)
{
    in.shiftUp = pendingShiftUp;
    in.shiftDown = pendingShiftDown;
    pendingShiftUp = pendingShiftDown = false;

    // Red: llegan los estados de los demás, sale el propio y sus motos (cinemáticas) se mueven a
    // donde van a estar al final de este paso.
    if (mp.Active()) mp.Step(kDt, LocalState(), playerName, *physics, bikeParams);
    // Pruebas de red: cuando entra otro, todos a la largada juntos (netduel); en netcrash, además, el
    // anfitrión se cae a propósito 1 s después (el invitado, quieto, mira su ragdoll).
    // netchoque: en vez de la largada, cada uno a su lugar para el choque (NetChoqueStep).
    const bool netChoque = opt.test.rfind("netchoque", 0) == 0;
    if (opt.test == "netduel" || opt.test == "netcrash" || netChoque) {
        int others = 0;
        for (int id = 0; id < Multiplayer::kMaxPlayers; ++id) others += mp.Remotes()[id].active ? 1 : 0;
        if (others > duelOthers) {
            if (!netChoque) Respawn(track.startLine - 6.0f);
            lapStart = -1.0f;
            duelStart = simTime;
        }
        duelOthers = others;
        if (netChoque) NetChoqueStep();
        if (opt.test == "netcrash" && mp.GetMode() == Multiplayer::Mode::Host && duelStart >= 0.0f && simTime - duelStart > 1.0f &&
            bike.RiderOnBike()) {
            bike.crashed = true;
            duelStart = -1.0f;
        }
    }

    bike.wheelLog = simTime >= opt.wheelLogFrom && simTime <= opt.wheelLogTo;
    if (bike.wheelLog) std::printf("paso t=%.4f\n", simTime);
    bike.PrePhysics(in, kDt, *physics, terrain);
    const Vec3 velBeforeStep = bike.Velocity(), angVelBeforeStep = bike.AngularVelocity();
    physics->Step(kDt);
    bike.PostPhysics();
    ragdoll.PostPhysics(bike.BodyID());
    simTime += kDt;

    // Choque con otra moto: sacude la cámara y, si te la pusieron fuerte, te caés: de costado, de atrás o
    // de arriba a más de 4 m/s; con la trompa, de frente (los dos) o si te lo trajo el otro, a más de
    // crash_impact_speed, y si embestiste vos, a 1.5 veces eso. Cada jugador resuelve su parte en su PC (JudgeRemoteHit).
    if (mp.Active() && bike.RiderOnBike() && !bike.crashed) {
        const PhysicsWorld::BikeHit hit = physics->LastBikeHit();
        const Multiplayer::Remote* other =
            hit.player >= 0 && hit.player < Multiplayer::kMaxPlayers && mp.Remotes()[hit.player].hasState ? &mp.Remotes()[hit.player] : nullptr;
        const RemoteHitJudge j =
            other ? JudgeRemoteHit(hit, *other, bike.Rotation() * Vec3::sAxisZ(), bikeParams.crashImpactSpeed) : RemoteHitJudge{};
        if (other && j.headOn) headOnAt[hit.player] = simTime;   // aunque apenas se toquen (ver abajo)
        if (hit.closing > 0.3f && other) {
            // El primer golpe fuerte decide: lo que llega en el medio segundo siguiente es el eco.
            const bool echo = simTime - bikeHitAt[hit.player] < kBikeHitEcho;
            const bool fall = j.fall && !echo;
            if (j.closing > kBikeHitFall && !echo) bikeHitAt[hit.player] = simTime;
            if (j.closing > 1.5f) camera.AddShake(std::min(0.45f, j.closing * 0.04f));
            if (opt.telemetry)
                std::printf("CHOQUE t=%.2f con %d: se acercaban a %.2f m/s (el otro traía %.2f), %s (trompa %.2f, la suya %.2f), umbral %.1f%s"
                            " [cuerpo %.2f/%.2f]\n",
                            simTime, hit.player, j.closing, j.fromOther, j.Kind(), j.frontMe, j.frontOther, j.limit,
                            fall ? ": SE CAE" : (j.fall ? ": eco, no cuenta" : ""), hit.closing, hit.fromOther);
            if (j.closing > 0.3f && choqueHitSpeed < 0.0f) choqueHitSpeed = velBeforeStep.Length();   // prueba netchoque
            if (j.closing > choqueClosing) {         // prueba netchoque: el más fuerte
                choqueClosing = j.closing;
                choqueFromOther = j.fromOther;
            }
            if (fall) {
                bike.crashed = true;
                ShowMessage((!other->name.empty() ? other->name : std::string("Otro piloto")) + " te tiró", 3.0f);
            }
        }
    }
    // De frente caen los dos: cada PC ve su versión del choque y la que lo vio primero empuja a su moto
    // para atrás; a la otra le llega retrocediendo y apenas la toca. Si el otro se cae justo después de un
    // contacto trompa contra trompa con nosotros, su PC vio el golpe entero: caemos también.
    for (int id = 0; id < Multiplayer::kMaxPlayers; ++id) {
        const Multiplayer::Remote& r = mp.Remotes()[id];
        const bool down = mp.Active() && r.active && r.hasState && (r.state.flags & BikeState::kCrashed);
        if (down && !remoteDown[id] && simTime - headOnAt[id] < kBikeHitEcho && bike.RiderOnBike() && !bike.crashed) {
            bike.crashed = true;
            if (opt.telemetry) std::printf("CHOQUE t=%.2f con %d: se cayó tras chocarnos de frente: SE CAE\n", simTime, id);
            ShowMessage("Chocaste de frente con " + (!r.name.empty() ? r.name : std::string("otro piloto")), 3.0f);
        }
        remoteDown[id] = down;
    }

    // Golpe fuerte contra algo fijo (un muro, una casa, una barrera, un escalón alto): el piloto sale
    // despedido aunque la moto no vuelque, con la velocidad que traía antes del golpe (la moto se frena
    // contra el muro, él sigue: pasa por encima del manubrio o se estampa).
    bool impactCrash = false;
    if (bike.RiderOnBike() && !bike.crashed) {
        const PhysicsWorld::Impact imp = physics->LastImpact();
        const float limit = std::fabs(imp.normalY) < 0.7f ? bikeParams.crashImpactSpeed : bikeParams.crashImpactVertical;
        if (limit > 0.0f && imp.speed > limit) {
            bike.crashed = true;
            impactCrash = true;
            camera.AddShake(0.6f);
            if (opt.telemetry) std::printf("GOLPE t=%.2f a %.1f m/s (normal y %.2f): el piloto sale despedido\n", simTime, imp.speed, imp.normalY);
        }
    }

    // Caída: el piloto se suelta de la moto y sigue como ragdoll con la velocidad que llevaba.
    if (bike.crashed && bike.RiderOnBike()) {
        {   // Para dónde iba (en un mapa libre reaparece mirando para ahí; sólo se anota).
            const Vec3 v = impactCrash ? velBeforeStep : bike.Velocity(), f = bike.Rotation() * Vec3::sAxisZ();
            const Vec3 flatV(v.GetX(), 0.0f, v.GetZ());
            crashDir = (flatV.Length() > 1.5f ? flatV : Vec3(f.GetX(), 0.0f, f.GetZ())).NormalizedOr(crashDir);
        }
        // Con el modelo, el ragdoll se arma con sus articulaciones (el generado se dibuja igual con esas).
        const RiderPose pose = riderModel.Loaded() ? riderModel.JointPose(bike.RiderPoseLocal()) : bike.RiderPoseLocal();
        ragdoll.Spawn(*physics, terrain, pose, bike.Position(), bike.Rotation(), impactCrash ? velBeforeStep : bike.Velocity(),
                      impactCrash ? angVelBeforeStep : bike.AngularVelocity());
        ragdoll.report = opt.telemetry;
        if (riderModel.Loaded()) riderModel.BindToRagdoll(bike.RiderPoseLocal(), ragdoll);
        bike.DetachRider(*physics);
        camera.AddShake(0.75f);
    }

    UpdateLap();
    // Grau: la delantera en el aire y la trasera en el piso, andando (aunque sea a paso de hombre).
    {
        const bool grau = bike.RiderOnBike() && !bike.crashed && bike.wheels[Bike::REAR].grounded && !bike.wheels[Bike::FRONT].grounded &&
                          bike.pitch > mu::Rad(8.0f) && bike.forwardSpeed > 0.5f;
        if (grau) {
            grauTime += kDt;
        } else if (grauTime > 0.0f) {
            if (grauTime > 1.0f) {
                lastGrau = grauTime;
                grauShow = 2.5f;
                if (grauTime > bestGrau) bestGrau = grauTime;
                if (opt.telemetry) std::printf("GRAU %.2f s\n", grauTime);
            }
            grauTime = 0.0f;
        }
        grauShow = std::max(0.0f, grauShow - kDt);
    }
    // Saltos (mapas libres, sólo se muestran): cuánto voló, al tocar el suelo con el piloto arriba.
    if (bike.airTime > 0.0f && bike.RiderOnBike()) {
        jumpAir = bike.airTime;
    } else if (jumpAir > 0.0f) {
        if (jumpAir > 0.7f && bike.RiderOnBike() && !bike.crashed) {
            lastJump = jumpAir;
            bestJump = std::max(bestJump, jumpAir);
            jumpShow = 3.0f;
        }
        jumpAir = 0.0f;
    }
    jumpShow = std::max(0.0f, jumpShow - kDt);
    if (!opt.headless) EmitEffects();
    if (deformation.physicalRuts) {
        const Wheel& rw = bike.wheels[Bike::REAR];
        if (rw.grounded)
            deformation.DigRut(terrain, rw.contactPoint.GetX(), rw.contactPoint.GetZ(), rw.omega * rw.radius - rw.longVel, std::fabs(rw.latVel));
        deformation.CommitRuts(terrain, *physics, kDt);
    }
    const float respawnAfter = AutoRespawnAfter(opt);
    if (bike.crashed && respawnAfter >= 0.0f && bike.crashedTime > respawnAfter) {
        if (opt.respawnHere && FreeRide()) RespawnHere();   // prueba de lo que hace la R del jugador en un mapa libre
        else RespawnNearest();
    }
    // El bot (pruebas) no sabe salir de reversa: si queda trabado contra una pared, reaparece.
    stuckTime = (opt.bot && bike.speed < 0.6f && !bike.crashed) ? stuckTime + kDt : 0.0f;
    if (stuckTime > 4.0f) {
        RespawnNearest();
        stuckTime = 0.0f;
    }
    if (bike.Position().GetY() < -60.0f) RespawnNearest();

    if (opt.telemetry && simTime - lastTelemetry >= opt.telemetryInterval - 1e-4f) {
        PrintTelemetry();
        lastTelemetry = simTime;
    }
}

// Prueba netchoque-TIPO-KMH[-KMH] (en red, con --flat y sin --bot): el anfitrión recibe el golpe y el
// invitado embiste. TIPO: lado (a 90°, contra el costado), atras, frente, roce (se le cierra 15°) o angN
// (N grados). El primer número es la velocidad del que embiste; el segundo, la del que recibe (si no se
// da: quieto, salvo frente y roce, que van a la misma). Los dos pasarían por el centro del mapa a los 3 s
// de ubicarse; a los 5 s cada uno imprime si se cayó y el choque más fuerte que midió.
namespace {
struct NetChoqueSpec {
    bool ok = false;
    std::string kind;
    float angle = 0.0f;                  // rumbo del que embiste respecto del que recibe (grados)
    float vHit = 0.0f, vRecv = 0.0f;     // m/s
};
NetChoqueSpec ParseNetChoque(const std::string& test)
{
    NetChoqueSpec s;
    const size_t a = test.find('-'), b = a == std::string::npos ? a : test.find('-', a + 1);
    if (b == std::string::npos) return s;
    s.kind = test.substr(a + 1, b - a - 1);
    char* end = nullptr;
    const float kmh = std::strtof(test.c_str() + b + 1, &end);
    const bool recvGiven = end && *end == '-';
    if (s.kind == "lado") s.angle = 90.0f;
    else if (s.kind == "atras") s.angle = 0.0f;
    else if (s.kind == "frente") s.angle = 180.0f;
    else if (s.kind == "roce") s.angle = 15.0f;
    else if (s.kind.rfind("ang", 0) == 0) s.angle = (float)std::atof(s.kind.c_str() + 3);
    else return s;
    const bool together = s.kind == "frente" || s.kind == "roce";
    s.vHit = kmh / 3.6f;
    s.vRecv = (recvGiven ? std::strtof(end + 1, nullptr) : (together ? kmh : 0.0f)) / 3.6f;
    s.ok = true;
    return s;
}
constexpr float kChoqueSetup = 1.0f;     // s desde que se ven hasta ubicarse
constexpr float kChoqueMeet = 3.0f;      // s hasta el cruce
constexpr float kChoqueEnd = 5.0f;       // s hasta el resumen (antes de reaparecer, 3 s después de caerse)
} // namespace

void Game::NetChoqueStep()
{
    const NetChoqueSpec spec = ParseNetChoque(opt.test);
    if (!spec.ok || duelStart < 0.0f) return;
    const bool receives = mp.LocalId() == 0;
    const float tau = simTime - duelStart - kChoqueSetup;
    if (tau < 0.0f) return;
    if (!choquePlaced) {
        // El cruce es en el centro del mapa, lejos de la largada: el salto hasta ahí es de más de 4 m y la
        // otra PC lo toma como una reaparición (no empuja nada en el camino). El que recibe va por el eje
        // Z hacia el origen; si se mueve, el que embiste espera a verlo en su recta y se ubica para llegar
        // al cruce cuando llega él (así no importa cuándo empezó cada PC). Quieto, el cruce es donde esté
        // y con su rumbo: así sirve contra otra versión que no tiene esta prueba (quieta en la largada).
        float meet = kChoqueMeet, baseYaw = 0.0f;
        Vec3 cross = Vec3::sZero();
        if (!receives) {
            const Multiplayer::Remote& r = mp.Remotes()[0];
            if (!r.active || !r.hasState || tau < 0.5f) return;   // medio segundo: que llegue dónde se ubicó
            const Vec3 p = r.DisplayPos();
            if (spec.vRecv > 0.0f) {
                meet = -p.GetZ() / spec.vRecv;
                if (std::fabs(p.GetX()) > 2.0f || meet < 1.5f || meet > kChoqueMeet + 0.5f) return;
            } else {
                const Vec3 f = r.DisplayRot() * Vec3::sAxisZ();
                baseYaw = std::atan2(f.GetX(), f.GetZ());
                cross = Vec3(p.GetX(), 0.0f, p.GetZ());
            }
        }
        const float yaw = receives ? 0.0f : baseYaw + mu::Rad(spec.angle), v = receives ? spec.vRecv : spec.vHit;
        choqueDir = Vec3(std::sin(yaw), 0.0f, std::cos(yaw));
        choqueFrom = cross - choqueDir * (v * meet);
        choqueMeetAt = tau + meet;
        ragdoll.Remove();
        bike.Reset(*physics, choqueFrom + Vec3(0.0f, terrain.Height(choqueFrom.GetX(), choqueFrom.GetZ()) + 0.88f, 0.0f), yaw);
        bike.SetVelocity(*physics, choqueDir * v);
        for (Wheel& w : bike.wheels) w.omega = v / w.radius;
        // La marcha en la que va a esa velocidad (a menos de 3/4 del corte).
        const float omega = bike.wheels[Bike::REAR].omega;
        while (bike.engine.gear < 5 && omega * bike.engine.TotalRatio() * 9.5493f > 0.75f * bikeParams.engine.revLimit) ++bike.engine.gear;
        camera.Reset(ToRl(bike.Position()), ToRl(choqueDir));
        choquePlaced = true;
        choqueCrashAt = choqueHitSpeed = -1.0f;
        choqueClosing = choqueFromOther = 0.0f;
        choqueOtherFell = false;
        std::printf("NETCHOQUE %s: %s a %.0f km/h, rumbo %.0f, desde (%.1f, %.1f)\n", receives ? "recibe" : "embiste", spec.kind.c_str(), v * 3.6f,
                    receives ? 0.0f : spec.angle, choqueFrom.GetX(), choqueFrom.GetZ());
        return;
    }
    if (tau > kChoqueEnd) {
        if (!choqueReported) {
            choqueReported = true;
            std::printf("NETCHOQUE %s %s %.0f km/h contra %.0f: %s", receives ? "recibe" : "embiste", spec.kind.c_str(),
                        (receives ? spec.vRecv : spec.vHit) * 3.6f, (receives ? spec.vHit : spec.vRecv) * 3.6f, choqueCrashAt >= 0.0f ? "SE CAYO" : "no se cayo");
            if (choqueCrashAt >= 0.0f) std::printf(" a los %.2f s", choqueCrashAt);
            std::printf("; choque mas fuerte: cierre %.2f m/s (el otro traia %.2f), iba a %.1f km/h al tocarse; el otro %s\n", choqueClosing,
                        choqueFromOther, std::max(0.0f, choqueHitSpeed) * 3.6f, choqueOtherFell ? "se cayo" : "no se cayo");
        }
        return;
    }
    if (bike.crashed && choqueCrashAt < 0.0f) choqueCrashAt = tau;
    for (int id = 0; id < Multiplayer::kMaxPlayers; ++id) {
        const Multiplayer::Remote& r = mp.Remotes()[id];
        if (r.active && r.hasState && (r.state.flags & BikeState::kCrashed)) choqueOtherFell = true;
    }
}

BikeInput Game::NetChoqueInput()
{
    BikeInput in;
    const NetChoqueSpec spec = ParseNetChoque(opt.test);
    if (!spec.ok || !choquePlaced || bike.crashed) return in;
    bike.engine.autoShift = true;
    const bool receives = mp.LocalId() == 0;
    const float vt = receives ? spec.vRecv : spec.vHit, v = bike.forwardSpeed;
    // El que embiste suelta el gas al tocar al otro (o pasado el cruce); el que recibe sigue igual.
    if (vt <= 0.0f || (!receives && (choqueClosing > 0.3f || simTime - duelStart - kChoqueSetup > choqueMeetAt + 0.5f))) return in;
    // Sigue su recta (como el bot: apunta a un punto por delante) a velocidad constante.
    const Vec3 pos = bike.Position(), fwd = bike.Rotation() * Vec3::sAxisZ();
    const Vec3 fwdFlat = Vec3(fwd.GetX(), 0.0f, fwd.GetZ()).NormalizedOr(choqueDir), rightFlat = fwdFlat.Cross(Vec3::sAxisY());
    const Vec3 target = choqueFrom + choqueDir * ((pos - choqueFrom).Dot(choqueDir) + 4.0f + v * 0.55f);
    const Vec3 to(target.GetX() - pos.GetX(), 0.0f, target.GetZ() - pos.GetZ());
    in.steer = mu::Clamp(std::atan2(to.Dot(rightFlat), to.Dot(fwdFlat)) * 2.2f, -1.0f, 1.0f);
    in.throttle = mu::Clamp((vt - v) * 0.4f + 0.25f, 0.0f, 1.0f);
    in.frontBrake = v > vt + 1.0f ? 0.3f : 0.0f;
    in.lean = 0.3f;
    return in;
}

// ======================================================================================= input
void Game::HandleKeys()
{
    const bool pad = IsGamepadAvailable(0);
    // Esc o Start: el menú (antes Start pausaba y con el joystick solo no había cómo abrirlo).
    if (IsKeyPressed(KEY_ESCAPE) || (pad && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT))) {
        OpenMenu(Menu::Main);
        return;
    }
    if (IsKeyPressed(KEY_F10) && mp.GetMode() == Multiplayer::Mode::Host) {
        SetClipboardText(mp.InviteCode().c_str());
        ShowMessage("Código copiado: " + mp.InviteCode());
    }
    if (mp.Active() && (IsKeyPressed(KEY_F4) || IsKeyPressed(KEY_P))) ShowMessage("En red no se puede pausar ni usar cámara lenta");
    if (IsKeyPressed(KEY_F1)) debugVectors = !debugVectors;
    if (IsKeyPressed(KEY_F2)) showHud = !showHud;
    if (IsKeyPressed(KEY_F3) || (pad && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_LEFT))) {
        bike.engine.autoShift = !bike.engine.autoShift;
        if (PlayerDriving()) {                       // lo mismo que en Ajustes: queda guardado
            prefAutoShift = bike.engine.autoShift;
            SavePrefs();
        }
        ShowMessage(bike.engine.autoShift ? "Caja automática" : "Caja manual  (Q y E para los cambios)");
    }
    if (IsKeyPressed(KEY_F4) && !mp.Active()) {
        slowMotion = !slowMotion;
        ShowMessage(slowMotion ? "Cámara lenta x0.25" : "Velocidad normal");
    }
    if (IsKeyPressed(KEY_F5)) {
        if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) ReloadMods(true);   // mapas nuevos o cambiados
        else LoadTuning(true);
    }
    if (IsKeyPressed(KEY_M)) {
        muted = !muted;
        ApplySound();
        SavePrefs();
        ShowMessage(muted ? "Sonido apagado  (M lo prende)" : "Sonido prendido");
    }
    if (IsKeyPressed(KEY_H)) helpPinned = HelpVisible() ? 0 : 1;          // la ayuda de teclas
    if (IsKeyPressed(KEY_T)) {                                            // datos técnicos
        techData = !techData;
        SavePrefs();
    }
    if (IsKeyPressed(KEY_C) || (pad && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_THUMB))) camera.ToggleMode();
    // Cámara orbital: arrastrar con el mouse (cualquier botón) gira alrededor de la moto; la rueda acerca.
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        const Vector2 d = GetMouseDelta();
        camera.Orbit(-d.x * 0.006f, d.y * 0.006f);
    }
    if (const float wheel = GetMouseWheelMove(); wheel != 0.0f) camera.Zoom(wheel);
    if (IsKeyPressed(KEY_F8)) {
        deformation.physicalRuts = !deformation.physicalRuts;
        ShowMessage(deformation.physicalRuts ? "Surcos físicos prendidos" : "Surcos físicos apagados");
    }
    if (IsKeyPressed(KEY_F9) && riderModel.Loaded()) {
        useRiderModel = !useRiderModel;
        ShowMessage(useRiderModel ? "Piloto: modelo" : "Piloto: generado");
    }
    if (IsKeyPressed(KEY_F7)) {
        postEffects = !postEffects;
        ShowMessage(postEffects ? "Efectos de imagen prendidos" : "Efectos de imagen apagados  (F7 los prende)");
    }
    if (IsKeyPressed(KEY_F6)) {
        bike.tractionControl = !bike.tractionControl;
        if (PlayerDriving()) {
            prefTraction = bike.tractionControl;
            SavePrefs();
        }
        ShowMessage(bike.tractionControl ? "Control de tracción prendido" : "Control de tracción apagado");
    }
    if (!mp.Active() && IsKeyPressed(KEY_P)) paused = !paused;
    // Reaparecer: en la pista, en la pista; en un mapa libre, donde quedaste (RespawnHere).
    if (IsKeyPressed(KEY_R) || (pad && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_UP))) {
        if (FreeRide() && PlayerDriving()) RespawnHere();
        else RespawnNearest();
    }
    if (IsKeyPressed(KEY_BACKSPACE) || (pad && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_LEFT))) {
        Respawn(track.startLine - 6.0f);
        lapStart = -1.0f;
        // Empezar de nuevo: los objetos sueltos (tambores, cajas, bolos, pelotas) vuelven a su lugar.
        // Antes quedaban donde cayeron hasta recargar el mapa. En red cada PC mueve los suyos.
        props.Reset(*physics);
    }

    // Ajuste en vivo del centro de masa (debug)
    bool comChanged = false;
    if (IsKeyPressed(KEY_RIGHT_BRACKET)) { bikeParams.comForward += 0.02f; comChanged = true; }
    if (IsKeyPressed(KEY_LEFT_BRACKET)) { bikeParams.comForward -= 0.02f; comChanged = true; }
    if (IsKeyPressed(KEY_EQUAL)) { bikeParams.comHeight += 0.02f; comChanged = true; }
    if (IsKeyPressed(KEY_MINUS)) { bikeParams.comHeight -= 0.02f; comChanged = true; }
    if (IsKeyPressed(KEY_ZERO)) { bikeParams.comHeight = bikeParams.comForward = 0.0f; comChanged = true; }
    if (comChanged) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "Centro de masa  ·  altura %+.2f m  ·  adelante %+.2f m", bikeParams.comHeight, bikeParams.comForward);
        ShowMessage(buf);
    }
}

BikeInput Game::ReadPlayerInput(float dt)
{
    BikeInput in;
    auto ramp = [dt](float& v, float target, float riseRate, float fallRate) {
        v = mu::MoveTowards(v, target, (std::fabs(target) > std::fabs(v) ? riseRate : fallRate) * dt);
    };
    // Teclado: los ejes digitales se suavizan con rampas.
    ramp(kbThrottle, IsKeyDown(KEY_W) ? 1.0f : 0.0f, 6.0f, 10.0f);
    ramp(kbFront, IsKeyDown(KEY_S) ? 1.0f : 0.0f, 5.0f, 12.0f);
    ramp(kbRear, IsKeyDown(KEY_SPACE) ? 1.0f : 0.0f, 8.0f, 12.0f);
    ramp(kbSteer, (IsKeyDown(KEY_D) ? 1.0f : 0.0f) - (IsKeyDown(KEY_A) ? 1.0f : 0.0f), 3.5f, 6.0f);
    ramp(kbLean, (IsKeyDown(KEY_UP) ? 1.0f : 0.0f) - (IsKeyDown(KEY_DOWN) ? 1.0f : 0.0f), 5.0f, 6.0f);
    ramp(kbSide, (IsKeyDown(KEY_RIGHT) ? 1.0f : 0.0f) - (IsKeyDown(KEY_LEFT) ? 1.0f : 0.0f), 5.0f, 6.0f);
    in.throttle = kbThrottle;
    in.frontBrake = kbFront;
    in.rearBrake = std::max(kbRear, kbFront * 0.5f);   // S frena con las dos, Space sólo atrás
    in.steer = kbSteer;
    in.lean = kbLean;
    in.side = kbSide;
    in.shiftUp = IsKeyPressed(KEY_E);
    in.shiftDown = IsKeyPressed(KEY_Q);

    if (IsGamepadAvailable(0)) {
        // Los gatillos reposan en -1. Hasta ver ese valor no se confía en ellos.
        float rt = GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_TRIGGER);
        float lt = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_TRIGGER);
        if (rt < -0.5f) rtArmed = true;
        if (lt < -0.5f) ltArmed = true;
        float padThrottle = rtArmed ? mu::Clamp((rt + 1.0f) * 0.5f, 0.0f, 1.0f) : 0.0f;
        float padFront = ltArmed ? mu::Clamp((lt + 1.0f) * 0.5f, 0.0f, 1.0f) : 0.0f;
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_2) && !rtArmed) padThrottle = 1.0f;
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_TRIGGER_2) && !ltArmed) padFront = 1.0f;
        in.throttle = std::max(in.throttle, padThrottle < 0.03f ? 0.0f : padThrottle);
        in.frontBrake = std::max(in.frontBrake, padFront < 0.03f ? 0.0f : padFront);
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) in.rearBrake = 1.0f;

        float sx = StickCurve(GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X), 0.12f);
        float sy = StickCurve(GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y), 0.15f);
        if (sx != 0.0f) in.steer = sx;
        if (sy != 0.0f) in.lean = -sy;
        const float rx = StickCurve(GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_X), 0.15f);   // stick derecho: el cuerpo de costado
        if (rx != 0.0f) in.side = rx;
        if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) in.shiftUp = true;
        if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_TRIGGER_1)) in.shiftDown = true;
    }
    return in;
}

// Piloto automático para pruebas: persigue un punto de la línea central y regula la velocidad
// según la curvatura que viene.
BikeInput Game::BotInput()
{
    BikeInput in;
    const Vec3 pos = bike.Position();
    const Vec3 fwd = bike.Rotation() * Vec3::sAxisZ();
    const Vec3 vel = bike.Velocity();
    const Vec3 fwdFlat = Vec3(fwd.GetX(), 0.0f, fwd.GetZ()).NormalizedOr(Vec3::sAxisZ());
    const Vec3 rightFlat = fwdFlat.Cross(Vec3::sAxisY());
    const float v = Vec3(vel.GetX(), 0.0f, vel.GetZ()).Length();

    botIndex = track.Nearest(pos.GetX(), pos.GetZ(), botIndex, 60);
    const float s = track.Points()[botIndex].s;
    const TrackPoint target = track.At(s + 4.0f + v * 0.55f);
    const Vec3 toTarget(target.x - pos.GetX(), 0.0f, target.z - pos.GetZ());
    const float angle = std::atan2(toTarget.Dot(rightFlat), toTarget.Dot(fwdFlat));
    in.steer = mu::Clamp(angle * 2.2f, -1.0f, 1.0f);

    // Velocidad límite en cada punto por delante (curvas y obstáculos) y velocidad a la que se
    // puede ir ahora para llegar a ese límite frenando suave (a = 4 m/s^2).
    auto featureSpeed = [](FeatureKind kind) {
        switch (kind) {
        case FeatureKind::Double: return 14.2f;
        case FeatureKind::Tabletop: return 12.5f;
        case FeatureKind::StepUp: return 10.0f;
        case FeatureKind::Kicker: return 10.5f;
        case FeatureKind::Whoops: return 12.0f;
        case FeatureKind::Rollers: return 12.0f;
        case FeatureKind::QuebraMola: return 12.0f;
        case FeatureKind::Escadaria: return 4.0f;
        case FeatureKind::Rampa: return 10.0f;
        }
        return 13.0f;
    };
    float vTarget = current.botSpeed;
    const float botLateral = current.botLateral > 0.0f ? current.botLateral : opt.botLateralAccel;
    const float lookAhead = std::max(60.0f, v * v / 8.0f + 20.0f);   // lo que tarda en frenar (motos rápidas)
    for (float d = 0.0f; d < lookAhead; d += 1.0f) {
        const float sd = s + d;
        float limit = std::sqrt(botLateral / std::max(std::fabs(track.At(sd).curvature), 1e-3f));
        for (const TrackFeature& f : track.Features())
            if (track.Wrap(sd - f.start) < f.length) limit = std::min(limit, featureSpeed(f.kind));
        // En las calles (las bajadas del morro) se frena menos (la pendiente empuja): planea con menos desaceleración.
        const float decel = current.botBrake > 0.0f ? current.botBrake : (terrain.Streets() ? 2.5f : 4.0f);
        vTarget = std::min(vTarget, std::sqrt(limit * limit + 2.0f * decel * d));
    }

    if (bike.airTime > 0.05f) {
        in.throttle = 0.0f;                         // en el aire no acelera
        in.steer = 0.0f;                            // ni tira la moto de costado (whip)
    } else if (v < vTarget) {
        in.throttle = mu::Clamp((vTarget - v) * 0.35f + 0.3f, 0.0f, 1.0f);
        // Acostado, un piloto no abre a fondo (con lisas, en la salida de la curva la cola se va): bot_lean_throttle.
        if (current.botLeanThrottle > 0.0f) in.throttle *= 1.0f - current.botLeanThrottle * mu::Smoothstep(mu::Rad(10.0f), mu::Rad(45.0f), std::fabs(bike.roll));
    } else {
        // Frenar menos cuanto más inclinada va (como haría un piloto).
        const float upright = std::cos(bike.roll) * std::cos(bike.roll);
        in.frontBrake = mu::Clamp((v - vTarget) * 0.25f, 0.0f, 0.9f) * upright;
        in.rearBrake = mu::Clamp((v - vTarget) * 0.2f, 0.0f, 0.6f);
    }
    // Piloto: adelante si la delantera se levanta; en el aire busca caer un poco de nariz.
    const Vec3 w = bike.AngularVelocity();
    const float pitchRate = w.Dot(fwd.Cross(bike.Rotation() * Vec3::sAxisY()));
    if (bike.airTime > 0.1f) in.lean = mu::Clamp((bike.pitch - mu::Rad(-8.0f)) * 3.0f + pitchRate * 1.2f, -1.0f, 1.0f);
    else in.lean = (in.throttle > 0.6f && v < 16.0f) || (bike.pitch > 0.12f && !bike.wheels[Bike::FRONT].grounded) ? 1.0f : 0.3f;

    // Prueba scrub (--bot --test scrub): en cada salto tira la moto de costado, un lado y el otro
    // alternados: un poco de dirección subiendo la cara y el stick a fondo los primeros 0.5 s de vuelo.
    // whipair: sólo en el aire.
    if (opt.test == "scrub" || opt.test == "whipair") {
        const std::vector<TrackFeature>& features = track.Features();
        for (size_t k = 0; k < features.size(); ++k) {
            if (track.Wrap(s - features[k].start + 3.0f) > features[k].length + 3.0f) continue;
            const float side = k % 2 ? 1.0f : -1.0f;
            const bool face = track.Profile(s + 1.0f) - track.Profile(s) > 0.2f;
            if (bike.airTime > 0.0f) in.steer = bike.airTime < 0.5f ? side : 0.0f;
            else if (face && opt.test == "scrub") in.steer = side * 0.6f;
        }
    }
    return in;
}

BikeInput Game::TestInput()
{
    if (opt.test.rfind("netchoque", 0) == 0) return NetChoqueInput();
    BikeInput in;
    const float t = simTime;
    if (opt.test == "accel") {                   // aceleración a fondo y frenada fuerte
        if (t > 1.0f && t < 5.5f) {
            in.throttle = 1.0f;
            in.lean = 1.0f;
        } else if (t >= 5.5f && t < 9.0f) {
            in.frontBrake = 1.0f;
            in.rearBrake = 0.6f;
        }
    } else if (opt.test == "pose") {             // quieto: piloto neutro, tirado atrás, tirado adelante, neutro y gas frenado
        in.lean = (t < 2.0f || t >= 6.0f) ? 0.0f : (t < 4.0f ? -1.0f : 1.0f);
        if (t >= 8.0f && t < 9.0f) {                 // gas con los dos frenos: la muñeca derecha gira
            in.throttle = 1.0f;
            in.frontBrake = in.rearBrake = 1.0f;
        }
    } else if (opt.test == "reverse") {          // caja auto: freno parado -> marcha atrás; después gas -> primera
        bike.engine.autoShift = true;
        if (t > 1.0f && t < 5.0f) in.frontBrake = 1.0f;
        else if (t > 6.0f && t < 8.0f) in.throttle = 0.5f;
    } else if (opt.test == "crashloop") {        // gas a fondo siempre: se estrella, reaparece y vuelve (ragdoll)
        bike.engine.autoShift = true;
        in.throttle = 1.0f;
        in.lean = 1.0f;
    } else if (opt.test == "launch" || opt.test == "launchfwd" || opt.test == "launchlate") {
        // Gas a fondo con caja automática: wheelie por marcha con el piloto neutro o adelante.
        // launchlate: arranca neutro (sube la rueda) y a los 2.2 s se tira adelante.
        bike.engine.autoShift = true;
        if (t > 1.0f) {
            in.throttle = 1.0f;
            in.lean = opt.test == "launchfwd" || (opt.test == "launchlate" && t > 2.2f) ? 1.0f : 0.0f;
        }
    } else if (opt.test == "whip" || opt.test == "whipL" || opt.test == "whiphold") {
        // Con --drop: stick al costado en el aire (derecha; whipL izquierda) de 0.15 a 0.75 s y
        // soltar para aterrizar derecho; whiphold lo mantiene hasta el suelo.
        const float dir = opt.test == "whipL" ? -1.0f : 1.0f;
        if (t > 0.15f && (t < 0.75f || opt.test == "whiphold") && bike.airTime > 0.0f) in.steer = dir;
    } else if (opt.test.rfind("frenacurva", 0) == 0) {
        // frenacurvaN[xS][dD][aA][n]: como frenadaN, pero doblando como con la D (la rampa del teclado, hasta S;
        // sin xS, a fondo; S < 0, a la izquierda). Por defecto dobla desde que empieza a frenar y no suelta. dD:
        // la dirección empieza D s después de frenar (D < 0: antes, viniendo inclinado con un poco de gas); aA:
        // la suelta a los A s (un toque, o la curva que se suelta para frenar). n: no frena, para comparar.
        bike.engine.autoShift = true;
        const char* arg = opt.test.c_str() + 10;
        const float vt = (float)std::atof(arg) / 3.6f;
        const char* x = std::strchr(arg, 'x');
        const char* d = std::strchr(arg, 'd');
        const char* a = std::strchr(arg, 'a');
        const float steer = x ? (float)std::atof(x + 1) : 1.0f;
        const float delay = d ? (float)std::atof(d + 1) : 0.0f;
        const float steerFor = a ? (float)std::atof(a + 1) : 1e9f;
        const float brakeAmount = std::strchr(arg, 'n') ? 0.0f : 1.0f;
        if (!testBraking && bike.forwardSpeed >= vt) {
            testBraking = true;
            testT0 = simTime;
        }
        if (!testBraking) {
            in.throttle = 1.0f;
            in.lean = 1.0f;
        } else {
            const float brakeAt = testT0 + std::max(0.0f, -delay), steerAt = brakeAt + delay, now = simTime;
            const bool braking = now >= brakeAt;
            const float steerTarget = now >= steerAt && now < steerAt + steerFor ? steer : 0.0f;
            testBrake = braking ? std::min(brakeAmount, testBrake + 5.0f * kDt) : 0.0f;
            testSteer = mu::MoveTowards(testSteer, steerTarget, (std::fabs(steerTarget) > std::fabs(testSteer) ? 3.5f : 6.0f) * kDt);
            if (!braking) in.throttle = mu::Clamp((vt - bike.forwardSpeed) * 0.4f + 0.3f, 0.0f, 1.0f);
            in.frontBrake = testBrake;
            in.rearBrake = testBrake * 0.5f;
            in.steer = testSteer;
        }
    } else if (opt.test.rfind("frenada", 0) == 0) {
        // frenadaN: a fondo con el piloto adelante hasta N km/h y después frena como la S (la delantera sube
        // en 0.2 s, la trasera a la mitad), sin tocar la dirección. Para medir cuánto se cruza y cuánto frena.
        bike.engine.autoShift = true;
        const float vt = (float)std::atof(opt.test.c_str() + 7) / 3.6f;
        if (!testBraking && bike.forwardSpeed >= vt) testBraking = true;
        if (!testBraking) {
            in.throttle = 1.0f;
            in.lean = 1.0f;
        } else {
            testBrake = std::min(1.0f, testBrake + 5.0f * kDt);
            in.frontBrake = testBrake;
            in.rearBrake = testBrake * 0.5f;
        }
    } else if (opt.test == "airlean") {          // en el aire (con --drop): adelante y atrás a fondo, cada 0.6 s
        in.lean = t < 0.1f ? 0.0f : (((int)((t - 0.1f) / 0.6f)) % 2 == 0 ? 1.0f : -1.0f);
    } else if (opt.test == "airrot") {           // stick atrás a fondo para frenar la rotación
        if (t > 0.1f) in.lean = -1.0f;
    } else if (opt.test == "flip" || opt.test == "frontflip") {
        // Mortal atrás/adelante (con --drop): stick a fondo hasta casi completar la vuelta y
        // después frenar el giro para aterrizar derecha.
        const float dir = opt.test == "flip" ? 1.0f : -1.0f;       // + = nariz arriba
        const Quat q = bike.Rotation();
        const Vec3 fwd = q * Vec3::sAxisZ();
        const float theta = std::atan2(fwd.GetY(), fwd.Dot(flipRef));
        flipAngle += std::remainder(theta - flipPrev, 2.0f * mu::kPi);
        flipPrev = theta;
        const bool grounded = bike.wheels[Bike::FRONT].grounded || bike.wheels[Bike::REAR].grounded;
        if (!grounded && !bike.crashed) {
            const float pitchRate = bike.AngularVelocity().Dot(fwd.Cross(q * Vec3::sAxisY())) * dir;
            if (flipAngle * dir < 2.0f * mu::kPi - 0.5f) in.lean = -dir;
            else in.lean = pitchRate > 0.05f ? dir * 0.1f : 0.0f;
        }
        if (!flipReported && (bike.crashed || (grounded && t > 0.5f))) {
            std::printf("%s: giro %.0f grados, %s a los %.2f s\n", opt.test.c_str(), mu::Deg(flipAngle * dir),
                        bike.crashed ? "CAIDA" : "aterrizo", t);
            flipReported = true;
        }
    } else if (opt.test == "wheelie") {          // 1ª a fondo tirado atrás
        if (t > 1.0f && t < 4.0f) {
            in.throttle = 1.0f;
            in.lean = -1.0f;
        }
    } else if (opt.test == "turn") {             // velocidad constante y giro a la derecha
        float v = bike.forwardSpeed;
        if (t > 1.0f) in.throttle = mu::Clamp((11.0f - v) * 0.4f + 0.25f, 0.0f, 1.0f);
        if (t > 3.5f) in.steer = 0.7f;
        if (t > 3.5f && bike.engine.gear < 2 && bike.engine.rpm > 8000.0f) in.shiftUp = true;
    } else if (opt.test == "sharpturn" || opt.test == "slide") {
        // sharpturn: 25 km/h y stick a fondo (como el teclado). slide: 40 km/h, stick y gas a fondo.
        const bool slide = opt.test == "slide";
        const float vt = slide ? 11.0f : 7.0f;
        float v = bike.forwardSpeed;
        if (t > 1.0f) in.throttle = mu::Clamp((vt - v) * 0.4f + 0.25f, 0.0f, 1.0f);
        in.lean = 0.6f;
        if (t > 3.5f) {
            in.steer = 1.0f;
            if (slide) in.throttle = 1.0f;
        }
        if (bike.engine.gear < 2 && bike.engine.rpm > 8500.0f) in.shiftUp = true;
    } else if (opt.test.rfind("subida", 0) == 0 || opt.test.rfind("bajada", 0) == 0) {
        // subidaN (con --spawn S): la pista al revés desde S, recto a N km/h (p. ej. subir la escadaria);
        // bajadaN: lo mismo en el sentido de la pista (bajarla). Sin tocar la dirección.
        bike.engine.autoShift = true;
        const float vt = (float)std::atof(opt.test.c_str() + 6) / 3.6f;
        if (opt.test[0] == 'b') in.steer = BotInput().steer;   // bajando sigue la línea de la pista
        in.throttle = mu::Clamp((vt - bike.forwardSpeed) * 0.5f + 0.3f, 0.0f, 1.0f);
        in.frontBrake = bike.forwardSpeed > vt + 1.0f ? 0.3f : 0.0f;
    } else if (opt.test.rfind("circle", 0) == 0) {
        // circleN: sostiene N m/s (caja automática) y a los 4 s dirección a fondo a la derecha.
        // Para medir el radio de giro mínimo a cada velocidad (--flat).
        bike.engine.autoShift = true;
        const float vt = (float)std::atof(opt.test.c_str() + 6);
        const float v = bike.forwardSpeed;
        in.throttle = mu::Clamp((vt - v) * 0.4f + 0.25f, 0.0f, 1.0f);
        in.lean = 0.4f;
        if (t > 4.0f) in.steer = 1.0f;
        // circleNx: a los 8 s se cae en plena curva (ragdoll que sale con la pata afuera).
        if (opt.test.back() == 'x' && t > 8.0f && bike.RiderOnBike()) bike.crashed = true;
    } else if (opt.test == "grau" || opt.test == "graucurva" || opt.test == "grau2") {
        // Grau (wheelie largo, estilo calle): a 12 km/h, tirado atrás y gas de golpe (embrague); el
        // gas queda a fondo y el piloto atrás 8 s; después se suelta la flecha y baja. graucurva: a
        // los 4 s dobla a la derecha en una rueda. grau2: arranca en 2ª a 20 km/h.
        bike.engine.autoShift = false;
        const bool second = opt.test == "grau2";
        const float cruise = second ? 5.5f : 3.3f;
        if (t < 2.0f) {
            if (second && bike.engine.gear < 2 && bike.forwardSpeed > 2.5f) in.shiftUp = true;
            in.throttle = mu::Clamp((cruise - bike.forwardSpeed) * 0.5f + 0.15f, 0.0f, 0.6f);
            if (t > 1.5f) in.lean = -1.0f;                       // primero se tira atrás, después el gas de golpe
        } else if (t < 10.0f) {
            in.throttle = 1.0f;
            in.lean = -1.0f;
            if (opt.test == "graucurva" && t > 4.0f) in.steer = 0.5f;
        } else if (t < 12.0f) {
            in.throttle = 0.3f;
        }
    } else if (opt.test.rfind("graulento", 0) == 0) {
        // Grau lento como lo hace un jugador con teclado (W y la flecha suben en rampa, como en
        // ReadPlayerInput): graulento a 8 km/h en 1ª, flecha abajo y enseguida W, las dos 8 s;
        // graulento0 igual pero desde parado; graulentos levanta la rueda y a los 0.8 s suelta W:
        // sigue sólo con la flecha (grau lento sostenido); graulentoc igual y a los 5 s dobla a la
        // derecha en una rueda. Después suelta todo.
        bike.engine.autoShift = true;
        const bool fromStop = opt.test == "graulento0";
        const float start = fromStop ? 1.0f : 2.0f;
        if (t < start - 0.3f) {
            kbThrottle = fromStop ? 0.0f : mu::Clamp((2.2f - bike.forwardSpeed) * 0.5f + 0.15f, 0.0f, 0.6f);
            kbLean = 0.0f;
        } else {
            const bool slow = opt.test == "graulentos" || opt.test == "graulentoc";
            const bool w = t >= start && t < start + 8.0f && !(slow && t > start + 0.8f);
            if (opt.test == "graulentoc" && t > start + 5.0f && t < start + 8.0f) in.steer = 0.5f;
            const bool down = t < start + 8.0f;
            kbThrottle = mu::MoveTowards(kbThrottle, w ? 1.0f : 0.0f, (w ? 6.0f : 10.0f) * kDt);
            kbLean = mu::MoveTowards(kbLean, down ? -1.0f : 0.0f, (down ? 5.0f : 6.0f) * kDt);
        }
        in.throttle = kbThrottle;
        in.lean = kbLean;
    } else if (opt.test == "arranque" || opt.test == "arranquea") {
        // Arranque como un jugador: parado, a los 1 s W a fondo (en rampa, como el teclado) y la caja
        // automática, 7 s; el piloto neutro (arranquea: tirado adelante, flecha arriba).
        bike.engine.autoShift = true;
        kbThrottle = t < 1.0f ? 0.0f : mu::MoveTowards(kbThrottle, t < 8.0f ? 1.0f : 0.0f, 6.0f * kDt);
        in.throttle = kbThrottle;
        if (opt.test == "arranquea") in.lean = 1.0f;
    } else if (opt.test == "colazo" || opt.test == "colazof") {
        // Wheelie pasado hasta apoyar la cola: a 15 km/h, a los 2 s sin ayudas (ni el piloto del grau ni
        // el limitador de wheelie) y tirado atrás; el gas sube la trompa a ~0.8 rad/s hasta 50° y ahí se
        // suelta: pasada del equilibrio, la moto se va de espaldas sola hasta apoyar la cola. A los 6 s
        // se tira adelante. colazof: la trompa sube a ~2 rad/s (golpe más fuerte contra la cola).
        bike.engine.autoShift = true;
        if (t < 2.0f) {
            in.throttle = mu::Clamp((4.2f - bike.forwardSpeed) * 0.5f + 0.2f, 0.0f, 0.7f);
        } else {
            bikeParams.grauAssist = 0.0f;
            bikeParams.wheelieStartDeg = bikeParams.wheelieEndDeg = 0.0f;
            const Vec3 right = (bike.Rotation() * Vec3::sAxisZ()).Cross(bike.Rotation() * Vec3::sAxisY());
            const float rate = bike.AngularVelocity().Dot(right), want = opt.test == "colazof" ? 2.0f : 0.8f;
            in.throttle = bike.pitch < mu::Rad(50.0f) && t < 5.0f ? mu::Clamp((want - rate) * 1.5f, 0.0f, 1.0f) : 0.0f;
            in.lean = t < 6.0f ? -1.0f : 1.0f;
        }
    } else if (opt.test == "cuerpo") {
        // Cuerpo de costado a 29 km/h: derecho con el cuerpo a la derecha (3-5 s), doblando a la
        // derecha con el cuerpo adentro (5-7 s) y con el cuerpo afuera (7-9 s).
        bike.engine.autoShift = true;
        in.throttle = mu::Clamp((8.0f - bike.forwardSpeed) * 0.4f + 0.25f, 0.0f, 1.0f);
        if (t > 3.0f && t < 5.0f) in.side = 1.0f;
        if (t > 5.0f && t < 9.0f) in.steer = 0.4f;
        if (t > 5.0f && t < 7.0f) in.side = 1.0f;
        if (t > 7.0f && t < 9.0f) in.side = -1.0f;
    } else if (opt.test == "brakestraight") {       // freno trasero a fondo en línea recta a 32 km/h
        bike.engine.autoShift = true;
        if (t < 3.0f) in.throttle = mu::Clamp((9.0f - bike.forwardSpeed) * 0.4f + 0.25f, 0.0f, 1.0f);
        else if (t < 5.0f) in.rearBrake = 1.0f;
    } else if (opt.test.rfind("brakeslide", 0) == 0) {
        // Derrape con la trasera bloqueada (como se "cuadra" una horquilla en motocross): 32 km/h
        // derecho; a los 3 s freno trasero a fondo doblando a la derecha; a los 4.3 s suelta el freno y
        // sale acelerando. brakeslide2: entra frenando con los dos y suelta el de adelante al doblar.
        bike.engine.autoShift = true;
        const float v = bike.forwardSpeed;
        if (t < 3.0f) {
            const float entry = opt.test.size() > 10 && std::isdigit((unsigned char)opt.test[10]) ? (float)std::atof(opt.test.c_str() + 10) : 9.0f;
            in.throttle = mu::Clamp((entry - v) * 0.4f + 0.25f, 0.0f, 1.0f);
            in.lean = 0.3f;
            if (opt.test.back() == 'b' && t > 2.6f) {
                in.throttle = 0.0f;
                in.frontBrake = 0.8f;
                in.rearBrake = 1.0f;
            }
        } else if (t < 4.3f) {
            in.rearBrake = 1.0f;
            in.steer = 0.8f;
        } else if (t < 7.0f) {
            in.throttle = 0.6f;
            in.steer = 0.4f;
            in.lean = 0.2f;
        }
    } else if (opt.test == "braketurn") {        // frenada fuerte tipo tecla S (delantero + 50% trasero) doblando un poco
        if (t > 1.0f && t < 3.2f) {
            in.throttle = 1.0f;
            in.lean = 0.8f;
            if (bike.engine.rpm > 9000.0f) in.shiftUp = true;
        } else if (t >= 3.2f && t < 7.0f) {
            in.frontBrake = 1.0f;
            in.rearBrake = 0.5f;
            in.steer = 0.35f;
        }
    } else if (opt.test == "burnout") {          // freno delantero + gas a fondo sin TC, después salir
        bike.tractionControl = false;
        if (t > 1.0f && t < 4.0f) {
            in.throttle = 1.0f;
            in.frontBrake = 1.0f;
            in.lean = 1.0f;
        } else if (t >= 4.0f && t < 7.0f) {
            in.throttle = 0.6f;
            in.lean = 0.5f;
        }
    } else if (opt.test == "brake") {            // frenada sólo delantera a fondo
        if (t > 1.0f && t < 4.5f) {
            in.throttle = 1.0f;
            in.lean = 0.5f;
            if (bike.engine.rpm > 9000.0f) in.shiftUp = true;
        } else if (t >= 4.5f && t < 8.0f) {
            in.frontBrake = 1.0f;
        }
    }
    return in;
}

// ======================================================================================= juego
void Game::Respawn(float s)
{
    const TrackPoint p = track.At(s);
    const float yaw = std::atan2(p.tx, p.tz);
    // En red cada jugador aparece en su carril, para no encimarse con los demás.
    static const float kLane[Multiplayer::kMaxPlayers] = {-1.3f, 1.3f, -3.9f, 3.9f};
    const float lane = mp.Active() ? kLane[mp.LocalId() % Multiplayer::kMaxPlayers] : 0.0f;
    const float x = p.x + p.tz * lane, z = p.z - p.tx * lane;
    // Altura: la máxima del suelo bajo las dos ruedas, para no aparecer enterrado en una pendiente.
    float ground = terrain.Height(x, z);
    for (float d : {-0.8f, 0.9f}) ground = std::max(ground, terrain.Height(x + p.tx * d, z + p.tz * d));
    ragdoll.Remove();
    bike.Reset(*physics, Vec3(x, ground + 0.88f, z), yaw);
    camera.Reset(ToRl(bike.Position()), ToRl(bike.Rotation() * Vec3::sAxisZ()));
    trackIndex = track.Nearest(p.x, p.z);
    trackS = track.Points()[trackIndex].s;
    botIndex = trackIndex;
    wasGrounded[0] = wasGrounded[1] = false;
}

void Game::RespawnNearest()
{
    const Vec3 pos = bike.Position();
    int idx = track.Nearest(pos.GetX(), pos.GetZ());
    // En un cruce el punto más cercano puede ser de la otra calle: se sigue el avance de la vuelta.
    if (trackIndex >= 0) {
        const TrackPoint& t = track.Points()[trackIndex];
        const float dx = t.x - pos.GetX(), dz = t.z - pos.GetZ();
        if (dx * dx + dz * dz < 20.0f * 20.0f) idx = trackIndex;
    }
    float s = track.Points()[idx].s;
    // Retroceder hasta una zona plana (sin saltos ni escalones) para reaparecer.
    auto onFeature = [&](float at) {
        if (std::fabs(track.Profile(at)) > 0.02f) return true;
        for (const TrackFeature& f : track.Features())
            if (f.kind == FeatureKind::Escadaria && track.Wrap(at - f.start + 3.0f) < f.length + 4.0f) return true;
        return false;
    };
    for (int k = 0; k < 160 && onFeature(s); ++k) s -= 0.5f;
    Respawn(s);
}

void Game::RespawnHere()
{
    // Desde donde quedó el piloto (o la moto, si no se cayó), mirando para donde iba. Se busca en anillos cada
    // vez más grandes el suelo más parejo: la pendiente mayor a 1 y a 2.5 m alrededor, menos de ~9° (en la cara
    // de un médano no se puede arrancar), y que sea el terreno y no un objeto. Si en 45 m no hay, a la guía.
    const Vec3 from = ragdoll.Active() ? ragdoll.Position(1.0f) : bike.Position();
    Vec3 dir = crashDir;
    if (!bike.crashed) {
        const Vec3 f = bike.Rotation() * Vec3::sAxisZ();
        dir = Vec3(f.GetX(), 0.0f, f.GetZ()).NormalizedOr(crashDir);
    }
    auto steepness = [&](float x, float z) {
        const float h0 = terrain.Height(x, z);
        float worst = 0.0f;
        for (int a = 0; a < 8; ++a) {
            const float ang = mu::kPi * 0.25f * (float)a, c = std::cos(ang), s = std::sin(ang);
            for (float r : {1.0f, 2.5f}) worst = std::max(worst, std::fabs(terrain.Height(x + c * r, z + s * r) - h0) / r);
        }
        return worst;
    };
    struct StaticOrProp : JPH::ObjectLayerFilter {
        bool ShouldCollide(JPH::ObjectLayer layer) const override { return layer == Layers::STATIC || layer == Layers::PROP; }
    } solid;
    auto onTerrain = [&](float x, float z) {       // lo primero que hay de arriba para abajo es el terreno
        const float h = terrain.Height(x, z);
        const JPH::RRayCast ray(Vec3(x, h + 30.0f, z), Vec3(0.0f, -32.0f, 0.0f));
        JPH::RayCastResult hit;
        return !physics->Query().CastRay(ray, hit, JPH::BroadPhaseLayerFilter(), solid) || hit.mBodyID == terrain.BodyID();
    };
    const float lo = terrain.OriginX() + 8.0f, hi = terrain.OriginX() + terrain.Size() - 8.0f;
    constexpr float kMaxSteep = 0.16f;             // tan 9°
    float bestX = 0.0f, bestZ = 0.0f;
    bool found = false;
    for (float r = 0.0f; r <= 45.0f && !found; r += 1.5f) {
        const int n = r <= 0.0f ? 1 : std::max(8, (int)(2.0f * mu::kPi * r / 1.5f));
        float best = kMaxSteep;
        for (int k = 0; k < n; ++k) {
            const float ang = 2.0f * mu::kPi * (float)k / (float)n;
            const float x = from.GetX() + r * std::cos(ang), z = from.GetZ() + r * std::sin(ang);
            if (x < lo || x > hi || z < lo || z > hi) continue;
            const float st = steepness(x, z);
            if (st >= best || !onTerrain(x, z)) continue;
            best = st;
            bestX = x;
            bestZ = z;
            found = true;
        }
    }
    if (!found) {
        RespawnNearest();
        return;
    }
    // Si para donde iba se termina el mapa (la pared del borde), mirando para el centro.
    const float aheadX = bestX + dir.GetX() * 25.0f, aheadZ = bestZ + dir.GetZ() * 25.0f;
    if (aheadX < lo || aheadX > hi || aheadZ < lo || aheadZ > hi) {
        const float mid = terrain.OriginX() + terrain.Size() * 0.5f;
        dir = Vec3(mid - bestX, 0.0f, mid - bestZ).NormalizedOr(dir);
    }
    const float yaw = std::atan2(dir.GetX(), dir.GetZ());
    float ground = terrain.Height(bestX, bestZ);
    for (float d : {-0.8f, 0.9f}) ground = std::max(ground, terrain.Height(bestX + dir.GetX() * d, bestZ + dir.GetZ() * d));
    if (opt.telemetry) {
        const TrackPoint& g = track.Points()[track.Nearest(bestX, bestZ)];
        std::printf("REAPARECE t=%.2f en (%.1f, %.1f) h=%.2f, a %.1f m de donde quedo (%.1f, %.1f), pendiente %.0f%%, rumbo %.0f, a %.0f m de la guia\n",
                    simTime, bestX, bestZ, ground, std::hypot(bestX - from.GetX(), bestZ - from.GetZ()), from.GetX(), from.GetZ(),
                    100.0f * steepness(bestX, bestZ), mu::Deg(yaw), std::hypot(g.x - bestX, g.z - bestZ));
    }
    ragdoll.Remove();
    bike.Reset(*physics, Vec3(bestX, ground + 0.88f, bestZ), yaw);
    camera.Reset(ToRl(bike.Position()), ToRl(bike.Rotation() * Vec3::sAxisZ()));
    trackIndex = track.Nearest(bestX, bestZ);
    trackS = track.Points()[trackIndex].s;
    botIndex = trackIndex;
    wasGrounded[0] = wasGrounded[1] = false;
}

void Game::UpdateLap()
{
    const Vec3 pos = bike.Position();
    trackIndex = track.Nearest(pos.GetX(), pos.GetZ(), trackIndex, 40);
    const float prev = trackS;
    trackS = track.Points()[trackIndex].s;

    const float L = track.Length();
    const float a = track.Wrap(prev - track.startLine);
    const float b = track.Wrap(trackS - track.startLine);
    if (b > L * 0.4f && b < L * 0.6f) halfway = true;
    if (a > L - 40.0f && b < 40.0f) {             // cruzó la línea hacia adelante
        if (lapStart >= 0.0f && halfway) {
            lastLap = simTime - lapStart;
            bestLap = bestLap <= 0.0f ? lastLap : std::min(bestLap, lastLap);
            ++lapCount;
            if (!FreeRide()) ShowMessage("Vuelta " + std::to_string(lapCount) + "   " + FormatTime(lastLap), 3.0f);   // la guía no es una vuelta
            if (opt.telemetry) std::printf("LAP %d %.2f s\n", lapCount, lastLap);
        }
        lapStart = simTime;
        halfway = false;
    }
}

void Game::LoadTuning(bool reload)
{
    // Directorio actual, junto al ejecutable (p. ej. al lanzar desde Visual Studio) o la raíz.
    auto load = [&](const std::string& file) {
        const std::string candidates[] = {file, std::string(GetApplicationDirectory()) + file, "../" + file};
        for (const std::string& path : candidates)
            if (tuning.Load(path)) {
                baseTuningPath = path;
                return true;
            }
        return false;
    };
    const bool ok = load("tuning.ini");
    // La moto del mapa: su archivo (si tiene) pisa lo que cambia (peso, motor, suspensión...).
    const bool extra = !bikeTuning.empty() && tuning.Load(bikeTuning);
    if (!bikeTuning.empty() && !extra) std::printf("tuning: no se pudo leer %s\n", bikeTuning.c_str());
    if (reload) {
        if (ok) bike.ApplyMassProperties();
        ReloadBikeData();
        const std::string file = bikeTuning.substr(bikeTuning.find_last_of("/\\") + 1);
        ShowMessage(!ok ? std::string("No se encontró tuning.ini") : (extra ? "tuning.ini + " + file + " recargados" : "tuning.ini recargado"));
    }
}

void Game::SetFullscreen(bool on, bool remember)
{
    if (opt.headless) return;
    if (IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE) != on) ToggleBorderlessWindowed();
    fullscreen = on;
    if (remember) SavePrefs();
}

static std::string PrefsPath() { return std::string(GetApplicationDirectory()) + "preferencias.ini"; }

void Game::LoadPrefs()
{
    // "clave = valor" por línea, como tuning.ini (# comenta). Si falta una clave (preferencias de una versión
    // anterior), queda el default.
    if (!FileExists(PrefsPath().c_str())) return;
    char* text = LoadFileText(PrefsPath().c_str());
    if (!text) return;
    std::map<std::string, std::string> values;
    const std::string s(text);
    UnloadFileText(text);
    for (size_t at = 0; at < s.size();) {
        size_t end = s.find('\n', at);
        if (end == std::string::npos) end = s.size();
        const std::string line = s.substr(at, end - at);
        at = end + 1;
        const size_t eq = line.find('=');
        if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
        auto trim = [](std::string v) {
            v.erase(0, v.find_first_not_of(" \t\r"));
            v.erase(v.find_last_not_of(" \t\r") + 1);
            return v;
        };
        values[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    auto flag = [&](const char* key, bool& value) {
        if (auto it = values.find(key); it != values.end() && !it->second.empty()) value = std::atoi(it->second.c_str()) != 0;
    };
    flag("pantalla_completa", fullscreen);
    flag("sacudon_camara", camera.shakeEnabled);
    flag("motion_blur", motionBlur);
    flag("datos_tecnicos", techData);
    flag("caja_automatica", prefAutoShift);
    flag("control_traccion", prefTraction);
    bool sound = !muted;
    flag("sonido", sound);
    muted = !sound;
    if (auto it = values.find("volumen"); it != values.end() && !it->second.empty())   // en %: 0, 10... 100
        volume = std::clamp((std::atoi(it->second.c_str()) + 5) / 10, 0, 10);
    if (auto it = values.find("ayuda_teclas"); it != values.end())
        helpMode = it->second == "siempre" ? HelpMode::Always : (it->second == "nunca" ? HelpMode::Never : HelpMode::Start);
    if (auto it = values.find("moto"); it != values.end()) chosenBike = savedBike = it->second;   // vacío = la de cada mapa
    if (auto it = values.find("nombre"); it != values.end()) savedName = it->second;   // vacío = el usuario de Windows
}

void Game::SavePrefs() const
{
    auto b = [](bool v) { return v ? "1" : "0"; };
    const char* help = helpMode == HelpMode::Always ? "siempre" : (helpMode == HelpMode::Never ? "nunca" : "al_empezar");
    const std::string text = std::string("# Preferencias de MotoSim (las guarda el juego: menú -> Ajustes)\n") +
                             "pantalla_completa = " + b(fullscreen) + "\nsacudon_camara = " + b(camera.shakeEnabled) +
                             "\nmotion_blur = " + b(motionBlur) + "\nsonido = " + b(!muted) + "\nvolumen = " + std::to_string(volume * 10) +
                             "\nayuda_teclas = " + help + "\ndatos_tecnicos = " + b(techData) + "\ncaja_automatica = " + b(prefAutoShift) +
                             "\ncontrol_traccion = " + b(prefTraction) + "\nmoto = " + savedBike + "\nnombre = " + savedName + "\n";
    SaveFileText(PrefsPath().c_str(), const_cast<char*>(text.c_str()));
}

void Game::ApplySound()
{
    sound.SetMuted(muted);
    EngineSound::SetVolume((float)volume / 10.0f);
}

bool Game::HelpVisible() const
{
    if (helpPinned >= 0) return helpPinned == 1;     // H manda hasta que se cambie el ajuste
    if (helpMode == HelpMode::Always) return true;
    if (helpMode == HelpMode::Never) return false;
    return helpTimer > 0.0f || paused;               // al empezar, y en pausa
}

void Game::ShowMessage(const std::string& text, float seconds)
{
    message = text;
    messageTime = seconds;
    if (opt.headless) std::printf("%s\n", text.c_str());
}

void Game::PrintTelemetry() const
{
    const Wheel& f = bike.wheels[Bike::FRONT];
    const Wheel& r = bike.wheels[Bike::REAR];
    const Quat q = bike.Rotation();
    const Vec3 w = bike.AngularVelocity();
    const Vec3 fwd = q * Vec3::sAxisZ(), up = q * Vec3::sAxisY(), right = fwd.Cross(up);
    const Vec3 vel = bike.Velocity();
    // Deriva de la moto: ángulo entre hacia dónde apunta y hacia dónde va (en el plano).
    const float beta = Vec3(vel.GetX(), 0, vel.GetZ()).Length() > 2.0f ? std::atan2(vel.Dot(right), vel.Dot(fwd)) : 0.0f;
    std::printf("t=%5.2f v=%5.1f rpm=%5.0f g=%d thr=%.2f fb=%.2f rb=%.2f pitch=%6.1f roll=%6.1f leanT=%5.1f steer=%5.1f "
                "fC=%.2f rC=%.2f fN=%5.0f rN=%5.0f rSlip=%5.2f fUse=%.2f rUse=%.2f gnd=%d%d s=%6.1f crash=%d "
                "wr=%5.2f wp=%5.2f wy=%5.2f fLat=%5.0f rLat=%5.0f beta=%6.1f h=%5.2f fV=%5.2f rV=%5.2f vy=%6.3f sB=%5.1f sC=%5.1f leg=%5.2f tilt=%5.1f side=%5.2f gB=%5.1f scr=%4.1f\n",
                simTime, bike.speed * 3.6f, bike.engine.rpm, bike.engine.gear, bike.throttle, bike.frontBrake, bike.rearBrake,
                mu::Deg(bike.pitch), mu::Deg(bike.roll), mu::Deg(bike.leanTarget), mu::Deg(bike.steerAngle), f.Compression01(),
                r.Compression01(), f.normalForce, r.normalForce, r.slipRatio, f.gripUsage, r.gripUsage, f.grounded ? 1 : 0,
                r.grounded ? 1 : 0, trackS, bike.crashed ? 1 : 0, w.Dot(fwd), w.Dot(right), w.Dot(up), f.latForce, r.latForce,
                mu::Deg(beta), bike.Position().GetY() - terrain.Height(bike.Position().GetX(), bike.Position().GetZ()),
                f.susp.velocity, r.susp.velocity, vel.GetY(), mu::Deg(bike.steerBase), mu::Deg(bike.steerCaster), bike.legOut, mu::Deg(bike.bodyTilt), bike.riderSide, mu::Deg(bike.grauToBalance),
                physics->LastScrape().active ? physics->LastScrape().speed : -1.0f);
    if (ragdoll.Active()) {                          // piloto suelto: altura de la pelvis sobre el suelo y velocidad
        const Vec3 p = ragdoll.Position(1.0f);
        std::printf("   ragdoll h=%5.2f v=%5.2f conMoto=%d\n", p.GetY() - terrain.Height(p.GetX(), p.GetZ()), ragdoll.Velocity().Length(),
                    ragdoll.PartsCollidingWithBike());
    }
    if (mp.Active()) {                               // red: latencia, fotos recibidas, correcciones, los demás
        std::printf("   net %s id=%d rtt=%.1fms fotos=%d correccion=%.3fm", mp.GetMode() == Multiplayer::Mode::Host ? "anfitrion" : "invitado",
                    mp.LocalId(), mp.PingMs(), mp.statesReceived, mp.correctionAvg);
        for (int id = 0; id < Multiplayer::kMaxPlayers; ++id) {
            const Multiplayer::Remote& r = mp.Remotes()[id];
            if (!r.active || !r.hasState) continue;
            std::printf(" | %d:%s d=%.1fm v=%.1f edad=%.0fms ping=%d moto=%s(%s)%s", id, r.name.c_str(), (r.DisplayPos() - bike.Position()).Length(),
                        r.state.vel.Length(), r.age * 1000.0f, r.ping, r.bikeId.empty() ? "?" : r.bikeId.c_str(),
                        BikeStyleName(r.bike.Params() ? r.bike.Params()->visualStyle : 0), (r.state.flags & BikeState::kRiderOn) ? "" : " CAIDO");
        }
        std::printf("\n");
    }
}

float Game::Rand(float a, float b)
{
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return a + (b - a) * (float)(rngState & 0xffffff) / (float)0xffffff;
}

void Game::EmitBackfire(float s, int owner, const Mat44& bikeWorld, Vec3 bikeVel, float gain)
{
    // El fuego va en el espacio de la moto: un destello en la boca y la llama, que se sigue alimentando
    // desde la salida mientras dura (FeedFlames), así nunca se despega del escape. El humo va en el
    // mundo (queda atrás).
    const Vec3 tip = ExhaustTip(), dir = ExhaustDir();
    particles.Emit(Particles::FLAME, ToRl(tip + dir * 0.03f), Vector3{0.0f, 0.0f, 0.0f}, 0.28f + 0.3f * s, 0.035f, Color{255, 240, 210, 255}, owner);
    Flame& f = flames[owner];
    f.time = std::max(f.time, 0.05f + 0.04f * s);
    f.strength = std::max(f.strength, s);
    const Vec3 worldTip = bikeWorld * (tip + dir * 0.1f), worldDir = bikeWorld.Multiply3x3(dir);
    for (int k = 0; k < 2; ++k)
        particles.Emit(Particles::DUST, ToRl(worldTip), ToRl(bikeVel * 0.7f + worldDir * Rand(0.8f, 1.8f) + Vec3(0.0f, 0.3f, 0.0f)),
                       Rand(0.22f, 0.35f), Rand(0.8f, 1.3f), Color{64, 60, 58, (unsigned char)Rand(70.0f, 110.0f)});
    if (gain > 0.01f) sound.Backfire(s * gain);
    if (owner == 0) {                                  // los propios viajan por red
        ++localPops;
        lastPopStrength = s;
    }
}

void Game::FeedFlames()
{
    // Llama viva: mientras dura el petardeo sale fuego nuevo de la boca en cada paso (en el espacio
    // de la moto, corto y lento): queda pegada a la salida y se achica al apagarse.
    const Vec3 exDir = ExhaustDir(), exTip = ExhaustTip();
    const Vec3 side = exDir.GetNormalizedPerpendicular(), up = exDir.Cross(side);
    for (int owner = 0; owner < 1 + Multiplayer::kMaxPlayers; ++owner) {
        Flame& f = flames[owner];
        if (f.time <= 0.0f) continue;
        const float k = std::min(1.0f, f.time / 0.05f);
        for (int n = 0; n < 2; ++n) {
            const Vec3 v = exDir * (Rand(1.5f, 4.0f) * (0.7f + 0.6f * f.strength)) + side * Rand(-0.5f, 0.5f) + up * Rand(-0.5f, 0.5f);
            particles.Emit(Particles::FLAME, ToRl(exTip + exDir * Rand(0.0f, 0.04f)), ToRl(v),
                           Rand(0.08f, 0.15f) * (0.7f + 0.6f * f.strength) * (0.5f + 0.5f * k), Rand(0.035f, 0.07f),
                           Color{255, (unsigned char)Rand(150.0f, 215.0f), (unsigned char)Rand(50.0f, 110.0f), 255}, owner);
        }
        f.time -= kDt;
        if (f.time <= 0.0f) f.strength = 0.0f;
    }
}

BikeState Game::LocalState() const
{
    BikeState s;
    s.pos = bike.Position();
    s.rot = bike.Rotation();
    s.vel = bike.Velocity();
    s.angVel = bike.AngularVelocity();
    for (int i = 0; i < 2; ++i) {
        const Wheel& w = bike.wheels[i];
        s.susp[i] = w.extension;
        s.wheelAngle[i] = std::fmod(w.angle, 6.2831853f);
        s.wheelOmega[i] = w.omega;
        s.contactX[i] = w.contactPoint.GetX();
        s.contactZ[i] = w.contactPoint.GetZ();
        s.load[i] = w.normalForce;
        s.markSlip[i] = std::max(std::fabs(w.slipRatio), std::fabs(w.latVel) * 0.25f);
        s.spin[i] = w.omega * w.radius - w.longVel;
        s.latVel[i] = w.latVel;
    }
    s.steer = bike.steerAngle;
    s.lean = bike.riderLean;
    s.gripThrottle = bike.gripThrottle;
    s.legOut = bike.legOut;
    s.bodyTilt = bike.bodyTilt;
    s.riderSide = bike.riderSide;
    s.rpm = bike.engine.rpm;
    s.throttle = bike.throttle;
    s.gear = (uint8_t)(bike.engine.reverse ? 0 : bike.engine.gear);
    const bool riderOn = bike.RiderOnBike() || !ragdoll.Active();
    s.flags = (uint8_t)((riderOn ? BikeState::kRiderOn : 0) | (bike.crashed ? BikeState::kCrashed : 0) |
                        (bike.engine.limiter ? BikeState::kLimiter : 0) | (bike.engine.shiftTimer > 0.0f ? BikeState::kShifting : 0) |
                        (bike.wheels[Bike::FRONT].grounded ? BikeState::kFrontGround : 0) | (bike.wheels[Bike::REAR].grounded ? BikeState::kRearGround : 0));
    s.pops = localPops;
    s.popStrength = lastPopStrength;
    s.laps = (uint16_t)lapCount;
    s.bestLap = bestLap;
    if (!riderOn)
        for (int i = 0; i < RiderPart::Count; ++i) s.parts[i] = ragdoll.PartTransform(i, 1.0f);
    return s;
}

void Game::EmitEffects()
{
    // La cola raspando el piso (wheelie pasado): chispas que salen del contacto hacia donde desliza,
    // más cuanto más rápido, y el ruido del raspado. Contra el pavimento, un objeto o una casa, el acero
    // saca muchas chispas; en la tierra salen menos (las piedritas) y levanta polvo.
    {
        const PhysicsWorld::Scrape sc = physics->LastScrape();
        const float amount = sc.active ? mu::Smoothstep(0.3f, 6.0f, sc.speed) : 0.0f;
        const float hard = !sc.onTerrain ? 1.0f : (terrain.Streets() ? terrain.TrackMask(sc.point.GetX(), sc.point.GetZ()) : 0.0f);
        EngineSound::SetScrape(amount * hard);           // en la tierra no suena: sólo en asfalto, cemento y objetos
        sparkAccum += amount * mu::Lerp(90.0f, 260.0f, hard) * kDt;
        while (sparkAccum >= 1.0f) {
            sparkAccum -= 1.0f;
            const Vec3 along = sc.slideDir * (sc.speed * Rand(0.25f, 0.8f));
            const Vector3 vel = {along.GetX() + Rand(-1.5f, 1.5f), Rand(0.8f, 3.2f), along.GetZ() + Rand(-1.5f, 1.5f)};
            const Vector3 at = {sc.point.GetX() + Rand(-0.06f, 0.06f), sc.point.GetY() + 0.02f, sc.point.GetZ() + Rand(-0.06f, 0.06f)};
            particles.Emit(Particles::SPARK, at, vel, Rand(0.04f, 0.08f), Rand(0.3f, 0.7f), WHITE);
        }
        if (amount > 0.0f && hard < 0.5f && Rand(0.0f, 1.0f) < amount * 0.5f) {
            const float dirt = terrain.Dustiness(sc.point.GetX(), sc.point.GetZ());
            particles.Emit(Particles::DUST, {sc.point.GetX(), sc.point.GetY() + 0.05f, sc.point.GetZ()},
                           {sc.slideDir.GetX() * sc.speed * 0.3f, 0.4f, sc.slideDir.GetZ() * sc.speed * 0.3f}, 0.3f, 1.2f,
                           Color{168, 150, 124, (unsigned char)(120 * dirt)});
        }
    }

    // Petardeos: al subir un cambio con el motor arriba (el corte de encendido deja nafta sin quemar
    // que explota en el escape), casi siempre al reducir y a veces al soltar el gas de golpe con
    // muchas vueltas. Cada ráfaga son varias explosiones seguidas.
    if (bike.RiderOnBike() && !bike.crashed) {
        const int gearNow = bike.engine.reverse ? 0 : bike.engine.gear;
        const bool shifted = bike.engine.shiftTimer > 0.0f && lastGear > 0 && gearNow > 0;   // un reset no cuenta
        auto burst = [&](int count, float strength, float delay) {
            float at = simTime + delay;
            for (int k = 0; k < count; ++k) {
                pops.push_back({at, strength * Rand(0.6f, 1.0f)});
                at += Rand(0.035f, 0.09f);
            }
        };
        const float high = mu::Smoothstep(5000.0f, 9500.0f, lastRpm);
        if (shifted && gearNow > lastGear && lastRpm > 5500.0f) {
            burst(2 + (Rand(0.0f, 1.0f) < high ? 2 : (Rand(0.0f, 1.0f) < 0.5f ? 1 : 0)), 0.55f + 0.45f * high, 0.02f);
        } else if (shifted && gearNow < lastGear && lastRpm > 3500.0f && Rand(0.0f, 1.0f) < 0.7f) {
            burst(1 + (int)Rand(0.0f, 2.99f), 0.45f + 0.35f * high, 0.06f);
        } else if (GetBikeStyle(bikeParams.visualStyle).openExhaust && lastGripThrottle > 0.6f && bike.gripThrottle < 0.2f && lastRpm > 5000.0f && Rand(0.0f, 1.0f) < 0.85f) {
            burst(2 + (int)Rand(0.0f, 3.99f), 0.45f + 0.4f * high, 0.03f);     // escape aberto: pipoco
        } else if (lastGripThrottle > 0.7f && bike.gripThrottle < 0.15f && lastRpm > 7500.0f && Rand(0.0f, 1.0f) < 0.5f) {
            burst(1 + (int)Rand(0.0f, 1.99f), 0.4f, 0.03f);
        }
        lastGear = gearNow;
    } else {
        pops.clear();
        flames[0] = Flame{};
    }
    lastRpm = bike.engine.rpm;
    lastGripThrottle = bike.gripThrottle;
    const Mat44 bikeWorld = Mat44::sRotationTranslation(bike.Rotation(), bike.Position());
    for (size_t k = 0; k < pops.size();) {
        if (pops[k].at <= simTime) {
            EmitBackfire(pops[k].strength, 0, bikeWorld, bike.Velocity(), 1.0f);
            pops.erase(pops.begin() + (long)k);
        } else {
            ++k;
        }
    }
    // Los de los demás: su llamarada y su estampido, más bajo cuanto más lejos.
    for (const auto& [id, strength] : mp.TakePops()) {
        const Multiplayer::Remote& r = mp.Remotes()[id];
        if (!r.active || !r.hasState) continue;
        const Vec3 pos = r.DisplayPos();
        const float d = (pos - ToJph(camera.cam.position)).Length();
        EmitBackfire(strength, 1 + id, Mat44::sRotationTranslation(r.DisplayRot(), pos), r.state.vel, 1.0f / (1.0f + d / 8.0f));
    }
    FeedFlames();

    // Ruedas: huellas en la tierra, tierra que salta y polvo. Las de los demás salen de lo que manda
    // cada uno (dónde apoya cada rueda, con cuánta carga y cuánto patina), corrido por lo que avanzó.
    const Vec3 fwd = bike.Rotation() * Vec3::sAxisZ();
    for (int i = 0; i < 2; ++i) {
        const Wheel& w = bike.wheels[i];
        const float slide = std::fabs(w.latVel);
        EmitWheelEffects(0, i, {w.grounded, w.contactPoint, w.contactNormal, w.omega * w.radius - w.longVel, w.latVel, w.normalForce,
                                std::max(std::fabs(w.slipRatio), slide * 0.25f)},
                         fwd, bike.Velocity());
    }
    for (int id = 0; id < Multiplayer::kMaxPlayers; ++id) {
        const Multiplayer::Remote& r = mp.Remotes()[id];
        if (!r.active || !r.hasState) continue;
        const BikeState& s = r.state;
        const Vec3 moved = s.vel * std::min(r.age, 0.1f);
        for (int i = 0; i < 2; ++i) {
            const bool grounded = (s.flags & (i == 0 ? BikeState::kFrontGround : BikeState::kRearGround)) != 0;
            const float x = s.contactX[i] + moved.GetX(), z = s.contactZ[i] + moved.GetZ();
            EmitWheelEffects(1 + id, i, {grounded, Vec3(x, terrain.Height(x, z), z), Vec3::sAxisY(), s.spin[i], s.latVel[i], s.load[i], s.markSlip[i]},
                             r.DisplayRot() * Vec3::sAxisZ(), s.vel);
        }
    }
}

void Game::EmitWheelEffects(int bikeSlot, int wheel, const WheelFx& w, Vec3 fwd, Vec3 bikeVel)
{
    const int slot = 2 * bikeSlot + wheel;
    const bool landed = w.grounded && !wasGrounded[slot];
    wasGrounded[slot] = w.grounded;
    deformation.WheelContact(slot, w.contact.GetX(), w.contact.GetZ(), w.grounded, w.load, w.markSlip);
    if (!w.grounded) return;

    const float slide = std::fabs(w.latVel);
    const Vec3 n = w.normal;
    const Vec3 f = (fwd - n * fwd.Dot(n)).NormalizedOr(fwd);
    const Vec3 side = n.Cross(f);
    const Vec3 cp = w.contact + n * 0.04f;
    const float dirty = terrain.Dustiness(cp.GetX(), cp.GetZ());
    const bool red = terrain.Streets();

    // Tierra (roost): proporcional al patinaje de la trasera y a la velocidad de la rueda,
    // sale hacia atrás y arriba; si derrapa, también hacia afuera.
    if (wheel == Bike::REAR) {
        float& roost = roostAccum[bikeSlot];
        roost += (std::max(0.0f, w.spin - 0.6f) * 55.0f + std::max(0.0f, slide - 1.2f) * 30.0f) * dirty * kDt;
        while (roost >= 1.0f) {
            roost -= 1.0f;
            Vec3 v = -f * (std::max(w.spin, 1.0f) * Rand(0.45f, 0.9f)) + n * Rand(1.2f, 4.2f) + side * Rand(-1.3f, 1.3f) -
                     side * (w.latVel * Rand(0.2f, 0.6f));
            unsigned char r = (unsigned char)Rand(70.0f, 112.0f);
            particles.Emit(Particles::DIRT, ToRl(cp), ToRl(v), Rand(0.05f, 0.12f), Rand(0.8f, 1.4f),
                           Color{(unsigned char)(red ? r * 1.25f : r), (unsigned char)(r * (red ? 0.6f : 0.72f)), (unsigned char)(r * (red ? 0.42f : 0.5f)), 255});
        }
    }

    // Polvo: por velocidad, más si patina o derrapa.
    const float speed = bikeVel.Length();
    dustAccum[slot] += (std::max(0.0f, speed - 3.0f) * 1.1f + std::max(0.0f, w.spin) * 3.0f + slide * 2.5f) * dirty * kDt;
    while (dustAccum[slot] >= 1.0f) {
        dustAccum[slot] -= 1.0f;
        // El polvo sale arrastrado por la rueda: conserva parte de la velocidad de la moto.
        Vec3 v = bikeVel * Rand(0.2f, 0.45f) - f * Rand(0.3f, 1.2f) + n * Rand(0.3f, 0.9f) + side * Rand(-0.6f, 0.6f);
        particles.Emit(Particles::DUST, ToRl(cp + n * 0.25f), ToRl(v), Rand(0.8f, 1.3f), Rand(1.8f, 2.8f),
                       red ? Color{196, 142, 108, (unsigned char)Rand(70.0f, 110.0f)} : Color{186, 166, 136, (unsigned char)Rand(70.0f, 110.0f)});
    }

    // Aterrizaje fuerte: nube de polvo, terrones y (la moto propia) sacudón de cámara.
    if (landed && w.load > 3500.0f) {
        const float impact = std::min(1.0f, (w.load - 3000.0f) / 9000.0f) * (0.3f + 0.7f * dirty);   // en el pavimento, poco polvo
        // 70% de lo que era (0.12 + 0.45·impacto): el usuario lo quería más suave. La vibración va con el cuadrado
        // (queda a la mitad); el zoom hacia adentro y la caída de la cámara, al 70%. Los choques no cambian.
        if (bikeSlot == 0) camera.AddShake(0.084f + 0.315f * impact);
        for (int k = 0; k < (int)(8 + 16 * impact); ++k) {
            Vec3 v = f * Rand(-1.5f, 1.5f) + side * Rand(-2.5f, 2.5f) + n * Rand(0.5f, 2.5f);
            unsigned char r = (unsigned char)Rand(80.0f, 115.0f);
            particles.Emit(Particles::DIRT, ToRl(cp), ToRl(v), Rand(0.04f, 0.08f), Rand(0.5f, 1.0f),
                           Color{r, (unsigned char)(r * 0.72f), (unsigned char)(r * 0.5f), 255});
        }
        for (int k = 0; k < (int)(3 + 6 * impact); ++k) {
            Vec3 v = side * Rand(-1.5f, 1.5f) + f * Rand(-1.0f, 1.0f) + n * Rand(0.3f, 1.0f);
            particles.Emit(Particles::DUST, ToRl(cp + n * 0.2f), ToRl(v), Rand(0.7f, 1.2f), Rand(1.8f, 2.8f),
                           Color{182, 162, 132, (unsigned char)Rand(60.0f, 90.0f)});
        }
    }
}

// ======================================================================================= dibujo
void Game::BuildTrackDressing()
{
    dressing.clear();
    if (!current.markers) return;                   // p. ej. la favela: tiene sus propias cosas (Favela.cpp)
    const float hw = track.HalfWidth();
    const Color white = {238, 238, 234, 255}, red = {214, 52, 40, 255};
    int k = 0;
    for (float s = 0.0f; s < track.Length(); s += 9.0f, ++k) {
        const TrackPoint p = track.At(s);
        for (float side : {-1.0f, 1.0f}) {
            float off = (hw + 1.8f) * side;
            float x = p.x - p.tz * off, z = p.z + p.tx * off;
            float y = terrain.Height(x, z);
            dressing.push_back({Mat44::sTranslation(Vec3(x, y + 0.45f, z)) * Mat44::sScale(Vec3(0.07f, 0.9f, 0.07f)), (k & 1) ? red : white});
        }
    }
    // Pórtico de largada/llegada
    const TrackPoint p = track.At(track.startLine);
    const float yaw = std::atan2(p.tx, p.tz);
    const Quat q = Quat::sRotation(Vec3::sAxisY(), yaw);
    const float ground = terrain.Height(p.x, p.z);
    for (float side : {-1.0f, 1.0f}) {
        float off = (hw + 1.0f) * side;
        float x = p.x - p.tz * off, z = p.z + p.tx * off;
        float y = terrain.Height(x, z);
        dressing.push_back({Mat44::sRotationTranslation(q, Vec3(x, y + 2.35f, z)) * Mat44::sScale(Vec3(0.18f, 4.7f, 0.18f)), {40, 40, 46, 255}});
    }
    dressing.push_back({Mat44::sRotationTranslation(q, Vec3(p.x, ground + 4.45f, p.z)) * Mat44::sScale(Vec3(2.0f * hw + 2.2f, 0.6f, 0.08f)), kAccent});
    const int squares = 12;
    for (int i = 0; i < squares; ++i) {
        float lat = -hw + (i + 0.5f) * (2.0f * hw / squares);
        float x = p.x - p.tz * lat, z = p.z + p.tx * lat;
        float y = terrain.Height(x, z);
        Color c = (i & 1) ? Color{20, 20, 22, 255} : white;
        dressing.push_back({Mat44::sRotationTranslation(q, Vec3(x, y + 0.01f, z)) * Mat44::sScale(Vec3(2.0f * hw / squares, 0.03f, 0.35f)), c});
    }
}

// Todo lo sólido: en la pasada de sombras sólo el terreno cercano al foco.
void Game::DrawScene(bool shadowCasters)
{
    if (shadowCasters) {
        const Vector3 f = renderer.ShadowFocus();
        terrain.Draw(renderer, deformation.MarksTexture(), f.x, f.z, Renderer::kShadowHalfSize * 1.5f + 10.0f);
    } else {
        terrain.Draw(renderer, deformation.MarksTexture());
        renderer.DrawGrass(grassTufts.data(), (int)grassTufts.size(), deformation.MarksTexture(), terrain.OriginX(), terrain.Size());
    }
    for (const Prop& p : dressing) renderer.Box(p.transform, p.color);
    if (shadowCasters) props.Draw(renderer, *physics, renderer.ShadowFocus(), Renderer::kShadowHalfSize * 1.5f + 10.0f);
    else props.Draw(renderer, *physics);
    if (circuit.Active()) {
        if (shadowCasters) circuit.DrawShadows(renderer, renderer.ShadowFocus(), Renderer::kShadowHalfSize * 1.5f + 10.0f);
        else circuit.Draw(renderer);
    }
    if (favela.Active()) {
        if (shadowCasters) favela.DrawShadows(renderer, renderer.ShadowFocus(), Renderer::kShadowHalfSize * 1.5f + 10.0f);
        else favela.Draw(renderer, (float)GetTime());
        // Motos estacionadas (con la pata puesta: un poco inclinadas) y el baú de los motoboys.
        for (const Favela::Parked& pk : favela.ParkedBikes()) {
            const Quat q = Quat::sRotation(Vec3::sAxisY(), pk.yaw) * Quat::sRotation(Vec3::sAxisZ(), -0.12f);
            const Vec3 at = pk.pos + Vec3(0.0f, 0.62f, 0.0f);
            if (shadowCasters && (at - ToJph(renderer.ShadowFocus())).Length() > 60.0f) continue;
            parkedBike.SetVisualState(at, q, false);
            for (Wheel& w : parkedBike.wheels) w.extension = w.travel * 0.7f;
            parkedBike.Draw(renderer, 0.0f, false, pk.livery);
            if (pk.deliveryBox) renderer.Box(Mat44::sRotationTranslation(q, at) * Mat44::sTranslation(Vec3(0.0f, 0.42f, -0.62f)) * Mat44::sScale(Vec3(0.42f, 0.36f, 0.42f)),
                                             Color{200, 30, 30, 255});
        }
    }
    if (menu == Menu::Bikes && !previewBikeId.empty()) {
        // Selector de motos: la marcada, sin piloto, girando sobre una plataforma oscura con un aro del color
        // del juego (la plataforma crece al entrar y sube la moto con ella).
        const Vec3 base(previewPos.GetX(), showroomGround, previewPos.GetZ());
        const float grow = mu::Smoothstep(0.0f, 1.0f, showroomBlend), h = kPlatformHeight * grow, r = 1.2f * (0.6f + 0.4f * grow);
        // El aro: una franja apenas más ancha que la tapa, justo debajo de ella. El pedestal oscuro baja 0.6 m
        // (en una pendiente se mete en el suelo del lado alto y no flota del lado bajo).
        renderer.Disc(Mat44::sTranslation(base + Vec3(0.0f, h - 0.0255f, 0.0f)) * Mat44::sScale(Vec3(r + 0.05f, 0.039f, r + 0.05f)), kAccent);
        renderer.Disc(Mat44::sTranslation(base + Vec3(0.0f, (h - 0.6f) * 0.5f, 0.0f)) * Mat44::sScale(Vec3(r, h + 0.6f, r)), Color{30, 32, 38, 255});
        renderer.Disc(Mat44::sTranslation(base + Vec3(0.0f, h + 0.001f, 0.0f)) * Mat44::sScale(Vec3(r * 0.62f, 0.002f, r * 0.62f)), Color{40, 43, 50, 255});
        previewBike.SetVisualState(previewPos, Quat::sRotation(Vec3::sAxisY(), menuCamTime * 0.55f + 2.2f), false);
        previewBike.Draw(renderer, 0.0f, false, mp.LocalId());
    } else {
        bike.Draw(renderer, alpha, !drawRiderModel && !opt.hideRider, mp.LocalId());
        if (!drawRiderModel) ragdoll.Draw(renderer, alpha);
        else if ((bike.RiderOnBike() && !opt.hideRider) || ragdoll.Active()) riderModel.Draw(renderer, riderWorld);
    }

    // Los demás jugadores: su moto con sus colores y su piloto (sentado o como ragdoll).
    for (int id = 0; id < Multiplayer::kMaxPlayers; ++id) {
        const Multiplayer::Remote& r = mp.Remotes()[id];
        if (!r.active || !r.hasState) continue;
        const bool riderOn = (r.state.flags & BikeState::kRiderOn) != 0;
        r.bike.Draw(renderer, 0.0f, riderOn && !r.rider, id);
        if (r.rider) r.rider->Draw(renderer, riderOn ? ToRl(Mat44::sRotationTranslation(r.DisplayRot(), r.DisplayPos())) : MatrixIdentity());
    }
}

void Game::Draw()
{
    deformation.Flush();             // dibuja en la textura de huellas (fuera de BeginDrawing)

    // Piloto con modelo: pose y skinning una vez por frame (sirven para la sombra y para la imagen).
    drawRiderModel = riderModel.Loaded() && useRiderModel;
    if (drawRiderModel) {
        if (bike.RiderOnBike()) {
            riderModel.PoseOnBike(bike.RiderPoseLocal());
            riderWorld = ToRl(Mat44::sRotationTranslation(bike.RenderRotation(alpha), bike.RenderPosition(alpha)));
        } else if (ragdoll.Active()) {
            riderModel.PoseFromRagdoll(ragdoll, alpha);
            riderWorld = MatrixIdentity();
        }
        riderModel.Skin();
    }
    mp.UpdateVisuals(GetFrameTime());                   // motos y pilotos de los demás (pose y skinning)

    // Sombras: mapa de profundidad desde el sol, centrado un poco por delante de lo que sigue la cámara.
    const Vec3 subject = ragdoll.Active() ? ragdoll.Position(alpha) : bike.RenderPosition(alpha);
    Vector3 look = Vector3Subtract(camera.cam.target, camera.cam.position);
    look.y = 0.0f;
    look = Vector3Length(look) > 0.01f ? Vector3Normalize(look) : Vector3{0.0f, 0.0f, 1.0f};
    const Vector3 ahead = Vector3Add(ToRl(subject), Vector3Scale(look, 12.0f));
    renderer.BeginShadowPass(ahead);
    DrawScene(true);
    renderer.EndShadowPass();

    // Pasto 3D en un radio por delante de la cámara; se recalcula al moverse más de 1 m.
    if (Vector2Distance({ahead.x, ahead.z}, {grassCenter.x, grassCenter.z}) > 1.0f) {
        terrain.GrassTufts(ahead.x, ahead.z, 30.0f, grassTufts);
        grassCenter = ahead;
    }

    renderer.BeginScene();
    ClearBackground(BLACK);
    BeginMode3D(camera.cam);
    renderer.BeginFrame(camera.cam);
    renderer.DrawSky(camera.cam);
    DrawScene(false);
    // Cada moto tal como se dibuja (las llamas de su escape viven en su espacio).
    Matrix frames[1 + Multiplayer::kMaxPlayers];
    frames[0] = ToRl(Mat44::sRotationTranslation(bike.RenderRotation(alpha), bike.RenderPosition(alpha)));
    for (int id = 0; id < Multiplayer::kMaxPlayers; ++id) {
        const Multiplayer::Remote& r = mp.Remotes()[id];
        frames[1 + id] = r.active ? ToRl(Mat44::sRotationTranslation(r.DisplayRot(), r.DisplayPos())) : MatrixIdentity();
    }
    particles.Draw(camera.cam, frames, 1 + Multiplayer::kMaxPlayers);
    if (debugVectors) {
        rlDrawRenderBatchActive();
        rlDisableDepthTest();
        bike.DrawDebug(alpha);
        rlDrawRenderBatchActive();
        rlEnableDepthTest();
    }
    EndMode3D();
    renderer.EndScene();

    // Post-proceso (F7): motion blur radial centrado en la moto, más fuerte cuanto más rápido.
    Renderer::PostFX fx;
    if (postEffects) {
        const Vector2 onScreen = GetWorldToScreen(Vector3Add(ToRl(subject), {0.0f, 0.4f, 0.0f}), camera.cam);
        fx.focus = {mu::Clamp(onScreen.x / (float)GetScreenWidth(), 0.2f, 0.8f), mu::Clamp(onScreen.y / (float)GetScreenHeight(), 0.2f, 0.8f)};
        const float speed = ragdoll.Active() ? ragdoll.Velocity().Length() : bike.speed;
        fx.speedBlur = motionBlur ? mu::Smoothstep(11.0f, 28.0f, speed) * 0.035f : 0.0f;
        fx.aberration = 0.0015f + mu::Smoothstep(14.0f, 30.0f, speed) * 0.0035f;
    } else {
        fx.vignette = fx.grain = 0.0f;                  // sólo queda el FXAA
    }

    BeginDrawing();
    ClearBackground(BLACK);
    renderer.DrawPost(fx);
    if (showHud && menu == Menu::None) DrawHUD();
    if (menu != Menu::None) DrawMenu();
    if (mapLoadIn >= 0) {
        const std::string t = "Cargando " + (mapRequested >= 0 && mapRequested < (int)mods.Maps().size() ? mods.Maps()[mapRequested].name : std::string("mapa")) + "...";
        const Vector2 m = MeasureTextEx(font, t.c_str(), 44.0f, 0.0f);
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{0, 0, 0, 170});
        DrawTextEx(font, t.c_str(), {(GetScreenWidth() - m.x) * 0.5f, GetScreenHeight() * 0.45f}, 44.0f, 0.0f, kText);
    }

    if (opt.screenshotAt >= 0.0f && !screenshotTaken && simTime >= opt.screenshotAt) {
        rlDrawRenderBatchActive();
        // Captura propia: TakeScreenshot de raylib escala por DPI y lee fuera del framebuffer.
        const int w = GetRenderWidth(), h = GetRenderHeight();
        Image img = {rlReadScreenPixels(w, h), w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        std::string file = opt.screenshotFile;
        if (opt.screenshotEvery > 0.0f) {               // serie: archivo_000.png, archivo_001.png...
            char num[16];
            std::snprintf(num, sizeof(num), "_%03d", screenshotCount++);
            const size_t dot = file.rfind('.');
            file = dot == std::string::npos ? file + num : file.substr(0, dot) + num + file.substr(dot);
            opt.screenshotAt += opt.screenshotEvery;
        } else {
            screenshotTaken = true;
        }
        ExportImage(img, file.c_str());
        UnloadImage(img);
    }
    EndDrawing();
}

void Game::DrawHUD()
{
    const int W = GetScreenWidth(), H = GetScreenHeight();
    // Todo escalado con el alto de la ventana (pensado para 900 px; más chico que el 90% no se lee) y el texto suelto
    // con una sombra: sobre la arena o el cielo el gris claro no se leía.
    const float ui = mu::Clamp((float)H / 900.0f, 0.9f, 2.0f);
    auto text = [&](const Font& f, const std::string& s, float x, float y, float size, Color c) {
        const float o = std::max(1.0f, size * 0.05f);
        DrawTextEx(f, s.c_str(), {x + o, y + o}, size, 0.0f, Color{0, 0, 0, (unsigned char)(c.a * 0.5f)});
        DrawTextEx(f, s.c_str(), {x, y}, size, 0.0f, c);
    };
    auto textRight = [&](const Font& f, const std::string& s, float xRight, float y, float size, Color c) {
        text(f, s, xRight - MeasureTextEx(f, s.c_str(), size, 0.0f).x, y, size, c);
    };
    auto textCenter = [&](const Font& f, const std::string& s, float cx, float y, float size, Color c) {
        text(f, s, cx - MeasureTextEx(f, s.c_str(), size, 0.0f).x * 0.5f, y, size, c);
    };
    char buf[128];

    const std::string gearText = bike.engine.reverse ? std::string("R") : std::to_string(bike.engine.gear);

    // ------------------------------------------------------------ datos técnicos (arriba izquierda, T)
    // Para mirar la física: escondido por defecto (Ajustes → Datos técnicos, o T), con los cuadros por segundo.
    if (techData || opt.techData) {
        const Wheel& fw = bike.wheels[Bike::FRONT];
        const Wheel& rw = bike.wheels[Bike::REAR];
        struct Line { const char* label; std::string value; Color color; };
        auto fmt = [&](const char* f, float v) { std::snprintf(buf, sizeof(buf), f, v); return std::string(buf); };
        auto gripColor = [](float usage) { return usage >= 1.0f ? Color{255, 120, 80, 255} : kText; };
        // Agarre usado: fracción del disponible (100% = al límite; "desliza" = pasado).
        auto grip = [&](const Wheel& w) {
            if (!w.grounded) return std::string("aire");
            return w.gripUsage >= 1.0f ? std::string("desliza") : fmt("%4.0f %%", w.gripUsage * 100.0f);
        };
        const int fps = GetFPS();
        std::vector<Line> lines = {
            {"Cuadros/s", std::to_string(fps), fps < 45 ? Color{255, 190, 90, 255} : kTextDim},
            {"Velocidad", fmt("%6.1f km/h", bike.speed * 3.6f), kText},
            {"Motor", fmt("%6.0f rpm", bike.engine.rpm) + (bike.engine.limiter ? "  corte" : (bike.engine.clutchSlipping ? "  embrague" : "")), kText},
            {"Marcha", gearText + (bike.engine.autoShift ? "  auto" : "  manual"), kText},
            {"Gas", fmt("%4.0f %%", bike.throttle * 100.0f), kText},
            {"Tracción", !bike.tractionControl ? std::string("apagada") : (bike.tcFactor < 0.97f ? std::string("cortando") : std::string("prendida")),
             bike.tractionControl && bike.tcFactor < 0.97f ? Color{255, 190, 90, 255} : kText},
            {"Susp. adelante", fmt("%4.0f %%", fw.Compression01() * 100.0f), kText},
            {"Susp. atrás", fmt("%4.0f %%", rw.Compression01() * 100.0f), kText},
            {"Agarre adelante", grip(fw), gripColor(fw.gripUsage)},
            {"Agarre atrás", grip(rw), gripColor(rw.gripUsage)},
            {"Patina atrás", fmt("%6.2f", rw.slipRatio), std::fabs(rw.slipRatio) > 0.3f ? Color{255, 190, 90, 255} : kText},
            {"Cabeceo", fmt("%6.1f°", mu::Deg(bike.pitch)), kText},
            {"Inclinación", fmt("%6.1f°", mu::Deg(bike.roll)), kText},
        };
        std::snprintf(buf, sizeof(buf), "%+.2f / %+.2f", bikeParams.comHeight, bikeParams.comForward);
        lines.push_back({"Centro de masa", buf, kTextDim});

        const float fs = 17.0f * ui, lineH = 21.0f * ui, panelX = 18.0f * ui, panelY = 18.0f * ui;
        float labelW = 0.0f, valueW = 0.0f;
        for (const Line& l : lines) {
            labelW = std::max(labelW, MeasureTextEx(mono, l.label, fs, 0.0f).x);
            valueW = std::max(valueW, MeasureTextEx(mono, l.value.c_str(), fs, 0.0f).x);
        }
        const float panelW = labelW + valueW + 40.0f * ui, panelH = 14.0f * ui + lineH * (float)lines.size();
        DrawRectangle((int)panelX, (int)panelY, (int)panelW, (int)panelH, kPanel);
        DrawRectangle((int)panelX, (int)panelY, (int)(44.0f * ui), (int)(2.0f * ui), kAccent);
        for (size_t i = 0; i < lines.size(); ++i) {
            const float y = panelY + 8.0f * ui + lineH * (float)i;
            DrawTextEx(mono, lines[i].label, {panelX + 12.0f * ui, y}, fs, 0.0f, kTextDim);
            const float vw = MeasureTextEx(mono, lines[i].value.c_str(), fs, 0.0f).x;
            DrawTextEx(mono, lines[i].value.c_str(), {panelX + panelW - 12.0f * ui - vw, y}, fs, 0.0f, lines[i].color);
        }
    }

    // ------------------------------------------------------------ velocímetro (abajo derecha)
    const float right = (float)W - 36.0f * ui, bottom = (float)H - 30.0f * ui;
    std::snprintf(buf, sizeof(buf), "%.0f", bike.speed * 3.6f);
    textRight(font, buf, right - 92.0f * ui, bottom - 92.0f * ui, 84.0f * ui, kText);
    text(font, "km/h", right - 84.0f * ui, bottom - 40.0f * ui, 22.0f * ui, kTextDim);
    DrawRectangle((int)(right - 64.0f * ui), (int)(bottom - 104.0f * ui), (int)(64.0f * ui), (int)(64.0f * ui), kPanel);
    DrawRectangle((int)(right - 64.0f * ui), (int)(bottom - 104.0f * ui), (int)(64.0f * ui), (int)(2.0f * ui), kAccent);
    textCenter(font, gearText, right - 32.0f * ui, bottom - 102.0f * ui, 58.0f * ui, kText);

    // barra de RPM segmentada
    const int segs = 40;
    const float barW = 320.0f * ui, barX = right - barW, barY = bottom - 128.0f * ui;
    const float rpm01 = mu::Clamp(bike.engine.rpm / bikeParams.engine.revLimit, 0.0f, 1.0f);
    for (int i = 0; i < segs; ++i) {
        float t = (float)(i + 1) / (float)segs;
        float x = barX + (barW / segs) * i;
        Color c = t > 0.9f ? Color{232, 64, 48, 255} : (t > 0.75f ? Color{240, 170, 60, 255} : kText);
        if (t > rpm01) c = Color{255, 255, 255, 40};
        DrawRectangle((int)x, (int)barY, (int)(barW / segs - 2.0f * ui), (int)(10.0f * ui), c);
    }

    // ------------------------------------------------------------ vueltas o saltos (arriba centro)
    const float cx = (float)W * 0.5f;
    if (!FreeRide()) {
        const float lapTime = lapStart >= 0.0f ? simTime - lapStart : 0.0f;
        DrawRectangle((int)(cx - 20.0f * ui), (int)(18.0f * ui), (int)(40.0f * ui), (int)(2.0f * ui), kAccent);
        textCenter(font, lapStart >= 0.0f ? FormatTime(lapTime) : "--:--.--", cx, 24.0f * ui, 40.0f * ui, kText);
        std::snprintf(buf, sizeof(buf), "Vuelta %d    Última %s    Mejor %s", lapCount + 1, FormatTime(lastLap).c_str(), FormatTime(bestLap).c_str());
        textCenter(font, buf, cx, 68.0f * ui, 20.0f * ui, kTextDim);
    } else {
        // Mapa libre (la guía no es una vuelta): cuánto volás. En el aire, lo que va; al caer, el salto y el mejor.
        const bool live = jumpAir > 0.7f && bike.RiderOnBike();
        if (live || jumpShow > 0.0f) {
            const unsigned char a = (unsigned char)(255.0f * (live ? 1.0f : mu::Clamp(jumpShow, 0.0f, 1.0f)));
            std::snprintf(buf, sizeof(buf), live ? "%.1f s en el aire" : "Salto  %.1f s", live ? jumpAir : lastJump);
            DrawRectangle((int)(cx - 20.0f * ui), (int)(18.0f * ui), (int)(40.0f * ui), (int)(2.0f * ui), Color{kAccent.r, kAccent.g, kAccent.b, a});
            textCenter(font, buf, cx, 24.0f * ui, 40.0f * ui, Color{kText.r, kText.g, kText.b, a});
            std::snprintf(buf, sizeof(buf), "Mejor  %.1f s", bestJump);
            if (bestJump > 0.0f) textCenter(font, buf, cx, 68.0f * ui, 20.0f * ui, Color{kTextDim.r, kTextDim.g, kTextDim.b, a});
        }
    }

    if (messageTime > 0.0f) {
        unsigned char a = (unsigned char)(255.0f * mu::Clamp(messageTime * 2.0f, 0.0f, 1.0f));
        textCenter(font, message, cx, 104.0f * ui, 24.0f * ui, Color{kText.r, kText.g, kText.b, a});
    }

    // Grau: con una moto de grau (la trilheira), el contador del wheelie (el actual grande, el último y el mejor).
    if (bikeParams.grauAssist > 0.0f && (grauTime > 0.4f || grauShow > 0.0f)) {
        const bool live = grauTime > 0.4f;
        const float t = live ? grauTime : lastGrau;
        const unsigned char a = (unsigned char)(255.0f * (live ? 1.0f : mu::Clamp(grauShow, 0.0f, 1.0f)));
        std::snprintf(buf, sizeof(buf), "GRAU  %.1f s", t);
        textCenter(font, buf, cx, (float)H * 0.2f, (live ? 58.0f : 46.0f) * ui, Color{252, 214, 40, a});
        std::snprintf(buf, sizeof(buf), "mejor %.1f s", bestGrau);
        if (bestGrau > 0.0f) textCenter(font, buf, cx, (float)H * 0.2f + 62.0f * ui, 22.0f * ui, Color{kTextDim.r, kTextDim.g, kTextDim.b, a});
    }

    // Caída: el título enseguida. El jugador no reaparece solo (mira la caída): de a poco aparece el
    // cartel de la R. El bot y las pruebas reaparecen solos (AutoRespawnAfter) y queda la línea de siempre.
    if (bike.crashed) {
        textCenter(font, "Caída", cx, (float)H * 0.36f, 64.0f * ui, kText);
        if (AutoRespawnAfter(opt) >= 0.0f) {
            textCenter(font, "R / Y para reaparecer", cx, (float)H * 0.36f + 70.0f * ui, 24.0f * ui, kTextDim);
        } else if (bike.crashedTime > 0.8f) {
            const float fade = mu::Clamp((bike.crashedTime - 0.8f) * 2.5f, 0.0f, 1.0f);
            auto alpha = [fade](Color c) { return Color{c.r, c.g, c.b, (unsigned char)(c.a * fade)}; };
            const char* hint = "Tocá R para reaparecer";
            const char* pad = IsGamepadAvailable(0) ? "o Y en el joystick" : (FreeRide() ? "acá cerca, mirando para donde ibas" : "");
            const float size = 34.0f * ui, y = (float)H * 0.36f + 84.0f * ui;
            const float w = std::max(MeasureTextEx(font, hint, size, 0.0f).x, MeasureTextEx(font, pad, 20.0f * ui, 0.0f).x) + 56.0f * ui;
            const float h = (*pad ? 84.0f : 60.0f) * ui;
            DrawRectangle((int)(cx - w * 0.5f), (int)y, (int)w, (int)h, alpha(kPanel));
            DrawRectangle((int)(cx - 22.0f * ui), (int)y, (int)(44.0f * ui), (int)(2.0f * ui), alpha(kAccent));
            textCenter(font, hint, cx, y + 12.0f * ui, size, alpha(kText));
            if (*pad) textCenter(font, pad, cx, y + 52.0f * ui, 20.0f * ui, alpha(kTextDim));
        }
    }
    if (paused) {
        textCenter(font, "Pausa", cx, (float)H * 0.42f, 56.0f * ui, kText);
        textCenter(font, "P para seguir", cx, (float)H * 0.42f + 62.0f * ui, 22.0f * ui, kTextDim);
    }

    DrawHelp(ui);
    textRight(font, MOTOSIM_VERSION, (float)W - 12.0f * ui, (float)H - 22.0f * ui, 15.0f * ui, Color{kTextDim.r, kTextDim.g, kTextDim.b, 170});
    DrawSession();
}

// La ayuda de teclas (abajo a la izquierda): lo justo para manejar, en dos columnas. Todas las teclas están en
// menú → Controles. Aparece los primeros segundos (y en pausa), siempre o nunca según Ajustes; H la muestra o
// la esconde. Escondida queda una línea chica para acordarse.
void Game::DrawHelp(float ui)
{
    const float H = (float)GetScreenHeight();
    float a = HelpVisible() ? 1.0f : 0.0f;
    if (a > 0.0f && helpPinned < 0 && helpMode == HelpMode::Start && !paused) a = mu::Clamp(helpTimer / 1.5f, 0.0f, 1.0f);   // se va de a poco
    auto fadeTo = [](Color c, float k) { return Color{c.r, c.g, c.b, (unsigned char)((float)c.a * mu::Clamp(k, 0.0f, 1.0f))}; };
    auto text = [&](const std::string& s, float x, float y, float size, Color c, float spacing = 0.0f) {
        const float o = std::max(1.0f, size * 0.05f);
        DrawTextEx(font, s.c_str(), {x + o, y + o}, size, spacing, Color{0, 0, 0, (unsigned char)(c.a * 0.5f)});
        DrawTextEx(font, s.c_str(), {x, y}, size, spacing, c);
    };
    if (helpMode != HelpMode::Never && a < 1.0f && !paused && PlayerDriving())
        text("H  ayuda de teclas", 18.0f * ui, H - 30.0f * ui, 15.0f * ui, fadeTo(Color{230, 232, 236, 150}, 1.0f - a));
    if (a <= 0.0f) return;

    struct Pair { std::string keys, action; };
    const Pair left[] = {
        {"W  /  RT", "gas"},
        {"S  /  LT", "freno (parado: marcha atrás)"},
        {"Espacio  /  A", "freno de atrás"},
        {"A  D  /  stick", "doblar"},
        {"Q  E  /  LB  RB", bike.engine.autoShift ? "cambios (la caja es automática)" : "cambios"},
    };
    const Pair right[] = {
        {"\xE2\x86\x91  \xE2\x86\x93  /  stick", "cuerpo adelante y atrás"},
        {"\xE2\x86\x90  \xE2\x86\x92  /  stick der.", "cuerpo a los costados"},
        {"R  /  Y", FreeRide() ? "reaparecer acá cerca" : "reaparecer"},
        {"Esc  /  Start", "menú: ajustes y controles"},
        {"H", "esconder esta ayuda"},
    };
    const int rows = 5;
    const float fs = 17.0f * ui, rowH = 22.0f * ui, pad = 14.0f * ui, gap = 12.0f * ui, colGap = 30.0f * ui;
    auto colWidths = [&](const Pair* p, float& keyW, float& actW) {
        keyW = actW = 0.0f;
        for (int i = 0; i < rows; ++i) {
            keyW = std::max(keyW, MeasureTextEx(font, p[i].keys.c_str(), fs, 0.0f).x);
            actW = std::max(actW, MeasureTextEx(font, p[i].action.c_str(), fs, 0.0f).x);
        }
    };
    float k1, a1, k2, a2;
    colWidths(left, k1, a1);
    colWidths(right, k2, a2);
    const float panelW = pad * 2.0f + k1 + gap + a1 + colGap + k2 + gap + a2;
    const float panelH = pad + 22.0f * ui + rowH * rows + pad * 0.6f;
    const float x0 = 18.0f * ui, y0 = H - 18.0f * ui - panelH;
    DrawRectangle((int)x0, (int)y0, (int)panelW, (int)panelH, fadeTo(kPanel, a));
    DrawRectangle((int)x0, (int)y0, (int)(44.0f * ui), (int)(2.0f * ui), fadeTo(kAccent, a));
    text("CONTROLES", x0 + pad, y0 + pad - 2.0f * ui, 13.0f * ui, fadeTo(kTextDim, a), 2.0f);
    const std::string more = "todas las teclas: Esc, Controles";
    text(more, x0 + panelW - pad - MeasureTextEx(font, more.c_str(), 14.0f * ui, 0.0f).x, y0 + pad - 3.0f * ui, 14.0f * ui, fadeTo(kTextDim, a));
    auto column = [&](const Pair* p, float x, float keyW) {
        for (int i = 0; i < rows; ++i) {
            const float y = y0 + pad + 22.0f * ui + rowH * (float)i;
            text(p[i].keys, x, y, fs, fadeTo(kText, a));
            text(p[i].action, x + keyW + gap, y, fs, fadeTo(Color{200, 204, 212, 255}, a));
        }
    };
    column(left, x0 + pad, k1);
    column(right, x0 + pad + k1 + gap + a1 + colGap, k2);
}

// ======================================================================================= red
void Game::DrawSession()
{
    if (!mp.Active()) return;
    const int W = GetScreenWidth();
    const float u = mu::Clamp((float)GetScreenHeight() / 900.0f, 0.9f, 2.0f);   // escala, como el resto del HUD
    auto text = [&](const Font& f, const std::string& s, float x, float y, float size, Color c) { DrawTextEx(f, s.c_str(), {x, y}, size, 0.0f, c); };
    auto width = [&](const Font& f, const std::string& s, float size) { return MeasureTextEx(f, s.c_str(), size, 0.0f).x; };

    // Nombre sobre cada moto de los demás (o sobre el piloto, si se cayó).
    for (int id = 0; id < Multiplayer::kMaxPlayers; ++id) {
        const Multiplayer::Remote& r = mp.Remotes()[id];
        if (!r.active || !r.hasState) continue;
        const bool riderOn = (r.state.flags & BikeState::kRiderOn) != 0;
        const Vec3 head = riderOn ? r.DisplayPos() + Vec3(0.0f, 1.45f, 0.0f) : r.state.parts[RiderPart::Pelvis].GetTranslation() + Vec3(0.0f, 0.8f, 0.0f);
        const Vector3 wp = ToRl(head);
        const Vector3 toP = Vector3Subtract(wp, camera.cam.position), fwd = Vector3Subtract(camera.cam.target, camera.cam.position);
        if (Vector3DotProduct(toP, fwd) <= 0.0f) continue;
        const float dist = Vector3Length(toP), size = mu::Clamp(26.0f - dist * 0.2f, 15.0f, 26.0f) * u;
        const Vector2 sp = GetWorldToScreen(wp, camera.cam);
        const std::string label = r.name.empty() ? std::string("...") : r.name;
        const float tw = width(font, label, size);
        DrawRectangle((int)(sp.x - tw * 0.5f - size * 0.9f), (int)(sp.y - size * 0.6f), (int)(tw + size * 1.6f), (int)(size * 1.25f), kPanel);
        DrawCircle((int)(sp.x - tw * 0.5f - size * 0.35f), (int)sp.y, size * 0.22f, Livery(id).plastic);
        text(font, label, sp.x - tw * 0.5f, sp.y - size * 0.55f, size, kText);
    }

    // Panel arriba a la derecha: código para invitar y jugadores.
    const bool host = mp.GetMode() == Multiplayer::Mode::Host;
    const auto roster = mp.Roster(playerName);
    const float pw = 320.0f * u, px = (float)W - pw - 18.0f * u, py = 40.0f * u, x = px + 14.0f * u;
    const float extra = host ? 76.0f + 18.0f * (float)mp.OtherCodes().size() : (mp.Connected() ? 0.0f : 46.0f);
    const float h = (44.0f + extra + 26.0f * (float)roster.size()) * u;
    float y = py + 12.0f * u;
    DrawRectangle((int)px, (int)py, (int)pw, (int)h, kPanel);
    DrawRectangle((int)px, (int)py, (int)(44.0f * u), (int)(2.0f * u), kAccent);
    text(font, host ? "PARTIDA LAN · anfitrión" : (mp.Connected() ? "PARTIDA LAN · conectado" : "PARTIDA LAN · conectando"), x, y, 20.0f * u, kTextDim);
    y += 30.0f * u;
    if (host) {
        text(font, "Código para invitar (F10 lo copia)", x, y, 17.0f * u, kTextDim);
        text(mono, mp.InviteCode(), x, y + 20.0f * u, 40.0f * u, kText);
        y += 70.0f * u;
        for (const std::string& other : mp.OtherCodes()) {
            text(font, "otra red: " + other, x, y, 16.0f * u, kTextDim);
            y += 18.0f * u;
        }
        y += 6.0f * u;
    } else if (!mp.Connected()) {
        text(font, mp.Status(), x, y, 17.0f * u, kText);
        text(mono, mp.InviteCode(), x, y + 20.0f * u, 22.0f * u, kTextDim);
        y += 46.0f * u;
    }
    for (const Multiplayer::Entry& e : roster) {
        DrawRectangle((int)x, (int)(y + 5.0f * u), (int)(12.0f * u), (int)(12.0f * u), Livery(e.id).plastic);
        text(font, e.name + (e.ping < 0 ? "  (vos)" : ""), x + 22.0f * u, y, 20.0f * u, kText);
        if (e.ping >= 0) {
            const std::string p = std::to_string(e.ping) + " ms";
            text(mono, p, px + pw - 14.0f * u - width(mono, p, 18.0f * u), y + 2.0f * u, 18.0f * u, e.ping > 80 ? Color{255, 170, 90, 255} : kTextDim);
        }
        y += 26.0f * u;
    }
}

// ------------------------------------------------------------------------------------ motos
Game::BikeData* Game::DataForBike(const std::string& id)
{
    auto it = bikeData.find(id);
    if (it != bikeData.end()) return it->second.get();
    const BikeDef* def = mods.Bike(id);
    if (!def) return nullptr;
    auto d = std::make_unique<BikeData>();
    d->params.Register(d->tuning);
    if (!baseTuningPath.empty()) d->tuning.Load(baseTuningPath);
    if (!def->tuning.empty()) d->tuning.Load(def->tuning);
    d->params.visualStyle = std::max(0, BikeStyleFromName(def->style));
    d->stats = ComputeStats(d->params);
    return bikeData.emplace(id, std::move(d)).first->second.get();
}

void Game::ReloadBikeData()
{
    // En el lugar: las motos de los demás jugadores apuntan a estos números.
    for (auto& [id, d] : bikeData) {
        const BikeDef* def = mods.Bike(id);
        d->params = BikeParams{};
        if (!baseTuningPath.empty()) d->tuning.Load(baseTuningPath);
        if (def && !def->tuning.empty()) d->tuning.Load(def->tuning);
        d->params.visualStyle = def ? std::max(0, BikeStyleFromName(def->style)) : 0;
        d->stats = ComputeStats(d->params);
    }
}

// Números para el menú, sacados de la física de cada moto: potencia máxima (la curva de torque), peso sin
// el piloto, velocidad máxima (la menor entre la de la caja en 5ª al corte y la que deja el aire con esa
// potencia), 0-100 km/h (una cuenta simple con el límite de agarre y de wheelie de la trasera), agarre en
// curva, recorrido de suspensión y radio de giro.
Game::BikeStats Game::ComputeStats(const BikeParams& p)
{
    EngineParams ep = p.engine;
    Engine e;
    e.p = &ep;
    constexpr float kRpmToRad = 2.0f * mu::kPi / 60.0f, kG = 9.81f;
    float best = 0.0f;
    for (float rpm = ep.idleRPM; rpm <= ep.revLimit; rpm += 25.0f) best = std::max(best, e.Torque(rpm) * ep.torqueScale * rpm * kRpmToRad);
    BikeStats st{};
    st.hp = best / 745.7f;
    st.kg = p.mass - Bike::kRiderMass;
    const float r = p.rearTire.radius;
    const float vGear = ep.revLimit / (ep.gear[4] * ep.finalDrive) * kRpmToRad * r;
    float vDrag = 1.0f;
    for (int i = 0; i < 400; ++i) {
        const float need = 0.6f * p.dragArea * vDrag * vDrag * vDrag + p.rollingResistance * p.mass * kG * vDrag;
        if (need > best * ep.efficiency) break;
        vDrag += 0.25f;
    }
    st.topKmh = std::min(vGear, vDrag) * 3.6f;
    // 0-100: la moto no puede empujar más que lo que agarra la trasera ni que lo que la para en una rueda.
    const float limit = p.mass * kG * std::min(0.8f, 0.9f * p.rearTire.longGrip);
    float v = 0.0f, t = 0.0f;
    int gear = 1;
    while (v < 100.0f / 3.6f && t < 30.0f) {
        const float ratio = ep.gear[gear - 1] * ep.finalDrive;
        const float rpm = std::max(ep.clutchRPM, v / r * ratio / kRpmToRad);
        if (rpm > ep.revLimit * 0.97f && gear < 5) {
            ++gear;
            t += ep.shiftTime;
            continue;
        }
        // En 5ª al corte ya no empuja: una moto que no llega a 100 (la de trial) no tiene 0 a 100 (el menú muestra "-").
        const float drive = rpm >= ep.revLimit ? 0.0f : std::min(e.Torque(rpm) * ep.torqueScale * ratio * ep.efficiency / r, limit);
        const float a = (drive - 0.6f * p.dragArea * v * v - p.rollingResistance * p.mass * kG) / p.mass;
        if (a <= 0.0f) break;
        v += a * 0.01f;
        t += 0.01f;
    }
    st.zeroTo100 = v >= 100.0f / 3.6f ? t : 99.0f;
    st.latG = p.maxLateralAccel / kG;
    st.travelMm = 500.0f * (p.front.travel + p.rear.travel);
    st.turnRadius = p.minTurnRadius;
    return st;
}

// Cámara del menú: un plano de cine alrededor de la moto (teleobjetivo, bajo, girando despacio) con la
// moto a la derecha de la pantalla, donde no tapa el menú. Arranca desde donde estaba la cámara del juego
// y vuelve a ella al cerrar el menú. En el selector de motos la marcada gira en una plataforma y la cámara
// la encuadra arriba a la derecha (abajo van sus números).
void Game::UpdateMenuCamera(float dt)
{
    const bool on = menu != Menu::None;
    const Vec3 subject = ragdoll.Active() ? ragdoll.Position(alpha) : bike.RenderPosition(alpha);
    if (on && menuCamBlend <= 0.0f) {                // se abre: sigue desde donde mira la cámara del juego
        const Vector3 d = Vector3Subtract(camera.cam.position, ToRl(subject));
        menuCamYaw = std::atan2(d.x, d.z);
        menuCamTime = 0.0f;
    }
    menuCamBlend = mu::MoveTowards(menuCamBlend, on ? 1.0f : 0.0f, dt / 1.1f);
    if (menuCamBlend <= 0.0f) return;
    menuCamTime += dt;

    // La moto del selector: la marcada, parada en la plataforma sobre el suelo, girando.
    const bool showroom = menu == Menu::Bikes;
    if (showroom) {
        const std::vector<BikeDef>& bikes = mods.Bikes();
        const int n = std::max(1, (int)bikes.size());
        const std::string id = bikes.empty() ? current.bike : bikes[menuSel % n].id;
        if (id != previewBikeId) {
            if (BikeData* d = DataForBike(id)) {
                previewBike.InitVisual(d->params);
                for (Wheel& w : previewBike.wheels) w.extension = w.travel * 0.65f;
                float lowest = 1e9f;
                for (const Wheel& w : previewBike.wheels) lowest = std::min(lowest, w.AxleLocal().GetY() - w.radius);
                previewLift = -lowest;
                previewBikeId = id;
            }
        }
    }
    // Entre el plano del menú y el del selector se pasa de a poco (no de golpe).
    // La vitrina queda donde estaba la moto al entrar al selector: la carrera sigue de fondo y la moto
    // propia puede seguir andando sola, pero la cámara y la plataforma no la persiguen. La plataforma se
    // apoya en lo más alto del suelo que tiene abajo (en una pendiente no queda enterrada).
    if (showroom && showroomBlend <= 0.0f) {
        showroomAnchor = subject;
        float g = terrain.Height(subject.GetX(), subject.GetZ());
        for (int a = 0; a < 8; ++a) {
            const float ang = mu::kPi * 0.25f * (float)a;
            g = std::max(g, terrain.Height(subject.GetX() + 1.25f * std::cos(ang), subject.GetZ() + 1.25f * std::sin(ang)));
        }
        showroomGround = g;
    }
    showroomBlend = mu::MoveTowards(showroomBlend, showroom ? 1.0f : 0.0f, dt / 0.7f);
    const float k = mu::Smoothstep(0.0f, 1.0f, showroomBlend);
    const Vec3 anchor = showroomBlend > 0.0f ? showroomAnchor : subject;
    const float ground = showroomBlend > 0.0f ? showroomGround : terrain.Height(subject.GetX(), subject.GetZ());
    previewPos = Vec3(anchor.GetX(), ground + kPlatformHeight * k + previewLift, anchor.GetZ());

    // Encuadre: dónde queda la moto en la pantalla (fracción desde el centro) según el lente.
    const float aspect = (float)GetScreenWidth() / std::max(1.0f, (float)GetScreenHeight());
    const float fov = mu::Lerp(32.0f, 34.0f, k), dist = mu::Lerp(6.6f, 7.4f, k);
    const float screenX = mu::Lerp(0.4f, 0.46f, k), screenY = mu::Lerp(0.1f, 0.5f, k);   // + derecha / + arriba del centro
    menuCamYaw += dt * 0.09f * (1.0f - k);
    const float height = mu::Lerp(0.75f + 0.25f * std::sin(menuCamTime * 0.21f), 1.5f, k);
    const Vec3 center = subject + (previewPos - subject) * k;           // el centro de la moto
    Vec3 pos = center + Vec3(std::sin(menuCamYaw) * dist, height, std::cos(menuCamYaw) * dist);
    pos.SetY(std::max(pos.GetY(), terrain.Height(pos.GetX(), pos.GetZ()) + 0.4f));
    const Vec3 fwd = (center - pos).Normalized();
    const Vec3 right = fwd.Cross(Vec3::sAxisY()).NormalizedOr(Vec3::sAxisX());
    const Vec3 upv = right.Cross(fwd);
    const float halfH = std::tan(mu::Rad(fov * 0.5f)) * dist, halfW = halfH * aspect;
    const Vec3 target = center - right * (screenX * halfW) - upv * (screenY * halfH);

    const float s = mu::Smoothstep(0.0f, 1.0f, menuCamBlend);
    camera.cam.position = Vector3Lerp(camera.cam.position, ToRl(pos), s);
    camera.cam.target = Vector3Lerp(camera.cam.target, ToRl(target), s);
    camera.cam.fovy = mu::Lerp(camera.cam.fovy, fov, s);
}

// El estilo como se muestra en la lista de motos.
std::string Game::StyleLabel(const std::string& style)
{
    if (style == "mx") return "motocross";
    if (style == "mx2t") return "motocross 2T";
    if (style == "trail") return "trail de calle";
    if (style == "race") return "pista";
    return style;
}

void Game::OpenBikeMenu()
{
    OpenMenu(Menu::Bikes);
    menuSel = 0;                                     // marcada la que se usa (la del mapa si no se eligió otra)
    const std::vector<BikeDef>& bikes = mods.Bikes();
    for (size_t i = 0; i < bikes.size(); ++i)
        if (bikes[i].id == bikeId) menuSel = (int)i;
}

void Game::ChooseBike(const std::string& id)
{
    const std::string before = bikeId;
    chosenBike = savedBike = id;
    if (!opt.headless) SavePrefs();
    const std::string after = !chosenBike.empty() && mods.Bike(chosenBike) ? chosenBike : current.bike;
    if (after == before) return;
    const int i = CurrentMapIndex();                 // se rearma el mapa con la moto nueva
    if (i >= 0) {
        mapRequested = i;
        mapLoadIn = 2;
    } else {
        LoadMap(current);
    }
}

// ---------------------------------------------------------------------------------------- menú
std::vector<Game::MenuItem> Game::MenuItems()
{
    // Cada opción lleva una clave para volver a marcarla al salir de su pantalla (BackToMain): los índices
    // cambian con el menú (en red, anfitrión o invitado) y cuando se agregan opciones.
    std::vector<MenuItem> items;
    const MenuItem settings{"Ajustes", "Imagen, sonido, lo que se ve en pantalla y la caja.",
                            [this] { OpenMenu(Menu::Settings); }, "ajustes"};
    const MenuItem controls{"Controles", "Todas las teclas y los botones del joystick.", [this] { OpenMenu(Menu::Controls); }, "controles"};
    if (!mp.Active()) {
        items.push_back({raced ? "Seguir corriendo" : "Jugar solo", FreeRide() ? "Vos, la moto y todo el mapa para andar libre." : "Vos, la moto y la pista.",
                         [this] { menu = Menu::None; }, "jugar"});
        items.push_back({"Crear partida en red", "Tus amigos se suman con el código que te da el juego (misma red wifi).", [this] {
                             StartHost();
                             if (mp.Active()) OpenMenu(Menu::Lobby);
                         }, "crear"});
        items.push_back({"Unirse a una partida", "Escribí el código que te pasó el que creó la partida.", [this] {
                             codeInput.clear();
                             OpenMenu(Menu::Join);
                         }, "unirse"});
        items.push_back({"Mapa: " + current.name, "Dónde se corre (en red lo elige el que crea la partida).", [this] {
                             OpenMenu(Menu::Maps);
                             menuSel = std::max(0, CurrentMapIndex());
                         }, "mapa"});
        items.push_back({"Moto: " + bikeName, "Con cuál corrés: la de cada mapa o cualquiera, con sus números.", [this] { OpenBikeMenu(); }, "moto"});
        items.push_back({"Nombre: " + playerName, "Cómo te ven los demás en la carrera.", [this] {
                             nameInput = playerName;
                             OpenMenu(Menu::Name);
                         }, "nombre"});
        items.push_back(settings);
        items.push_back(controls);
    } else {
        items.push_back({"Volver a la carrera", "", [this] { menu = Menu::None; }, "jugar"});
        items.push_back({"Ver la partida", "Código para invitar y pilotos conectados.", [this] { OpenMenu(Menu::Lobby); }, "partida"});
        if (mp.GetMode() == Multiplayer::Mode::Host)
            items.push_back({"Mapa: " + current.name, "Cambiarlo lleva a todos al mapa nuevo.", [this] {
                                 OpenMenu(Menu::Maps);
                                 menuSel = std::max(0, CurrentMapIndex());
                             }, "mapa"});
        items.push_back({"Moto: " + bikeName, "Cada uno corre con la que quiere.", [this] { OpenBikeMenu(); }, "moto"});
        items.push_back(settings);
        items.push_back(controls);
        items.push_back({"Salir de la partida", "Seguís corriendo solo.", [this] { LeaveSession(); }, "dejar"});
    }
    items.push_back({"Salir del juego", "", [this] { quitRequested = true; }, "salir"});
    return items;
}

// Ajustes: lo que antes estaba suelto en el menú (pantalla completa, sacudón, motion blur) y lo que el jugador
// puede querer cambiar (volumen, ayuda, datos técnicos, caja, tracción). Todo se guarda al cambiarlo.
std::vector<Game::Setting> Game::SettingsItems()
{
    std::vector<Setting> items;
    // Sí / no: → prende, ← apaga, Enter o click cambia.
    auto toggle = [&](const char* group, const char* label, const std::string& hint, bool on, std::function<void()> flip) {
        Setting s;
        s.group = group;
        s.label = label;
        s.hint = hint;
        s.kind = Setting::Kind::Toggle;
        s.on = on;
        s.value = on ? "Sí" : "No";
        s.change = [on, flip](int d) {
            if (d == 0 || (d > 0) != on) flip();
        };
        items.push_back(s);
    };
    // Una de varias: → la siguiente, ← la anterior (dan la vuelta).
    auto choice = [&](const char* group, const char* label, const std::string& hint, const std::string& value, std::function<void(int)> step) {
        Setting s;
        s.group = group;
        s.label = label;
        s.hint = hint;
        s.kind = Setting::Kind::Choice;
        s.value = value;
        s.change = [step](int d) { step(d == 0 ? 1 : d); };
        items.push_back(s);
    };

    toggle("IMAGEN", "Pantalla completa", "Sin bordes, del tamaño del monitor. También con F11 o Alt+Enter.", fullscreen,
           [this] { SetFullscreen(!fullscreen); });
    toggle("IMAGEN", "Motion blur", "Los bordes de la pantalla se desenfocan cuando vas rápido.", motionBlur, [this] {
        motionBlur = !motionBlur;
        SavePrefs();
    });
    toggle("IMAGEN", "Sacudón de cámara", "La cámara se sacude al aterrizar y al chocar, y vibra a mucha velocidad.", camera.shakeEnabled, [this] {
        camera.shakeEnabled = !camera.shakeEnabled;
        SavePrefs();
    });
    {
        Setting s;
        s.group = "SONIDO";
        s.label = "Volumen";
        s.kind = Setting::Kind::Level;
        s.level = volume;
        s.levels = 10;
        s.dim = muted;
        s.value = muted ? std::string("Apagado") : std::to_string(volume * 10) + "%";
        s.hint = muted ? "Apagado: Enter o M lo prende." : "El motor, los petardeos y las cubiertas. M lo apaga y lo prende.";
        s.change = [this](int d) {
            if (d == 0) muted = !muted;              // Enter: prende o apaga
            else {
                volume = std::clamp(volume + d, 0, 10);
                muted = false;
            }
            ApplySound();
            SavePrefs();
        };
        items.push_back(s);
    }
    const char* helpNames[] = {"Al empezar", "Siempre", "Nunca"};
    const char* helpHints[] = {"Abajo a la izquierda los primeros segundos y en pausa. H la muestra o la esconde.",
                               "Siempre abajo a la izquierda. H la esconde.", "No aparece sola. H la muestra."};
    choice("EN PANTALLA", "Ayuda de teclas", helpHints[(int)helpMode], helpNames[(int)helpMode], [this](int d) {
        helpMode = (HelpMode)(((int)helpMode + d + 3) % 3);
        helpPinned = -1;
        SavePrefs();
    });
    toggle("EN PANTALLA", "Datos técnicos", "Arriba a la izquierda: velocidad, motor, suspensión, agarre y cuadros por segundo. También con T.",
           techData, [this] {
               techData = !techData;
               SavePrefs();
           });
    choice("MANEJO", "Caja", prefAutoShift ? "Pasa los cambios sola. También con F3 (X en el joystick)." : "Los cambios con Q y E (LB y RB). También con F3 (X).",
           prefAutoShift ? "Automática" : "Manual", [this](int) {
               prefAutoShift = !prefAutoShift;
               if (PlayerDriving()) bike.engine.autoShift = prefAutoShift;
               SavePrefs();
           });
    toggle("MANEJO", "Control de tracción", "Afloja el gas si la rueda de atrás patina de más; sin él, derrapa más. También con F6.", prefTraction, [this] {
        prefTraction = !prefTraction;
        if (PlayerDriving()) bike.tractionControl = prefTraction;
        SavePrefs();
    });
    return items;
}

void Game::OpenMenu(Menu m)
{
    menu = m;
    menuSel = 0;
    menuTime = 0.0f;
    menuSelY = -1.0f;
    while (GetCharPressed() > 0) {}                  // que no se cuelen teclas de la pantalla anterior
}

void Game::BackToMain(const std::string& key)
{
    OpenMenu(Menu::Main);
    const std::vector<MenuItem> items = MenuItems();
    for (size_t k = 0; k < items.size(); ++k)
        if (items[k].key == key) menuSel = (int)k;
}

void Game::LeaveSession()
{
    mp.Leave(*physics);
    lastStatus = mp.Status();
    Respawn(track.startLine - 6.0f);
    lapStart = -1.0f;
    OpenMenu(Menu::Main);
}

void Game::HandleMenuKeys()
{
    const bool pad = IsGamepadAvailable(0);
    // El stick izquierdo hace de cruz: un paso al moverlo y, si se lo mantiene, uno cada 0.12 s desde los 0.4 s.
    // Las flechas también repiten al mantenerlas.
    auto stick = [&](Stick& st, int axis) {
        const float v = pad ? GetGamepadAxisMovement(0, axis) : 0.0f;
        const int d = v > 0.6f ? 1 : (v < -0.6f ? -1 : 0);
        if (d != st.dir) {
            st.dir = d;
            st.held = 0.0f;
            return d;
        }
        if (d == 0) return 0;
        st.held += GetFrameTime();
        if (st.held < 0.4f) return 0;
        st.held -= 0.12f;
        return d;
    };
    const int sy = stick(stickY, GAMEPAD_AXIS_LEFT_Y), sx = stick(stickX, GAMEPAD_AXIS_LEFT_X);
    auto key = [](int k) { return IsKeyPressed(k) || IsKeyPressedRepeat(k); };
    const bool up = key(KEY_UP) || (pad && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_UP)) || sy < 0;
    const bool down = key(KEY_DOWN) || (pad && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN)) || sy > 0;
    const bool left = key(KEY_LEFT) || (pad && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT)) || sx < 0;
    const bool right = key(KEY_RIGHT) || (pad && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) || sx > 0;
    const bool altDown = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    const bool enter = ((IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) && !altDown) || (pad && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN));
    // Esc, B, Start o el botón derecho del mouse (sin teclado no había cómo salir de Ajustes o Controles): vuelve
    // (en el principal, a la carrera).
    const bool back = IsKeyPressed(KEY_ESCAPE) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) ||
                      (pad && (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT)));
    const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    const bool erase = IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE);
    auto closeMenu = [&] {
        menu = Menu::None;
        raced = true;
    };
    // Mouse: pasar por encima marca, click acepta (sobre la marcada).
    auto mousePick = [&](int n) {
        const Vector2 mouse = GetMousePosition();
        const bool moved = Vector2Distance(mouse, lastMouse) > 0.5f;
        lastMouse = mouse;
        bool clicked = false;
        for (int k = 0; k < (int)menuRects.size() && k < n; ++k)
            if (CheckCollisionPointRec(mouse, menuRects[k])) {
                if (moved) menuSel = k;
                clicked = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && menuSel == k;
            }
        return clicked;
    };

    switch (menu) {
    case Menu::Main: {
        const std::vector<MenuItem> items = MenuItems();
        const int n = (int)items.size();
        if (up || IsKeyPressed(KEY_W)) menuSel = (menuSel + n - 1) % n;
        if (down || IsKeyPressed(KEY_S)) menuSel = (menuSel + 1) % n;
        for (int k = 0; k < n && k < 9; ++k)             // atajos: 1, 2, 3...
            if (IsKeyPressed(KEY_ONE + k) || IsKeyPressed(KEY_KP_1 + k)) {
                menuSel = k;
                items[k].action();
                if (menu == Menu::None) raced = true;
                return;
            }
        const bool clicked = mousePick(n);
        if (enter || IsKeyPressed(KEY_SPACE) || clicked) {
            items[menuSel % n].action();
            if (menu == Menu::None) raced = true;
        } else if (back) {
            closeMenu();
        }
        break;
    }
    case Menu::Settings: {
        const std::vector<Setting> items = SettingsItems();
        const int n = (int)items.size();
        if (up || IsKeyPressed(KEY_W)) menuSel = (menuSel + n - 1) % n;
        if (down || IsKeyPressed(KEY_S)) menuSel = (menuSel + 1) % n;
        const bool clicked = mousePick(n);
        const Setting& s = items[menuSel % n];
        const Vector2 mouse = GetMousePosition();
        if (left || IsKeyPressed(KEY_A)) {
            s.change(-1);
        } else if (right || IsKeyPressed(KEY_D)) {
            s.change(1);
        } else if (enter || IsKeyPressed(KEY_SPACE)) {
            s.change(0);
        } else if (clicked && s.kind == Setting::Kind::Level && settingArrows.size() == 3) {
            // El nivel: ◀ baja, ▶ sube, sobre la barra va a ese punto; en el resto de la fila no hace nada.
            if (CheckCollisionPointRec(mouse, settingArrows[0])) s.change(-1);
            else if (CheckCollisionPointRec(mouse, settingArrows[1])) s.change(1);
            else if (CheckCollisionPointRec(mouse, settingArrows[2]) && settingArrows[2].width > 0.0f) {
                const int target = (int)std::lround((mouse.x - settingArrows[2].x) / settingArrows[2].width * (float)s.levels);
                if (std::clamp(target, 0, s.levels) != s.level) s.change(std::clamp(target, 0, s.levels) - s.level);
            }
        } else if (clicked) {
            s.change(0);
        } else if (back) {
            BackToMain("ajustes");
        }
        break;
    }
    case Menu::Controls:
        if (back || enter) BackToMain("controles");
        break;
    case Menu::Join: {
        // Letras y números (el guion lo pone solo), Backspace, Ctrl+V pega.
        auto add = [&](int c) {
            if (codeInput.size() < 10 && c > 0 && c < 128 && std::isalnum(c)) codeInput += (char)std::toupper(c);
        };
        for (int c = GetCharPressed(); c > 0; c = GetCharPressed()) add(c);
        if (ctrl && IsKeyPressed(KEY_V))
            if (const char* clip = GetClipboardText())
                for (const char* p = clip; *p; ++p) add((unsigned char)*p);
        if (erase && !codeInput.empty()) codeInput.pop_back();
        if (enter) {
            StartJoin(codeInput);
            if (mp.Active()) OpenMenu(Menu::Lobby);
        } else if (back) {
            BackToMain("unirse");
        }
        break;
    }
    case Menu::Name: {
        for (int c = GetCharPressed(); c > 0; c = GetCharPressed()) {
            int bytes = 0;
            const char* utf8 = CodepointToUTF8(c, &bytes);
            if (c >= 32 && nameInput.size() + (size_t)bytes <= 16) nameInput.append(utf8, (size_t)bytes);
        }
        if (erase && !nameInput.empty()) {
            size_t n = nameInput.size() - 1;                 // una letra entera (puede ocupar varios bytes)
            while (n > 0 && ((unsigned char)nameInput[n] & 0xC0) == 0x80) --n;
            nameInput.resize(n);
        }
        if (enter) {
            if (!nameInput.empty()) {
                playerName = nameInput;
                if (opt.name.empty()) {                  // se recuerda (con --name, sólo por esta vez)
                    savedName = playerName;
                    SavePrefs();
                }
            }
            BackToMain("nombre");
        } else if (back) {
            BackToMain("nombre");
        }
        break;
    }
    case Menu::Bikes: {
        const int n = std::max(1, (int)mods.Bikes().size());
        if (up || IsKeyPressed(KEY_W)) menuSel = (menuSel + n - 1) % n;
        if (down || IsKeyPressed(KEY_S)) menuSel = (menuSel + 1) % n;
        const bool clicked = mousePick(n);
        const float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) menuSel = std::clamp(menuSel - (wheel > 0.0f ? 1 : -1), 0, n - 1);
        if (enter || IsKeyPressed(KEY_SPACE) || clicked) {
            // Elegir la del mapa vuelve a "cada mapa con su moto".
            const std::string id = mods.Bikes().empty() ? std::string() : mods.Bikes()[menuSel % n].id;
            ChooseBike(id == current.bike ? std::string() : id);
            BackToMain("moto");
        } else if (back) {
            BackToMain("moto");
        }
        break;
    }
    case Menu::Maps: {
        const int n = std::max(1, (int)mods.Maps().size());
        if (up || left || IsKeyPressed(KEY_W) || IsKeyPressed(KEY_A)) menuSel = (menuSel + n - 1) % n;
        if (down || right || IsKeyPressed(KEY_S) || IsKeyPressed(KEY_D)) menuSel = (menuSel + 1) % n;
        const bool clicked = mousePick(n);
        const float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) menuSel = std::clamp(menuSel - (wheel > 0.0f ? 1 : -1), 0, n - 1);
        if ((enter || IsKeyPressed(KEY_SPACE) || clicked) && !mods.Maps().empty()) {
            const int chosen = menuSel % n;
            BackToMain("mapa");
            if (chosen != CurrentMapIndex()) {
                mapRequested = chosen;
                mapLoadIn = 2;                           // (LoadMap le avisa a la partida)
            }
        } else if (back) {
            BackToMain("mapa");
        }
        break;
    }
    case Menu::Lobby:
        if ((IsKeyPressed(KEY_C) || IsKeyPressed(KEY_F10)) && mp.GetMode() == Multiplayer::Mode::Host) {
            SetClipboardText(mp.InviteCode().c_str());
            ShowMessage("Código copiado: " + mp.InviteCode());
            copiedTime = 2.0f;
        }
        if (enter) {
            if (mp.Active()) closeMenu();
            else {                                       // se cortó o no se encontró: reintentar
                StartJoin(codeInput);
                OpenMenu(mp.Active() ? Menu::Lobby : Menu::Join);
            }
        } else if (back) {
            BackToMain(mp.Active() ? "partida" : "unirse");
        }
        break;
    case Menu::None: break;
    }
}

void Game::DrawMenu()
{
    const float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    const float dt = GetFrameTime();
    menuTime += dt;
    copiedTime = std::max(0.0f, copiedTime - dt);
    const float fade = mu::Smoothstep(0.0f, 0.35f, menuTime);
    auto alpha = [&](Color c, float a) { return Color{c.r, c.g, c.b, (unsigned char)(c.a * mu::Clamp(a, 0.0f, 1.0f))}; };
    auto text = [&](const Font& f, const std::string& s, float x, float y, float size, Color c, float spacing = 0.0f) {
        DrawTextEx(f, s.c_str(), {x, y}, size, spacing, c);
    };
    auto width = [&](const Font& f, const std::string& s, float size, float spacing = 0.0f) { return MeasureTextEx(f, s.c_str(), size, spacing).x; };

    // La carrera de fondo, oscurecida a la izquierda donde va el menú.
    DrawRectangleGradientH(0, 0, (int)(W * 0.62f), (int)H, alpha(Color{7, 9, 13, 240}, fade), alpha(Color{7, 9, 13, 0}, fade));
    DrawRectangle(0, 0, (int)W, (int)H, alpha(Color{0, 0, 0, 70}, fade));
    const float slide = (1.0f - fade) * -36.0f;
    const float x = std::max(40.0f, W * 0.07f) + slide;
    const float scale = mu::Clamp(H / 900.0f, 0.75f, 1.4f);

    // Abajo también se oscurece un poco (el pie y las tarjetas se leen sobre cualquier fondo).
    DrawRectangleGradientV(0, (int)(H * 0.72f), (int)W, (int)(H * 0.28f), alpha(Color{7, 9, 13, 0}, fade), alpha(Color{7, 9, 13, 170}, fade));

    // Título, con sombra.
    const float titleSize = 118.0f * scale, titleY = H * 0.10f;
    const float motoW = width(font, "MOTO", titleSize, 2.0f);
    text(font, "MOTO", x + 3.0f * scale, titleY + 4.0f * scale, titleSize, alpha(Color{0, 0, 0, 120}, fade), 2.0f);
    text(font, "SIM", x + motoW + 4.0f + 3.0f * scale, titleY + 4.0f * scale, titleSize, alpha(Color{0, 0, 0, 120}, fade), 2.0f);
    text(font, "MOTO", x, titleY, titleSize, alpha(kText, fade), 2.0f);
    text(font, "SIM", x + motoW + 4.0f, titleY, titleSize, alpha(kAccent, fade), 2.0f);
    const float barW = 96.0f * scale * (0.85f + 0.15f * std::sin((float)GetTime() * 1.6f));   // la raya respira apenas
    DrawRectangle((int)x, (int)(titleY + titleSize * 0.98f), (int)barW, (int)(5.0f * scale), alpha(kAccent, fade));
    text(font, std::string("motos con física de verdad  ·  ") + MOTOSIM_VERSION, x + 110.0f * scale, titleY + titleSize * 0.9f, 22.0f * scale,
         alpha(kTextDim, fade));

    float y = H * 0.40f;
    auto footer = [&](const std::string& s) { text(font, s, x, H - 58.0f * scale, 20.0f * scale, alpha(kTextDim, fade)); };
    auto status = [&](float atY) {
        if (!mp.Status().empty() && !mp.Connected()) text(font, mp.Status(), x, atY, 21.0f * scale, alpha(Color{255, 186, 110, 255}, fade));
    };
    // Casilleros de un código de 10 letras (5-5); cursor: dónde va la próxima letra (-1 = ninguno).
    auto codeCells = [&](const std::string& code, float cy, int cursor) {
        const float cw = 54.0f * scale, ch = 72.0f * scale, gap = 10.0f * scale, dash = 30.0f * scale;
        const bool blink = std::fmod(GetTime(), 1.0) < 0.55;
        for (int k = 0; k < 10; ++k) {
            const float cx = x + k * (cw + gap) + (k >= 5 ? dash : 0.0f);
            const bool active = k == cursor;
            DrawRectangle((int)cx, (int)cy, (int)cw, (int)ch, alpha(Color{255, 255, 255, (unsigned char)(active ? 34 : 18)}, fade));
            DrawRectangle((int)cx, (int)(cy + ch - 4.0f * scale), (int)cw, (int)(4.0f * scale), alpha(active && blink ? kAccent : Color{255, 255, 255, 60}, fade));
            if (k < (int)code.size()) {
                const std::string c(1, code[k]);
                text(mono, c, cx + (cw - width(mono, c, 50.0f * scale)) * 0.5f, cy + 8.0f * scale, 50.0f * scale, alpha(kText, fade));
            }
        }
        text(mono, "-", x + 5 * (cw + gap) + dash * 0.5f - 12.0f * scale, cy + 8.0f * scale, 50.0f * scale, alpha(kTextDim, fade));
        return cy + ch;
    };

    switch (menu) {
    case Menu::Main: {
        const std::vector<MenuItem> items = MenuItems();
        menuSel = std::min(menuSel, (int)items.size() - 1);
        // Las opciones entran entre el título y el pie en cualquier pantalla; la marcada va sobre una
        // franja que se desliza de una a otra.
        y = H * 0.34f;
        const float itemH = std::min(56.0f * scale, (H - 160.0f * scale - y) / (float)items.size());
        const float itemW = std::min(480.0f * scale, W * 0.42f);
        const float targetY = y + menuSel * itemH;
        menuSelY = menuSelY < 0.0f ? targetY : menuSelY + (targetY - menuSelY) * (1.0f - std::exp(-18.0f * dt));
        DrawRectangleRounded({x - 22.0f * scale, menuSelY + 3.0f * scale, itemW, itemH - 6.0f * scale}, 0.4f, 8, alpha(Color{255, 255, 255, 24}, fade));
        DrawRectangle((int)(x - 22.0f * scale), (int)(menuSelY + 9.0f * scale), (int)(5.0f * scale), (int)(itemH - 18.0f * scale), alpha(kAccent, fade));
        menuRects.clear();
        for (int k = 0; k < (int)items.size(); ++k) {
            const bool sel = k == menuSel;
            const float size = itemH * (sel ? 0.64f : 0.56f);
            const float ix = x + (sel ? 10.0f * scale : 0.0f), iy = y + k * itemH + (itemH - size) * 0.5f;
            const float appear = mu::Smoothstep(0.05f * k, 0.05f * k + 0.3f, menuTime);   // entran de a una
            text(font, items[k].label, ix + (1.0f - appear) * -24.0f, iy, size, alpha(sel ? kText : Color{210, 214, 222, 150}, appear * fade));
            menuRects.push_back({x - 30.0f, y + k * itemH, itemW + 8.0f, itemH});
        }
        y += items.size() * itemH + 14.0f * scale;
        if (!items[menuSel].hint.empty()) text(font, items[menuSel].hint, x, y, 20.0f * scale, alpha(kTextDim, fade));
        status(y + 32.0f * scale);

        // Tarjeta de lo que se corre ahora: mapa (con su color), moto con sus números y la partida en red.
        {
            const float cw = std::min(440.0f * scale, W * 0.36f), ch = 150.0f * scale;
            const float cx = W - cw - std::max(40.0f, W * 0.05f), cy = H - 96.0f * scale - ch;
            const float appear = mu::Smoothstep(0.25f, 0.6f, menuTime);
            const Color mapColor = current.hasColor ? Color{current.color[0], current.color[1], current.color[2], 255} : kAccent;
            DrawRectangleRounded({cx, cy, cw, ch}, 0.08f, 6, alpha(Color{10, 12, 16, 180}, fade * appear));
            DrawRectangle((int)cx, (int)(cy + 16.0f * scale), (int)(5.0f * scale), (int)(ch - 32.0f * scale), alpha(mapColor, fade * appear));
            const float tx = cx + 24.0f * scale;
            text(font, "AHORA", tx, cy + 14.0f * scale, 15.0f * scale, alpha(kTextDim, fade * appear), 2.0f);
            text(font, current.name, tx, cy + 34.0f * scale, 30.0f * scale, alpha(kText, fade * appear));
            text(font, current.kind, tx, cy + 70.0f * scale, 17.0f * scale, alpha(mapColor, fade * appear));
            std::string bikeLine = bikeName;
            if (BikeData* d = DataForBike(bikeId)) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "   ·   %.0f hp  ·  %.0f kg  ·  %.0f km/h", d->stats.hp, d->stats.kg, d->stats.topKmh);
                bikeLine += buf;
            }
            text(font, bikeLine, tx, cy + 96.0f * scale, 18.0f * scale, alpha(Color{200, 204, 210, 255}, fade * appear));
            if (mp.Active()) {
                int riders = 1;
                for (int id = 0; id < Multiplayer::kMaxPlayers; ++id) riders += mp.Remotes()[id].active ? 1 : 0;
                text(font, "En red  ·  " + std::to_string(riders) + (riders == 1 ? " piloto" : " pilotos"), tx, cy + 122.0f * scale, 17.0f * scale,
                     alpha(kTextDim, fade * appear));
            }
        }
        footer("\xE2\x86\x91 \xE2\x86\x93  elegir      Enter  aceptar      Esc  volver      (joystick: stick o cruz, A y B)");
        break;
    }
    case Menu::Join: {
        text(font, "Unirse a una partida", x, y - 24.0f * scale, 44.0f * scale, alpha(kText, fade));
        text(font, "Escribí o pegá (Ctrl+V) el código que te pasó el que creó la partida.", x, y + 34.0f * scale, 21.0f * scale, alpha(kTextDim, fade));
        const float bottom = codeCells(codeInput, y + 84.0f * scale, (int)codeInput.size() < 10 ? (int)codeInput.size() : -1);
        text(font, "Tienen que estar en la misma red (mismo wifi o router).", x, bottom + 22.0f * scale, 20.0f * scale, alpha(kTextDim, fade));
        status(bottom + 56.0f * scale);
        footer("Enter  conectar      Backspace  borrar      Esc  volver");
        break;
    }
    case Menu::Name: {
        text(font, "Tu nombre", x, y - 24.0f * scale, 44.0f * scale, alpha(kText, fade));
        text(font, "Así te ven los demás, arriba de tu moto.", x, y + 34.0f * scale, 21.0f * scale, alpha(kTextDim, fade));
        const float bw = 520.0f * scale, bh = 72.0f * scale, by = y + 84.0f * scale;
        DrawRectangle((int)x, (int)by, (int)bw, (int)bh, alpha(Color{255, 255, 255, 24}, fade));
        DrawRectangle((int)x, (int)(by + bh - 4.0f * scale), (int)bw, (int)(4.0f * scale), alpha(kAccent, fade));
        const bool blink = std::fmod(GetTime(), 1.0) < 0.55;
        text(font, nameInput + (blink ? "_" : ""), x + 18.0f * scale, by + 14.0f * scale, 42.0f * scale, alpha(kText, fade));
        footer("Enter  guardar      Esc  cancelar");
        break;
    }
    case Menu::Lobby: {
        const bool host = mp.GetMode() == Multiplayer::Mode::Host;
        text(font, "Partida en red", x, y - 24.0f * scale, 44.0f * scale, alpha(kText, fade));
        float ly = y + 34.0f * scale;
        if (host) {
            text(font, copiedTime > 0.0f ? "¡Copiado! Pegalo en WhatsApp o donde quieras." : "Pasales este código a tus amigos (C lo copia):", x, ly, 21.0f * scale,
                 alpha(copiedTime > 0.0f ? Color{140, 230, 140, 255} : kTextDim, fade));
            std::string plain;
            for (char c : mp.InviteCode())
                if (c != '-') plain += c;
            ly = codeCells(plain, ly + 36.0f * scale, -1) + 16.0f * scale;
            for (const std::string& other : mp.OtherCodes()) {
                text(font, "otra red: " + other, x, ly, 18.0f * scale, alpha(kTextDim, fade));
                ly += 22.0f * scale;
            }
        } else if (!mp.Active()) {
            text(font, mp.Status(), x, ly, 22.0f * scale, alpha(Color{255, 186, 110, 255}, fade));
            ly += 40.0f * scale;
        } else if (!mp.Connected()) {
            const int dots = (int)(GetTime() * 3.0) % 4;
            text(font, "Buscando la partida" + std::string((size_t)dots, '.'), x, ly, 24.0f * scale, alpha(kText, fade));
            text(mono, mp.InviteCode(), x, ly + 34.0f * scale, 24.0f * scale, alpha(kTextDim, fade));
            ly += 76.0f * scale;
        } else {
            text(font, "Conectado  ·  " + std::to_string((int)(mp.PingMs() + 0.5f)) + " ms", x, ly, 22.0f * scale, alpha(Color{140, 230, 140, 255}, fade));
            ly += 40.0f * scale;
        }
        if (mp.Active()) {
            const auto roster = mp.Roster(playerName);
            ly += 16.0f * scale;
            text(font, "Pilotos  " + std::to_string(roster.size()) + " / " + std::to_string(Multiplayer::kMaxPlayers), x, ly, 22.0f * scale, alpha(kTextDim, fade));
            ly += 34.0f * scale;
            for (const Multiplayer::Entry& e : roster) {
                DrawRectangle((int)x, (int)(ly + 6.0f * scale), (int)(8.0f * scale), (int)(28.0f * scale), alpha(Livery(e.id).plastic, fade));
                text(font, e.name, x + 22.0f * scale, ly, 32.0f * scale, alpha(kText, fade));
                const std::string right = e.ping < 0 ? std::string("vos") : std::to_string(e.ping) + " ms";
                text(font, right, x + 22.0f * scale + width(font, e.name, 32.0f * scale) + 16.0f * scale, ly + 8.0f * scale, 20.0f * scale, alpha(kTextDim, fade));
                ly += 42.0f * scale;
            }
            if (roster.size() < 2) text(font, host ? "Esperando que se sume alguien..." : "", x, ly + 4.0f * scale, 20.0f * scale, alpha(kTextDim, fade));
        }
        footer(mp.Active() ? "Enter  correr      Esc  menú" : "Enter  reintentar      Esc  menú");
        break;
    }
    case Menu::Bikes: {
        // A la izquierda las motos (al abrir, marcada la que se usa), abajo a la derecha los números de la
        // marcada, con barras relativas a la mejor de todas en cada cosa.
        const std::vector<BikeDef>& bikes = mods.Bikes();
        const int n = std::max(1, (int)bikes.size());
        y = std::min(y, H * 0.31f);
        text(font, "Elegí la moto", x, y - 24.0f * scale, 44.0f * scale, alpha(kText, fade));
        text(font, mp.Active() ? "En la partida cada uno corre con la suya (los demás tienen que tenerla en su carpeta mods/)."
                               : "Sirve en cualquier mapa. Las motos son archivos de la carpeta mods/ (ver MODDING.md).",
             x, y + 34.0f * scale, 21.0f * scale, alpha(kTextDim, fade));
        auto idOf = [&](int k) { return bikes.empty() ? current.bike : bikes[k].id; };
        BikeStats top{};                                 // lo mejor de cada número (para las barras)
        top.zeroTo100 = top.turnRadius = 1e9f;
        for (int k = 0; k < (int)bikes.size(); ++k)
            if (BikeData* d = DataForBike(bikes[k].id)) {
                const BikeStats& b = d->stats;
                top.hp = std::max(top.hp, b.hp);
                top.kg = std::max(top.kg, b.kg);
                top.topKmh = std::max(top.topKmh, b.topKmh);
                top.zeroTo100 = std::min(top.zeroTo100, b.zeroTo100);
                top.latG = std::max(top.latG, b.latG);
                top.travelMm = std::max(top.travelMm, b.travelMm);
                top.turnRadius = std::min(top.turnRadius, b.turnRadius);
            }
        menuRects.clear();
        const float rowH = 58.0f * scale, gap = 8.0f * scale, listW = std::min(470.0f * scale, W * 0.36f);
        const float top0 = y + 84.0f * scale;
        const int visible = std::max(1, (int)((H - 110.0f * scale - top0 + gap) / (rowH + gap)));
        mapScroll = std::clamp(mapScroll, std::max(0, menuSel - visible + 1), std::min(menuSel, std::max(0, n - visible)));
        float ry = top0;
        for (int k = 0; k < n; ++k) {
            if (k < mapScroll || k >= mapScroll + visible) {
                menuRects.push_back({0, 0, 0, 0});
                continue;
            }
            const BikeDef* bd = mods.Bike(idOf(k));
            const bool sel = k == menuSel, now = idOf(k) == bikeId;
            const float appear = mu::Smoothstep(0.05f * (k - mapScroll), 0.05f * (k - mapScroll) + 0.3f, menuTime);
            const float rx = x + (1.0f - appear) * -30.0f;
            DrawRectangle((int)rx, (int)ry, (int)listW, (int)rowH, alpha(Color{255, 255, 255, (unsigned char)(sel ? 30 : 14)}, fade * appear));
            DrawRectangle((int)rx, (int)ry, (int)(6.0f * scale), (int)rowH, alpha(sel ? kAccent : Color{255, 255, 255, 50}, fade * appear));
            const std::string name = bd ? bd->name : idOf(k);
            const std::string sub = bd ? StyleLabel(bd->style) + (bd->id.rfind("base/", 0) == 0 ? "" : "  ·  mod") : "";
            text(font, name, rx + 22.0f * scale, ry + 7.0f * scale, 26.0f * scale, alpha(sel ? kText : Color{210, 214, 222, 200}, fade * appear));
            text(font, sub, rx + 22.0f * scale, ry + 35.0f * scale, 17.0f * scale, alpha(kTextDim, fade * appear));
            if (now) {                                   // la que se está usando
                const float tw = width(font, "EN USO", 14.0f * scale, 1.5f);
                text(font, "EN USO", rx + listW - tw - 18.0f * scale, ry + 12.0f * scale, 14.0f * scale, alpha(kAccent, fade * appear), 1.5f);
            }
            menuRects.push_back({rx, ry, listW, rowH});
            ry += rowH + gap;
        }
        // Números de la marcada.
        if (BikeData* d = DataForBike(idOf(menuSel % n))) {
            const BikeStats& b = d->stats;
            const BikeDef* bd = mods.Bike(idOf(menuSel % n));
            const float pw = std::min(560.0f * scale, W * 0.42f), ph = 330.0f * scale;
            const float px = W - pw - std::max(40.0f, W * 0.05f);
            float py = H - 96.0f * scale - ph;
            DrawRectangleRounded({px, py, pw, ph}, 0.05f, 6, alpha(Color{10, 12, 16, 190}, fade));
            DrawRectangle((int)px, (int)(py + 18.0f * scale), (int)(5.0f * scale), (int)(40.0f * scale), alpha(kAccent, fade));
            text(font, bd ? bd->name : idOf(menuSel % n), px + 24.0f * scale, py + 14.0f * scale, 32.0f * scale, alpha(kText, fade));
            if (bd && !bd->description.empty()) text(font, bd->description, px + 24.0f * scale, py + 52.0f * scale, 17.0f * scale, alpha(kTextDim, fade));
            py += 88.0f * scale;
            auto stat = [&](const char* label, const std::string& value, float fill) {
                text(font, label, px + 24.0f * scale, py, 19.0f * scale, alpha(kTextDim, fade));
                const float bx = px + 210.0f * scale, bw = pw - 310.0f * scale, bh = 8.0f * scale;
                DrawRectangleRounded({bx, py + 8.0f * scale, bw, bh}, 1.0f, 4, alpha(Color{255, 255, 255, 34}, fade));
                DrawRectangleRounded({bx, py + 8.0f * scale, bw * mu::Clamp(fill, 0.03f, 1.0f), bh}, 1.0f, 4, alpha(kAccent, fade));
                text(font, value, bx + bw + 14.0f * scale, py, 19.0f * scale, alpha(kText, fade));
                py += 32.0f * scale;
            };
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%.0f hp", b.hp);
            stat("Potencia", buf, b.hp / std::max(top.hp, 1.0f));
            std::snprintf(buf, sizeof(buf), "%.0f kg", b.kg);
            stat("Peso (sin piloto)", buf, b.kg / std::max(top.kg, 1.0f));
            std::snprintf(buf, sizeof(buf), "%.0f km/h", b.topKmh);
            stat("Velocidad máxima", buf, b.topKmh / std::max(top.topKmh, 1.0f));
            std::snprintf(buf, sizeof(buf), b.zeroTo100 < 90.0f ? "%.1f s" : "-", b.zeroTo100);
            stat("0 a 100 km/h", buf, top.zeroTo100 / std::max(b.zeroTo100, 0.1f));
            std::snprintf(buf, sizeof(buf), "%.2f g", b.latG);
            stat("Agarre en curva", buf, b.latG / std::max(top.latG, 0.1f));
            std::snprintf(buf, sizeof(buf), "%.0f mm", b.travelMm);
            stat("Suspensión", buf, b.travelMm / std::max(top.travelMm, 1.0f));
            std::snprintf(buf, sizeof(buf), "%.1f m", b.turnRadius);
            stat("Giro más cerrado", buf, top.turnRadius / std::max(b.turnRadius, 0.1f));
        }
        footer("\xE2\x86\x91 \xE2\x86\x93  elegir      Enter  correr con esa      Esc  volver");
        break;
    }
    case Menu::Maps: {
        if (mods.Maps().size() > 2) y = H * 0.31f;               // con muchos mapas la lista empieza más arriba (bajo el título)
        text(font, "Elegí el mapa", x, y - 24.0f * scale, 44.0f * scale, alpha(kText, fade));
        text(font,
             mp.GetMode() == Multiplayer::Mode::Host ? "Los que están en tu partida vienen con vos (y tienen que tener el mapa en su carpeta mods/)."
                                                     : "Cada mapa se corre con su moto. Los mapas son archivos de la carpeta mods/ (ver MODDING.md).",
             x,
             y + 34.0f * scale, 21.0f * scale, alpha(kTextDim, fade));
        menuRects.clear();
        const std::vector<MapDef>& maps = mods.Maps();
        const int n = (int)maps.size();
        float cw = 460.0f * scale;                           // lo que ocupe el texto más largo (sin pasar de media pantalla y algo)
        for (const MapDef& m : maps) cw = std::max(cw, width(font, m.description, 19.0f * scale) + 60.0f * scale);
        cw = std::min(cw, W * 0.62f);
        // Con más de dos mapas, tarjetas compactas (la moto va en la línea del tipo de mapa).
        const bool compact = n > 2;
        const float ch = (compact ? 112.0f : 176.0f) * scale, gap = (compact ? 10.0f : 18.0f) * scale;
        const float top = y + 84.0f * scale, bottom = H - 104.0f * scale;
        // Los que entran; con más mapas la lista se corre para que el elegido siempre se vea.
        const int visible = std::max(1, (int)((bottom - top + gap) / (ch + gap)));
        mapScroll = std::clamp(mapScroll, std::max(0, menuSel - visible + 1), std::min(menuSel, std::max(0, n - visible)));
        const int nowIdx = CurrentMapIndex();
        float cy = top;
        for (int k = 0; k < n; ++k) {
            if (k < mapScroll || k >= mapScroll + visible) {
                menuRects.push_back({0, 0, 0, 0});
                continue;
            }
            const MapDef& mi = maps[k];
            const BikeDef* bd = mods.Bike(mi.bike);
            const bool sel = k == menuSel;
            const float appear = mu::Smoothstep(0.06f * (k - mapScroll), 0.06f * (k - mapScroll) + 0.3f, menuTime);
            const float cx0 = x + (1.0f - appear) * -30.0f;
            DrawRectangle((int)cx0, (int)cy, (int)cw, (int)ch, alpha(Color{255, 255, 255, (unsigned char)(sel ? 30 : 14)}, fade * appear));
            DrawRectangle((int)cx0, (int)cy, (int)(6.0f * scale), (int)ch, alpha(sel ? kAccent : Color{255, 255, 255, 50}, fade * appear));
            const Color accent = mi.hasColor ? Color{mi.color[0], mi.color[1], mi.color[2], 255} : kAccent;
            const std::string kind = mi.kind + (mi.mod != "base" ? "   ·   mod: " + mi.mod : std::string());
            const std::string bikeLine = "Moto: " + (bd ? bd->name : mi.bike) + (k == nowIdx ? "     ·  ahora" : "");
            const Color nameColor = sel ? kText : Color{210, 214, 222, 200};
            if (compact) {
                text(font, mi.name, cx0 + 26.0f * scale, cy + 10.0f * scale, 32.0f * scale, alpha(nameColor, fade * appear));
                text(font, kind, cx0 + 26.0f * scale, cy + 50.0f * scale, 18.0f * scale, alpha(accent, fade * appear));
                text(font, bikeLine, cx0 + 26.0f * scale + width(font, kind, 18.0f * scale) + 28.0f * scale, cy + 50.0f * scale, 18.0f * scale,
                     alpha(Color{200, 204, 210, 255}, fade * appear));
                text(font, mi.description, cx0 + 26.0f * scale, cy + 78.0f * scale, 17.0f * scale, alpha(kTextDim, fade * appear));
            } else {
                text(font, mi.name, cx0 + 26.0f * scale, cy + 16.0f * scale, 40.0f * scale, alpha(nameColor, fade * appear));
                text(font, kind, cx0 + 26.0f * scale, cy + 62.0f * scale, 20.0f * scale, alpha(accent, fade * appear));
                text(font, mi.description, cx0 + 26.0f * scale, cy + 94.0f * scale, 19.0f * scale, alpha(kTextDim, fade * appear));
                text(font, bikeLine, cx0 + 26.0f * scale, cy + 128.0f * scale, 19.0f * scale, alpha(Color{200, 204, 210, 255}, fade * appear));
            }
            menuRects.push_back({cx0, cy, cw, ch});
            cy += ch + gap;
        }
        if (mapScroll > 0) text(font, "\xE2\x86\x91  " + std::to_string(mapScroll) + " más", x + cw + 20.0f * scale, top, 20.0f * scale, alpha(kTextDim, fade));
        if (mapScroll + visible < n)
            text(font, "\xE2\x86\x93  " + std::to_string(n - mapScroll - visible) + " más", x + cw + 20.0f * scale, cy - gap - 24.0f * scale, 20.0f * scale,
                 alpha(kTextDim, fade));
        // Errores de los mods (formato de algún archivo): el primero, para poder arreglarlo.
        if (!mods.Errors().empty()) {
            std::string e = mods.Errors().front();
            while (e.size() > 8 && width(font, e, 18.0f * scale) > W - 2.0f * x) e = e.substr(0, e.size() - 8) + "...";
            text(font, std::to_string(mods.Errors().size()) + (mods.Errors().size() == 1 ? " error en mods/:  " : " errores en mods/:  ") + e, x,
                 H - 92.0f * scale, 18.0f * scale, alpha(Color{255, 120, 100, 255}, fade));
        }
        footer("\xE2\x86\x91 \xE2\x86\x93  elegir      Enter  correr ahí      Esc  volver");
        break;
    }
    case Menu::Settings: {
        // Filas agrupadas (IMAGEN, SONIDO...) con el valor a la derecha: llave para sí / no, ◀ opción ▶, y el
        // volumen en una barra. Debajo del título, lo que hace la marcada (no hay un subtítulo fijo: así entra).
        const std::vector<Setting> items = SettingsItems();
        const int n = (int)items.size();
        menuSel = std::clamp(menuSel, 0, n - 1);
        y = std::min(y, H * 0.31f);
        text(font, "Ajustes", x, y - 24.0f * scale, 44.0f * scale, alpha(kText, fade));
        text(font, items[menuSel].hint, x, y + 34.0f * scale, 21.0f * scale, alpha(kTextDim, fade));
        int groups = 0;
        for (int k = 0; k < n; ++k) groups += k == 0 || items[k].group != items[k - 1].group ? 1 : 0;
        const float top = y + 76.0f * scale, headH = 28.0f * scale, gapH = 6.0f * scale;
        const float rowH = std::min(46.0f * scale, (H - 100.0f * scale - top - (float)groups * headH - (float)(groups - 1) * gapH) / (float)n);
        const float rowW = std::min(600.0f * scale, W * 0.46f);
        std::vector<float> rowY((size_t)n);
        float ry = top;
        for (int k = 0; k < n; ++k) {
            if (k == 0 || items[k].group != items[k - 1].group) ry += (k ? gapH : 0.0f) + headH;
            rowY[(size_t)k] = ry;
            ry += rowH;
        }
        const float targetY = rowY[(size_t)menuSel];
        menuSelY = menuSelY < 0.0f ? targetY : menuSelY + (targetY - menuSelY) * (1.0f - std::exp(-18.0f * dt));
        DrawRectangleRounded({x - 22.0f * scale, menuSelY + 2.0f * scale, rowW + 22.0f * scale, rowH - 4.0f * scale}, 0.4f, 8,
                             alpha(Color{255, 255, 255, 24}, fade));
        DrawRectangle((int)(x - 22.0f * scale), (int)(menuSelY + 8.0f * scale), (int)(5.0f * scale), (int)(rowH - 16.0f * scale), alpha(kAccent, fade));
        // Triángulos (◀ ▶): la fuente no los trae.
        auto arrowTri = [&](float cx, float cy, float r, int dir, Color c) {
            if (dir > 0) DrawTriangle({cx - 0.6f * r, cy - r}, {cx - 0.6f * r, cy + r}, {cx + 0.8f * r, cy}, c);
            else DrawTriangle({cx + 0.6f * r, cy - r}, {cx - 0.8f * r, cy}, {cx + 0.6f * r, cy + r}, c);
        };
        menuRects.clear();
        settingArrows.assign(3, Rectangle{0.0f, 0.0f, 0.0f, 0.0f});
        const float right = x + rowW - 14.0f * scale;    // donde terminan los valores
        for (int k = 0; k < n; ++k) {
            const Setting& s = items[(size_t)k];
            const bool sel = k == menuSel;
            const float a = mu::Smoothstep(0.04f * k, 0.04f * k + 0.3f, menuTime) * fade;
            const float cy = rowY[(size_t)k] + rowH * 0.5f;
            if (k == 0 || s.group != items[(size_t)k - 1].group)
                text(font, s.group, x, rowY[(size_t)k] - headH + 7.0f * scale, 14.0f * scale, alpha(kAccent, a), 2.0f);
            const float size = std::min(rowH * 0.58f, 26.0f * scale), vs = size * 0.88f;
            text(font, s.label, x + (sel ? 10.0f * scale : 0.0f), cy - size * 0.56f, size, alpha(sel ? kText : Color{210, 214, 222, 170}, a));
            const Color valueColor = sel ? kText : Color{210, 214, 222, 200};
            const Color arrowColor = sel ? kAccent : Color{255, 255, 255, 70};
            switch (s.kind) {
            case Setting::Kind::Toggle: {
                // Llave: prendida, del color del juego con la bolita a la derecha; apagada, gris a la izquierda.
                const float pw = 44.0f * scale, ph = 22.0f * scale, px = right - pw, py = cy - ph * 0.5f;
                DrawRectangleRounded({px, py, pw, ph}, 1.0f, 12, alpha(s.on ? kAccent : Color{255, 255, 255, 46}, a));
                DrawCircleV({s.on ? px + pw - ph * 0.5f : px + ph * 0.5f, cy}, ph * 0.36f, alpha(s.on ? kText : Color{190, 194, 200, 255}, a));
                text(font, s.value, px - 12.0f * scale - width(font, s.value, vs), cy - vs * 0.56f, vs, alpha(valueColor, a));
                break;
            }
            case Setting::Kind::Choice: {
                const float tri = 6.0f * scale, vw = width(font, s.value, vs);
                arrowTri(right - tri, cy, tri, 1, alpha(arrowColor, a));
                text(font, s.value, right - 2.0f * tri - 10.0f * scale - vw, cy - vs * 0.56f, vs, alpha(valueColor, a));
                arrowTri(right - 2.0f * tri - 20.0f * scale - vw - tri, cy, tri, -1, alpha(arrowColor, a));
                break;
            }
            case Setting::Kind::Level: {
                // ◀ ▮▮▮▮▮▮▮▮▯▯ ▶  80%   (apagado: la barra gris y "Apagado")
                const float tri = 6.0f * scale, valueW = width(font, "Apagado", vs);
                text(font, s.value, right - width(font, s.value, vs), cy - vs * 0.56f, vs, alpha(s.dim ? kTextDim : valueColor, a));
                const float plusX = right - valueW - 16.0f * scale - tri;
                const float barR = plusX - tri - 12.0f * scale, barW = 150.0f * scale, barL = barR - barW, segH = 12.0f * scale;
                const float minusX = barL - 12.0f * scale - tri;
                arrowTri(plusX, cy, tri, 1, alpha(arrowColor, a));
                arrowTri(minusX, cy, tri, -1, alpha(arrowColor, a));
                for (int i = 0; i < s.levels; ++i) {
                    const float sw = barW / (float)s.levels;
                    const bool full = i < s.level;
                    const Color c = !full ? Color{255, 255, 255, 36} : (s.dim ? Color{150, 154, 160, 255} : kAccent);
                    DrawRectangle((int)(barL + sw * (float)i), (int)(cy - segH * 0.5f), (int)(sw - 3.0f * scale), (int)segH, alpha(c, a));
                }
                const float hit = rowH * 0.5f;
                settingArrows[0] = {minusX - hit * 0.7f, cy - hit, hit * 1.4f, hit * 2.0f};
                settingArrows[1] = {plusX - hit * 0.7f, cy - hit, hit * 1.4f, hit * 2.0f};
                settingArrows[2] = {barL, cy - hit, barW, hit * 2.0f};
                break;
            }
            }
            menuRects.push_back({x - 30.0f, rowY[(size_t)k], rowW + 38.0f, rowH});
        }
        footer("\xE2\x86\x91 \xE2\x86\x93  elegir      \xE2\x86\x90 \xE2\x86\x92  cambiar      Enter  cambiar      Esc  volver      (se guardan solos)");
        break;
    }
    case Menu::Controls: {
        // Tres tablas (acción, tecla, botón del joystick) y lo de probar en un párrafo, sobre un panel oscuro (pasa
        // por delante de la moto del fondo).
        y = std::min(y, H * 0.31f);
        text(font, "Controles", x, y - 24.0f * scale, 44.0f * scale, alpha(kText, fade));
        text(font, IsGamepadAvailable(0) ? "Teclado y joystick (hay uno conectado)." : "Teclado y joystick (los botones sirven con cualquier joystick de Xbox o parecido).",
             x, y + 34.0f * scale, 21.0f * scale, alpha(kTextDim, fade));
        struct Row { const char *action, *keys, *pad; };
        static const Row driving[] = {
            {"Acelerar", "W", "RT"},
            {"Frenar (parado: marcha atrás)", "S", "LT"},
            {"Freno de atrás, derrape", "Espacio", "A"},
            {"Doblar", "A  D", "stick izq."},
            {"Cambios", "Q  E", "LB  RB"},
            {"Caja automática o manual", "F3", "X"},
        };
        static const Row rider[] = {
            {"Cuerpo adelante y atrás", "\xE2\x86\x91  \xE2\x86\x93", "stick izq."},
            {"Cuerpo a los costados", "\xE2\x86\x90  \xE2\x86\x92", "stick der."},
            {"Reaparecer", "R", "Y"},
            {"Volver a empezar", "Retroceso", "Back"},   // a la largada, y los objetos sueltos a su lugar
        };
        static const Row game[] = {
            {"Menú, ajustes y controles", "Esc", "Start"},
            {"Pausa", "P", ""},
            {"Mirar alrededor (arrastrando)", "mouse", ""},
            {"Acercar la cámara", "rueda", ""},
            {"Cámara de costado", "C", "clic stick der."},
            {"Ayuda de teclas", "H", ""},
            {"Datos técnicos", "T", ""},
            {"Sonido", "M", ""},
            {"Pantalla completa (o Alt+Enter)", "F11", ""},
            {"Copiar el código de la partida", "F10", ""},
        };
        const float fs = 18.0f * scale, rh = 25.0f * scale, colA = 236.0f * scale, colK = 112.0f * scale, tableW = colA + colK + 118.0f * scale;
        auto table = [&](float tx, float ty, const char* title, const Row* rows, int count) {
            const float a = mu::Smoothstep(0.1f, 0.4f, menuTime) * fade;
            text(font, title, tx, ty, 14.0f * scale, alpha(kAccent, a), 2.0f);
            text(font, "TECLADO", tx + colA, ty, 13.0f * scale, alpha(kTextDim, a), 1.5f);
            text(font, "JOYSTICK", tx + colA + colK, ty, 13.0f * scale, alpha(kTextDim, a), 1.5f);
            ty += 24.0f * scale;
            for (int i = 0; i < count; ++i) {
                if (i % 2 == 0) DrawRectangle((int)(tx - 8.0f * scale), (int)(ty - 2.0f * scale), (int)(tableW + 8.0f * scale), (int)rh, alpha(Color{255, 255, 255, 10}, a));
                text(font, rows[i].action, tx, ty + 1.0f * scale, fs, alpha(Color{214, 218, 224, 255}, a));
                text(font, rows[i].keys, tx + colA, ty + 1.0f * scale, fs, alpha(kText, a));
                text(font, rows[i].pad, tx + colA + colK, ty + 1.0f * scale, fs, alpha(kTextDim, a));
                ty += rh;
            }
            return ty;
        };
        // Para probar (lo de la física y el tuning): en líneas que entran en el ancho de las dos tablas.
        static const char* tools[] = {"F1 vectores físicos", "F2 esconder todo el HUD", "F4 cámara lenta", "F5 releer tuning.ini (Shift+F5: los mods)",
                                      "F6 control de tracción", "F7 efectos de imagen", "F8 surcos", "F9 piloto modelo o generado",
                                      "[ ]  - =  0  centro de masa"};
        const float gapX = 44.0f * scale, maxW = 2.0f * tableW + gapX, ts = 17.0f * scale;
        std::vector<std::string> toolLines(1);
        for (const char* t : tools) {
            const std::string next = toolLines.back().empty() ? std::string(t) : toolLines.back() + "   \xC2\xB7   " + t;
            if (!toolLines.back().empty() && width(font, next, ts) > maxW) toolLines.push_back(t);
            else toolLines.back() = next;
        }
        const int nDriving = (int)(sizeof(driving) / sizeof(driving[0])), nRider = (int)(sizeof(rider) / sizeof(rider[0]));
        const int nGame = (int)(sizeof(game) / sizeof(game[0]));
        const float top = y + 84.0f * scale;
        const float tablesH = std::max(48.0f * scale + 16.0f * scale + (float)(nDriving + nRider) * rh, 24.0f * scale + (float)nGame * rh);
        const float panelH = tablesH + 18.0f * scale + 24.0f * scale + 23.0f * scale * (float)toolLines.size() + 26.0f * scale;
        DrawRectangleRounded({x - 24.0f * scale, top - 16.0f * scale, maxW + 48.0f * scale, panelH}, 0.04f, 6,
                             alpha(Color{10, 12, 16, 200}, fade));
        float leftY = table(x, top, "MANEJO", driving, nDriving);
        leftY = table(x, leftY + 16.0f * scale, "PILOTO", rider, nRider);
        const float rightY = table(x + tableW + gapX, top, "JUEGO", game, nGame);
        float py = std::max(leftY, rightY) + 18.0f * scale;
        text(font, "PARA PROBAR", x, py, 14.0f * scale, alpha(kAccent, fade), 2.0f);
        py += 24.0f * scale;
        for (const std::string& l : toolLines) {
            text(font, l, x, py, ts, alpha(kTextDim, fade));
            py += 23.0f * scale;
        }
        footer("Esc  volver      (joystick: B)");
        break;
    }
    case Menu::None: break;
    }
}
