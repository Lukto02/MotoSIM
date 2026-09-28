#pragma once
#include <string>
#include <vector>

// Archivo de tuning "clave = valor" (tuning.ini). Cada parámetro ajustable se registra con
// un nombre; Load() pisa los valores registrados con lo que diga el archivo. Las claves que
// falten conservan el valor por defecto del código. F5 en el juego lo recarga.
class Tuning {
public:
    void Add(const std::string& name, float* value);
    bool Load(const std::string& path);
    const std::string& LastPath() const { return path; }

private:
    struct Entry {
        std::string name;
        float* value;
    };
    std::vector<Entry> entries;
    std::string path;
};
