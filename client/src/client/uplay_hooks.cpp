#include "uplay_hooks.hpp"
#ifdef _WIN32
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include "hookcore/hookcore.hpp"

namespace fc5mp::uplay {
namespace {

static FILE* g_log = nullptr;
static void ulog(const char* fmt, ...) {
  if (!g_log) g_log = fopen("fc5mp-uplay-hooks.txt", "w");
  if (!g_log) return;
  va_list ap; va_start(ap, fmt);
  vfprintf(g_log, fmt, ap);
  va_end(ap);
  fflush(g_log);
}

static const char kUsername[]  = "FC5MP_Player";
static const char kAccountId[] = "fe525702-a958-4b13-b052-f4af64e83c7d";
static const char kTicket[]    = "fc5mp_stub_ticket";

using Start_t         = int  (*)(unsigned int, unsigned int);
using Update_t        = bool (*)();
using GetNextEvent_t  = bool (*)(void*);
using IsConnected_t   = bool (*)();
using IsOwned_t       = bool (*)(unsigned int);
using IsOffline_t     = bool (*)();
using GetNameUtf8_t   = const char* (*)();
using GetAccountIdUtf8_t = const char* (*)();
using GetTicketUtf8_t    = const char* (*)();
using GetEmailUtf8_t     = const char* (*)();

// UPLAY_GetActivate: int param_1, uint* param_2, uint param_3, void** param_4
using GetActivate_t = unsigned long long (*)(int, unsigned int*, unsigned int, void**);

Start_t         o_Start = nullptr;
Update_t        o_Update = nullptr;
GetNextEvent_t  o_GetNextEvent = nullptr;
IsConnected_t   o_IsConnected = nullptr;
IsOwned_t       o_IsOwned = nullptr;
IsOffline_t     o_IsOffline = nullptr;
GetNameUtf8_t   o_GetNameUtf8 = nullptr;
GetAccountIdUtf8_t o_GetAccountIdUtf8 = nullptr;
GetTicketUtf8_t    o_GetTicketUtf8 = nullptr;
GetEmailUtf8_t     o_GetEmailUtf8 = nullptr;
GetActivate_t   o_GetActivate = nullptr;

static int g_update_count = 0;
static int g_event_count = 0;
static volatile bool g_start_called = false;
static volatile DWORD g_start_tick = 0;

using ExitProcess_t = void (WINAPI*)(UINT);
using TerminateProcess_t = BOOL (WINAPI*)(HANDLE, UINT);
using NtTerminateProcess_t = long (NTAPI*)(HANDLE, long);

static ExitProcess_t o_ExitProcess = nullptr;
static TerminateProcess_t o_TerminateProcess = nullptr;
static NtTerminateProcess_t o_NtTerminateProcess = nullptr;

static void log_caller(const char* func) {
  void* frames[16];
  USHORT n = CaptureStackBackTrace(0, 16, frames, nullptr);
  ulog("  tid=%lu stack (%u frames):\n", (unsigned long)GetCurrentThreadId(), n);
  for (USHORT i = 0; i < n && i < 8; i++) {
    HMODULE mod = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)frames[i], &mod);
    char name[MAX_PATH] = "???";
    if (mod) GetModuleFileNameA(mod, name, MAX_PATH);
    char* slash = strrchr(name, '\\');
    if (!slash) slash = strrchr(name, '/');
    const char* short_name = slash ? slash + 1 : name;
    ulog("    [%u] %p %s+0x%llx\n", i, frames[i], short_name,
         mod ? (unsigned long long)((char*)frames[i] - (char*)mod) : 0ULL);
  }
}

static volatile long g_tp_count = 0;
static volatile long g_ep_count = 0;

static bool too_early_to_exit() {
  if (!g_start_called) return true;
  DWORD elapsed = GetTickCount() - g_start_tick;
  return elapsed < 120000;
}

static void WINAPI d_ExitProcess(UINT code) {
  long c = InterlockedIncrement(&g_ep_count);
  if (too_early_to_exit()) {
    if (c <= 3) {
      ulog("ExitProcess(%u) SUPPRESSED #%ld tid=%lu (suspending thread)\n", code, c,
           (unsigned long)GetCurrentThreadId());
      log_caller("ExitProcess");
    }
    SuspendThread(GetCurrentThread());
    return;
  }
  ulog("ExitProcess(%u) allowed\n", code);
  o_ExitProcess(code);
}

