#include "Maps.h"

#include "BikeStyles.h"
#include "Json.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

std::string Lower(std::string s)
{
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool ReadVec(const Json& v, float* out, int n)
{
    if (!v.IsArray() || (int)v.Size() < n) return false;
    for (int i = 0; i < n; ++i) out[i] = v[(size_t)i].AsNum(out[i]);
    return true;
}

// "#rrggbb" o [r, g, b] (0..255).
bool ReadColor(const Json& v, unsigned char* out)
{
    if (v.IsString() && v.string.size() == 7 && v.string[0] == '#') {
        for (int i = 0; i < 3; ++i) out[i] = (unsigned char)std::strtol(v.string.substr(1 + 2 * i, 2).c_str(), nullptr, 16);
        return true;
    }
    float c[3] = {(float)out[0], (float)out[1], (float)out[2]};
    if (!ReadVec(v, c, 3)) return false;
    for (int i = 0; i < 3; ++i) out[i] = (unsigned char)std::clamp(c[i], 0.0f, 255.0f);
    return true;
}

bool ReadSurface(const std::string& name, Surface& out)
{
    const std::string n = Lower(name);
    if (n == "track" || n == "pista") out = Surface::Track;
    else if (n == "asphalt" || n == "asfalto") out = Surface::Asphalt;
    else if (n == "concrete" || n == "cemento") out = Surface::Concrete;
    else if (n == "dirt" || n == "tierra") out = Surface::Dirt;
    else return false;
    return true;
}

// Los errores muestran la ruta desde la carpeta mods/ ("sandbox/maps/park.json:12:5: ..."), que se lee
// en pantalla, en vez de la ruta completa.
std::string Shown(const std::string& path, const std::string& mod, const char* kind)
{
    return mod + "/" + kind + "/" + fs::path(path).filename().string();
}

std::string Shorten(const std::string& error, const std::string& path, const std::string& shown)
{
    return error.compare(0, path.size(), path) == 0 ? shown + error.substr(path.size()) : error;
}

} // namespace

const char* FeatureName(FeatureKind k)
{
    static const char* names[] = {"Rollers", "Tabletop", "Whoops", "Doble", "Escalon", "Kicker", "QuebraMola", "Escadaria", "Rampa"};
    const int i = (int)k;
    return i >= 0 && i < (int)FeatureKind::Count ? names[i] : "?";
}

bool FeatureFromName(const std::string& name, FeatureKind& out)
{
    const std::string n = Lower(name);
    struct Alias { const char* name; FeatureKind kind; };
    static const Alias aliases[] = {
        {"rollers", FeatureKind::Rollers}, {"tabletop", FeatureKind::Tabletop}, {"mesa", FeatureKind::Tabletop},
        {"whoops", FeatureKind::Whoops}, {"doble", FeatureKind::Double}, {"double", FeatureKind::Double},
        {"escalon", FeatureKind::StepUp}, {"stepup", FeatureKind::StepUp}, {"kicker", FeatureKind::Kicker},
        {"quebramola", FeatureKind::QuebraMola}, {"lomo", FeatureKind::QuebraMola}, {"speedbump", FeatureKind::QuebraMola},
        {"escadaria", FeatureKind::Escadaria}, {"escalera", FeatureKind::Escadaria}, {"stairs", FeatureKind::Escadaria},
        {"rampa", FeatureKind::Rampa}, {"ramp", FeatureKind::Rampa},
    };
    for (const Alias& a : aliases)
        if (n == a.name) {
            out = a.kind;
            return true;
        }
    return false;
}

