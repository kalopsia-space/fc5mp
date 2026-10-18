#ifdef _WIN32
#include <windows.h>

extern "C" __declspec(dllexport) void* UPLAY_GetActivate() {
    return nullptr;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved) {
    (void)h; (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(h);
    return TRUE;
}

#endif
