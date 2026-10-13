#ifdef _WIN32
#include <windows.h>
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved){
  (void)reserved;
  if(reason==DLL_PROCESS_ATTACH){
    DisableThreadLibraryCalls(h);
    LoadLibraryW(L"fc5mp-client.dll");
  }
  return TRUE;
}
#endif