void ModRegistry::Scan(const std::vector<std::string>& candidates)
{
    maps.clear();
    bikes.clear();
    errors.clear();
    roots.clear();
    std::vector<fs::path> seen;
    for (const std::string& root : candidates) {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) continue;
        const fs::path canon = fs::weakly_canonical(root, ec);
        if (std::find(seen.begin(), seen.end(), canon) != seen.end()) continue;
        seen.push_back(canon);
        roots.push_back(canon.string());
        // (Si hay más de una carpeta mods/, un mapa o moto con el mismo id vale el de la primera.)
        // Mods en orden: "base" primero, después por nombre.
        std::vector<fs::path> mods;
        for (const auto& e : fs::directory_iterator(canon, ec))
            if (e.is_directory()) mods.push_back(e.path());
        std::sort(mods.begin(), mods.end(), [](const fs::path& a, const fs::path& b) {
            const bool ab = a.filename() == "base", bb = b.filename() == "base";
            return ab != bb ? ab : a.filename().string() < b.filename().string();
        });
        for (const fs::path& mod : mods) {
            const std::string modName = mod.filename().string();
            for (const char* kind : {"bikes", "maps"}) {
                std::vector<fs::path> files;
                for (const auto& e : fs::directory_iterator(mod / kind, ec))
                    if (e.is_regular_file() && Lower(e.path().extension().string()) == ".json") files.push_back(e.path());
                std::sort(files.begin(), files.end());
                for (const fs::path& f : files) {
                    if (std::string(kind) == "bikes") LoadBike(f.string(), modName, mod.string());
                    else LoadMap(f.string(), modName, mod.string());
                }
            }
        }
    }
    // Sin ningún mapa (no está la carpeta mods/ o no se pudo leer nada): un óvalo para poder andar.
    if (maps.empty()) {
        errors.push_back("no se encontró ningún mapa en mods/*/maps/ (queda un óvalo de emergencia)");
        MapDef m;
        m.id = "builtin/ovalo";
        m.mod = "builtin";
        m.name = "Óvalo de emergencia";
        m.kind = "sin mods";
        m.description = "No se encontró la carpeta mods/ junto al juego.";
        const float oval[][2] = {{-60, -30}, {60, -30}, {80, 0}, {60, 30}, {-60, 30}, {-80, 0}};
        for (const auto& p : oval) m.track.push_back({p[0], p[1], Surface::Keep});
        m.track[0].surface = Surface::Track;
        m.halfWidth = 4.5f;
        maps.push_back(m);
    }
    // En el menú: por "order"; a igual orden, los del juego primero y después como se encontraron.
    std::stable_sort(maps.begin(), maps.end(), [](const MapDef& a, const MapDef& b) {
        if (a.order != b.order) return a.order < b.order;
        return (a.mod == "base") > (b.mod == "base");
    });
    for (const std::string& e : errors) std::printf("mods: %s\n", e.c_str());
    std::printf("mods: %zu mapas, %zu motos\n", maps.size(), bikes.size());
}

const BikeDef* ModRegistry::Bike(const std::string& id) const
{
    for (const BikeDef& b : bikes)
        if (b.id == id) return &b;
    return nullptr;
}

int ModRegistry::FindMap(const std::string& key) const
{
    const std::string k = Lower(key);
    for (size_t i = 0; i < maps.size(); ++i) {
        const MapDef& m = maps[i];
        const std::string file = m.id.substr(m.id.find('/') + 1);
        if (Lower(m.id) == k || Lower(file) == k || Lower(m.name) == k) return (int)i;
    }
    return -1;
}

bool ModRegistry::LoadBike(const std::string& path, const std::string& mod, const std::string& dir)
{
    const std::string id = mod + "/" + fs::path(path).stem().string();
    if (Bike(id)) return false;
    Json j;
    std::string err;
    const std::string shown = Shown(path, mod, "bikes");
    if (!Json::Load(path, j, err)) {
        errors.push_back(Shorten(err, path, shown));
        return false;
    }
    BikeDef b;
    b.id = id;
    b.dir = dir;
    b.name = j.Str("name", b.id);
    b.description = j.Str("description", "");
    b.style = Lower(j.Str("style", "mx"));
    if (BikeStyleFromName(b.style) < 0) errors.push_back(shown + ": \"style\" desconocido \"" + b.style + "\" (mx, trail, race o trial)");
    const std::string tuning = j.Str("tuning", "");
    if (!tuning.empty()) {
        b.tuning = (fs::path(path).parent_path() / tuning).string();
        std::error_code ec;
        if (!fs::is_regular_file(b.tuning, ec)) errors.push_back(shown + ": no está \"" + tuning + "\" (va junto al .json)");
    }
    bikes.push_back(b);
    return true;
}

