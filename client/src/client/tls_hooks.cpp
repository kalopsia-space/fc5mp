#include "tls_hooks.hpp"
#if defined(_WIN32)
#include <winsock2.h>
#include <windows.h>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <vector>
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
  if(!g_tls_log) g_tls_log = fopen("fc5mp-tls-trace.txt", "w");
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
    tls_trace("ssl_connect ENTER ssl=%p fd=%d redir=0 state=0x%x\n",
              ssl, fd, state_before);
    int ret = o_ssl_connect(ssl);
    tls_trace("ssl_connect DONE ssl=%p fd=%d ret=%d\n", ssl, fd, ret);
    return ret;
  }
  // Redirected connection: disable cert verification + busy-poll handshake
  auto* base = static_cast<uint8_t*>(ssl);
  *reinterpret_cast<int*>(base + 0x140) = 0; // SSL_VERIFY_NONE
  tls_trace("ssl_connect ENTER ssl=%p fd=%d redir=1 state=0x%x\n",
            ssl, fd, state_before);
  // Busy-poll: retry ssl_connect with select() waits, up to 5s total
  for(int attempt = 0; attempt < 50; attempt++){
    int ret = o_ssl_connect(ssl);
    if(ret == 1){
      int st = *reinterpret_cast<int*>(base + 0x48);
      tls_trace("ssl_connect OK ssl=%p fd=%d attempts=%d state=0x%x\n",
                ssl, fd, attempt+1, st);
      return 1;
    }
    DWORD wsa = WSAGetLastError();
    if(wsa != 10035 && wsa != 0){
      int st = *reinterpret_cast<int*>(base + 0x48);
      tls_trace("ssl_connect FAIL ssl=%p fd=%d ret=%d wsa=%lu state=0x%x attempts=%d\n",
                ssl, fd, ret, wsa, st, attempt+1);
      return ret;
    }
    // Wait for socket to become readable or writable (100ms timeout per try)
    fd_set rfds, wfds;
    FD_ZERO(&rfds); FD_SET((SOCKET)fd, &rfds);
    FD_ZERO(&wfds); FD_SET((SOCKET)fd, &wfds);
    struct timeval tv = {0, 100000}; // 100ms
    select(0, &rfds, &wfds, nullptr, &tv);
  }
  int st = *reinterpret_cast<int*>(base + 0x48);
  tls_trace("ssl_connect TIMEOUT ssl=%p fd=%d state=0x%x\n", ssl, fd, st);
  WSASetLastError(10035);
  return -1;
}

int d_ssl_read(void* ssl, void* buf, int num){
  Reentry g;
  int r = o_ssl_read(ssl, buf, num);
  DWORD err = GetLastError(); int en = errno;
  if(g.active){
    if(r > 0){
      log_plain(cap::kind::TLS_READ, ssl, static_cast<const uint8_t*>(buf), (size_t)r);
      int fd = redirect::ssl_get_fd(ssl);
      bool re = fd >= 0 && redirect::is_redirected((uint64_t)fd);
      if(re) tls_trace("ssl_read ssl=%p fd=%d len=%d first=%.40s\n",
                        ssl, fd, r, std::string((const char*)buf, r < 40 ? r : 40).c_str());
    }
  }
  SetLastError(err); errno = en;
  return r;
}
int d_ssl_write(void* ssl, const void* buf, int num){
  Reentry g;
  int r = o_ssl_write(ssl, buf, num);
  DWORD err = GetLastError(); int en = errno;
  if(g.active){
    if(r > 0){
      log_plain(cap::kind::TLS_WRITE, ssl, static_cast<const uint8_t*>(buf), (size_t)r);
      int fd = redirect::ssl_get_fd(ssl);
      bool re = fd >= 0 && redirect::is_redirected((uint64_t)fd);
      if(re) tls_trace("ssl_write ssl=%p fd=%d len=%d first=%.80s\n",
                        ssl, fd, r, std::string((const char*)buf, r < 80 ? r : 80).c_str());
    }
  }
  SetLastError(err); errno = en;
  return r;
}
}  // namespace

void install_at(void* ssl_read, void* ssl_write, void* ssl_connect){
  if(!ssl_read || !hook::create(ssl_read, (void*)&d_ssl_read, (void**)&o_ssl_read)){
    uint8_t id = API_SSL_READ;  client::sink().write(cap::kind::HOOK_FAILED, &id, 1);
  }
  if(!ssl_write || !hook::create(ssl_write, (void*)&d_ssl_write, (void**)&o_ssl_write)){
    uint8_t id = API_SSL_WRITE; client::sink().write(cap::kind::HOOK_FAILED, &id, 1);
  }
  if(!ssl_connect || !hook::create(ssl_connect, (void*)&d_ssl_connect, (void**)&o_ssl_connect)){
    uint8_t id = API_SSL_CONNECT; client::sink().write(cap::kind::HOOK_FAILED, &id, 1);
  }
}
}
#else
namespace fc5mp::tls { void install_at(void*, void*, void*){} }
#endif
