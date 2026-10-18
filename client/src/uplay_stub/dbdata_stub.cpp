#ifdef _WIN32
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>

static FILE* g_log = nullptr;
static int g_call_count = 0;

static void dblog(const char* fmt, ...) {
    if (!g_log) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fflush(g_log);
}

// Vtable layout reverse-engineered from denuvosanctuary/ubi-dbdata:
//  [0] is_token_loaded(this) -> u32          : 1 = token ready
//  [1] return_0() -> i64                     : always 0
//  [2] get_cached_or_fresh_token(this, buf, len) -> bool : true = have token
//  [3] invalidate_cached_token(this) -> void
//  [4] get_buffer(this, *len) -> *token_data : returns Denuvo token bytes
//  [5] new_thread_get_burn_ticket_res(this, param) -> *void
//  [6] get_thread(this, *param) -> *void
//  [7] get_ownership_buffer(this, *len) -> *void
//  [8] get_dlcs(this, *count) -> *i32
//  [9] set_arg_to_0(this, *arg) -> void

// Fake token: FC5 had Denuvo removed in 2019, so the game may accept any
// non-empty token. We provide a plausible-looking one.
static const char g_fake_token[] = "fc5mp_denuvo_token_placeholder_v1";
static uint32_t g_fake_dlcs[] = { 5067, 5068, 5104 };

static uint32_t __fastcall slot0_is_token_loaded(void* thisptr) {
    dblog("  [0] is_token_loaded(this=%p) -> 0\n", thisptr);
    return 0;
}

static int64_t __fastcall slot1_return_0(void* thisptr) {
    dblog("  [1] return_0() -> 0\n");
    return 0;
}

static int32_t __fastcall slot2_get_cached_or_fresh_token(void* thisptr, void* token_buf, int32_t length) {
    dblog("  [2] get_cached_or_fresh_token(this=%p, buf=%p, len=%d) -> 0 (no token)\n",
          thisptr, token_buf, length);
    return 0;
}

static void __fastcall slot3_invalidate_cached_token(void* thisptr) {
    dblog("  [3] invalidate_cached_token(this=%p)\n", thisptr);
}

static const void* __fastcall slot4_get_buffer(void* thisptr, uint64_t* out_length) {
    dblog("  [4] get_buffer(this=%p, out_len=%p) -> null (no token)\n",
          thisptr, out_length);
    if (out_length) *out_length = 0;
    return nullptr;
}

static const void* __fastcall slot5_new_thread_get_burn_ticket_res(void* thisptr, int64_t param) {
    dblog("  [5] new_thread_get_burn_ticket_res(this=%p, param=%lld) -> null\n",
          thisptr, (long long)param);
    return nullptr;
}

static const void* __fastcall slot6_get_thread(void* thisptr, uint64_t* param) {
    dblog("  [6] get_thread(this=%p, param=%p) -> null\n", thisptr, param);
    return nullptr;
}

static const void* __fastcall slot7_get_ownership_buffer(void* thisptr, uint64_t* out_length) {
    dblog("  [7] get_ownership_buffer(this=%p, out_len=%p) -> empty\n", thisptr, out_length);
    if (out_length) *out_length = 0;
    return "";
}

static const int32_t* __fastcall slot8_get_dlcs(void* thisptr, int64_t* out_count) {
    dblog("  [8] get_dlcs(this=%p, out_count=%p) -> 3 dlcs\n", thisptr, out_count);
    if (out_count) *out_count = 3;
    return (const int32_t*)g_fake_dlcs;
}

static void __fastcall slot9_set_arg_to_0(void* thisptr, uint64_t* arg) {
    dblog("  [9] set_arg_to_0(this=%p, arg=%p)\n", thisptr, arg);
    if (arg) *arg = 0;
}

typedef void* (*vtable_fn)();

static vtable_fn g_vtable[10] = {
    (vtable_fn)slot0_is_token_loaded,
    (vtable_fn)slot1_return_0,
    (vtable_fn)slot2_get_cached_or_fresh_token,
    (vtable_fn)slot3_invalidate_cached_token,
    (vtable_fn)slot4_get_buffer,
    (vtable_fn)slot5_new_thread_get_burn_ticket_res,
    (vtable_fn)slot6_get_thread,
    (vtable_fn)slot7_get_ownership_buffer,
    (vtable_fn)slot8_get_dlcs,
    (vtable_fn)slot9_set_arg_to_0,
};

static struct {
    vtable_fn* vptr;
} g_fake_interface = { g_vtable };

extern "C" __declspec(dllexport)
void* getGameTokenInterface(void* param1, unsigned long long param2) {
    g_call_count++;
    dblog("getGameTokenInterface(p1=%p, p2=%llu, call#%d) -> NULL\n",
          param1, param2, g_call_count);
    return nullptr;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        wchar_t dllpath[512];
        GetModuleFileNameW(h, dllpath, 511);
        wchar_t* slash = wcsrchr(dllpath, L'\\');
        if (slash) wcscpy(slash + 1, L"dbdata_stub.log");
        else wcscpy(dllpath, L"dbdata_stub.log");
        g_log = _wfopen(dllpath, L"w");
        if (g_log) { fprintf(g_log, "=== dbdata stub loaded ===\n"); fflush(g_log); }
    }
    if (reason == DLL_PROCESS_DETACH) {
        if (g_log) { fprintf(g_log, "=== dbdata stub unloading (calls=%d) ===\n", g_call_count); fclose(g_log); }
    }
    return TRUE;
}

#endif
