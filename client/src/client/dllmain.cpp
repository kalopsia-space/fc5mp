#ifdef _WIN32
#include <windows.h>
#include "versionlock.hpp"
#include "sha256.hpp"
#include "hookcore/hookcore.hpp"
#include "targets.hpp"
#include "net_hooks.hpp"
#include "uplay_hooks.hpp"
#include "redirect.hpp"
#include "memdump.hpp"
#include "sink.hpp"
#include "capture/kinds.hpp"
#include <cstdint>
#include <cstdio>
#include <string>
static const wchar_t* kDllRel = L"FC_m64.dll";

enum ProcessKind { PROC_UNKNOWN, PROC_GAME, PROC_UPC };

static ProcessKind classify_process(){
  wchar_t exe[MAX_PATH];
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  wchar_t* name = wcsrchr(exe, L'\\');
  if(!name) name = wcsrchr(exe, L'/');
  if(name) name++;
  else name = exe;
  if(_wcsicmp(name, L"FarCry5.exe") == 0) return PROC_GAME;
  if(_wcsicmp(name, L"upc.exe") == 0) return PROC_UPC;
  if(_wcsicmp(name, L"UbisoftConnect.exe") == 0) return PROC_UPC;
  if(_wcsicmp(name, L"UplayService.exe") == 0) return PROC_UPC;
  return PROC_UNKNOWN;
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
    wchar_t exepath[MAX_PATH];
    GetModuleFileNameW(nullptr, exepath, MAX_PATH);
    wchar_t* sl = wcsrchr(exepath, L'\\');
    if (sl) wcscpy(sl + 1, L"fc5mp-boot.log");
    else wcscpy(exepath, L"fc5mp-boot.log");
    g_boot_log = _wfopen(exepath, L"w");
    if (!g_boot_log) return;
  }
  DWORD ms = GetTickCount();
  fprintf(g_boot_log, "[%u.%03u] ", ms / 1000, ms % 1000);
  va_list ap; va_start(ap, fmt);
  vfprintf(g_boot_log, fmt, ap);
  va_end(ap);
  fflush(g_boot_log);
}

static bool is_steam_mode(){
  const char* cmd = GetCommandLineA();
  return cmd && strstr(cmd, "-uplay_steam_mode") != nullptr;
}

static DWORD WINAPI Boot(LPVOID){
  blog("boot thread started, pid=%u tid=%u\n", GetCurrentProcessId(), GetCurrentThreadId());
  auto kind = classify_process();
  if(kind == PROC_UNKNOWN){
    blog("not a known process, skipping hooks\n");
    if(g_boot_log) fclose(g_boot_log);
    g_boot_log = nullptr;
    return 0;
  }

  if(kind == PROC_UPC){
    blog("UPC process detected — installing DNS+connect hooks only\n");
    fc5mp::redirect::load();
    blog("redirect config loaded (active=%d)\n", fc5mp::redirect::active() ? 1 : 0);
    if(!fc5mp::hook::init()) {
      blog("ABORT: MinHook init failed\n");
      if(g_boot_log) fclose(g_boot_log);
      return 0;
    }
    fc5mp::net::install();
    fc5mp::redirect::set_tls_ready();
    bool en = fc5mp::hook::enable_all();
    blog("hooks enabled=%d, boot complete (UPC mode)\n", en ? 1 : 0);
    return 0;
  }

  boot_mark(0);
  bool steam = is_steam_mode();
  blog("stage 0: game process confirmed (mode=%s)\n", steam ? "steam" : "crack");

  boot_mark(1);
  blog("stage 1: version-lock skipped (Wine perf)\n");

  fc5mp::redirect::load();
  blog("redirect config loaded (active=%d)\n", fc5mp::redirect::active() ? 1 : 0);

  if(!fc5mp::hook::init()) {
    blog("ABORT: MinHook init failed\n");
    if(g_boot_log) fclose(g_boot_log);
    return 0;
  }
  blog("MinHook initialized\n");

  fc5mp::uplay::install();
  blog("uplay hooks created (mode=%s)\n", steam ? "steam" : "crack");

  fc5mp::net::install();
  boot_mark(2);
  blog("stage 2: UDP + uplay hooks created\n");

  fc5mp::targets::install_tls_known();
  blog("TLS detours installed at known offsets\n");

  bool en = fc5mp::hook::enable_all();
  blog("enable_all=%d\n", en ? 1 : 0);

  {
    constexpr uintptr_t RVA_SSL_READ = 0x3dd0560;
    HMODULE m = GetModuleHandleW(L"FC_m64.dll");
    if(m){
      auto addr = reinterpret_cast<const uint8_t*>(m) + RVA_SSL_READ;
      blog("ssl_read @%p first_bytes: %02x %02x %02x %02x %02x %02x\n",
           addr, addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
    }
  }

  fc5mp::redirect::set_tls_ready();
  boot_mark(3);
  blog("stage 3: TLS detours live, hooks enabled\n");

  // Memory dump: if FC5MP_DUMP=1, wait for Denuvo to decrypt code during
  // startup, then dump FC_m64.dll's memory to disk for decompilation.
  const char* dump_env = getenv("FC5MP_DUMP");
  if (dump_env && dump_env[0] == '1') {
    int delay = 30;
    const char* delay_env = getenv("FC5MP_DUMP_DELAY");
    if (delay_env) { int d = atoi(delay_env); if (d > 0) delay = d; }
    blog("memory dump scheduled in %d seconds (FC5MP_DUMP=1)\n", delay);
    Sleep(delay * 1000);
    blog("starting FC_m64.dll memory dump...\n");
    fc5mp::memdump::dump_fc_module();
    blog("memory dump complete\n");
  }

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
