#ifdef _WIN32
#include <windows.h>
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved) {
  (void)h; (void)reason; (void)reserved; return TRUE;
}
#endif
