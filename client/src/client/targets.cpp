#include "targets.hpp"
#include "sig/signature.hpp"
#if defined(_WIN32)
#include <windows.h>
#include "capture/record.hpp"
#endif
namespace fc5mp::targets {
const std::vector<Target>& table(){
  static const std::vector<Target> t = {
    // verified unique in FC_m64.dll (sha 00833fae…af2) via fc5analyze
    {"rdv_createsession",
     "55 53 41 56 48 8D AC 24 60 F9 FF FF 48 81 EC A0", 10},
    {"create_coop_leash",
     "55 53 57 41 54 41 56 41 57 48 8D AC 24 F8 FE FF", 11},
    {"do_spawn_player",
     "48 89 E0 55 57 48 8D A8 A8 FF FF FF 48 81 EC 48 01 00 00 80 3D 46 4B E4", 12},
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
void install_all(){
#if defined(_WIN32)
  HMODULE m = GetModuleHandleW(L"FC_m64.dll");
  if(!m) return;
  // module image is mapped; scan its headers for SizeOfImage
  auto base = reinterpret_cast<const uint8_t*>(m);
  auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  auto nt  = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  size_t img = nt->OptionalHeader.SizeOfImage;
  cap::Writer w(std::string(getenv("FC5MP_CAPTURE") ? getenv("FC5MP_CAPTURE")
                                                    : "fc5mp-addresses.bin"));
  for(const auto& [idx, off] : resolve(base, img)){
    uint64_t va = reinterpret_cast<uint64_t>(base) + off;    // resolved address
    w.write(table()[idx].kind, reinterpret_cast<const uint8_t*>(&va), sizeof(va));
  }
  // NOTE: inline MinHook detours for these + the TLS/UDP byte points are
  // installed in the live session (plan T9), where ABI and target-correctness
  // are confirmed against the running game. P1 stays observe-only.
#endif
}
}
