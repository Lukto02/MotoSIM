#pragma once
// Red mínima para jugar por LAN: un socket UDP no bloqueante, direcciones IPv4 y el código de
// invitación (la IP y el puerto del anfitrión escritos en letras, fáciles de dictar). Sin raylib ni
// Jolt, para que los encabezados de Windows no choquen con ellos.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace net {

constexpr uint16_t kDefaultPort = 27015;

struct Address {
    uint32_t ip = 0;                 // orden de host (192.168.0.10 = 0xC0A8000A)
    uint16_t port = 0;
    bool operator==(const Address& o) const { return ip == o.ip && port == o.port; }
    bool operator!=(const Address& o) const { return !(*this == o); }
};
constexpr uint32_t kBroadcast = 0xFFFFFFFFu;

std::string IpToString(uint32_t ip);
// IPv4 de esta PC en la red local, la más probable primero (192.168.*, 10.*, 172.16-31.*).
std::vector<uint32_t> LocalIPv4s();

// Código de invitación: IP + puerto + control, en base 32 sin letras confundibles: "ABCDE-FGH23".
std::string EncodeInvite(uint32_t ip, uint16_t port);
bool DecodeInvite(const std::string& code, uint32_t& ip, uint16_t& port);

class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket() { Close(); }
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    bool Open(uint16_t port);        // 0 = cualquier puerto libre; habilita broadcast
    void Close();
    bool IsOpen() const { return handle != kInvalid; }
    uint16_t LocalPort() const { return localPort; }
    bool Send(const Address& to, const void* data, int size);
    // Un paquete si hay (devuelve su tamaño); -1 si no hay nada. Nunca bloquea.
    int Receive(Address& from, void* data, int maxSize);

private:
#ifdef _WIN32
    using Handle = uintptr_t;
#else
    using Handle = int;
#endif
    static constexpr Handle kInvalid = (Handle)~(Handle)0;
    Handle handle = kInvalid;
    uint16_t localPort = 0;
};

// Serialización simple (little endian, sin alinear).
class Writer {
public:
    template <typename T> void Put(T v)
    {
        const size_t at = data.size();
        data.resize(at + sizeof(T));
        std::memcpy(&data[at], &v, sizeof(T));
    }
    void PutString(const std::string& s, size_t maxLen)
    {
        const size_t n = s.size() < maxLen ? s.size() : maxLen;
        Put<uint8_t>((uint8_t)n);
        data.insert(data.end(), s.begin(), s.begin() + (long)n);
    }
    std::vector<uint8_t> data;
};

class Reader {
public:
    Reader(const uint8_t* d, int n) : p(d), left(n) {}
    template <typename T> T Get()
    {
        T v{};
        if (left < (int)sizeof(T)) {
            ok = false;
            return v;
        }
        std::memcpy(&v, p, sizeof(T));
        p += sizeof(T);
        left -= (int)sizeof(T);
        return v;
    }
    std::string GetString()
    {
        const int n = Get<uint8_t>();
        if (!ok || left < n) {
            ok = false;
            return {};
        }
        std::string s((const char*)p, (size_t)n);
        p += n;
        left -= n;
        return s;
    }
    bool ok = true;

private:
    const uint8_t* p;
    int left;
};

} // namespace net
