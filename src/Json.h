#pragma once
// Lector de JSON para los mods: acepta comentarios (// y /* */) y comas de más al final de listas y
// objetos, y los errores dicen en qué línea y columna están (los arman personas a mano).
#include <map>
#include <string>
#include <vector>

struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Json> array;
    std::vector<std::pair<std::string, Json>> object;   // en el orden del archivo

    bool IsNull() const { return type == Type::Null; }
    bool IsNumber() const { return type == Type::Number; }
    bool IsString() const { return type == Type::String; }
    bool IsArray() const { return type == Type::Array; }
    bool IsObject() const { return type == Type::Object; }

    // Miembro de un objeto (o un Json nulo si no está).
    const Json& operator[](const std::string& key) const;
    const Json& operator[](size_t i) const;
    size_t Size() const { return type == Type::Array ? array.size() : object.size(); }
    bool Has(const std::string& key) const;

    // Con valor por defecto si falta o es de otro tipo.
    float Num(const std::string& key, float def) const;
    std::string Str(const std::string& key, const std::string& def) const;
    bool Bool(const std::string& key, bool def) const;
    float AsNum(float def = 0.0f) const { return type == Type::Number ? (float)number : def; }

    // Lee un archivo / texto. Si falla, error = "archivo:línea:columna: qué pasó".
    static bool Parse(const std::string& text, Json& out, std::string& error, const std::string& name = "");
    static bool Load(const std::string& path, Json& out, std::string& error);
};
