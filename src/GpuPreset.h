#pragma once
// Qué preset de calidad (0 Bajo, 1 Medio, 2 Alto) le toca a una placa de video, por lo que dice OpenGL
// (glGetString de GL_VENDOR y GL_RENDERER). Sin dependencias (sólo <string>): se prueba suelta con cadenas.
// Ultra no sale nunca de acá: lo elige el jugador. Placa que no se reconoce: Medio.
#include <algorithm>
#include <cctype>
#include <string>

namespace gpupreset {

inline std::string Lower(std::string s)
{
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// El número que sigue a `word` (saltando espacios y guiones), o -1 si no hay: "gtx 1650 ti" con "gtx" da 1650.
inline int NumberAfter(const std::string& s, const char* word)
{
    const size_t at = s.find(word);
    if (at == std::string::npos) return -1;
    size_t i = at + std::char_traits<char>::length(word);
    while (i < s.size() && (s[i] == ' ' || s[i] == '-')) ++i;
    if (i >= s.size() || !std::isdigit((unsigned char)s[i])) return -1;
    int n = 0;
    for (; i < s.size() && std::isdigit((unsigned char)s[i]) && n < 100000; ++i) n = n * 10 + (s[i] - '0');
    return n;
}

// La palabra `word` suelta (sin letras pegadas a los lados): "gt" está en "geforce gt 730" pero no en "gtx" ni en "gt2".
inline bool Word(const std::string& s, const char* word)
{
    const size_t len = std::char_traits<char>::length(word);
    for (size_t at = s.find(word); at != std::string::npos; at = s.find(word, at + 1)) {
        const bool before = at == 0 || !std::isalnum((unsigned char)s[at - 1]);
        const bool after = at + len >= s.size() || !std::isalnum((unsigned char)s[at + len]);
        if (before && after) return true;
    }
    return false;
}

inline bool Has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

// vendor y renderer tal como los da OpenGL (cualquier mayúscula). Devuelve 0, 1 o 2.
inline int ForGpu(const std::string& vendorText, const std::string& rendererText)
{
    const std::string r = Lower(rendererText), all = Lower(vendorText) + " " + r;
    constexpr int kLow = 0, kMedium = 1, kHigh = 2;

    // Sin placa: dibujo por software (Mesa, Windows sin driver).
    if (Has(all, "llvmpipe") || Has(all, "softpipe") || Has(all, "swiftshader") || Has(all, "gdi generic") ||
        Has(all, "basic render") || Has(all, "software rasterizer"))
        return kLow;

    // Apple: los M base, Medio; Pro, Max y Ultra, Alto.
    if (Has(all, "apple")) {
        if (Has(r, " pro") || Has(r, " max") || Has(r, " ultra")) return kHigh;
        return kMedium;
    }

    // NVIDIA.
    if (Has(all, "nvidia") || Has(all, "geforce") || Has(all, "quadro") || Has(all, "titan")) {
        if (Has(r, "rtx") || Has(r, "titan")) return kHigh;                       // RTX 20, 30, 40, 50
        if (Has(r, "gtx")) {
            const int n = NumberAfter(r, "gtx");
            if (n >= 1070 && n < 1600) return kHigh;                              // 1070, 1080 (y Ti)
            if (n >= 1050 && n < 1070) return kMedium;                            // 1050, 1060
            if (n >= 1600 && n < 1700) return kMedium;                            // 1650, 1660
            if (n >= 970 && n < 1000) return kHigh;                               // 970, 980
            if (n >= 950 && n < 970) return kMedium;                              // 950, 960
            if (n >= 770 && n < 800) return kMedium;                              // 770, 780
            if (n > 0 && n < 900) return kLow;                                    // 4xx a 7xx
            return kMedium;
        }
        // GeForce MX150, 940MX...: notebooks de entrada. GT 730, GT 1030: tarjetas de entrada.
        for (size_t at = r.find("mx"); at != std::string::npos; at = r.find("mx", at + 1)) {
            const bool before = at == 0 || r[at - 1] == ' ' || std::isdigit((unsigned char)r[at - 1]);
            const bool after = at + 2 >= r.size() || !std::isalpha((unsigned char)r[at + 2]);
            if (before && after) return kLow;
        }
        if (Word(r, "gt") || Word(r, "gts")) return kLow;
        const int n = NumberAfter(r, "geforce");                                  // "GeForce 9800", "GeForce 210": muy viejas
        if (n > 0 && n < 1000) return kLow;
        return kMedium;
    }

    // Intel.
    if (Has(all, "intel")) {
        if (Has(r, "arc")) {                                                      // Arc: A3xx, Medio; A5xx, A7xx, B5xx, Alto
            for (size_t i = 0; i + 3 < r.size(); ++i)
                if ((r[i] == 'a' || r[i] == 'b') && (i == 0 || !std::isalnum((unsigned char)r[i - 1])) && std::isdigit((unsigned char)r[i + 1]) &&
                    std::isdigit((unsigned char)r[i + 2]) && std::isdigit((unsigned char)r[i + 3])) {
                    const int tier = r[i + 1] - '0';
                    return tier >= 5 ? kHigh : kMedium;
                }
            return kMedium;                                                       // "Arc(TM) Graphics" (la integrada de Meteor Lake)
        }
        if (Has(r, "iris")) return Has(r, " xe") || Has(r, "plus") ? kMedium : kLow;   // Iris Xe y Plus; el Iris viejo, Bajo
        if (Word(r, "xe")) return kMedium;                                        // "Intel(R) Xe Graphics"
        return kLow;                                                              // HD y UHD Graphics
    }

    // AMD.
    if (Has(all, "amd") || Has(all, "radeon") || Word(all, "ati")) {
        if (Has(r, "radeon pro")) {                                               // Mac y estaciones de trabajo
            const int n = NumberAfter(r, "radeon pro");
            return n >= 5000 ? kHigh : kMedium;
        }
        if (Has(r, "rx vega") || Has(r, "radeon vii")) return kHigh;
        if (Has(r, " rx")) {
            const int n = NumberAfter(r, " rx");
            if (n >= 5000) return kHigh;                                          // RX 5000 en adelante
            return kMedium;                                                       // RX 4xx, 5xx
        }
        if (Has(r, "vega")) return kLow;                                          // Vega integrada (Ryzen 2000 a 5000)
        // Radeon 610M (dos unidades), Bajo; 660M, 680M, 780M, 890M (integradas nuevas), Medio; 8060S y 8050S (Strix Halo), Alto.
        for (size_t i = 0; i + 3 < r.size(); ++i)
            if (std::isdigit((unsigned char)r[i]) && std::isdigit((unsigned char)r[i + 1]) && std::isdigit((unsigned char)r[i + 2]) &&
                (i == 0 || !std::isalnum((unsigned char)r[i - 1])) && r[i + 3] == 'm') {
                const int n = (r[i] - '0') * 100 + (r[i + 1] - '0') * 10 + (r[i + 2] - '0');
                return n <= 610 ? kLow : kMedium;
            }
        for (size_t i = 0; i + 4 < r.size(); ++i)                                 // 8060S, 8050S
            if (std::isdigit((unsigned char)r[i]) && std::isdigit((unsigned char)r[i + 1]) && std::isdigit((unsigned char)r[i + 2]) &&
                std::isdigit((unsigned char)r[i + 3]) && r[i + 4] == 's' && (i == 0 || !std::isalnum((unsigned char)r[i - 1])))
                return kHigh;
        if (Has(r, "radeon hd") || Has(r, "radeon r5") || Has(r, "radeon r7") || Has(r, "radeon(tm) r5") || Has(r, "radeon(tm) r7") ||
            Has(r, "radeon(tm) hd"))
            return kLow;
        if (Has(r, "radeon r9") || Has(r, "radeon(tm) r9")) return kMedium;
        // "AMD Radeon(TM) Graphics" a secas: la integrada de Ryzen (Vega o RDNA2 de 2 unidades).
        if (Has(r, "radeon(tm) graphics") || Has(r, "radeon graphics")) return kLow;
        return kMedium;
    }
    return kMedium;                                                               // no se reconoce
}

} // namespace gpupreset
