#ifdef _WIN32
#include <windows.h>
#include "versionlock.hpp"
#include "sha256.hpp"
#include "hookcore/hookcore.hpp"
#include "targets.hpp"
#include "net_hooks.hpp"
#include "redirect.hpp"
#include "sink.hpp"
#include "capture/kinds.hpp"
#include <cstdint>
#include <string>
static const wchar_t* kDllRel = L"FC_m64.dll";
static std::string hash_fc_module(){
  // hash the on-disk bin/FC_m64.dll next to the running exe (matches P0 build SHA)
  wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr,exe,MAX_PATH);
  std::wstring p(exe); auto slash=p.find_last_of(L"\\/");
  std::wstring dir = slash==std::wstring::npos ? L"." : p.substr(0,slash);
  std::wstring dll = dir + L"\\" + kDllRel;
  char path[MAX_PATH*2]; int n=WideCharToMultiByte(CP_UTF8,0,dll.c_str(),-1,path,sizeof(path),nullptr,nullptr);
  if(n<=0) return "";
  return fc5mp::client::sha256_file_hex(path);
}
static void boot_mark(uint8_t stage){
  fc5mp::client::sink().write(fc5mp::cap::kind::BOOT, &stage, 1);
}
static DWORD WINAPI Boot(LPVOID){
  boot_mark(0);                     // boot thread start (before the ~hashing wait)
  if(!fc5mp::client::build_supported(hash_fc_module())) return 0;  // self-disable
  boot_mark(1);                     // version-lock passed
  fc5mp::redirect::load();           // redirect config (no-op if file absent)
  if(!fc5mp::hook::init()) return 0;
  fc5mp::net::install();
  fc5mp::hook::enable_all();        // UDP capture live now, not after FC_m64 maps
  boot_mark(2);                     // UDP hooks live
  fc5mp::targets::install_all();    // may wait up to ~20s for FC_m64.dll; installs TLS
  fc5mp::hook::enable_all();
  boot_mark(3);                     // TLS hooks live (addresses resolved + enabled)
  return 0;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID){
  if(reason==DLL_PROCESS_ATTACH){
    DisableThreadLibraryCalls(h);
    CreateThread(nullptr,0,Boot,nullptr,0,nullptr);
  }
  return TRUE;
}
#endif
