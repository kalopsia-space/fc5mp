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
#include <cstdio>
#include <string>
static const wchar_t* kDllRel = L"FC_m64.dll";

static bool is_game_process(){
  wchar_t exe[MAX_PATH];
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  wchar_t* name = wcsrchr(exe, L'\\');
  if(!name) name = wcsrchr(exe, L'/');
  if(name) name++;
  else name = exe;
  return _wcsicmp(name, L"FarCry5.exe") == 0;
}

static std::string hash_fc_module(){
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
static FILE* g_boot_log = nullptr;
static void blog(const char* fmt, ...) {
  if (!g_boot_log) {
    g_boot_log = fopen("fc5mp-boot.log", "w");
    if (!g_boot_log) return;
  }
  DWORD ms = GetTickCount();
  fprintf(g_boot_log, "[%u.%03u] ", ms / 1000, ms % 1000);
  va_list ap; va_start(ap, fmt);
  vfprintf(g_boot_log, fmt, ap);
  va_end(ap);
  fflush(g_boot_log);
}

static DWORD WINAPI Boot(LPVOID){
  blog("boot thread started, pid=%u tid=%u\n", GetCurrentProcessId(), GetCurrentThreadId());
  if(!is_game_process()){
    blog("not FarCry5.exe, skipping hooks\n");
    if(g_boot_log) fclose(g_boot_log);
    g_boot_log = nullptr;
    return 0;
  }
  boot_mark(0);
  blog("stage 0: game process confirmed\n");

  auto hash = hash_fc_module();
  blog("FC_m64.dll SHA256: %s\n", hash.empty() ? "(failed)" : hash.c_str());
  if(!fc5mp::client::build_supported(hash)) {
    blog("ABORT: version-lock failed\n");
    if(g_boot_log) fclose(g_boot_log);
    return 0;
  }
  boot_mark(1);
  blog("stage 1: version-lock passed\n");

  fc5mp::redirect::load();
  blog("redirect config loaded (active=%d)\n", fc5mp::redirect::active() ? 1 : 0);

  if(!fc5mp::hook::init()) {
    blog("ABORT: MinHook init failed\n");
    if(g_boot_log) fclose(g_boot_log);
    return 0;
  }
  blog("MinHook initialized\n");

  fc5mp::net::install();
  fc5mp::hook::enable_all();
  boot_mark(2);
  blog("stage 2: UDP hooks live\n");

  fc5mp::targets::install_all();
  fc5mp::hook::enable_all();
  fc5mp::redirect::set_tls_ready();
  boot_mark(3);
  blog("stage 3: TLS hooks live, all hooks enabled\n");
  blog("boot complete\n");
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
