#pragma once
namespace fc5mp::net {
// Hooked ws2_32 datagram APIs; the id is the payload of API_SEEN / HOOK_FAILED.
enum Api : unsigned char { API_SENDTO=0, API_RECVFROM=1, API_WSASENDTO=2, API_WSARECVFROM=3, API_COUNT=4 };
constexpr unsigned char API_GETOVERLAPPEDRESULT = 6;   // HOOK_FAILED id (0-3 ws2_32, 4-5 TLS)
constexpr unsigned char API_GETADDRINFO = 7;
constexpr unsigned char API_CONNECT = 8;
// Windows: create observe-only MinHook detours on the ws2_32 datagram APIs
// (caller enables them via hook::enable_all). Logs HOOK_FAILED(api) for any
// export it can't resolve or hook. No-op elsewhere.
void install();
}
