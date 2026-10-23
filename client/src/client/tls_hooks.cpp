#include "tls_hooks.hpp"
#if defined(_WIN32)
#include <winsock2.h>
#include <windows.h>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include "capture/kinds.hpp"
#include "capture/redact.hpp"
#include "hookcore/hookcore.hpp"
#include "redirect.hpp"
#include "sink.hpp"
namespace fc5mp::tls {
namespace {
using ssl_read_t    = int (*)(void*, void*, int);
using ssl_write_t   = int (*)(void*, const void*, int);
using ssl_connect_t = int (*)(void*);
ssl_read_t    o_ssl_read    = nullptr;
ssl_write_t   o_ssl_write   = nullptr;
ssl_connect_t o_ssl_connect = nullptr;
thread_local bool t_in_hook = false;
static FILE* g_tls_log = nullptr;
struct Reentry {
  bool active;
  Reentry() : active(!t_in_hook) { if(active) t_in_hook = true; }
  ~Reentry() { if(active) t_in_hook = false; }
};

void log_plain(uint8_t kind, void* ssl, const uint8_t* p, size_t n){
  auto rec = cap::encode_tls(reinterpret_cast<uint64_t>(ssl), p, n);
  client::sink().write(kind, rec.data(), rec.size());
}
static void tls_trace(const char* fmt, ...){
  if(!g_tls_log){
    wchar_t exepath[MAX_PATH];
    GetModuleFileNameW(nullptr, exepath, MAX_PATH);
    wchar_t* sl = wcsrchr(exepath, L'\\');
    if(sl) wcscpy(sl + 1, L"fc5mp-tls-trace.txt");
    else wcscpy(exepath, L"fc5mp-tls-trace.txt");
    g_tls_log = _wfopen(exepath, L"w");
  }
  if(!g_tls_log) return;
  va_list ap; va_start(ap, fmt);
  vfprintf(g_tls_log, fmt, ap);
  va_end(ap);
  fflush(g_tls_log);
}

int d_ssl_connect(void* ssl){
  int fd = redirect::ssl_get_fd(ssl);
  bool redir = redirect::active() && fd >= 0 && redirect::is_redirected((uint64_t)fd);
  int state_before = *reinterpret_cast<int*>(static_cast<uint8_t*>(ssl) + 0x48);
  if(!redir){
    int ret = o_ssl_connect(ssl);
    return ret;
  }
  fd_set wfds; FD_ZERO(&wfds); FD_SET((SOCKET)fd, &wfds);
  struct timeval wtv = {2, 0};
  int sel = select(0, nullptr, &wfds, nullptr, &wtv);
  int so_err = 0; int so_len = sizeof(so_err);
  getsockopt((SOCKET)fd, SOL_SOCKET, SO_ERROR, (char*)&so_err, &so_len);
  if(sel <= 0 || so_err != 0){
    redirect::unmark_redirected((uint64_t)fd);
    tls_trace("ssl_connect STALE ssl=%p fd=%d sel=%d so_err=%d (unmarked)\n",
              ssl, fd, sel, so_err);
    return o_ssl_connect(ssl);
  }
  auto* base = static_cast<uint8_t*>(ssl);
  *reinterpret_cast<int*>(base + 0x140) = 0; // SSL_VERIFY_NONE
  tls_trace("ssl_connect ssl=%p fd=%d state=0x%x\n", ssl, fd, state_before);
  for(int attempt = 0; attempt < 50; attempt++){
    int ret = o_ssl_connect(ssl);
    if(ret == 1){
      constexpr size_t SSL_VERIFY_RESULT_OFF = 0x17c;
      sockaddr_in pa2{}; int pa2len = sizeof(pa2);
      bool loopback = getpeername((SOCKET)fd, reinterpret_cast<sockaddr*>(&pa2), &pa2len) == 0
                      && pa2.sin_family == AF_INET
                      && (ntohl(pa2.sin_addr.s_addr) >> 24) == 127;
      if(loopback)
        *reinterpret_cast<int*>(base + SSL_VERIFY_RESULT_OFF) = 0; // X509_V_OK
      tls_trace("ssl_connect OK ssl=%p fd=%d attempts=%d loopback=%d\n",
                ssl, fd, attempt+1, loopback ? 1 : 0);
      return 1;
    }
    DWORD wsa = WSAGetLastError();
    if(wsa == 10038){
      tls_trace("ssl_connect NOTSOCK ssl=%p fd=%d attempts=%d\n",
                ssl, fd, attempt+1);
      if(attempt == 4){
        // Persistent NOTSOCK — Wine has corrupted this socket.
        // Create a fresh TCP connection to our server and splice it
        // into the SSL object so the handshake can proceed.
        SOCKET ns = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if(ns != INVALID_SOCKET){
          sockaddr_in sa{};
          sa.sin_family = AF_INET;
          sa.sin_addr.s_addr = redirect::server_ip_nbo();
          sa.sin_port = htons(redirect::rest_port());
          if(::connect(ns, (sockaddr*)&sa, sizeof(sa)) == 0){
            u_long nb = 1;
            ioctlsocket(ns, FIONBIO, &nb);
            int nfd = (int)(intptr_t)ns;
            auto* rbio = *reinterpret_cast<void**>(base + 0x10);
            auto* wbio = *reinterpret_cast<void**>(base + 0x18);
            if(rbio) *reinterpret_cast<int*>(static_cast<uint8_t*>(rbio) + 0x28) = nfd;
            if(wbio && wbio != rbio) *reinterpret_cast<int*>(static_cast<uint8_t*>(wbio) + 0x28) = nfd;
            redirect::unmark_redirected((uint64_t)fd);
            redirect::mark_redirected((uint64_t)nfd);
            *reinterpret_cast<int*>(base + 0x48) = 0x5000;
            tls_trace("ssl_connect REPLACED old_fd=%d new_fd=%d\n", fd, nfd);
            fd = nfd;
            continue;
          } else {
            tls_trace("ssl_connect REPLACE_FAIL wsa=%lu\n", WSAGetLastError());
            closesocket(ns);
          }
        }
      }
      Sleep(50);
      continue;
    }
    if(wsa != 10035 && wsa != 0){
      tls_trace("ssl_connect FAIL ssl=%p fd=%d wsa=%lu attempts=%d\n",
                ssl, fd, wsa, attempt+1);
      return ret;
    }
    fd_set rfds, wfds;
    FD_ZERO(&rfds); FD_SET((SOCKET)fd, &rfds);
    FD_ZERO(&wfds); FD_SET((SOCKET)fd, &wfds);
    struct timeval tv = {0, 100000};
    select(0, &rfds, &wfds, nullptr, &tv);
  }
  tls_trace("ssl_connect TIMEOUT ssl=%p fd=%d\n", ssl, fd);
  WSASetLastError(10035);
  return -1;
}

int d_ssl_read(void* ssl, void* buf, int num){
  Reentry g;
  int r = o_ssl_read(ssl, buf, num);
  DWORD err = GetLastError(); int en = errno;
  int fd = redirect::ssl_get_fd(ssl);
  bool re = fd >= 0 && redirect::is_redirected((uint64_t)fd);
  if(g.active && r > 0){
    log_plain(cap::kind::TLS_READ, ssl, static_cast<const uint8_t*>(buf), (size_t)r);
    if(re)
      tls_trace("ssl_read ssl=%p fd=%d got=%d\n", ssl, fd, r);
  } else if(re && r <= 0){
    tls_trace("ssl_read ssl=%p fd=%d ret=%d wsa=%lu\n", ssl, fd, r, WSAGetLastError());
  }
  SetLastError(err); errno = en;
  return r;
}

int d_ssl_write(void* ssl, const void* buf, int num){
  Reentry g;
  int r = o_ssl_write(ssl, buf, num);
  DWORD err = GetLastError(); int en = errno;
  if(g.active && r > 0){
    log_plain(cap::kind::TLS_WRITE, ssl, static_cast<const uint8_t*>(buf), (size_t)r);
    int fd = redirect::ssl_get_fd(ssl);
    bool re = fd >= 0 && redirect::is_redirected((uint64_t)fd);
    if(re){
      tls_trace("ssl_write ssl=%p fd=%d len=%d first=%.200s\n",
                ssl, fd, r, std::string((const char*)buf, r < 200 ? r : 200).c_str());
      // Wait for the server's response to arrive on the socket before
      // returning to the game. The game's HTTP library polls for data
      // after ssl_write returns and only calls ssl_read if data is ready.
      fd_set rfds;
      FD_ZERO(&rfds); FD_SET((SOCKET)fd, &rfds);
      struct timeval tv = {3, 0};
      select(0, &rfds, nullptr, nullptr, &tv);
    }
  }
  SetLastError(err); errno = en;
  return r;
}
}  // namespace

void install_at(void* ssl_read, void* ssl_write, void* ssl_connect){
  bool r_ok = ssl_read  && hook::create(ssl_read,  (void*)&d_ssl_read,  (void**)&o_ssl_read);
  bool w_ok = ssl_write && hook::create(ssl_write, (void*)&d_ssl_write, (void**)&o_ssl_write);
  bool c_ok = ssl_connect && hook::create(ssl_connect, (void*)&d_ssl_connect, (void**)&o_ssl_connect);
  tls_trace("hook_create: read=%d write=%d connect=%d\n", r_ok?1:0, w_ok?1:0, c_ok?1:0);
  if(!r_ok){ uint8_t id = API_SSL_READ;  client::sink().write(cap::kind::HOOK_FAILED, &id, 1); }
  if(!w_ok){ uint8_t id = API_SSL_WRITE; client::sink().write(cap::kind::HOOK_FAILED, &id, 1); }
  if(!c_ok){ uint8_t id = API_SSL_CONNECT; client::sink().write(cap::kind::HOOK_FAILED, &id, 1); }
}
}
#else
namespace fc5mp::tls { void install_at(void*, void*, void*){} }
#endif