static BOOL WINAPI d_TerminateProcess(HANDLE h, UINT code) {
  if ((h == GetCurrentProcess() || h == (HANDLE)-1) && too_early_to_exit()) {
    long c = InterlockedIncrement(&g_tp_count);
    if (c <= 5)
      ulog("TerminateProcess(self, %u) BLOCKED #%ld tid=%lu (suspending)\n", code, c,
           (unsigned long)GetCurrentThreadId());
    if (c <= 3) log_caller("TerminateProcess");
    SuspendThread(GetCurrentThread());
    return TRUE;
  }
  ulog("TerminateProcess(%p, %u) allowed\n", h, code);
  return o_TerminateProcess(h, code);
}

static long NTAPI d_NtTerminateProcess(HANDLE h, long status) {
  if ((h == GetCurrentProcess() || h == (HANDLE)-1 || h == nullptr) && too_early_to_exit()) {
    long c = InterlockedIncrement(&g_tp_count);
    if (c <= 5)
      ulog("NtTerminateProcess(self, 0x%lx) BLOCKED #%ld tid=%lu (too_early)\n", status, c,
           (unsigned long)GetCurrentThreadId());
    if (c <= 3) log_caller("NtTerminateProcess");
    return 0;
  }
  ulog("NtTerminateProcess(%p, 0x%lx) allowed\n", h, status);
  return o_NtTerminateProcess(h, status);
}

static unsigned char g_activate_ctx[2048] = {};
static int g_activate_ctx_size = 0;

static void load_cached_activation() {
  const char* paths[] = {
    "C:\\users\\steamuser\\AppData\\Local\\Ubisoft Game Launcher\\cache\\activations\\fe525702-a958-4b13-b052-f4af64e83c7d",
    nullptr
  };
  for (int i = 0; paths[i]; i++) {
    FILE* f = fopen(paths[i], "rb");
    if (f) {
      g_activate_ctx_size = (int)fread(g_activate_ctx, 1, sizeof(g_activate_ctx), f);
      fclose(f);
      ulog("loaded cached activation from %s (%d bytes)\n", paths[i], g_activate_ctx_size);
      return;
    }
  }
  ulog("no cached activation found, using zeroed buffer\n");
  g_activate_ctx_size = 256;
}

static unsigned long long d_GetActivate(int param_1, unsigned int* param_2,
                                         unsigned int param_3, void** param_4) {
  ulog("UPLAY_GetActivate(p1=%d, p2=%p [%u], p3=%u, p4=%p) HOOKED\n",
       param_1, param_2, param_2 ? *param_2 : 0, param_3, param_4);

  if (g_activate_ctx_size == 0) load_cached_activation();
  if (param_4) *param_4 = g_activate_ctx;
  ulog("  -> returning 1 with activation ctx at %p (%d bytes)\n",
       g_activate_ctx, g_activate_ctx_size);
  return 1;
}

int d_Start(unsigned int uplay_id, unsigned int flags) {
  g_start_called = true;
  g_start_tick = GetTickCount();
  int real = o_Start(uplay_id, flags);
  ulog("UPLAY_Start(id=%u, flags=%u) real=%d -> returning %d\n", uplay_id, flags, real, real);
  return real;
}

bool d_Update() {
  g_update_count++;
  if (g_update_count <= 3 || (g_update_count % 5000) == 0)
    ulog("UPLAY_Update() [call #%d]\n", g_update_count);
  return true;
}

bool d_GetNextEvent(void* event) {
  g_event_count++;
  if (g_event_count <= 3 || (g_event_count % 5000) == 0)
    ulog("UPLAY_GetNextEvent() [call #%d] -> false\n", g_event_count);
  return false;
}

bool d_IsConnected() {
  return true;
}

bool d_IsOwned(unsigned int uplay_id) {
  ulog("UPLAY_USER_IsOwned(id=%u) -> true (HOOKED)\n", uplay_id);
  return true;
}

bool d_IsOffline() {
  return false;
}

const char* d_GetNameUtf8() {
  return kUsername;
}

const char* d_GetAccountIdUtf8() {
  return kAccountId;
}

const char* d_GetTicketUtf8() {
  return kTicket;
}

const char* d_GetEmailUtf8() {
  return "fc5mp@local";
}

struct HookEntry {
  const char* name;
  void* detour;
  void** original;
};

} // namespace

