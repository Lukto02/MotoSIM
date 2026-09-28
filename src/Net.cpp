#include "Net.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cctype>

namespace net {

namespace {

#ifdef _WIN32
// Winsock se inicia una sola vez, la primera vez que se usa la red.
struct WinsockInit {
    bool ok = false;
    WinsockInit()
    {
        WSADATA data;
        ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~WinsockInit()
    {
        if (ok) WSACleanup();
    }
};
bool EnsureWinsock()
{
    static WinsockInit init;
    return init.ok;
}
#else
bool EnsureWinsock() { return true; }
#endif

// Crockford base 32: sin I, L, O ni U (se confunden al dictar o escribir).
const char kAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

uint8_t Check(const uint8_t* b, int n)
{
    uint8_t h = 0x5A;
    for (int i = 0; i < n; ++i) h = (uint8_t)((h * 31) ^ b[i]);
    return h;
}

int Rank(uint32_t ip)                  // cuanto más chico, más probable que sea la LAN de verdad
{
    const uint32_t a = ip >> 24, b = (ip >> 16) & 0xFF;
    if (a == 192 && b == 168) return 0;
    if (a == 10) return 1;
    if (a == 172 && b >= 16 && b < 32) return 2;
    if (a == 169 && b == 254) return 4;  // sin DHCP
    return 3;
}

} // namespace

std::string IpToString(uint32_t ip)
{
    return std::to_string(ip >> 24) + "." + std::to_string((ip >> 16) & 0xFF) + "." + std::to_string((ip >> 8) & 0xFF) + "." +
           std::to_string(ip & 0xFF);
}

std::vector<uint32_t> LocalIPv4s()
{
    std::vector<uint32_t> out;
    if (!EnsureWinsock()) return out;
    char name[256] = {};
    if (gethostname(name, sizeof(name) - 1) != 0) return out;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    addrinfo* list = nullptr;
    if (getaddrinfo(name, nullptr, &hints, &list) != 0) return out;
    for (addrinfo* a = list; a; a = a->ai_next) {
        const uint32_t ip = ntohl(((sockaddr_in*)a->ai_addr)->sin_addr.s_addr);
        if ((ip >> 24) == 127 || std::find(out.begin(), out.end(), ip) != out.end()) continue;
        out.push_back(ip);
    }
    freeaddrinfo(list);
    std::stable_sort(out.begin(), out.end(), [](uint32_t a, uint32_t b) { return Rank(a) < Rank(b); });
    return out;
}

std::string EncodeInvite(uint32_t ip, uint16_t port)
{
    uint8_t b[6] = {(uint8_t)(ip >> 24), (uint8_t)(ip >> 16), (uint8_t)(ip >> 8), (uint8_t)ip, (uint8_t)(port - kDefaultPort), 0};
    b[5] = Check(b, 5);
    uint64_t bits = 0;
    for (uint8_t v : b) bits = (bits << 8) | v;
    std::string s;
    for (int i = 9; i >= 0; --i) s += kAlphabet[(bits >> (5 * i)) & 31];
    return s.substr(0, 5) + "-" + s.substr(5);
}

bool DecodeInvite(const std::string& code, uint32_t& ip, uint16_t& port)
{
    uint64_t bits = 0;
    int count = 0;
    for (char ch : code) {
        char c = (char)std::toupper((unsigned char)ch);
        if (c == '-' || c == ' ') continue;
        if (c == 'O') c = '0';
        if (c == 'I' || c == 'L') c = '1';
        const char* at = std::strchr(kAlphabet, c);
        if (!at || c == 0) return false;
        bits = (bits << 5) | (uint64_t)(at - kAlphabet);
        ++count;
    }
    if (count != 10) return false;
    uint8_t b[6];
    for (int i = 5; i >= 0; --i) {
        b[i] = (uint8_t)(bits & 0xFF);
        bits >>= 8;
    }
    if (Check(b, 5) != b[5]) return false;
    ip = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
    port = (uint16_t)(kDefaultPort + b[4]);
    return true;
}

bool UdpSocket::Open(uint16_t port)
{
    Close();
    if (!EnsureWinsock()) return false;
    const auto s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
    if (s == INVALID_SOCKET) return false;
    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);
    // Sin esto, un paquete a un puerto cerrado hace que el próximo recvfrom falle (WSAECONNRESET).
    BOOL reset = FALSE;
    DWORD bytes = 0;
    WSAIoctl(s, SIO_UDP_CONNRESET, &reset, sizeof(reset), nullptr, 0, &bytes, nullptr, nullptr);
#else
    if (s < 0) return false;
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char*)&yes, sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(s, (sockaddr*)&addr, sizeof(addr)) != 0) {
#ifdef _WIN32
        closesocket(s);
#else
        close(s);
#endif
        return false;
    }
    socklen_t len = sizeof(addr);
    getsockname(s, (sockaddr*)&addr, &len);
    localPort = ntohs(addr.sin_port);
    handle = (Handle)s;
    return true;
}

void UdpSocket::Close()
{
    if (handle == kInvalid) return;
#ifdef _WIN32
    closesocket((SOCKET)handle);
#else
    close(handle);
#endif
    handle = kInvalid;
}

bool UdpSocket::Send(const Address& to, const void* data, int size)
{
    if (handle == kInvalid) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(to.ip);
    addr.sin_port = htons(to.port);
    return sendto((decltype(socket(0, 0, 0)))handle, (const char*)data, size, 0, (sockaddr*)&addr, sizeof(addr)) == size;
}

int UdpSocket::Receive(Address& from, void* data, int maxSize)
{
    if (handle == kInvalid) return -1;
    for (;;) {
        sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        const int n = (int)recvfrom((decltype(socket(0, 0, 0)))handle, (char*)data, maxSize, 0, (sockaddr*)&addr, &len);
        if (n < 0) {
#ifdef _WIN32
            if (WSAGetLastError() == WSAEMSGSIZE) continue;   // paquete de más: se descarta
#endif
            return -1;                                          // nada (o error): se intenta el próximo cuadro
        }
        from.ip = ntohl(addr.sin_addr.s_addr);
        from.port = ntohs(addr.sin_port);
        return n;
    }
}

} // namespace net
