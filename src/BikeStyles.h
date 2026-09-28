#pragma once
// Estilos de moto (el "style" del .json de cada moto en mods/*/bikes/): qué piezas se dibujan y cómo va
// sentado el piloto. Los números de la física (peso, motor, suspensión, cubiertas) no son del estilo:
// salen de tuning.ini y del .ini de cada moto.
#include <string>

namespace BikeStyle {
enum Id { Motocross, Trail, Race, Trial, TwoStroke, Count };
}

int BikeStyleFromName(const std::string& name);   // "mx", "trail", "race", "trial", "mx2t"; -1 si no existe
const char* BikeStyleName(int id);
