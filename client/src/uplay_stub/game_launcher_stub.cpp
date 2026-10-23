// Stub replacement for UbisoftGameLauncher.exe
// Parses the base64-encoded exe path from -upc_exe_path, decodes it,
// and launches the game directly — bypassing Ubisoft Connect authentication.
#ifdef _WIN32
#include <windows.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>

static FILE* g_log = nullptr;
static void slog(const char* fmt, ...) {
  if (!g_log) {
    char dir[MAX_PATH];
    GetModuleFileNameA(nullptr, dir, MAX_PATH);
    char* sl = strrchr(dir, '\\');
    if (sl) strcpy(sl + 1, "fc5mp-launcher-stub.txt");
    else strcpy(dir, "fc5mp-launcher-stub.txt");
    g_log = fopen(dir, "w");
    if (!g_log) return;
  }
  va_list ap; va_start(ap, fmt);
  vfprintf(g_log, fmt, ap);
  va_end(ap);
  fflush(g_log);
}

static int b64_val(unsigned char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

static int b64_decode(const char* in, char* out, int outmax) {
  int len = 0;
  for (int i = 0; in[i]; i += 4) {
    if (len + 3 > outmax) break;
    int a = b64_val(in[i]);
    int b = in[i+1] ? b64_val(in[i+1]) : 0;
    int c = (in[i+2] && in[i+2] != '=') ? b64_val(in[i+2]) : 0;
    int d = (in[i+3] && in[i+3] != '=') ? b64_val(in[i+3]) : 0;
    if (a < 0) break;
    unsigned triple = ((unsigned)a << 18) | ((unsigned)b << 12) | ((unsigned)c << 6) | (unsigned)d;
    out[len++] = (triple >> 16) & 0xFF;
    if (in[i+2] && in[i+2] != '=') out[len++] = (triple >> 8) & 0xFF;
    if (in[i+3] && in[i+3] != '=') out[len++] = triple & 0xFF;
    if (!in[i+1] || !in[i+2] || !in[i+3]) break;
  }
  out[len] = 0;
  return len;
}

static const char* find_arg(const char* cmdline, const char* key) {
  const char* p = cmdline;
  int klen = (int)strlen(key);
  while ((p = strstr(p, key)) != nullptr) {
    // Make sure it's preceded by start-of-string or space
    if (p != cmdline && *(p-1) != ' ') { p += klen; continue; }
    // Skip past the key
    p += klen;
    // Skip spaces to find value
    while (*p == ' ') p++;
    return p;
  }
  return nullptr;
}

static int arg_len(const char* p) {
  int n = 0;
  while (p[n] && p[n] != ' ' && p[n] != '\t') n++;
  return n;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR cmdLine, int) {
  slog("stub launcher started\n");
  slog("cmdLine: [%s]\n", cmdLine ? cmdLine : "(null)");

  char exe_path[MAX_PATH] = {};
  char work_dir[MAX_PATH] = {};
  char arguments[1024] = {};

  const char* p;
  int n;

  p = find_arg(cmdLine, "-upc_exe_path");
  if (p) {
    n = arg_len(p);
    slog("exe_path b64: [%.*s]\n", n, p);
    char b64[512] = {};
    memcpy(b64, p, n < 511 ? n : 511);
    b64_decode(b64, exe_path, MAX_PATH - 1);
    slog("exe_path decoded: [%s]\n", exe_path);
  } else {
    slog("ERROR: no -upc_exe_path found\n");
  }

  p = find_arg(cmdLine, "-upc_working_directory");
  if (p) {
    n = arg_len(p);
    char b64[512] = {};
    memcpy(b64, p, n < 511 ? n : 511);
    b64_decode(b64, work_dir, MAX_PATH - 1);
    slog("work_dir decoded: [%s]\n", work_dir);
  }

  p = find_arg(cmdLine, "-upc_arguments");
  if (p && *p && *p != '-') {
    n = arg_len(p);
    slog("arguments b64: [%.*s]\n", n, p);
    char b64[512] = {};
    memcpy(b64, p, n < 511 ? n : 511);
    b64_decode(b64, arguments, 1023);
    slog("arguments decoded: [%s]\n", arguments);
  } else {
    slog("no -upc_arguments value, defaulting to -uplay_steam_mode\n");
    strcpy(arguments, "-uplay_steam_mode");
  }

  // Signal the wait handle so steam.exe doesn't block
  p = find_arg(cmdLine, "-gamelauncher_wait_handle");
  if (p) {
    int hval = atoi(p);
    slog("wait_handle: %d\n", hval);
    HANDLE h = (HANDLE)(uintptr_t)hval;
    if (h) SetEvent(h);
  }

  if (!exe_path[0]) {
    slog("ERROR: exe_path is empty, cannot launch\n");
    return 1;
  }

  char full_cmd[2048];
  if (arguments[0])
    snprintf(full_cmd, sizeof(full_cmd), "\"%s\" %s", exe_path, arguments);
  else
    snprintf(full_cmd, sizeof(full_cmd), "\"%s\"", exe_path);

  slog("launching: %s\n", full_cmd);

  STARTUPINFOA si = {};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi = {};

  BOOL ok = CreateProcessA(
    nullptr, full_cmd, nullptr, nullptr, FALSE,
    0, nullptr, work_dir[0] ? work_dir : nullptr,
    &si, &pi);

  if (ok) {
    slog("game launched, pid=%u\n", pi.dwProcessId);
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    slog("game exited with code %u\n", code);
    CloseHandle(pi.hProcess);
    return (int)code;
  }

  DWORD err = GetLastError();
  slog("CreateProcess FAILED, error=%u\n", err);
  return 2;
}
#endif
