#ifdef _WIN32
#include <windows.h>
// Loading another DLL synchronously inside DllMain runs its CRT init + DllMain
// while we hold the loader lock — a documented deadlock hazard. Defer the client
// load to a worker thread: CreateThread under the loader lock is safe (the new
// thread does not run its body until the lock is released).
static DWORD WINAPI LoadClient(LPVOID){ LoadLibraryW(L"fc5mp-client.dll"); return 0; }
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved){
  (void)reserved;
  if(reason==DLL_PROCESS_ATTACH){
    DisableThreadLibraryCalls(h);
    HANDLE t = CreateThread(nullptr,0,LoadClient,nullptr,0,nullptr);
    if(t) CloseHandle(t);
  }
  return TRUE;
}
#endif
