// Windows integration test (run under Wine): hook two stand-in functions with
// the SSL_read / SSL_write ABI via tls::install_at, drive them, then parse the
// capture and check: transferred bytes logged per SSL*, secrets redacted, the
// caller's buffer untouched, <=0 returns not logged, last-error + errno kept.
//
// The stand-ins are ordinary functions, so MinHook relocates a compiler
// prologue rather than FC_m64's exact `mov eax,38h; call __chkstk`. That real
// 5-byte relocation is validated separately by the host offset test
// (tests/host/test_targets.cpp, RVAs 0x3dd0560 / 0x3dd09c0); here the subject
// is the detour behaviour, not the prologue copy.
#include <windows.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "capture/kinds.hpp"
#include "client/sink.hpp"
#include "client/tls_hooks.hpp"
#include "hookcore/hookcore.hpp"

static int fails = 0;
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } }while(0)

// Scripted fakes: return value + the error state the real OpenSSL would leave.
static int g_ret = 0;
static std::string g_src;        // bytes a read delivers into the caller buffer
static DWORD g_err = 0;
static int g_errno = 0;
static volatile int g_sink = 0;  // defeat tail-call / identical-folding of the two fakes

// Keep these out of inlining so install_at has a real address to patch.
__attribute__((noinline)) int real_ssl_read(void* ssl, void* buf, int num){
  g_sink += (int)(uintptr_t)ssl + num;
  if(g_ret > 0){ int k = g_ret < num ? g_ret : num; std::memcpy(buf, g_src.data(), (size_t)k); }
  SetLastError(g_err); errno = g_errno;
  return g_ret;
}
__attribute__((noinline)) int real_ssl_write(void* ssl, const void* buf, int num){
  g_sink += (int)(uintptr_t)buf - num - (int)(uintptr_t)ssl;
  SetLastError(g_err); errno = g_errno;
  return g_ret;
}

struct Frame { uint8_t kind; std::vector<uint8_t> payload; };
static std::vector<Frame> read_frames(const char* path){
  std::vector<Frame> out;
  FILE* f = std::fopen(path, "rb"); if(!f) return out;
  std::vector<uint8_t> v; uint8_t buf[4096]; size_t n;
  while((n = std::fread(buf, 1, sizeof buf, f)) > 0) v.insert(v.end(), buf, buf + n);
  std::fclose(f);
  for(size_t off = 0; off + 13 <= v.size();){
    uint32_t len; std::memcpy(&len, &v[off + 9], 4);
    if(off + 13 + len > v.size()) break;
    out.push_back({v[off + 8], std::vector<uint8_t>(v.begin() + off + 13, v.begin() + off + 13 + len)});
    off += 13 + len;
  }
  return out;
}

// decode a TLS frame payload: [u64 ssl][bytes]
static uint64_t frame_ssl(const Frame& f){ uint64_t s = 0; if(f.payload.size() >= 8) std::memcpy(&s, f.payload.data(), 8); return s; }
static std::string frame_body(const Frame& f){ return f.payload.size() >= 8 ? std::string(f.payload.begin() + 8, f.payload.end()) : std::string(); }

int main(){
  char tmp[MAX_PATH]; GetTempPathA(MAX_PATH, tmp);
  std::string cap = std::string(tmp) + "fc5mp_test_tls_hooks.bin";
  DeleteFileA(cap.c_str());
  _putenv_s("FC5MP_CAPTURE", cap.c_str());

  CHECK(fc5mp::hook::init());
  fc5mp::tls::install_at((void*)&real_ssl_read, (void*)&real_ssl_write);
  CHECK(fc5mp::hook::enable_all());

  void* sslA = (void*)0x1111; void* sslB = (void*)0x2222;
  char rb[256];

  // 1) SSL_write: a request with an Authorization header and a JSON ticket,
  //    both must be masked in the capture; caller's buffer unchanged.
  std::string req =
    "POST /v3/profiles/sessions HTTP/1.1\r\nAuthorization: Ubi_v1 t=SECRET\r\n\r\n"
    "{\"ticket\":\"abc.def\",\"name\":\"bob\"}";
  std::string req_before = req;
  g_ret = (int)req.size(); g_err = 0; g_errno = 0;
  CHECK(real_ssl_write(sslA, req.data(), (int)req.size()) == (int)req.size());
  CHECK(req == req_before);                 // observe-only: source not modified

  // 2) SSL_read on a second connection returns a body with a sessionKey.
  g_src = "{\"sessionKey\":\"zzz\",\"ok\":true}"; g_ret = (int)g_src.size();
  CHECK(real_ssl_read(sslB, rb, sizeof rb) == (int)g_src.size());
  CHECK(std::memcmp(rb, g_src.data(), g_src.size()) == 0);  // original data still delivered

  // 3) partial read: only `r` bytes are plaintext even if the buffer is bigger.
  g_src = std::string(512, 'Z'); g_ret = 10;   // r < num and r < g_src
  CHECK(real_ssl_read(sslA, rb, sizeof rb) == 10);

  // 4) <=0 returns (WANT_READ / closed / error) log nothing, keep error state.
  g_ret = 0;  g_err = 0;              CHECK(real_ssl_read(sslA, rb, sizeof rb) == 0);
  g_ret = -1; g_err = 10035; g_errno = 11;    // WSAEWOULDBLOCK, EAGAIN
  CHECK(real_ssl_read(sslA, rb, sizeof rb) == -1);
  CHECK(GetLastError() == 10035 && errno == 11);   // preserved across the detour
  CHECK(real_ssl_write(sslB, "x", 1) == -1);       // g_ret still -1
  CHECK(GetLastError() == 10035 && errno == 11);

  fc5mp::hook::uninit();

  auto frames = read_frames(cap.c_str());
  int writes = 0, reads = 0, failed = 0;
  std::string write_body, readB_body; size_t partial_len = 0;
  for(auto& f : frames){
    if(f.kind == fc5mp::cap::kind::HOOK_FAILED) failed++;
    if(f.kind == fc5mp::cap::kind::TLS_WRITE){
      writes++; CHECK(frame_ssl(f) == 0x1111); write_body = frame_body(f);
    }
    if(f.kind == fc5mp::cap::kind::TLS_READ){
      reads++;
      if(frame_ssl(f) == 0x2222) readB_body = frame_body(f);
      else if(frame_ssl(f) == 0x1111) partial_len = frame_body(f).size();
    }
  }
  std::printf("frames=%zu writes=%d reads=%d hook_failed=%d\n",
              frames.size(), writes, reads, failed);
  CHECK(failed == 0);
  CHECK(writes == 1);                 // the one >0 write; the -1 write not logged
  CHECK(reads == 2);                  // connection B body + the partial read on A
  CHECK(write_body.find("SECRET") == std::string::npos);     // header masked
  CHECK(write_body.find("abc.def") == std::string::npos);    // ticket masked
  CHECK(write_body.find("\"name\":\"bob\"") != std::string::npos);   // non-secret kept
  CHECK(readB_body.find("zzz") == std::string::npos);        // sessionKey masked
  CHECK(readB_body.find("\"ok\":true") != std::string::npos);
  CHECK(partial_len == 10);           // only the transferred bytes, not the buffer

  std::printf("%s (%d failures)\n", fails ? "FAILURES" : "OK", fails);
  return fails ? 1 : 0;
}
