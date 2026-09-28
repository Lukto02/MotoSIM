#pragma once
#include "Maps.h"

#include <vector>

// Circuito: una línea central cerrada (Catmull-Rom centrípeta sobre puntos de control),
// re-muestreada cada `spacing` metros, más perfiles a lo largo de la distancia s:
//   - Profile(s): saltos, whoops, rollers (altura sobre la base de la pista)
//   - Bank(s):    altura del peralte (berm) en el borde exterior de las curvas
struct TrackPoint {
    float x, z;        // posición
    float tx, tz;      // tangente unitaria
    float s;           // distancia desde la salida
    float curvature;   // 1/m, positiva = curva a la derecha
};

struct TrackFeature {
    FeatureKind kind;
    float start, length;
    const char* name;
};

class Track {
public:
    // Arma el trazado del mapa: puntos de control, ancho, peraltes, superficies y obstáculos.
    void Build(const MapDef& def);

    float Length() const { return length; }
    float Spacing() const { return spacing; }
    float HalfWidth() const { return halfWidth; }
    const std::vector<TrackPoint>& Points() const { return points; }
    const std::vector<TrackFeature>& Features() const { return features; }

    TrackPoint At(float s) const;                     // interpolado, con vuelta
    float Profile(float s) const;                      // m sobre la base
    float Bank(float s) const;                         // m, con signo: + peralte a la izquierda
    // Calles: cuánto está pavimentada (1) o es de tierra (0), y si el pavimento es asfalto (1) o
    // cemento (0). Salen de la superficie de cada punto del trazado.
    float Pavement(float s) const;
    float Asphalt(float s) const;
    float MaxCurvatureAhead(float s, float distance) const;
    // Punto más cercano. Con hint >= 0 busca sólo en una ventana alrededor del hint.
    int Nearest(float x, float z, int hint = -1, int window = 80) const;
    float Wrap(float s) const;

    float startLine = 14.0f;                           // s de la línea de largada/llegada

private:
    void PlaceFeatures(const std::vector<std::vector<FeatureKind>>& plan, bool autoStart);
    void PlaceFeatures(const std::vector<MapFeature>& list);
    void BakeProfile();
    float NearestS(float x, float z) const;           // s del punto de la línea central más cercano
    float SampleArray(const std::vector<float>& a, float s) const;

    std::vector<TrackPoint> points;
    std::vector<float> profile, bank, pavement, asphalt;
    std::vector<TrackFeature> features;
    float length = 0.0f;
    float spacing = 0.5f;
    float halfWidth = 4.5f;
};
