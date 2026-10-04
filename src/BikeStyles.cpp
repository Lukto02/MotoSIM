#include "BikeStyleDef.h"

using JPH::Vec3;

namespace {

const BikeStyleDef& MotocrossStyleDef()
{
    static const BikeStyleDef d = {
        "mx", GenBikeMeshes, GenLiveryMeshes, GenBikeBody, Livery,
        Vec3(0.0f, 0.14f, -0.06f), 0.345f,                  // manubrio ancho de motocross
        Vec3(0.17f, -0.40f, -0.10f),
        Vec3(0.0f, 0.30f, -0.30f), Vec3(0.0f, 0.68f, -0.04f), Vec3(0.19f, -0.02f, 0.06f),
        0.22f, 0.30f, 0.0f, 0.0f,
        Vec3(-0.18f, 0.037f, -0.805f), Vec3(-0.044f, 0.26f, -0.965f).Normalized(), false,
    };
    return d;
}

const BikeLivery& Livery600(int i)
{
    static const BikeLivery colors[kLiveries] = {
        {{20,188,208,255}, {245,245,240,255}, {24,32,48,255}, "600"},
        {{16,160,186,255}, {250,210,32,255}, {24,32,48,255}, "607"},
        {{38,208,218,255}, {245,245,240,255}, {64,42,110,255}, "644"},
        {{14,140,168,255}, {245,245,240,255}, {240,140,28,255}, "613"},
    };
    return colors[((i % kLiveries) + kLiveries) % kLiveries];
}
const BikeStyleDef& Motocross600StyleDef()
{
    static const BikeStyleDef d = [] {
        BikeStyleDef result = MotocrossStyleDef();
        result.name = "mx600";
        result.livery = Livery600;
        return result;
    }();
    return d;
}
const BikeStyleDef& TrailStyleDef()
{
    static const BikeStyleDef d = {
        "trail", GenTrailMeshes, GenTrailLiveryMeshes, GenTrailBody, TrailLivery,
        Vec3(0.0f, 0.21f, -0.10f), 0.36f,                   // manubrio alto, un poco más atrás y ancho
        Vec3(0.17f, -0.40f, -0.10f),
        Vec3(0.0f, 0.30f, -0.30f), Vec3(0.0f, 0.68f, -0.04f), Vec3(0.19f, -0.02f, 0.06f),
        0.22f, 0.30f, 0.0f, 0.0f,
        Vec3(-0.196f, 0.078f, -0.772f), Vec3(-0.063f, 0.316f, -0.947f).Normalized(), true,   // escape aberto
    };
    return d;
}

// Motocross con motor de dos tiempos: las mismas piezas y la misma pose, otro motor y otro escape.
const BikeStyleDef& TwoStrokeStyleDef()
{
    static const BikeStyleDef d = {
        "mx2t", GenBikeMeshes, GenLiveryMeshes, GenTwoStrokeBody, TwoStrokeLivery,
        Vec3(0.0f, 0.14f, -0.06f), 0.345f,
        Vec3(0.17f, -0.40f, -0.10f),
        Vec3(0.0f, 0.30f, -0.30f), Vec3(0.0f, 0.68f, -0.04f), Vec3(0.19f, -0.02f, 0.06f),
        0.22f, 0.30f, 0.0f, 0.0f,
        Vec3(-0.174f, 0.021f, -0.685f), Vec3(-0.009f, 0.058f, -0.19f).Normalized(), false,
    };
    return d;
}

} // namespace

const BikeStyleDef& GetBikeStyle(int id)
{
    switch (id) {
    case BikeStyle::Motocross600: return Motocross600StyleDef();
    case BikeStyle::Trail: return TrailStyleDef();
    case BikeStyle::Race: return RaceStyleDef();
    case BikeStyle::Trial: return TrialStyleDef();
    case BikeStyle::TwoStroke: return TwoStrokeStyleDef();
    default: return MotocrossStyleDef();
    }
}

const char* BikeStyleName(int id) { return GetBikeStyle(id).name; }

int BikeStyleFromName(const std::string& name)
{
    for (int i = 0; i < BikeStyle::Count; ++i)
        if (name == GetBikeStyle(i).name) return i;
    return -1;
}
