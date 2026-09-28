#pragma once
// Mapas, motos y objetos como datos: se leen de la carpeta mods/ (un mod = una carpeta con
// maps/*.json y bikes/*.json). Los mapas que vienen con el juego están en mods/base/. Formato y
// ejemplos: MODDING.md.
#include <string>
#include <vector>

// Obstáculos de la pista (su forma está en Track.cpp).
enum class FeatureKind { Rollers, Tabletop, Whoops, Double, StepUp, Kicker, QuebraMola, Escadaria, Rampa, Count };
const char* FeatureName(FeatureKind k);
bool FeatureFromName(const std::string& name, FeatureKind& out);

// Superficie de la pista desde un punto del trazado hasta el siguiente que diga otra.
enum class Surface { Track, Asphalt, Concrete, Dirt, Keep };   // Track: tierra de motocross

struct MapPoint {
    float x = 0.0f, z = 0.0f;
    Surface surface = Surface::Keep;
};

struct MapFeature {
    FeatureKind kind = FeatureKind::Kicker;
    bool atS = false;                  // en una distancia s de la pista (si no, en el punto más cercano a x, z)
    float s = 0.0f, x = 0.0f, z = 0.0f;
};

// Objeto puesto en el mapa: caja, rampa (cuña que sube hacia +Z local), cilindro o esfera; fijo (se
// puede andar encima) o suelto (se empuja y se cae; lo simula cada jugador en su PC).
struct MapObject {
    enum class Shape { Box, Ramp, Cylinder, Sphere };
    Shape shape = Shape::Box;
    float pos[3] = {0.0f, 0.0f, 0.0f};
    bool onGround = true;              // pos[1] es la altura sobre el terreno (si no, absoluta)
    float size[3] = {1.0f, 1.0f, 1.0f};// ancho, alto, largo (cilindro: diámetro, alto; esfera: diámetro)
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;   // grados
    unsigned char color[3] = {200, 200, 200};
    bool dynamic = false;
    float mass = 20.0f;                // kg (sueltos)
    float friction = 0.8f;
};

struct MapLook {                       // luz, cielo y texturas del suelo
    std::string preset = "day";        // day | sunset
    bool custom = false;               // si trae colores propios (pisan el preset)
    float sunDir[3] = {-0.55f, 0.62f, -0.45f};
    float sunColor[3] = {2.35f, 2.15f, 1.85f};
    float zenith[3] = {78, 128, 196}, horizon[3] = {186, 206, 224}, ground[3] = {92, 80, 64};   // 0..255
    float fog = 0.0042f, exposure = 1.0f;
    std::string groundTextures = "dirt";   // dirt (tierra de pista y pasto) | street (pavimento y tierra roja) | circuit (asfalto y pasto)
};

struct MapTerrain {
    std::string type = "hills";        // hills | flat | heightmap | favela
    float height = 10.0f, scale = 0.011f, detail = 0.5f;   // hills: lomas y rugosidad
    std::string image;                 // heightmap: PNG en gris (blanco = alto), estirado a todo el mapa
    float imageHeight = 30.0f, imageBase = 0.0f;
    float edgeHeight = 14.0f;          // el terreno sube en el borde del mapa
    float size = 255.5f;               // m de lado (el mapa es un cuadrado centrado en 0, 0)
    float resolution = 0.5f;           // m entre muestras del terreno (más grande = mapas más grandes, menos detalle)
};

struct MapTerraces {                   // mesetas a nivel en los cruces de una calle empinada (favela)
    bool on = false;
    float from[2] = {0, 0}, to[2] = {0, 0};
    std::vector<std::pair<float, float>> plateaus;
    float plateauHalf = 4.5f;
};

struct BikeDef {
    std::string id, name, dir, description;
    std::string style = "mx";          // mx | trail | race | trial (piezas de la moto y pose del piloto, BikeStyles.h)
    std::string tuning;                // .ini que pisa tuning.ini (ruta completa; vacío = la de siempre)
};

struct MapDef {
    std::string id;                    // "mod/archivo" (así viaja por red)
    std::string file, dir, mod;        // archivo, carpeta y nombre del mod
    std::string name, kind, description, hint;
    int order = 100;                   // lugar en la lista de mapas (menor = antes)
    bool hasColor = false;             // color del menú ("color": "#rrggbb")
    unsigned char color[3] = {255, 255, 255};
    std::string bike = "base/motocross";
    MapTerrain terrain;
    std::vector<MapPoint> track;       // puntos de control (cerrado, Catmull-Rom centrípeta)
    float layoutScale = 1.0f, halfWidth = 4.5f;   // la escala ya está aplicada a todas las posiciones
    bool banking = true;               // peraltes en las curvas
    std::string roadStyle = "track";   // track (base suavizada, banquinas largas) | street (calles niveladas, cruces que se mezclan)
    std::vector<MapFeature> features;  // puestos a mano
    std::vector<std::vector<FeatureKind>> autoFeatures;   // o un plan por recta, en orden (como la de motocross)
    bool hasStart = false;
    float startX = 0.0f, startZ = 0.0f;
    MapTerraces terraces;
    std::string generator;             // "" | "favela" (casas, postes, cables, carteles...) | "circuit" (circuito de velocidad)
    float botSpeed = 17.0f;            // m/s: lo más rápido que va el bot de pruebas
    float botLateral = -1.0f;          // m/s² con que encara las curvas (-1 = el de la línea de comandos)
    float botBrake = -1.0f;            // m/s² con que planea las frenadas (-1 = 4, o 2.5 en mapas de calles)
    float botLeanThrottle = 0.0f;      // 0..1: cuánto afloja el gas acostado (a 45°); 0 = nada
    bool markers = true;               // estacas de colores y pórtico de largada de motocross
    std::vector<MapObject> objects;
    MapLook look;
};

class ModRegistry {
public:
    // Lee todos los mods (cada carpeta de roots/*). Los errores de formato quedan en Errors().
    void Scan(const std::vector<std::string>& roots);
    const std::vector<MapDef>& Maps() const { return maps; }
    const BikeDef* Bike(const std::string& id) const;
    const std::vector<BikeDef>& Bikes() const { return bikes; }
    int FindMap(const std::string& idOrName) const;   // id, nombre de archivo o nombre visible; -1 si no está
    const std::vector<std::string>& Errors() const { return errors; }
    const std::vector<std::string>& Roots() const { return roots; }

private:
    bool LoadMap(const std::string& path, const std::string& mod, const std::string& dir);
    bool LoadBike(const std::string& path, const std::string& mod, const std::string& dir);
    std::vector<MapDef> maps;
    std::vector<BikeDef> bikes;
    std::vector<std::string> errors, roots;
};
