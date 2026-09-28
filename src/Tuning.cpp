#include "Tuning.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>

void Tuning::Add(const std::string& name, float* value)
{
    entries.push_back({name, value});
}

static std::string Trim(const std::string& s)
{
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool Tuning::Load(const std::string& p)
{
    std::ifstream file(p);
    if (!file) return false;
    path = p;

    std::string line;
    int lineNo = 0, applied = 0;
    while (std::getline(file, line)) {
        ++lineNo;
        size_t comment = line.find_first_of("#;");
        if (comment != std::string::npos) line.resize(comment);
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = Trim(line.substr(0, eq));
        std::string val = Trim(line.substr(eq + 1));
        if (key.empty()) continue;

        bool found = false;
        for (Entry& e : entries) {
            if (e.name != key) continue;
            found = true;
            char* end = nullptr;
            float v = std::strtof(val.c_str(), &end);
            if (end == val.c_str()) {
                std::printf("tuning: valor invalido en linea %d (%s)\n", lineNo, key.c_str());
            } else {
                *e.value = v;
                ++applied;
            }
            break;
        }
        if (!found) std::printf("tuning: clave desconocida '%s' (linea %d)\n", key.c_str(), lineNo);
    }
    std::printf("tuning: %d valores cargados de %s\n", applied, p.c_str());
    return true;
}
