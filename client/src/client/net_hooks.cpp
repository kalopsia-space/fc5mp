#include "net_hooks.hpp"
#if defined(_WIN32)
#include <winsock2.h>
#include <windows.h>
#include <atomic>
#include <vector>
#include "capture/kinds.hpp"
#include "capture/udp_frame.hpp"
#include "hookcore/hookcore.hpp"
#include "sink.hpp"
namespace fc5mp::net {
namespace {
// Observe-only: every detour calls the original with untouched arguments,
// returns its result unchanged, and restores the caller-visible last-error
// (WSAGetLastError == GetLastError) after logging, which may clobber it.

constexpr uint32_t kMaxPendingMarkers = 64;
std::atomic<bool> g_seen[API_COUNT];
std::atomic<uint32_t> g_pending{0};
thread_local bool t_in_hook = false;

// Nested calls from the same thread pass straight through unlogged, so if a
// ws2_32 build implements one hooked API via another, each datagram is still
// logged once, under the API the game called. (Not the case in the Wine
// tested so far; kept as cheap insurance for other Proton builds.)
struct Reentry {
  bool active;
  Reentry() : active(!t_in_hook) { if(active) t_in_hook = true; }
  ~Reentry() { if(active) t_in_hook = false; }
};

void seen(Api api){
  if(!g_seen[api].exchange(true)){
    uint8_t id = api;
    client::sink().write(cap::kind::API_SEEN, &id, 1);
  }
}

void emit(uint8_t kind, SOCKET s, const sockaddr* sa, int salen,
          const cap::Span* bufs, size_t nbufs, size_t total){
  auto peer = cap::peer_from_sockaddr(reinterpret_cast<const uint8_t*>(sa),
                                      sa && salen > 0 ? (size_t)salen : 0);
  auto rec = cap::encode_udp((uint64_t)s, peer, bufs, nbufs, total);
  client::sink().write(kind, rec.data(), rec.size());
}

std::vector<cap::Span> spans(const WSABUF* b, DWORD n){
  std::vector<cap::Span> v; v.reserve(n);
  for(DWORD i = 0; b && i < n; i++) v.push_back({(const uint8_t*)b[i].buf, (size_t)b[i].len});
  return v;
}

using sendto_t = int (WSAAPI*)(SOCKET, const char*, int, int, const sockaddr*, int);
using recvfrom_t = int (WSAAPI*)(SOCKET, char*, int, int, sockaddr*, int*);
using WSASendTo_t = int (WSAAPI*)(SOCKET, LPWSABUF, DWORD, LPDWORD, DWORD, const sockaddr*, int,
                                  LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);
using WSARecvFrom_t = int (WSAAPI*)(SOCKET, LPWSABUF, DWORD, LPDWORD, LPDWORD, sockaddr*, LPINT,
                                    LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);
sendto_t      o_sendto      = nullptr;
recvfrom_t    o_recvfrom    = nullptr;
WSASendTo_t   o_WSASendTo   = nullptr;
WSARecvFrom_t o_WSARecvFrom = nullptr;

int WSAAPI d_sendto(SOCKET s, const char* buf, int len, int flags, const sockaddr* to, int tolen){
  Reentry g;   // set before the call, so nested hooked calls pass through
  int r = o_sendto(s, buf, len, flags, to, tolen);
  DWORD err = GetLastError();
  if(g.active){
    seen(API_SENDTO);
    if(r > 0){
      cap::Span sp{(const uint8_t*)buf, (size_t)len};
      emit(cap::kind::UDP_SEND, s, to, tolen, &sp, 1, (size_t)r);
    }
  }
  SetLastError(err);
  return r;
}

int WSAAPI d_recvfrom(SOCKET s, char* buf, int len, int flags, sockaddr* from, int* fromlen){
  Reentry g;
  int r = o_recvfrom(s, buf, len, flags, from, fromlen);
  DWORD err = GetLastError();
  if(g.active){
    seen(API_RECVFROM);
    if(r > 0 && !(flags & MSG_PEEK)){       // a peek is re-read by a real recv
      cap::Span sp{(const uint8_t*)buf, (size_t)len};
      emit(cap::kind::UDP_RECV, s, from, fromlen ? *fromlen : 0, &sp, 1, (size_t)r);
    }
  }
  SetLastError(err);
  return r;
}

int WSAAPI d_WSASendTo(SOCKET s, LPWSABUF bufs, DWORD n, LPDWORD sent, DWORD flags,
                       const sockaddr* to, int tolen, LPWSAOVERLAPPED ov,
                       LPWSAOVERLAPPED_COMPLETION_ROUTINE cr){
  Reentry g;
  int r = o_WSASendTo(s, bufs, n, sent, flags, to, tolen, ov, cr);
  DWORD err = GetLastError();
  if(g.active){
    seen(API_WSASENDTO);
    // buffers are fully populated at call time, so a pending send is loggable too
    if(r == 0 || err == WSA_IO_PENDING){
      auto v = spans(bufs, n);
      size_t total = 0; for(auto& sp : v) total += sp.n;
      emit(cap::kind::UDP_SEND, s, to, tolen, v.data(), v.size(), total);
    }
  }
  SetLastError(err);
  return r;
}

int WSAAPI d_WSARecvFrom(SOCKET s, LPWSABUF bufs, DWORD n, LPDWORD recvd, LPDWORD flags,
                         sockaddr* from, LPINT fromlen, LPWSAOVERLAPPED ov,
                         LPWSAOVERLAPPED_COMPLETION_ROUTINE cr){
  Reentry g;
  DWORD in_flags = flags ? *flags : 0;      // in/out param: read before the call
  int r = o_WSARecvFrom(s, bufs, n, recvd, flags, from, fromlen, ov, cr);
  DWORD err = GetLastError();
  if(g.active){
    seen(API_WSARECVFROM);
    if(!(in_flags & MSG_PEEK)){
      if(r == 0 && recvd){
        auto v = spans(bufs, n);
        emit(cap::kind::UDP_RECV, s, from, fromlen ? *fromlen : 0, v.data(), v.size(), *recvd);
      } else if(r == SOCKET_ERROR && err == WSA_IO_PENDING
                && g_pending.fetch_add(1) < kMaxPendingMarkers){
        // completion-side capture is deferred until Phase A shows Storm uses it
        uint64_t sock = (uint64_t)s;
        client::sink().write(cap::kind::RECV_PENDING, (const uint8_t*)&sock, sizeof(sock));
      }
    }
  }
  SetLastError(err);
  return r;
}
}  // namespace

void install(){
  struct Spec { const char* name; void* detour; void** orig; };
  const Spec specs[API_COUNT] = {
    {"sendto",      (void*)&d_sendto,      (void**)&o_sendto},
    {"recvfrom",    (void*)&d_recvfrom,    (void**)&o_recvfrom},
    {"WSASendTo",   (void*)&d_WSASendTo,   (void**)&o_WSASendTo},
    {"WSARecvFrom", (void*)&d_WSARecvFrom, (void**)&o_WSARecvFrom},
  };
  HMODULE ws = LoadLibraryW(L"ws2_32.dll");
  for(uint8_t i = 0; i < API_COUNT; i++){
    void* t = ws ? (void*)GetProcAddress(ws, specs[i].name) : nullptr;
    if(!t || !hook::create(t, specs[i].detour, specs[i].orig))
      client::sink().write(cap::kind::HOOK_FAILED, &i, 1);
  }
}
}
#else
namespace fc5mp::net { void install(){} }
#endif
