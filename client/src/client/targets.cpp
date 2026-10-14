#include "targets.hpp"
#include "sig/signature.hpp"
#include "capture/kinds.hpp"
#if defined(_WIN32)
#include <windows.h>
#include "sink.hpp"
#include "tls_hooks.hpp"
#endif
namespace fc5mp::targets {
const std::vector<Target>& table(){
  static const std::vector<Target> t = {
    // verified unique in FC_m64.dll (sha 00833fae…af2) via fc5analyze
    {"rdv_createsession",
     "55 53 41 56 48 8D AC 24 60 F9 FF FF 48 81 EC A0", cap::kind::RDV_CREATESESSION},
    {"create_coop_leash",
     "55 53 57 41 54 41 56 41 57 48 8D AC 24 F8 FE FF", cap::kind::COOP_LEASH},
    {"do_spawn_player",
     "48 89 E0 55 57 48 8D A8 A8 FF FF FF 48 81 EC 48 01 00 00 80 3D 46 4B E4", cap::kind::SPAWN_PLAYER},
    // static OpenSSL 1.0.2n: `mov eax,38h; call __chkstk; sub rsp,rax;
    // cmp [rcx+30h],0 (handshake_func); jnz; SSLerr(func, ..., line)` — the
    // SSL_F_* code and __LINE__ tell SSL_read (0xDF, 1019) from SSL_write (0xD0, 1046)
    {"ssl_read",
     "B8 38 00 00 00 E8 ?? ?? ?? ?? 48 2B E0 48 83 79 30 00 75 28 BA DF 00 00 00 C7 44 24 20 FB 03 00 00",
     cap::kind::SSL_READ_ADDR},
    {"ssl_write",
     "B8 38 00 00 00 E8 ?? ?? ?? ?? 48 2B E0 48 83 79 30 00 75 28 BA D0 00 00 00 C7 44 24 20 16 04 00 00",
     cap::kind::SSL_WRITE_ADDR},
    {"ssl_connect",
     "40 53 B8 20 00 00 00 E8 ?? ?? ?? ?? 48 2B E0 48 83 79 30 00 48 8B D9 75 05 E8 ?? ?? ?? ?? 48 8B 43 08 48 8B CB 48 83 C4 20 5B 48 FF 60 28",
     cap::kind::SSL_CONNECT_ADDR},
  };
  return t;
}
std::vector<std::pair<size_t,size_t>> resolve(const uint8_t* image, size_t size){
  std::vector<std::pair<size_t,size_t>> out;
  const auto& t = table();
  for(size_t i=0;i<t.size();i++){
    auto s = fc5mp::sig::parse(t[i].sig);
    if(!s) continue;
    auto off = fc5mp::sig::find_unique(image, size, *s);
    if(off) out.emplace_back(i, *off);
  }
  return out;
}
std::vector<size_t> missing_indices(const std::vector<std::pair<size_t,size_t>>& resolved){
  const auto& t = table();
  std::vector<bool> found(t.size(), false);
  for(const auto& pr : resolved) if(pr.first < found.size()) found[pr.first] = true;
  std::vector<size_t> miss;
  for(size_t i=0;i<found.size();i++) if(!found[i]) miss.push_back(i);
  return miss;
}
void install_all(){
#if defined(_WIN32)
  auto& w = fc5mp::client::sink();
  // FC_m64.dll may not be mapped yet when this thread runs (loader import order
  // is not guaranteed). Wait for it rather than silently no-op.
  HMODULE m = nullptr;
  for(int i=0;i<200 && !m;i++){                    // up to ~20s
    m = GetModuleHandleW(L"FC_m64.dll");
    if(!m) Sleep(100);
  }
  if(!m){ w.write(cap::kind::MODULE_TIMEOUT, nullptr, 0); return; }
  auto base = reinterpret_cast<const uint8_t*>(m);
  auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  auto nt  = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  size_t img = nt->OptionalHeader.SizeOfImage;
  auto resolved = resolve(base, img);
  void* ssl_read = nullptr; void* ssl_write = nullptr; void* ssl_connect = nullptr;
  for(const auto& pr : resolved){
    uint64_t va = reinterpret_cast<uint64_t>(base) + pr.second;   // resolved address
    w.write(table()[pr.first].kind, reinterpret_cast<const uint8_t*>(&va), sizeof(va));
    if(table()[pr.first].kind == cap::kind::SSL_READ_ADDR)    ssl_read    = reinterpret_cast<void*>(va);
    if(table()[pr.first].kind == cap::kind::SSL_WRITE_ADDR)   ssl_write   = reinterpret_cast<void*>(va);
    if(table()[pr.first].kind == cap::kind::SSL_CONNECT_ADDR) ssl_connect = reinterpret_cast<void*>(va);
  }
  // Install TLS detours on the static OpenSSL entry points. ssl_connect is
  // hooked only when redirect mode is active (skips handshake so the server
  // receives plain HTTP). tls::install_at logs HOOK_FAILED for any it can't hook.
  fc5mp::tls::install_at(ssl_read, ssl_write, ssl_connect);
  // Log any table signature NOT found in the mapped image, so a short/empty
  // capture is self-diagnosing (on-disk uniqueness is verified by the host test;
  // mapped-image presence can only be confirmed here, in the live session).
  for(size_t idx : missing_indices(resolved)){
    uint8_t one = (uint8_t)idx;
    w.write(cap::kind::SIG_MISSING, &one, 1);
  }
  // NOTE: these are address markers only. UDP traffic is captured by the
  // ws2_32 detours in net_hooks.cpp; TLS plaintext (static OpenSSL in FC_m64)
  // is P1.5b.
#endif
}
}
