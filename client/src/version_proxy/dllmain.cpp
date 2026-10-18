#ifdef _WIN32
#include <windows.h>
static HINSTANCE g_hInst;
static DWORD WINAPI LoadClient(LPVOID){
  wchar_t path[MAX_PATH];
  GetModuleFileNameW(g_hInst, path, MAX_PATH);
  wchar_t* slash = wcsrchr(path, L'\\');
  if(!slash) slash = wcsrchr(path, L'/');
  if(slash) slash[1] = 0;
  else path[0] = 0;
  wcscat(path, L"fc5mp-client.dll");
  LoadLibraryW(path);
  return 0;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved){
  (void)reserved;
  if(reason==DLL_PROCESS_ATTACH){
    g_hInst = h;
    DisableThreadLibraryCalls(h);
    HANDLE t = CreateThread(nullptr,0,LoadClient,nullptr,0,nullptr);
    if(t) CloseHandle(t);
  }
  return TRUE;
}
#endif