void install() {
  // Hook exit paths FIRST to prevent premature exits
  {
    void* ep = (void*)GetProcAddress(GetModuleHandleA("kernel32.dll"), "ExitProcess");
    void* tp = (void*)GetProcAddress(GetModuleHandleA("kernel32.dll"), "TerminateProcess");
    void* nt = (void*)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtTerminateProcess");
    if (ep && hook::create(ep, (void*)d_ExitProcess, (void**)&o_ExitProcess))
      ulog("ExitProcess hooked\n");
    if (tp && hook::create(tp, (void*)d_TerminateProcess, (void**)&o_TerminateProcess))
      ulog("TerminateProcess hooked\n");
    if (nt && hook::create(nt, (void*)d_NtTerminateProcess, (void**)&o_NtTerminateProcess))
      ulog("NtTerminateProcess hooked\n");
  }

  // Hook UPLAY_GetActivate in uplay_aux_r164.dll via DLL load notification
  {
    HMODULE aux = GetModuleHandleA("uplay_aux_r164.dll");
    if (aux) {
      void* ga = (void*)GetProcAddress(aux, "UPLAY_GetActivate");
      if (ga && hook::create(ga, (void*)d_GetActivate, (void**)&o_GetActivate))
        ulog("UPLAY_GetActivate hooked at %p (early)\n", ga);
      else
        ulog("UPLAY_GetActivate hook FAILED (ga=%p)\n", ga);
    } else {
      // Use LdrRegisterDllNotification to catch it at load time
      typedef struct _UNICODE_STRING {
        USHORT Length; USHORT MaximumLength; PWSTR Buffer;
      } UNICODE_STRING;
      typedef struct {
        ULONG Flags; const UNICODE_STRING* FullDllName;
        const UNICODE_STRING* BaseDllName; PVOID DllBase;
        ULONG SizeOfImage;
      } LDR_DLL_NOTIFICATION_DATA;
      typedef void (CALLBACK* LDR_DLL_NOTIFICATION_FUNCTION)(ULONG reason,
        const LDR_DLL_NOTIFICATION_DATA* data, PVOID ctx);
      typedef long (NTAPI* LdrRegisterDllNotification_t)(ULONG flags,
        LDR_DLL_NOTIFICATION_FUNCTION fn, PVOID ctx, PVOID* cookie);

      auto reg = (LdrRegisterDllNotification_t)GetProcAddress(
          GetModuleHandleA("ntdll.dll"), "LdrRegisterDllNotification");
      if (reg) {
        static PVOID s_cookie = nullptr;
        auto cb = [](ULONG reason, const LDR_DLL_NOTIFICATION_DATA* data, PVOID) {
          if (reason != 1) return; // 1 = loaded
          if (!data || !data->BaseDllName || !data->BaseDllName->Buffer) return;
          // Check if it's uplay_aux_r164.dll (case-insensitive wide string compare)
          WCHAR target[] = L"uplay_aux_r164.dll";
          PWSTR name = data->BaseDllName->Buffer;
          bool match = true;
          for (int i = 0; target[i]; i++) {
            WCHAR a = name[i]; WCHAR b = target[i];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) { match = false; break; }
          }
          if (!match) return;
          ulog("DLL notification: uplay_aux_r164.dll loaded at %p tid=%lu\n",
               data->DllBase, (unsigned long)GetCurrentThreadId());
          HMODULE mod = (HMODULE)data->DllBase;

          // Patch FUN_1800053c0 (UPC pipe check) at offset +0x53C0
          // to return 0 (success) immediately: xor eax,eax; ret
          {
            unsigned char* pipe_fn = (unsigned char*)mod + 0x53C0;
            DWORD old_prot;
            if (VirtualProtect(pipe_fn, 4, PAGE_EXECUTE_READWRITE, &old_prot)) {
              pipe_fn[0] = 0x31; // xor eax, eax
              pipe_fn[1] = 0xC0;
              pipe_fn[2] = 0xC3; // ret
              VirtualProtect(pipe_fn, 4, old_prot, &old_prot);
              ulog("FUN_53C0 (pipe check) patched to ret 0 at %p\n", pipe_fn);
            } else {
              ulog("FUN_53C0 VirtualProtect FAILED at %p\n", pipe_fn);
            }
          }

          // Direct byte patch UPLAY_GetActivate (MinHook fails under loader lock)
          // Write: mov rax, <d_GetActivate>; jmp rax (12 bytes)
          void* ga = (void*)GetProcAddress(mod, "UPLAY_GetActivate");
          if (ga) {
            DWORD old_prot;
            if (VirtualProtect(ga, 14, PAGE_EXECUTE_READWRITE, &old_prot)) {
              unsigned char* p = (unsigned char*)ga;
              p[0] = 0x48; p[1] = 0xB8; // mov rax, imm64
              *(void**)(p + 2) = (void*)d_GetActivate;
              p[10] = 0xFF; p[11] = 0xE0; // jmp rax
              VirtualProtect(ga, 14, old_prot, &old_prot);
              ulog("UPLAY_GetActivate patched at %p -> %p (direct jmp)\n",
                   ga, (void*)d_GetActivate);
            } else {
              ulog("UPLAY_GetActivate VirtualProtect FAILED at %p\n", ga);
            }
          } else {
            ulog("UPLAY_GetActivate: GetProcAddress failed\n");
          }
        };
        long status = reg(0, (LDR_DLL_NOTIFICATION_FUNCTION)cb, nullptr, &s_cookie);
        ulog("LdrRegisterDllNotification: status=0x%lx cookie=%p\n", status, s_cookie);
      } else {
        ulog("LdrRegisterDllNotification not available, falling back to polling\n");
        CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
          for (int i = 0; i < 6000; i++) {
            Sleep(1);
            HMODULE aux = GetModuleHandleA("uplay_aux_r164.dll");
            if (aux) {
              void* ga = (void*)GetProcAddress(aux, "UPLAY_GetActivate");
              if (ga) {
                DWORD old_prot;
                if (VirtualProtect(ga, 14, PAGE_EXECUTE_READWRITE, &old_prot)) {
                  unsigned char* p = (unsigned char*)ga;
                  p[0] = 0x48; p[1] = 0xB8;
                  *(void**)(p + 2) = (void*)d_GetActivate;
                  p[10] = 0xFF; p[11] = 0xE0;
                  VirtualProtect(ga, 14, old_prot, &old_prot);
                  ulog("UPLAY_GetActivate patched at %p (poll, iter=%d)\n", ga, i);
                }
              }
              return 0;
            }
          }
          ulog("UPLAY_GetActivate: gave up polling\n");
          return 0;
        }, nullptr, 0, nullptr);
      }
    }
  }

  HMODULE uplay = GetModuleHandleA("uplay_r1_loader64.dll");
  if (!uplay) {
    ulog("uplay_r1_loader64.dll not loaded, skipping hooks\n");
    return;
  }
  ulog("uplay_r1_loader64.dll at %p\n", uplay);

  HookEntry entries[] = {
    {"UPLAY_Start",                  (void*)d_Start,          (void**)&o_Start},
    {"UPLAY_Update",                 (void*)d_Update,         (void**)&o_Update},
    {"UPLAY_GetNextEvent",           (void*)d_GetNextEvent,   (void**)&o_GetNextEvent},
    {"UPLAY_USER_IsConnected",       (void*)d_IsConnected,    (void**)&o_IsConnected},
    {"UPLAY_USER_IsOwned",           (void*)d_IsOwned,        (void**)&o_IsOwned},
    {"UPLAY_USER_IsInOfflineMode",   (void*)d_IsOffline,      (void**)&o_IsOffline},
    {"UPLAY_USER_GetNameUtf8",       (void*)d_GetNameUtf8,    (void**)&o_GetNameUtf8},
    {"UPLAY_USER_GetAccountIdUtf8",  (void*)d_GetAccountIdUtf8, (void**)&o_GetAccountIdUtf8},
    {"UPLAY_USER_GetTicketUtf8",     (void*)d_GetTicketUtf8,  (void**)&o_GetTicketUtf8},
    {"UPLAY_USER_GetEmailUtf8",      (void*)d_GetEmailUtf8,   (void**)&o_GetEmailUtf8},
  };

  int ok = 0, fail = 0;
  for (auto& e : entries) {
    void* target = (void*)GetProcAddress(uplay, e.name);
    if (!target) {
      ulog("  %s: GetProcAddress failed\n", e.name);
      fail++;
      continue;
    }
    if (hook::create(target, e.detour, e.original)) {
      ulog("  %s: hooked at %p\n", e.name, target);
      ok++;
    } else {
      ulog("  %s: MinHook create failed\n", e.name);
      fail++;
    }
  }
  ulog("uplay hooks: %d ok, %d failed\n", ok, fail);
}

} // namespace fc5mp::uplay
#endif
