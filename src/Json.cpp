#include "Json.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {

const Json kNull;

struct Parser {
    const std::string& s;
    size_t i = 0;
    int line = 1, col = 1;
    std::string error;

    char Peek() const { return i < s.size() ? s[i] : '\0'; }
    char Get()
    {
        const char c = Peek();
        ++i;
        if (c == '\n') {
            ++line;
            col = 1;
        } else {
            ++col;
        }
        return c;
    }
    bool Fail(const std::string& what)
    {
        if (error.empty()) error = std::to_string(line) + ":" + std::to_string(col) + ": " + what;
        return false;
    }
    void SkipSpace()
    {
        for (;;) {
            const char c = Peek();
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                Get();
            } else if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {        // comentario de línea
                while (Peek() != '\n' && Peek() != '\0') Get();
            } else if (c == '/' && i + 1 < s.size() && s[i + 1] == '*') {        // comentario de bloque
                Get();
                Get();
                while (Peek() != '\0' && !(Peek() == '*' && i + 1 < s.size() && s[i + 1] == '/')) Get();
                if (Peek() != '\0') {
                    Get();
                    Get();
                }
            } else {
                return;
            }
        }
    }
    bool ParseString(std::string& out)
    {
        Get();                                                                 // la comilla
        for (;;) {
            const char c = Get();
            if (c == '\0') return Fail("falta cerrar el texto con \"");
            if (c == '"') return true;
            if (c == '\\') {
                const char e = Get();
                switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'u': {                                                    // \uXXXX -> UTF-8
                    unsigned cp = 0;
                    for (int k = 0; k < 4; ++k) {
                        const char h = Get();
                        cp = cp * 16 + (unsigned)(h >= 'a' ? h - 'a' + 10 : (h >= 'A' ? h - 'A' + 10 : h - '0'));
                    }
                    if (cp < 0x80) {
                        out += (char)cp;
                    } else if (cp < 0x800) {
                        out += (char)(0xC0 | (cp >> 6));
                        out += (char)(0x80 | (cp & 0x3F));
                    } else {
                        out += (char)(0xE0 | (cp >> 12));
                        out += (char)(0x80 | ((cp >> 6) & 0x3F));
                        out += (char)(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default: out += e; break;
                }
            } else {
                out += c;
            }
        }
    }
    bool ParseValue(Json& v)
    {
        SkipSpace();
        const char c = Peek();
        if (c == '{') {
            Get();
            v.type = Json::Type::Object;
            for (;;) {
                SkipSpace();
                if (Peek() == '}') {
                    Get();
                    return true;
                }
                if (Peek() != '"') return Fail("se esperaba el nombre de un campo entre comillas");
                std::string key;
                if (!ParseString(key)) return false;
                SkipSpace();
                if (Get() != ':') return Fail("falta ':' después de \"" + key + "\"");
                Json child;
                if (!ParseValue(child)) return false;
                v.object.emplace_back(key, std::move(child));
                SkipSpace();
                if (Peek() == ',') {
                    Get();
                    continue;
                }
                if (Peek() == '}') continue;
                return Fail("falta ',' o '}' después de \"" + key + "\"");
            }
        }
        if (c == '[') {
            Get();
            v.type = Json::Type::Array;
            for (;;) {
                SkipSpace();
                if (Peek() == ']') {
                    Get();
                    return true;
                }
                Json child;
                if (!ParseValue(child)) return false;
                v.array.push_back(std::move(child));
                SkipSpace();
                if (Peek() == ',') {
                    Get();
                    continue;
                }
                if (Peek() == ']') continue;
                return Fail("falta ',' o ']' en la lista");
            }
        }
        if (c == '"') {
            v.type = Json::Type::String;
            return ParseString(v.string);
        }
        if (c == '-' || c == '+' || (c >= '0' && c <= '9') || c == '.') {
            const char* start = s.c_str() + i;
            char* end = nullptr;
            v.number = std::strtod(start, &end);
            if (end == start) return Fail("número inválido");
            for (const char* p = start; p < end; ++p) Get();
            v.type = Json::Type::Number;
            return true;
        }
        auto word = [&](const char* w) {
            size_t n = 0;
            while (w[n]) ++n;
            if (s.compare(i, n, w) != 0) return false;
            for (size_t k = 0; k < n; ++k) Get();
            return true;
        };
        if (word("true")) {
            v.type = Json::Type::Bool;
            v.boolean = true;
            return true;
        }
        if (word("false")) {
            v.type = Json::Type::Bool;
            return true;
        }
        if (word("null")) return true;
        if (c == '\0') return Fail("el archivo termina antes de tiempo");
        return Fail(std::string("no se entiende '") + c + "'");
    }
};

} // namespace

const Json& Json::operator[](const std::string& key) const
{
    if (type == Type::Object)
        for (const auto& kv : object)
            if (kv.first == key) return kv.second;
    return kNull;
}

const Json& Json::operator[](size_t i) const { return type == Type::Array && i < array.size() ? array[i] : kNull; }

bool Json::Has(const std::string& key) const { return !(*this)[key].IsNull(); }

float Json::Num(const std::string& key, float def) const
{
    const Json& v = (*this)[key];
    return v.type == Type::Number ? (float)v.number : def;
}

std::string Json::Str(const std::string& key, const std::string& def) const
{
    const Json& v = (*this)[key];
    return v.type == Type::String ? v.string : def;
}

bool Json::Bool(const std::string& key, bool def) const
{
    const Json& v = (*this)[key];
    return v.type == Type::Bool ? v.boolean : def;
}

bool Json::Parse(const std::string& text, Json& out, std::string& error, const std::string& name)
{
    Parser p{text};
    out = Json{};
    bool ok = p.ParseValue(out);
    if (ok) {
        p.SkipSpace();
        if (p.Peek() != '\0') ok = p.Fail("sobra texto después del final");
    }
    if (!ok) error = (name.empty() ? std::string() : name + ":") + p.error;
    return ok;
}

bool Json::Load(const std::string& path, Json& out, std::string& error)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        error = path + ": no se pudo abrir";
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string text = ss.str();
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) text.erase(0, 3);
    return Parse(text, out, error, path);
}
