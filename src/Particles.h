#pragma once
#include "raylib.h"

#include <vector>

class Terrain;

// Partículas extremadamente simples (sin cuerpos rígidos): posición, velocidad, vida y tamaño.
//  - DIRT: terrones pesados, caen con gravedad y mueren al tocar el suelo.
//  - DUST: billboards transparentes que crecen, suben un poco y se desvanecen.
//  - FLAME: fuego del escape: aditivo, muy breve, pasa de blanco amarillento a naranja y rojo. Vive
//    en el espacio de la moto (posición y velocidad locales): sale siempre de la boca del escape.
//  - SPARK: chispa de metal raspando el piso: aditiva, una estela corta en la dirección en que vuela;
//    cae, rebota en el suelo y se enfría de blanco amarillento a rojo.
class Particles {
public:
    enum Kind : unsigned char { DIRT, DUST, FLAME, SPARK };

    void Init(size_t maxParticles = 3000);
    void Unload();
    // owner: de qué moto es una llama (0 = la propia, 1.. = las de otros jugadores).
    void Emit(Kind kind, Vector3 pos, Vector3 vel, float size, float life, Color color, int owner = 0);
    void Update(float dt, const Terrain& terrain);
    // bikeFrames[owner]: cada moto tal como se dibuja (las llamas viven en su espacio).
    void Draw(const Camera3D& cam, const Matrix* bikeFrames, int frameCount) const;
    size_t Count() const { return items.size(); }
    void Clear() { items.clear(); }

private:
    struct Item {
        Vector3 pos, vel;
        float life, maxLife, size;
        Color color;
        Kind kind;
        unsigned char owner;
    };
    std::vector<Item> items;
    size_t capacity = 3000;
    Texture2D dirtTex{}, dustTex{};
};