bool ModRegistry::LoadMap(const std::string& path, const std::string& mod, const std::string& dir)
{
    const std::string id = mod + "/" + fs::path(path).stem().string();
    for (const MapDef& other : maps)
        if (other.id == id) return false;
    Json j;
    std::string err;
    const std::string shown = Shown(path, mod, "maps");
    if (!Json::Load(path, j, err)) {
        errors.push_back(Shorten(err, path, shown));
        return false;
    }
    auto warn = [&](const std::string& what) { errors.push_back(shown + ": " + what); };
    MapDef m;
    m.id = id;
    m.file = path;
    m.dir = dir;
    m.mod = mod;
    m.name = j.Str("name", m.id);
    m.kind = j.Str("kind", mod);
    m.description = j.Str("description", "");
    m.hint = j.Str("hint", "");
    m.hasColor = ReadColor(j["color"], m.color);
    m.order = (int)j.Num("order", (float)m.order);
    m.bike = j.Str("bike", m.bike);
    m.generator = Lower(j.Str("generator", ""));
    m.markers = j.Bool("markers", m.generator.empty());
    if (!m.generator.empty() && m.generator != "favela" && m.generator != "circuit")
        warn("generator desconocido \"" + m.generator + "\" (\"favela\" o \"circuit\")");
    m.botSpeed = j.Num("bot_speed", m.botSpeed);
    m.botLateral = j.Num("bot_lateral", m.botLateral);
    m.botBrake = j.Num("bot_brake", m.botBrake);
    m.botLeanThrottle = j.Num("bot_lean_throttle", m.botLeanThrottle);

    const Json& t = j["terrain"];
    if (t.IsObject()) {
        m.terrain.type = Lower(t.Str("type", m.terrain.type));
        m.terrain.height = t.Num("height", m.terrain.height);
        m.terrain.scale = t.Num("scale", m.terrain.scale);
        m.terrain.detail = t.Num("detail", m.terrain.detail);
        m.terrain.edgeHeight = t.Num("edge", m.terrain.edgeHeight);
        m.terrain.size = std::clamp(t.Num("size", m.terrain.size), 64.0f, 4096.0f);
        m.terrain.resolution = std::clamp(t.Num("resolution", m.terrain.resolution), 0.25f, 4.0f);
        if (m.terrain.type == "heightmap") {
            m.terrain.image = (fs::path(path).parent_path() / t.Str("image", "")).string();
            m.terrain.imageHeight = t.Num("height", m.terrain.imageHeight);
            m.terrain.imageBase = t.Num("base", m.terrain.imageBase);
            std::error_code ec;
            if (!fs::is_regular_file(m.terrain.image, ec)) warn("no está la imagen del terreno \"" + t.Str("image", "") + "\" (va junto al .json)");
        }
        if (m.terrain.type != "hills" && m.terrain.type != "flat" && m.terrain.type != "heightmap" && m.terrain.type != "favela" &&
            m.terrain.type != "dunes")
            warn("terrain.type desconocido: \"" + m.terrain.type + "\" (hills, flat, heightmap, favela o dunes)");
        m.terrain.wavelength = std::max(8.0f, t.Num("wavelength", m.terrain.wavelength));
        m.terrain.wind = t.Num("wind", m.terrain.wind);
        m.terrain.meander = t.Num("meander", m.terrain.meander);
        m.terrain.border = std::max(0.0f, t.Num("border", m.terrain.border));
        m.terrain.seed = (int)t.Num("seed", (float)m.terrain.seed);
        const Json& sh = t["shapes"];
        for (size_t i = 0; i < sh.Size(); ++i) {
            const Json& s = sh[i];
            const std::string where = "terrain.shapes[" + std::to_string(i) + "]";
            MapShape ms;
            const std::string kind = Lower(s.Str("shape", "line"));
            if (kind == "round" || kind == "redonda") ms.round = true;
            else if (kind != "line" && kind != "linea" && kind != "línea") {
                warn(where + ": shape desconocido \"" + kind + "\" (line o round)");
                continue;
            }
            float at[2] = {0, 0};
            if (!ReadVec(s["at"], at, 2)) {
                warn(where + ": falta \"at\": [x, z]");
                continue;
            }
            ms.x = at[0];
            ms.z = at[1];
            const Json& pr = s["profile"];
            bool ok = pr.IsArray() && pr.Size() >= 2;
            for (size_t k = 0; ok && k < pr.Size(); ++k) {
                float p[2] = {0, 0};
                ok = ReadVec(pr[k], p, 2) && (ms.profile.empty() || p[0] > ms.profile.back().first);
                if (ok) ms.profile.push_back({p[0], p[1]});
            }
            if (!ok || (ms.round && ms.profile.front().first < 0.0f)) {
                warn(where + ": \"profile\" tiene que ser una lista de al menos 2 puntos [u, alto] con u creciente" +
                     std::string(ms.round ? " (round: u es el radio, desde 0)" : ""));
                continue;
            }
            ms.yaw = s.Num("yaw", 0.0f);
            ms.width = std::max(0.0f, s.Num("width", ms.width));
            ms.edge = std::max(0.0f, s.Num("edge", ms.edge));
            ms.smooth = std::clamp(s.Num("smooth", ms.smooth), 0.0f, 20.0f);
            ms.bend = s.Num("bend", 0.0f);
            float st[2] = {1.0f, 1.0f};
            if (ReadVec(s["stretch"], st, 2)) {
                ms.stretch[0] = std::max(0.05f, st[0]);
                ms.stretch[1] = std::max(0.05f, st[1]);
            }
            if (s.Has("arc")) {
                ms.hasArc = ReadVec(s["arc"], ms.arc, 2);
                if (!ms.hasArc) warn(where + ": \"arc\" tiene que ser [desde, hasta] (rumbos en grados)");
                else if (!ms.round) warn(where + ": \"arc\" es sólo para \"round\"");
            }
            const std::string mode = Lower(s.Str("mode", "add"));
            if (mode == "level" || mode == "nivelar") ms.level = true;
            else if (mode != "add" && mode != "sumar") warn(where + ": mode desconocido \"" + mode + "\" (add o level)");
            ms.hasBase = s.Has("base");
            ms.base = s.Num("base", 0.0f);
            ms.dirt = s.Bool("dirt", false);
            m.terrain.shapes.push_back(ms);
        }
    }

    const Json& tr = j["track"];
    const Json& pts = tr["points"];
    if (!pts.IsArray() || pts.Size() < 4) {
        warn("track.points tiene que ser una lista de al menos 4 puntos [x, z] (o [x, z, \"superficie\"])");
        return false;
    }
    for (size_t i = 0; i < pts.Size(); ++i) {
        const Json& p = pts[i];
        MapPoint mp;
        if (!p.IsArray() || p.Size() < 2) {
            warn("track.points[" + std::to_string(i) + "] tiene que ser [x, z]");
            return false;
        }
        mp.x = p[0].AsNum();
        mp.z = p[1].AsNum();
        if (p.Size() >= 3 && p[2].IsString() && !ReadSurface(p[2].string, mp.surface))
            warn("superficie desconocida \"" + p[2].string + "\" (track, asphalt, concrete, dirt)");
        m.track.push_back(mp);
    }
    m.layoutScale = tr.Num("scale", 1.0f);
    m.halfWidth = tr.Num("width", 9.0f) * 0.5f;
    m.roadStyle = Lower(tr.Str("style", "track"));
    if (m.roadStyle != "track" && m.roadStyle != "street" && m.roadStyle != "guide")
        warn("track.style desconocido \"" + m.roadStyle + "\" (track, street o guide)");
    m.banking = tr.Bool("banking", m.roadStyle == "track");
    if (m.roadStyle == "guide") m.markers = j.Bool("markers", false);   // la guía no se ve: sin estacas ni pórtico
    if (m.track[0].surface == Surface::Keep) m.track[0].surface = m.roadStyle == "street" ? Surface::Concrete : Surface::Track;

    float start[2] = {0, 0};
    if (ReadVec(j["start"], start, 2)) {
        m.startX = start[0];
        m.startZ = start[1];
        m.hasStart = true;
    }

    const Json& fe = j["features"];
    for (size_t i = 0; i < fe.Size(); ++i) {
        const Json& f = fe[i];
        MapFeature mf;
        if (!FeatureFromName(f.Str("kind", ""), mf.kind)) {
            warn("features[" + std::to_string(i) + "]: \"kind\" desconocido \"" + f.Str("kind", "") + "\"");
            continue;
        }
        float at[2] = {0, 0};
        if (ReadVec(f["at"], at, 2)) {
            mf.x = at[0];
            mf.z = at[1];
        } else if (f.Has("s")) {
            mf.atS = true;
            mf.s = f.Num("s", 0.0f);
        } else {
            warn("features[" + std::to_string(i) + "]: falta \"at\": [x, z] o \"s\"");
            continue;
        }
        m.features.push_back(mf);
    }
    const Json& af = j["auto_features"];
    for (size_t i = 0; i < af.Size(); ++i) {
        std::vector<FeatureKind> run;
        for (size_t k = 0; k < af[i].Size(); ++k) {
            FeatureKind kind;
            if (FeatureFromName(af[i][k].string, kind)) run.push_back(kind);
            else warn("auto_features: obstáculo desconocido \"" + af[i][k].string + "\"");
        }
        m.autoFeatures.push_back(run);
    }

    const Json& te = j["terraces"];
    if (te.IsObject()) {
        m.terraces.on = ReadVec(te["from"], m.terraces.from, 2) && ReadVec(te["to"], m.terraces.to, 2);
        for (size_t i = 0; i < te["plateaus"].Size(); ++i) {
            float p[2] = {0, 0};
            if (ReadVec(te["plateaus"][i], p, 2)) m.terraces.plateaus.push_back({p[0], p[1]});
        }
        m.terraces.plateauHalf = te.Num("half", m.terraces.plateauHalf);
    }

    const Json& lk = j["look"];
    if (lk.IsObject()) {
        m.look.preset = Lower(lk.Str("preset", "day"));
        if (m.look.preset == "sunset") {                  // valores del atardecer (se pueden pisar abajo)
            // (calibrados con el estilo de color del post y el Ambient() nuevo; la exposición es 1.0 porque el estilo ya suma 5%)
            const float sd[3] = {-0.52f, 0.42f, -0.74f}, sc[3] = {2.6f, 1.78f, 1.02f}, ze[3] = {62, 108, 158}, ho[3] = {232, 168, 122},
                        gr[3] = {126, 106, 90};
            std::copy(sd, sd + 3, m.look.sunDir);
            std::copy(sc, sc + 3, m.look.sunColor);
            std::copy(ze, ze + 3, m.look.zenith);
            std::copy(ho, ho + 3, m.look.horizon);
            std::copy(gr, gr + 3, m.look.ground);
            m.look.fog = 0.0024f;
            m.look.exposure = 1.0f;
            m.look.groundTextures = "street";
        }
        m.look.custom |= ReadVec(lk["sun_dir"], m.look.sunDir, 3);
        m.look.custom |= ReadVec(lk["sun_color"], m.look.sunColor, 3);
        m.look.custom |= ReadVec(lk["zenith"], m.look.zenith, 3);
        m.look.custom |= ReadVec(lk["horizon"], m.look.horizon, 3);
        m.look.custom |= ReadVec(lk["ground"], m.look.ground, 3);
        m.look.fog = lk.Num("fog", m.look.fog);
        m.look.exposure = lk.Num("exposure", m.look.exposure);
        m.look.groundTextures = Lower(lk.Str("ground_textures", m.look.groundTextures));
    }

    const Json& ob = j["objects"];
    for (size_t i = 0; i < ob.Size(); ++i) {
        const Json& o = ob[i];
        MapObject mo;
        const std::string shape = Lower(o.Str("shape", "box"));
        if (shape == "box" || shape == "caja") mo.shape = MapObject::Shape::Box;
        else if (shape == "ramp" || shape == "rampa") mo.shape = MapObject::Shape::Ramp;
        else if (shape == "cylinder" || shape == "cilindro" || shape == "barrel" || shape == "tambor") mo.shape = MapObject::Shape::Cylinder;
        else if (shape == "sphere" || shape == "esfera" || shape == "ball") mo.shape = MapObject::Shape::Sphere;
        else {
            warn("objects[" + std::to_string(i) + "]: shape desconocido \"" + shape + "\" (box, ramp, cylinder, sphere)");
            continue;
        }
        if (!ReadVec(o["at"], mo.pos, 3) && !ReadVec(o["at"], mo.pos, 2)) {
            warn("objects[" + std::to_string(i) + "]: falta \"at\": [x, y, z] (y sobre el terreno) o [x, z]");
            continue;
        }
        if (o["at"].Size() == 2) {                        // [x, z]: apoyado en el suelo
            mo.pos[2] = mo.pos[1];
            mo.pos[1] = 0.0f;
        }
        mo.onGround = !o.Bool("absolute", false);
        // size: un número (cubo / diámetro), [diámetro, alto] (cilindro) o [ancho, alto, largo].
        const Json& sz = o["size"];
        if (sz.IsNumber()) {
            mo.size[0] = mo.size[1] = mo.size[2] = sz.AsNum(1.0f);
        } else if (sz.IsArray() && sz.Size() > 0) {
            mo.size[0] = sz[0].AsNum(1.0f);
            mo.size[1] = sz.Size() > 1 ? sz[1].AsNum(1.0f) : mo.size[0];
            mo.size[2] = sz.Size() > 2 ? sz[2].AsNum(1.0f) : mo.size[0];
        }
        mo.yaw = o.Num("yaw", 0.0f);
        mo.pitch = o.Num("pitch", 0.0f);
        mo.roll = o.Num("roll", 0.0f);
        ReadColor(o["color"], mo.color);
        mo.dynamic = o.Bool("dynamic", false);
        mo.mass = o.Num("mass", mo.mass);
        mo.friction = o.Num("friction", mo.friction);
        m.objects.push_back(mo);
    }
    // "scale" agranda o achica todo el mapa (posiciones, no tamaños de objetos).
    const float k = m.layoutScale;
    if (k != 1.0f) {
        for (MapPoint& p : m.track) {
            p.x *= k;
            p.z *= k;
        }
        for (MapFeature& f : m.features) {
            f.x *= k;
            f.z *= k;
        }
        m.startX *= k;
        m.startZ *= k;
        for (float* v : {m.terraces.from, m.terraces.to}) {
            v[0] *= k;
            v[1] *= k;
        }
        for (auto& pl : m.terraces.plateaus) {
            pl.first *= k;
            pl.second *= k;
        }
        for (MapObject& o : m.objects) {
            o.pos[0] *= k;
            o.pos[2] *= k;
        }
        for (MapShape& s : m.terrain.shapes) {
            s.x *= k;
            s.z *= k;
        }
    }
    maps.push_back(m);
    return true;
}
