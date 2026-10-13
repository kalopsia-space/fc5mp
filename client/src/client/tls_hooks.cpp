#include "tls_hooks.hpp"
#if defined(_WIN32)
#include <windows.h>
#include <cerrno>
#include <cstdint>
#include <vector>
#include "capture/kinds.hpp"
#include "capture/redact.hpp"
#include "hookcore/hookcore.hpp"
#include "sink.hpp"
namespace fc5mp::tls {
namespace {
// Observe-only: the original is called with untouched args, its return value is
// passed back unchanged, and the caller-visible last-error (curl reads
// WSAGetLastError on SSL_ERROR_SYSCALL) and errno are restored after logging.
using ssl_read_t  = int (*)(void*, void*, int);
using ssl_write_t = int (*)(void*, const void*, int);
ssl_read_t  o_ssl_read  = nullptr;
ssl_write_t o_ssl_write = nullptr;
thread_local bool t_in_hook = false;
struct Reentry {
  bool active;
  Reentry() : active(!t_in_hook) { if(active) t_in_hook = true; }
  ~Reentry() { if(active) t_in_hook = false; }
};
void log_plain(uint8_t kind, void* ssl, const uint8_t* p, size_t n){
  auto rec = cap::encode_tls(reinterpret_cast<uint64_t>(ssl), p, n);
  client::sink().write(kind, rec.data(), rec.size());
}

int d_ssl_read(void* ssl, void* buf, int num){
  Reentry g;
  int r = o_ssl_read(ssl, buf, num);
  DWORD err = GetLastError(); int en = errno;
  // Only >0 transferred bytes are plaintext; <=0 is WANT_READ/close/error and
  // the buffer holds nothing new, so log nothing and leave the error state.
  if(g.active && r > 0)
    log_plain(cap::kind::TLS_READ, ssl, static_cast<const uint8_t*>(buf), (size_t)r);
  SetLastError(err); errno = en;
  return r;
}
int d_ssl_write(void* ssl, const void* buf, int num){
  Reentry g;
  int r = o_ssl_write(ssl, buf, num);
  DWORD err = GetLastError(); int en = errno;
  if(g.active && r > 0)
    log_plain(cap::kind::TLS_WRITE, ssl, static_cast<const uint8_t*>(buf), (size_t)r);
  SetLastError(err); errno = en;
  return r;
}
}  // namespace

void install_at(void* ssl_read, void* ssl_write){
  if(!ssl_read || !hook::create(ssl_read, (void*)&d_ssl_read, (void**)&o_ssl_read)){
    uint8_t id = API_SSL_READ;  client::sink().write(cap::kind::HOOK_FAILED, &id, 1);
  }
  if(!ssl_write || !hook::create(ssl_write, (void*)&d_ssl_write, (void**)&o_ssl_write)){
    uint8_t id = API_SSL_WRITE; client::sink().write(cap::kind::HOOK_FAILED, &id, 1);
  }
}
}
#else
namespace fc5mp::tls { void install_at(void*, void*){} }
#endif
