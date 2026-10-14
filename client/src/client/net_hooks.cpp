#include "net_hooks.hpp"
#if defined(_WIN32)
#include <winsock2.h>
#include <windows.h>
#include <atomic>
#include <cstring>
#include <vector>
#include "capture/kinds.hpp"
#include "capture/udp_frame.hpp"
#include "hookcore/hookcore.hpp"
#include "overlapped.hpp"
#include "sink.hpp"
namespace fc5mp::net {
namespace {
// Observe-only: every detour calls the original with untouched arguments,
// returns its result unchanged, and restores the caller-visible last-error
// (WSAGetLastError == GetLastError) after logging, which may clobber it.

// Overlapped recvs aren't captured (see d_WSARecvFrom). Their marker carries a
// running count; the first kOverlappedMarkers are all logged, then every
// kOverlappedEvery-th, so the reader can still see the true total.
constexpr uint32_t kOverlappedMarkers = 64;
constexpr uint32_t kOverlappedEvery = 1024;
std::atomic<bool> g_seen[API_COUNT];
std::atomic<uint32_t> g_overlapped{0};
PendingRecvs g_pending;   // process-wide registry of in-flight overlapped recvs
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

std::vector<uint8_t> record(SOCKET s, const sockaddr* sa, int salen,
                            const cap::Span* bufs, size_t nbufs, size_t total){
  auto peer = cap::peer_from_sockaddr(reinterpret_cast<const uint8_t*>(sa),
                                      sa && salen > 0 ? (size_t)salen : 0);
  return cap::encode_udp((uint64_t)s, peer, bufs, nbufs, total);
}
void emit(uint8_t kind, SOCKET s, const sockaddr* sa, int salen,
          const cap::Span* bufs, size_t nbufs, size_t total){
  auto rec = record(s, sa, salen, bufs, nbufs, total);
  client::sink().write(kind, rec.data(), rec.size());
}
void overlapped_recv_marker(SOCKET s){
  uint32_t n = g_overlapped.fetch_add(1) + 1;
  if(n > kOverlappedMarkers && n % kOverlappedEvery) return;
  uint8_t p[12]; uint64_t sock = (uint64_t)s;
  std::memcpy(p, &sock, 8); std::memcpy(p + 8, &n, 4);
  client::sink().write(cap::kind::RECV_PENDING, p, sizeof p);
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
using GetOverlappedResult_t = BOOL (WINAPI*)(HANDLE, LPOVERLAPPED, LPDWORD, BOOL);
sendto_t      o_sendto      = nullptr;
recvfrom_t    o_recvfrom    = nullptr;
WSASendTo_t   o_WSASendTo   = nullptr;
WSARecvFrom_t o_WSARecvFrom = nullptr;
GetOverlappedResult_t o_GetOverlappedResult = nullptr;

int WSAAPI d_sendto(SOCKET s, const char* buf, int len, int flags, const sockaddr* to, int tolen){
  Reentry g;   // set before the call, so nested hooked calls pass through
  int r = o_sendto(s, buf, len, flags, to, tolen);
  DWORD err = GetLastError();
  if(g.active){
    seen(API_SENDTO);
    if(r >= 0){                              // 0-byte datagrams are real too
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
    if(r >= 0 && !(flags & MSG_PEEK)){       // a peek is re-read by a real recv
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
  // Encode BEFORE the call: with overlapped I/O, once the original returns a
  // completion worker may already own (re-arm/free) the buffers.
  std::vector<uint8_t> rec;
  if(g.active){
    auto v = spans(bufs, n);
    size_t total = 0; for(auto& sp : v) total += sp.n;
    rec = record(s, to, tolen, v.data(), v.size(), total);
  }
  int r = o_WSASendTo(s, bufs, n, sent, flags, to, tolen, ov, cr);
  DWORD err = GetLastError();
  if(g.active){
    seen(API_WSASENDTO);
    if(r == 0 || (r == SOCKET_ERROR && err == WSA_IO_PENDING))
      client::sink().write(cap::kind::UDP_SEND, rec.data(), rec.size());
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
      if(ov){
        // Overlapped: record the buffers/peer out-ptrs keyed by this OVERLAPPED*
        // so the completion can be logged when the game reaps it via
        // GetOverlappedResult (d_GetOverlappedResult). Keep the running-count
        // marker as the "issued" diagnostic. We don't touch recvd/from/buffers
        // here — a completion worker may own them until the reap.
        if(r == 0 || (r == SOCKET_ERROR && err == WSA_IO_PENDING)){
          PendingRecv e;
          e.sock = (uint64_t)s;
          for(DWORD i = 0; bufs && i < n; i++)
            e.bufs.push_back({(const uint8_t*)bufs[i].buf, (size_t)bufs[i].len});
          e.from = (const uint8_t*)from;
          e.fromlen = fromlen;
          g_pending.record(ov, std::move(e));
          overlapped_recv_marker(s);
        }
      } else if(r == 0 && recvd){
        auto v = spans(bufs, n);
        emit(cap::kind::UDP_RECV, s, from, fromlen ? *fromlen : 0, v.data(), v.size(), *recvd);
      }
    }
  }
  SetLastError(err);
  return r;
}

// kernel32 GetOverlappedResult is how FC_m64 reaps overlapped recvs (import
// table: no IOCP / WSAGetOverlappedResult / completion routines). Observe-only.
BOOL WINAPI d_GetOverlappedResult(HANDLE h, LPOVERLAPPED ov, LPDWORD xfer, BOOL wait){
  Reentry g;
  BOOL ok = o_GetOverlappedResult(h, ov, xfer, wait);
  DWORD err = GetLastError();
  if(g.active && ok && ov){
    if(auto e = g_pending.take(ov)){
      // Guard against OVERLAPPED* reuse for non-socket I/O: only log if this
      // reap is on the same handle the recv was posted on.
      if((HANDLE)e->sock == h){
        DWORD n = xfer ? *xfer : 0;
        int flen = e->fromlen ? *e->fromlen : 0;
        std::vector<cap::Span> sp; sp.reserve(e->bufs.size());
        for(auto& bl : e->bufs) sp.push_back({bl.first, bl.second});
        emit(cap::kind::UDP_RECV, (SOCKET)e->sock, (const sockaddr*)e->from, flen,
             sp.data(), sp.size(), (size_t)n);
      }
    }
  }
  SetLastError(err);
  return ok;
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
  // Completion-side capture: hook kernel32 GetOverlappedResult so overlapped
  // WSARecvFrom receives (e.g. the PRUDP :6570 socket) are logged when reaped.
  HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
  void* gor = k32 ? (void*)GetProcAddress(k32, "GetOverlappedResult") : nullptr;
  if(!gor || !hook::create(gor, (void*)&d_GetOverlappedResult, (void**)&o_GetOverlappedResult)){
    uint8_t id = API_GETOVERLAPPEDRESULT;
    client::sink().write(cap::kind::HOOK_FAILED, &id, 1);
  }
}
}
#else
namespace fc5mp::net { void install(){} }
#endif
