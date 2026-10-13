#pragma once
namespace fc5mp::net {
// Hooked ws2_32 datagram APIs; the id is the payload of API_SEEN / HOOK_FAILED.
enum Api : unsigned char { API_SENDTO=0, API_RECVFROM=1, API_WSASENDTO=2, API_WSARECVFROM=3, API_COUNT };
// Windows: create observe-only MinHook detours on the ws2_32 datagram APIs
// (caller enables them via hook::enable_all). Logs HOOK_FAILED(api) for any
// export it can't resolve or hook. No-op elsewhere.
void install();
}
