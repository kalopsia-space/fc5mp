# fc5mp P1 — Interception Harness: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the injected client harness — a `version.dll` proxy that loads `fc5mp-client.dll`, which installs a pattern-found inline-hook core and capture scaffolding over Far Cry 5's statically-linked libcurl / OpenSSL / sockets — up to (but not performing) a live 2-player traffic capture.

**Architecture:** Under Proton, Steam loads our proxy `version.dll` (via `WINEDLLOVERRIDES`), which forwards all 16 real exports to the system `version.dll` and, in `DllMain`, loads `fc5mp-client.dll`. The client finds target functions *inside* `FC_m64.dll` by byte-signature (the DLL exports nothing and statically links libcurl/OpenSSL, so there is no import table to hook), installs inline hooks with MinHook, and writes tagged capture records. Pure-logic units (signature scanner, PE export parsing, config, capture records) are OS-independent and unit-tested on Linux with clang; the Windows DLLs are cross-built with mingw-w64 and checked structurally.

**Tech Stack:** C++17; `x86_64-w64-mingw32-g++` 16.2 (cross-build Windows x64 DLLs); system `clang++` (host unit tests); CMake ≥ 3.25 + Ninja; MinHook (pinned, fetched); the Python `fc5analyze` toolkit from P0 (structural checks of built DLLs); Wine (optional DLL smoke-load).

**Spec:** `docs/superpowers/specs/2026-10-13-fc5mp-design.md` (§3 architecture, §4.1 client, §7 error handling, §9 P1 row). Prior feasibility: `docs/superpowers/research/2026-10-13-p0-findings.md`.

## Global Constraints

- **No DRM bypass, no game-file writes.** The harness is loaded via `WINEDLLOVERRIDES`; it never modifies any file under the game install. (Spec §2.)
- **Version lock.** The client hashes `FC_m64.dll` (SHA-256) at startup against a supported-build table and **disables itself** (clean, game runs vanilla) on mismatch or any required signature not found exactly once. Supported build SHA for this machine: `00833fae4d5d70213158a146ca98439b28a8e962b934885ecccd906204880af2`. Never partially hooked. (Spec §7.)
- **Hook only non-protected code.** Targets are statically-linked library functions (libcurl/OpenSSL) and OS APIs, never Denuvo-protected game functions; a target that trips tamper crashes is marked unsafe and moved to a library boundary. (Spec §4.1, §7.)
- **Game install:** `/home/kalopsia/.local/share/Steam/steamapps/common/FarCry5`; DLL at `bin/FC_m64.dll`. Read `FC5_DLL` env with that `bin/FC_m64.dll` as default.
- **P1 stays read-only on the wire.** Hooks in P1 only observe and log; they do not redirect or modify traffic (redirection is P2+). (Spec §9 P1 risk note.)
- **Languages:** client C++; host tests C++ with `<cassert>` + ctest (no test framework dependency).

## Review Focus

- **Game updated / wrong build** → version-lock must disable the harness cleanly and let the game run, never hook a mismatched DLL (Task 7 owns; test in Task 7).
- **A signature matches zero times or more than once in `FC_m64.dll`** → the scanner must report the count and the caller must refuse to hook on anything but exactly one, never hook the first of several (Task 4 owns; test in Task 4).
- **A forwarded `version.dll` export is called before `fc5mp-client.dll` finishes loading** → the proxy must resolve and forward every one of the 16 exports regardless of client state, never deadlock or crash in `DllMain` (Task 3 owns; structural test in Task 3; `DllMain` does no heavy work).
- **Capture file path unwritable / disk full** → the capture writer must fail closed (drop the record, keep the game running), never throw into a hook on the game's thread (Task 6 owns; test in Task 6).
- **Signature mask shorter/longer than the pattern, or all-wildcard** → the scanner must reject a malformed signature rather than match everything (Task 4 owns; test in Task 4).

---

## File Structure

```
client/
  CMakeLists.txt                 # Task 1: two toolchains (host tests, mingw DLLs), ctest
  cmake/mingw-w64-x86_64.cmake   # Task 1: cross toolchain file
  third_party/minhook/           # Task 5: MinHook (fetched, pinned)
  src/
    sig/signature.hpp/.cpp       # Task 4: parse "48 8D ?? 05" sigs, scan buffer, exactly-once
    pe/exports.hpp/.cpp          # Task 2: enumerate a PE's export names (for the forwarder)
    config/config.hpp/.cpp       # Task 8: parse fc5mp.ini (server addr, hook toggles, log path)
    capture/record.hpp/.cpp      # Task 6: encode/append tagged capture records
    hookcore/hookcore.hpp/.cpp   # Task 5: MinHook wrapper (create/enable/disable by address)
    version_proxy/
      version_proxy.def          # Task 3: generated forwarders to system version.dll
      dllmain.cpp                # Task 3: DllMain loads fc5mp-client.dll
    client/
      dllmain.cpp                # Task 7: client entry — version-lock, then install hooks
      targets.hpp/.cpp           # Task 9-prep: signature table for curl/ssl/socket (observe-only)
  tests/host/                    # clang-built, ctest-run host unit tests (<cassert>)
    test_signature.cpp           # Task 4
    test_exports.cpp             # Task 2
    test_config.cpp              # Task 8
    test_record.cpp              # Task 6
  tools/
    gen_version_def.py           # Task 3: emit version_proxy.def from the real version.dll
scripts/
  launch-capture.sh              # Task 9: Steam launch-option helper (user-gated)
docs/superpowers/research/
  2026-10-13-p1-protocol-map.md  # Task 9: the capture deliverable (user-gated)
```

Build model: one CMake project, configured twice — a **host** build (`clang++`, builds `tests/host/*` + the pure-logic libs as host libs, run by ctest) and a **mingw** build (`-DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake`, builds `version.dll` + `fc5mp-client.dll`). The pure-logic sources compile in both.

---

### Task 1: Build scaffolding — dual toolchain, smoke DLL, ctest

**Files:**
- Create: `client/CMakeLists.txt`, `client/cmake/mingw-w64-x86_64.cmake`, `client/tests/host/test_smoke.cpp`, `client/src/version_proxy/dllmain.cpp` (smoke stub)

**Interfaces:**
- Consumes: nothing.
- Produces: a host ctest target that runs, and a mingw build that emits `version.dll` (PE32+). No code interface yet.

- [ ] **Step 1: Write the host smoke test**

```cpp
// client/tests/host/test_smoke.cpp
#include <cassert>
int main() { assert(1 + 1 == 2); return 0; }
```

- [ ] **Step 2: Write the toolchain file and CMake**

```cmake
# client/cmake/mingw-w64-x86_64.cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)
set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++)
set(CMAKE_RC_COMPILER ${TOOLCHAIN_PREFIX}-windres)
set(CMAKE_FIND_ROOT_PATH /usr/${TOOLCHAIN_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
# static-link libgcc/libstdc++ so the DLL needs no mingw runtime DLLs beside it
set(CMAKE_CXX_STANDARD_LIBRARIES "-static -static-libgcc -static-libstdc++")
```

```cmake
# client/CMakeLists.txt
cmake_minimum_required(VERSION 3.25)
project(fc5mp_client CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  # --- mingw build: the injected DLLs ---
  add_library(version SHARED src/version_proxy/dllmain.cpp)
  set_target_properties(version PROPERTIES PREFIX "" OUTPUT_NAME "version")
else()
  # --- host build: unit tests ---
  enable_testing()
  add_executable(test_smoke tests/host/test_smoke.cpp)
  add_test(NAME smoke COMMAND test_smoke)
endif()
```

```cpp
// client/src/version_proxy/dllmain.cpp  (smoke stub; real forwarders in Task 3)
#ifdef _WIN32
#include <windows.h>
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved) {
  (void)h; (void)reason; (void)reserved; return TRUE;
}
#endif
```

- [ ] **Step 3: Run the host build + ctest (must pass)**

Run:
```bash
cd /home/kalopsia/fc5mp/client && cmake -G Ninja -B build-host -DCMAKE_CXX_COMPILER=clang++ && cmake --build build-host && ctest --test-dir build-host --output-on-failure
```
Expected: `smoke` test passes (1/1).

- [ ] **Step 4: Run the mingw build; confirm a PE32+ DLL**

Run:
```bash
cd /home/kalopsia/fc5mp/client && cmake -G Ninja -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake && cmake --build build-win && file build-win/version.dll
```
Expected: `build-win/version.dll: PE32+ executable ... (DLL), x86-64`.

- [ ] **Step 5: Commit**

```bash
cd /home/kalopsia/fc5mp
printf 'client/build-host/\nclient/build-win/\n' >> .gitignore
git add client/CMakeLists.txt client/cmake client/tests/host/test_smoke.cpp client/src/version_proxy/dllmain.cpp .gitignore
git commit -m "feat(p1): dual-toolchain CMake scaffolding (host ctest + mingw DLL)"
```

---

### Task 2: PE export enumeration (host-testable)

**Files:**
- Create: `client/src/pe/exports.hpp`, `client/src/pe/exports.cpp`, `client/tests/host/test_exports.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `namespace fc5mp::pe { std::vector<std::string> export_names(const uint8_t* data, size_t size); }` — parses a PE in a memory buffer and returns its export directory names, in ordinal order. Returns empty for a PE with no export directory.

- [ ] **Step 1: Write the failing test** (runs against the real Proton `version.dll`)

```cpp
// client/tests/host/test_exports.cpp
#include "../../src/pe/exports.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
#include <string>
#include <fstream>
static std::vector<uint8_t> slurp(const char* p){
  std::ifstream f(p, std::ios::binary); std::vector<uint8_t> v((std::istreambuf_iterator<char>(f)),{}); return v;
}
int main(int argc, char** argv){
  assert(argc==2);                       // path to a version.dll passed by ctest
  auto buf = slurp(argv[1]); assert(!buf.empty());
  auto names = fc5mp::pe::export_names(buf.data(), buf.size());
  // the real version.dll exports exactly these 16 symbols
  assert(names.size()==16);
  bool hasVQV=false; for(auto&n:names) if(n=="VerQueryValueW") hasVQV=true;
  assert(hasVQV);
  return 0;
}
```

- [ ] **Step 2: Wire the test in CMake (host branch) and run it (FAIL)**

Add to the host branch of `client/CMakeLists.txt`:
```cmake
  add_executable(test_exports tests/host/test_exports.cpp src/pe/exports.cpp)
  add_test(NAME exports COMMAND test_exports
    "/home/kalopsia/.local/share/Steam/steamapps/common/Proton Hotfix/files/lib/wine/x86_64-windows/version.dll")
```
Run: `cd /home/kalopsia/fc5mp/client && cmake --build build-host && ctest --test-dir build-host -R exports --output-on-failure`
Expected: FAIL to compile/link (`export_names` undefined).

- [ ] **Step 3: Implement the PE export parser**

```cpp
// client/src/pe/exports.hpp
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
namespace fc5mp::pe {
std::vector<std::string> export_names(const uint8_t* data, size_t size);
}
```

```cpp
// client/src/pe/exports.cpp
#include "exports.hpp"
#include <cstring>
namespace {
template<class T> bool rd(const uint8_t* d, size_t sz, size_t off, T& out){
  if(off + sizeof(T) > sz) return false; std::memcpy(&out, d+off, sizeof(T)); return true;
}
// map an RVA to a file offset using the section table
bool rva_to_off(const uint8_t* d, size_t sz, uint32_t rva, uint32_t secCount,
                size_t secTableOff, uint32_t& off){
  for(uint32_t i=0;i<secCount;i++){
    size_t s = secTableOff + i*40;
    uint32_t vaddr, vsize, praw, sraw;
    if(!rd(d,sz,s+12,vaddr)||!rd(d,sz,s+8,vsize)||!rd(d,sz,s+20,praw)||!rd(d,sz,s+16,sraw)) return false;
    uint32_t span = vsize>sraw?vsize:sraw;
    if(rva>=vaddr && rva<vaddr+span){ off = praw + (rva - vaddr); return true; }
  }
  return false;
}
}
namespace fc5mp::pe {
std::vector<std::string> export_names(const uint8_t* d, size_t sz){
  std::vector<std::string> out;
  uint32_t e_lfanew; if(!rd(d,sz,0x3C,e_lfanew)) return out;
  uint32_t sig; if(!rd(d,sz,e_lfanew,sig) || sig!=0x00004550) return out; // "PE\0\0"
  size_t fileHdr = e_lfanew + 4;
  uint16_t nsec, optSize; if(!rd(d,sz,fileHdr+2,nsec)||!rd(d,sz,fileHdr+16,optSize)) return out;
  size_t optHdr = fileHdr + 20;
  uint16_t magic; if(!rd(d,sz,optHdr,magic)) return out;       // 0x20b = PE32+
  size_t dataDirs = optHdr + (magic==0x20b ? 112 : 96);        // export dir is dir[0]
  uint32_t expRva, expSize; if(!rd(d,sz,dataDirs,expRva)||!rd(d,sz,dataDirs+4,expSize)) return out;
  if(!expRva) return out;
  size_t secTable = optHdr + optSize;
  uint32_t expOff; if(!rva_to_off(d,sz,expRva,nsec,secTable,expOff)) return out;
  uint32_t nNames, namePtrRva;
  if(!rd(d,sz,expOff+24,nNames)||!rd(d,sz,expOff+32,namePtrRva)) return out;
  uint32_t namePtrOff; if(!rva_to_off(d,sz,namePtrRva,nsec,secTable,namePtrOff)) return out;
  for(uint32_t i=0;i<nNames;i++){
    uint32_t strRva; if(!rd(d,sz,namePtrOff+i*4,strRva)) break;
    uint32_t strOff; if(!rva_to_off(d,sz,strRva,nsec,secTable,strOff)) break;
    std::string s; while(strOff<sz && d[strOff]) s.push_back((char)d[strOff++]);
    out.push_back(std::move(s));
  }
  return out;
}
}
```

- [ ] **Step 4: Run the test (PASS)**

Run: `cd /home/kalopsia/fc5mp/client && cmake -B build-host -G Ninja -DCMAKE_CXX_COMPILER=clang++ && cmake --build build-host && ctest --test-dir build-host -R exports --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
cd /home/kalopsia/fc5mp
git add client/src/pe client/tests/host/test_exports.cpp client/CMakeLists.txt
git commit -m "feat(p1): host-testable PE export-name parser"
```

---

### Task 3: version.dll proxy — forward all 16 exports, load the client

**Files:**
- Create: `client/tools/gen_version_def.py`, `client/src/version_proxy/version_proxy.def`, update `client/src/version_proxy/dllmain.cpp`, update `client/CMakeLists.txt`
- Test: structural, via the P0 Python toolkit + a host check of the generated `.def`

**Interfaces:**
- Consumes: `fc5mp::pe::export_names` (Task 2) is the reference for the forwarder list; the `.def` is generated from the real `version.dll`.
- Produces: a `version.dll` whose export table re-exports all 16 names as forwarders to the system `version.dll`, and whose `DllMain` calls `LoadLibraryW(L"fc5mp-client.dll")` on `DLL_PROCESS_ATTACH`.

- [ ] **Step 1: Write the .def generator**

```python
# client/tools/gen_version_def.py  — run: python gen_version_def.py <real version.dll> <out.def>
import sys, pefile
pe = pefile.PE(sys.argv[1], fast_load=True)
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXPORT']])
names = [e.name.decode() for e in pe.DIRECTORY_ENTRY_EXPORT.symbols if e.name]
with open(sys.argv[2], "w") as f:
    f.write("LIBRARY version\nEXPORTS\n")
    # forward each export to the WoW64 system copy by name
    for n in names:
        f.write(f"    {n}=version_real.{n} @{len(names) and ''}\n".replace(" @",""))
    # (forwarder form: NAME=version_real.NAME — Task's Step 3 note explains version_real)
print(f"wrote {len(names)} forwarders")
```

- [ ] **Step 2: Generate the .def and verify its contents**

Run:
```bash
cd /home/kalopsia/fc5mp/client && uv --project ../analysis run python tools/gen_version_def.py \
  "/home/kalopsia/.local/share/Steam/steamapps/common/Proton Hotfix/files/lib/wine/x86_64-windows/version.dll" \
  src/version_proxy/version_proxy.def
grep -c '=version_real\.' src/version_proxy/version_proxy.def
```
Expected: prints `wrote 16 forwarders`; grep count is `16`.

Note (Step 3 depends on this): the forwarder target `version_real` is the system `version.dll` copied/renamed beside our proxy, or an absolute forwarder. Under Wine, name the real DLL `version_real.dll` and ship it next to ours; the launch helper (Task 9) copies it. Document this in the `.def` header comment.

- [ ] **Step 3: Implement DllMain that loads the client**

```cpp
// client/src/version_proxy/dllmain.cpp
#ifdef _WIN32
#include <windows.h>
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved){
  (void)reserved;
  if(reason==DLL_PROCESS_ATTACH){
    DisableThreadLibraryCalls(h);
    // keep DllMain minimal: just kick off the client load; no heavy work here
    LoadLibraryW(L"fc5mp-client.dll");
  }
  return TRUE;
}
#endif
```

Update `client/CMakeLists.txt` (mingw branch) to link the `.def`:
```cmake
  add_library(version SHARED src/version_proxy/dllmain.cpp)
  set_target_properties(version PROPERTIES PREFIX "" OUTPUT_NAME "version"
    LINK_FLAGS "${CMAKE_CURRENT_SOURCE_DIR}/src/version_proxy/version_proxy.def")
```

- [ ] **Step 4: Build and structurally verify the proxy exports forward correctly**

Run:
```bash
cd /home/kalopsia/fc5mp/client && cmake --build build-win && cd /home/kalopsia/fc5mp/analysis && uv run python - <<'PY'
import pefile
pe=pefile.PE("/home/kalopsia/fc5mp/client/build-win/version.dll")
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXPORT']])
fwd=[e for e in pe.DIRECTORY_ENTRY_EXPORT.symbols if e.forwarder]
print("forwarders:",len(fwd))
assert len(fwd)==16, fwd
assert all(b"version_real" in e.forwarder for e in fwd)
print("OK all 16 forward to version_real")
PY
```
Expected: `forwarders: 16` and `OK all 16 forward to version_real`.

- [ ] **Step 5: Commit**

```bash
cd /home/kalopsia/fc5mp
git add client/tools/gen_version_def.py client/src/version_proxy/version_proxy.def client/src/version_proxy/dllmain.cpp client/CMakeLists.txt
git commit -m "feat(p1): version.dll proxy forwards all 16 exports and loads the client"
```

---

### Task 4: Signature scanner (host-testable, exactly-once)

**Files:**
- Create: `client/src/sig/signature.hpp`, `client/src/sig/signature.cpp`, `client/tests/host/test_signature.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `namespace fc5mp::sig { struct Sig { std::vector<uint8_t> bytes; std::vector<bool> mask; }; }`
  - `std::optional<Sig> parse(const std::string& pattern)` — `"48 8D ?? 05"` → bytes with `??` wildcards; returns `nullopt` for empty or all-wildcard patterns.
  - `std::vector<size_t> find_all(const uint8_t* data, size_t size, const Sig& s)` — offsets of every match.
  - `std::optional<size_t> find_unique(const uint8_t* data, size_t size, const Sig& s)` — the single offset, or `nullopt` if the count is not exactly 1.

- [ ] **Step 1: Write the failing test** (host, plus a real-DLL exactly-once check)

```cpp
// client/tests/host/test_signature.cpp
#include "../../src/sig/signature.hpp"
#include <cassert>
#include <fstream>
#include <vector>
#include <cstdlib>
using namespace fc5mp::sig;
static std::vector<uint8_t> slurp(const char* p){ std::ifstream f(p,std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),{}); }
int main(int argc, char** argv){
  // parse + wildcard
  auto s = parse("DE AD ?? EF"); assert(s.has_value());
  assert(s->bytes.size()==4 && s->mask.size()==4 && s->mask[2]==false);
  // malformed
  assert(!parse("").has_value());
  assert(!parse("?? ??").has_value());          // all-wildcard rejected
  // find in a small buffer
  std::vector<uint8_t> buf={0xDE,0xAD,0x01,0xEF, 0x00, 0xDE,0xAD,0x99,0xEF};
  assert(find_all(buf.data(),buf.size(),*s).size()==2);
  assert(!find_unique(buf.data(),buf.size(),*s).has_value());  // two matches → none
  auto s2=parse("DE AD 01 EF");
  assert(find_unique(buf.data(),buf.size(),*s2).value()==0);
  // real DLL: the RDV string-load site is a known unique code pattern.
  // 48 8D 15 DC 5E 65 FC == lea rdx,[rip-0x39a1a24] at 0x187ba5245 (from P0).
  if(const char* dll=std::getenv("FC5_DLL")){
    auto d=slurp(dll); if(!d.empty()){
      auto s3=parse("48 8D 15 DC 5E 65 FC");
      assert(find_unique(d.data(),d.size(),*s3).has_value());  // appears exactly once
    }
  }
  return 0;
}
```

- [ ] **Step 2: Wire in CMake (host) and run (FAIL)**

Add to host branch:
```cmake
  add_executable(test_signature tests/host/test_signature.cpp src/sig/signature.cpp)
  add_test(NAME signature COMMAND test_signature)
  set_tests_properties(signature PROPERTIES ENVIRONMENT
    "FC5_DLL=/home/kalopsia/.local/share/Steam/steamapps/common/FarCry5/bin/FC_m64.dll")
```
Run: `cd /home/kalopsia/fc5mp/client && cmake --build build-host && ctest --test-dir build-host -R signature --output-on-failure`
Expected: FAIL (undefined `parse`).

- [ ] **Step 3: Implement**

```cpp
// client/src/sig/signature.hpp
#pragma once
#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>
namespace fc5mp::sig {
struct Sig { std::vector<uint8_t> bytes; std::vector<bool> mask; }; // mask[i]=true means must-match
std::optional<Sig> parse(const std::string& pattern);
std::vector<size_t> find_all(const uint8_t* data, size_t size, const Sig& s);
std::optional<size_t> find_unique(const uint8_t* data, size_t size, const Sig& s);
}
```

```cpp
// client/src/sig/signature.cpp
#include "signature.hpp"
#include <sstream>
namespace fc5mp::sig {
std::optional<Sig> parse(const std::string& pattern){
  std::istringstream is(pattern); std::string tok; Sig s; bool anyFixed=false;
  while(is>>tok){
    if(tok=="??"||tok=="?"){ s.bytes.push_back(0); s.mask.push_back(false); }
    else {
      if(tok.size()!=2) return std::nullopt;
      char* end=nullptr; long v=strtol(tok.c_str(),&end,16);
      if(end!=tok.c_str()+2||v<0||v>255) return std::nullopt;
      s.bytes.push_back((uint8_t)v); s.mask.push_back(true); anyFixed=true;
    }
  }
  if(s.bytes.empty()||!anyFixed) return std::nullopt;
  return s;
}
std::vector<size_t> find_all(const uint8_t* d, size_t n, const Sig& s){
  std::vector<size_t> out; size_t m=s.bytes.size();
  if(m==0||n<m) return out;
  for(size_t i=0;i+m<=n;i++){
    bool ok=true;
    for(size_t j=0;j<m;j++){ if(s.mask[j] && d[i+j]!=s.bytes[j]){ ok=false; break; } }
    if(ok) out.push_back(i);
  }
  return out;
}
std::optional<size_t> find_unique(const uint8_t* d, size_t n, const Sig& s){
  auto all=find_all(d,n,s); if(all.size()==1) return all[0]; return std::nullopt;
}
}
```

- [ ] **Step 4: Run (PASS)**

Run: `cd /home/kalopsia/fc5mp/client && cmake --build build-host && ctest --test-dir build-host -R signature --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
cd /home/kalopsia/fc5mp
git add client/src/sig client/tests/host/test_signature.cpp client/CMakeLists.txt
git commit -m "feat(p1): exactly-once signature scanner (host-tested against FC_m64.dll)"
```

---

### Task 5: Hook core (MinHook wrapper)

**Files:**
- Create: `client/third_party/minhook/` (fetched), `client/src/hookcore/hookcore.hpp`, `client/src/hookcore/hookcore.cpp`, update `client/CMakeLists.txt`
- Test: build-only + symbol presence (MinHook needs a live Windows process to exercise; real exercise is Task 9)

**Interfaces:**
- Consumes: nothing (addresses come from Task 4 at call sites).
- Produces:
  - `namespace fc5mp::hook { bool init(); bool create(void* target, void* detour, void** original); bool enable(void* target); bool enable_all(); void uninit(); }`
  - Thin wrappers over MinHook returning `true` on `MH_OK`.

- [ ] **Step 1: Vendor MinHook (pinned) via CMake FetchContent**

Add near the top of `client/CMakeLists.txt`:
```cmake
include(FetchContent)
FetchContent_Declare(minhook
  GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
  GIT_TAG 4a455528f61b5a375b1f9d44e7d296d47f18bb18)  # v1.3.3
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  FetchContent_MakeAvailable(minhook)
endif()
```
If the network is unavailable at configure time, that is a blocker to report, not work around: MinHook is required for the Windows build (host tests do not need it).

- [ ] **Step 2: Write a build-time symbol test (host cannot run MinHook)**

```cpp
// client/tests/host/test_hookcore_iface.cpp  — compiles the header against a stub to catch signature drift
#include "../../src/hookcore/hookcore.hpp"
#include <cassert>
int main(){
  // pure interface/compile check; real hook behavior is exercised live in Task 9
  bool (*c)(void*,void*,void**) = &fc5mp::hook::create;
  bool (*e)(void*) = &fc5mp::hook::enable;
  (void)c; (void)e; return 0;
}
```

- [ ] **Step 3: Implement the wrapper (guarded so host build compiles without MinHook)**

```cpp
// client/src/hookcore/hookcore.hpp
#pragma once
namespace fc5mp::hook {
bool init();
bool create(void* target, void* detour, void** original);
bool enable(void* target);
bool enable_all();
void uninit();
}
```

```cpp
// client/src/hookcore/hookcore.cpp
#include "hookcore.hpp"
#if defined(_WIN32)
#include <MinHook.h>
namespace fc5mp::hook {
bool init(){ return MH_Initialize()==MH_OK; }
bool create(void* t, void* d, void** o){ return MH_CreateHook(t,d,o)==MH_OK; }
bool enable(void* t){ return MH_EnableHook(t)==MH_OK; }
bool enable_all(){ return MH_EnableHook(MH_ALL_HOOKS)==MH_OK; }
void uninit(){ MH_Uninitialize(); }
}
#else
// host build: no MinHook; symbols exist for interface/compile tests only
namespace fc5mp::hook {
bool init(){ return false; }
bool create(void*, void*, void**){ return false; }
bool enable(void*){ return false; }
bool enable_all(){ return false; }
void uninit(){}
}
#endif
```

Wire both: host test `test_hookcore_iface` links `src/hookcore/hookcore.cpp`; mingw build links MinHook into `fc5mp-client`.

- [ ] **Step 4: Run host compile test (PASS) and confirm mingw links MinHook**

Run:
```bash
cd /home/kalopsia/fc5mp/client && cmake --build build-host && ctest --test-dir build-host -R hookcore --output-on-failure
cmake --build build-win   # must link MinHook into the client (Task 7 adds the client target)
```
Expected: host `hookcore` test passes; mingw build succeeds once the client target exists (Task 7). Until then, build-win may have no client target — that is expected; the host test is the gate for this task.

- [ ] **Step 5: Commit**

```bash
cd /home/kalopsia/fc5mp
echo 'client/third_party/minhook/' >> .gitignore   # fetched, not vendored into git
git add client/src/hookcore client/tests/host/test_hookcore_iface.cpp client/CMakeLists.txt .gitignore
git commit -m "feat(p1): MinHook-backed hook core with host-compilable interface"
```

---

### Task 6: Capture record writer (host-testable, fail-closed)

**Files:**
- Create: `client/src/capture/record.hpp`, `client/src/capture/record.cpp`, `client/tests/host/test_record.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `namespace fc5mp::cap { class Writer { public: explicit Writer(std::string path); bool ok() const; void write(uint8_t kind, const uint8_t* data, size_t n) noexcept; }; }`
  - Record framing: `[u64 ts_ns][u8 kind][u32 len][bytes]`, little-endian, appended. `kind` tags the source (1=curl-url, 2=ssl-write, 3=ssl-read, 4=udp-send, 5=udp-recv). `write` is `noexcept` and drops silently if the file is not open (fail-closed; never throws on the game thread).

- [ ] **Step 1: Write the failing test**

```cpp
// client/tests/host/test_record.cpp
#include "../../src/capture/record.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>
using namespace fc5mp::cap;
int main(){
  const char* path="/tmp/fc5mp_test_capture.bin"; std::remove(path);
  { Writer w(path); assert(w.ok());
    const uint8_t msg[]={'G','E','T'}; w.write(1,msg,3); }
  std::ifstream f(path,std::ios::binary);
  std::vector<uint8_t> v((std::istreambuf_iterator<char>(f)),{});
  assert(v.size()==8+1+4+3);                 // ts + kind + len + payload
  assert(v[8]==1);                            // kind
  uint32_t len; std::memcpy(&len,&v[9],4); assert(len==3);
  assert(v[13]=='G'&&v[14]=='E'&&v[15]=='T');
  // fail-closed: unwritable path never throws
  Writer bad("/nonexistent-dir-xyz/cap.bin"); assert(!bad.ok());
  bad.write(1,(const uint8_t*)"x",1);         // must not throw
  std::remove(path);
  return 0;
}
```

- [ ] **Step 2: Wire in CMake (host) and run (FAIL)**

```cmake
  add_executable(test_record tests/host/test_record.cpp src/capture/record.cpp)
  add_test(NAME record COMMAND test_record)
```
Run: `cd /home/kalopsia/fc5mp/client && cmake --build build-host && ctest --test-dir build-host -R record --output-on-failure`
Expected: FAIL (undefined `Writer`).

- [ ] **Step 3: Implement**

```cpp
// client/src/capture/record.hpp
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <string>
namespace fc5mp::cap {
class Writer {
public:
  explicit Writer(std::string path);
  ~Writer();
  bool ok() const { return f_ != nullptr; }
  void write(uint8_t kind, const uint8_t* data, size_t n) noexcept;
private:
  std::FILE* f_ = nullptr;
};
}
```

```cpp
// client/src/capture/record.cpp
#include "capture/record.hpp"   // adjust include path per CMake include dirs; see note
#include <chrono>
#include <cstring>
namespace fc5mp::cap {
Writer::Writer(std::string path){ f_ = std::fopen(path.c_str(), "ab"); }
Writer::~Writer(){ if(f_) std::fclose(f_); }
void Writer::write(uint8_t kind, const uint8_t* data, size_t n) noexcept {
  if(!f_) return;                              // fail-closed
  uint64_t ts = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  uint32_t len = (uint32_t)n;
  std::fwrite(&ts,8,1,f_); std::fwrite(&kind,1,1,f_);
  std::fwrite(&len,4,1,f_); if(n) std::fwrite(data,1,n,f_);
  std::fflush(f_);
}
}
```
Note: the test includes `"../../src/capture/record.hpp"`; the `.cpp` uses `#include "capture/record.hpp"`. Add `target_include_directories(... src)` for the test target, or change the `.cpp` include to `"record.hpp"`. Use `"record.hpp"` to keep it simple.

- [ ] **Step 4: Run (PASS)**

Run: `cd /home/kalopsia/fc5mp/client && cmake --build build-host && ctest --test-dir build-host -R record --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
cd /home/kalopsia/fc5mp
git add client/src/capture client/tests/host/test_record.cpp client/CMakeLists.txt
git commit -m "feat(p1): fail-closed capture record writer"
```

---

### Task 7: Client entry — version lock, then install hooks

**Files:**
- Create: `client/src/client/dllmain.cpp`, update `client/CMakeLists.txt` (mingw: add `fc5mp-client` target)
- Test: host test for the version-lock decision logic (extracted into a pure function)

**Interfaces:**
- Consumes: `fc5mp::hook` (Task 5), `fc5mp::sig` (Task 4), `fc5mp::cap::Writer` (Task 6).
- Produces:
  - `namespace fc5mp::client { bool build_supported(const std::string& sha_hex); }` — pure, host-testable: true iff the SHA is in the supported table.
  - The Windows `DllMain` (client): on attach, hash `FC_m64.dll` in memory, call `build_supported`; if false, return without hooking (self-disable). The actual hook installation is scaffolded here and populated in Task 8.

- [ ] **Step 1: Write the failing host test for the version gate**

```cpp
// client/tests/host/test_versionlock.cpp
#include "../../src/client/versionlock.hpp"
#include <cassert>
int main(){
  using fc5mp::client::build_supported;
  assert(build_supported("00833fae4d5d70213158a146ca98439b28a8e962b934885ecccd906204880af2"));
  assert(!build_supported("deadbeef"));
  assert(!build_supported(""));
  return 0;
}
```

- [ ] **Step 2: Wire in CMake (host) and run (FAIL)**

```cmake
  add_executable(test_versionlock tests/host/test_versionlock.cpp src/client/versionlock.cpp)
  add_test(NAME versionlock COMMAND test_versionlock)
```
Expected: FAIL (undefined `build_supported`).

- [ ] **Step 3: Implement the pure gate + the Windows client DllMain**

```cpp
// client/src/client/versionlock.hpp
#pragma once
#include <string>
namespace fc5mp::client { bool build_supported(const std::string& sha_hex); }
```
```cpp
// client/src/client/versionlock.cpp
#include "versionlock.hpp"
#include <algorithm>
#include <array>
namespace fc5mp::client {
static const std::array<const char*,1> kSupported = {
  "00833fae4d5d70213158a146ca98439b28a8e962b934885ecccd906204880af2"
};
bool build_supported(const std::string& sha){
  std::string s; s.reserve(sha.size());
  for(char c: sha) s.push_back((char)std::tolower((unsigned char)c));
  for(auto* k: kSupported) if(s==k) return true;
  return false;
}
}
```
```cpp
// client/src/client/dllmain.cpp  (Windows only)
#ifdef _WIN32
#include <windows.h>
#include "versionlock.hpp"
#include "hookcore/hookcore.hpp"
#include "targets.hpp"   // Task 8 populates the signature table
#include <string>
// (SHA-256 of the mapped FC_m64.dll image; a small vendored sha256 or bcrypt.
//  Implementation detail left to the engineer: hash the on-disk bin/FC_m64.dll
//  — simplest and matches the P0 build SHA — via a tiny sha256 over the file.)
static std::string hash_fc_module();   // declared; engineer provides file-hash impl
static DWORD WINAPI Boot(LPVOID){
  if(!fc5mp::client::build_supported(hash_fc_module())) return 0;  // self-disable
  if(!fc5mp::hook::init()) return 0;
  fc5mp::targets::install_all();      // Task 8: observe-only curl/ssl/socket hooks
  fc5mp::hook::enable_all();
  return 0;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID){
  if(reason==DLL_PROCESS_ATTACH){
    DisableThreadLibraryCalls(h);
    CreateThread(nullptr,0,Boot,nullptr,0,nullptr);  // no heavy work in DllMain
  }
  return TRUE;
}
#endif
```
Note: `hash_fc_module` hashes `…/FarCry5/bin/FC_m64.dll` (the file whose SHA P0 recorded). A ~40-line public-domain sha256 is acceptable to vendor; keep it in `src/client/sha256.*`. The engineer writes it with a host test asserting it reproduces the known SHA — fold that into this task if time allows, else it rides Task 9's live check.

- [ ] **Step 4: Run host test (PASS); confirm mingw client target builds**

Run:
```bash
cd /home/kalopsia/fc5mp/client && cmake --build build-host && ctest --test-dir build-host -R versionlock --output-on-failure
```
Expected: `versionlock` passes. (The mingw `fc5mp-client` target fully builds after Task 8 provides `targets.hpp`; here it may be stubbed.)

- [ ] **Step 5: Commit**

```bash
cd /home/kalopsia/fc5mp
git add client/src/client client/tests/host/test_versionlock.cpp client/CMakeLists.txt
git commit -m "feat(p1): client entry with host-tested version-lock self-disable"
```

---

### Task 8: Observe-only hook targets (libcurl / OpenSSL / sockets)

**Files:**
- Create: `client/src/client/targets.hpp`, `client/src/client/targets.cpp`, `client/tests/host/test_targets.cpp`
- Test: host test that the signature table is well-formed and every signature parses and is unique in `FC_m64.dll`

**Interfaces:**
- Consumes: `fc5mp::sig` (Task 4), `fc5mp::hook` (Task 5), `fc5mp::cap::Writer` (Task 6).
- Produces:
  - `namespace fc5mp::targets { struct Target { const char* name; const char* sig; uint8_t kind; }; const std::vector<Target>& table(); void install_all(); }`
  - `install_all()` resolves each `sig` to a unique address in the loaded `FC_m64.dll`, creates a detour that writes a capture record then calls the original. In P1 detours **only observe** (no modification).

- [ ] **Step 1: Write the failing host test** (validates the table, not live hooking)

```cpp
// client/tests/host/test_targets.cpp
#include "../../src/client/targets.hpp"
#include "../../src/sig/signature.hpp"
#include <cassert>
#include <fstream>
#include <vector>
#include <cstdlib>
static std::vector<uint8_t> slurp(const char* p){ std::ifstream f(p,std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),{}); }
int main(){
  const auto& t = fc5mp::targets::table();
  assert(!t.empty());
  for(const auto& e : t){
    auto s = fc5mp::sig::parse(e.sig);
    assert(s.has_value());                       // every signature parses
    assert(e.kind>=1 && e.kind<=5);
  }
  // each signature must be unique in the real DLL (version-lock guarantee)
  if(const char* dll=std::getenv("FC5_DLL")){
    auto d=slurp(dll);
    if(!d.empty()) for(const auto& e: t){
      auto s=fc5mp::sig::parse(e.sig);
      assert(fc5mp::sig::find_unique(d.data(),d.size(),*s).has_value());
    }
  }
  return 0;
}
```

- [ ] **Step 2: Wire in CMake and run (FAIL)**

```cmake
  add_executable(test_targets tests/host/test_targets.cpp src/client/targets.cpp src/sig/signature.cpp)
  add_test(NAME targets COMMAND test_targets)
  set_tests_properties(targets PROPERTIES ENVIRONMENT
    "FC5_DLL=/home/kalopsia/.local/share/Steam/steamapps/common/FarCry5/bin/FC_m64.dll")
```
Expected: FAIL (undefined `table`).

- [ ] **Step 3: Implement the table and install_all**

The signatures below are **placeholders to be replaced with real, verified-unique signatures derived with the P0 `fc5analyze` toolkit** before this test can pass. Deriving them is part of this task: for each target function (e.g. libcurl `curl_easy_setopt`, OpenSSL `SSL_write`/`SSL_read`, Winsock `sendto`/`recvfrom` as used by Storm), locate it in `FC_m64.dll` (via nearby anchor strings or standard prologues), extract a byte pattern, and confirm `find_unique` returns it. Do not commit a signature the host test cannot prove unique.

```cpp
// client/src/client/targets.hpp
#pragma once
#include <cstdint>
#include <vector>
namespace fc5mp::targets {
struct Target { const char* name; const char* sig; uint8_t kind; };
const std::vector<Target>& table();
void install_all();   // Windows: resolve + hook; host: no-op
}
```
```cpp
// client/src/client/targets.cpp
#include "targets.hpp"
#if defined(_WIN32)
#include <windows.h>
#include "hookcore/hookcore.hpp"
#include "sig/signature.hpp"
#include "capture/record.hpp"
#endif
namespace fc5mp::targets {
const std::vector<Target>& table(){
  static const std::vector<Target> t = {
    // name, signature (DERIVE & verify unique via fc5analyze), capture-kind
    {"ssl_write", "<DERIVE: unique prologue of OpenSSL SSL_write in FC_m64>", 2},
    {"ssl_read",  "<DERIVE: unique prologue of OpenSSL SSL_read in FC_m64>",  3},
    {"udp_sendto","<DERIVE: unique site calling sendto in Storm>",            4},
    {"udp_recvfrom","<DERIVE: unique site calling recvfrom in Storm>",        5},
  };
  return t;
}
void install_all(){
#if defined(_WIN32)
  // for each target: find base of FC_m64.dll, scan its .xpdata for the unique
  // signature, MH_CreateHook(addr, detour, &orig). Detours observe-only:
  // write a capture record then tail-call orig. (Detour bodies added with the
  // real signatures; omitted here to avoid committing unverified addresses.)
#endif
}
}
```
Replace each `<DERIVE…>` with a real pattern and add the observe-only detour bodies once the host `targets` test proves each signature unique. The test is the gate: it must pass with real signatures before commit.

- [ ] **Step 4: Derive real signatures, then run the test (PASS)**

Use the P0 toolkit to find each function and extract a ≥12-byte unique signature; paste into the table; then:
Run: `cd /home/kalopsia/fc5mp/client && cmake --build build-host && ctest --test-dir build-host -R targets --output-on-failure`
Expected: PASS (every signature parses and is unique in `FC_m64.dll`).

- [ ] **Step 5: Build the full client DLL and structurally verify**

Run:
```bash
cd /home/kalopsia/fc5mp/client && cmake --build build-win && file build-win/fc5mp-client.dll
```
Expected: `PE32+ ... (DLL), x86-64`.

- [ ] **Step 6: Commit**

```bash
cd /home/kalopsia/fc5mp
git add client/src/client/targets.hpp client/src/client/targets.cpp client/tests/host/test_targets.cpp client/CMakeLists.txt
git commit -m "feat(p1): observe-only curl/ssl/socket hook targets (verified-unique signatures)"
```

---

### Task 9: Live 2-player capture — **USER-GATED (needs the game + a 2nd copy)**

**Files:**
- Create: `scripts/launch-capture.sh`, `docs/superpowers/research/2026-10-13-p1-protocol-map.md`
- No unit tests (this is the discovery deliverable; its output is a documented protocol map).

**Interfaces:**
- Consumes: the built `version.dll` + `fc5mp-client.dll` from Tasks 3/8, and `fc5analyze` for decoding captures.
- Produces: capture files under `~/.fc5mp/captures/` and a written protocol map (UbiServices routes, RdV session create/join messages, the in-game join trigger, Storm packet framing, and whether payloads are encrypted).

- [ ] **Step 1: Write the launch helper**

```bash
#!/usr/bin/env bash
# scripts/launch-capture.sh — prints the Steam launch option to inject the proxy.
set -euo pipefail
BIN="/home/kalopsia/.local/share/Steam/steamapps/common/FarCry5/bin"
echo "1) Copy the built DLLs beside the game (NOT overwriting game files — new files only):"
echo "   cp client/build-win/version.dll        \"$BIN/version.dll\""
echo "   cp client/build-win/fc5mp-client.dll   \"$BIN/fc5mp-client.dll\""
echo "   # and the renamed real loader for forwarding:"
echo "   cp \"\$(find /home/kalopsia/.local/share/Steam/steamapps/common/Proton* -name version.dll -path '*x86_64-windows*' | head -1)\" \"$BIN/version_real.dll\""
echo "2) In Steam → Far Cry 5 → Properties → Launch Options, paste:"
echo "   WINEDLLOVERRIDES=\"version=n,b\" %command%"
echo "3) Launch, start a 2-player co-op session, play ~5 min, quit."
echo "4) Captures land in ~/.fc5mp/captures/ — decode with analysis/ tools."
```

- [ ] **Step 2: Report readiness and STOP for the user**

This task cannot be executed autonomously: it needs Far Cry 5 running with two owned copies on two machines/accounts (Steam forbids one copy running twice). Present the launch helper's output to the user, confirm the DLLs are built, and wait. Per the spec's P1 risk note, keep the capture session short and use the least-sensitive account.

- [ ] **Step 3 (after the user captures): decode and write the protocol map**

Decode `~/.fc5mp/captures/*.bin` with a small reader (reuse the `record.hpp` framing) and `fc5analyze`, then write `docs/superpowers/research/2026-10-13-p1-protocol-map.md`: the UbiServices REST routes and bodies, the RdV websocket login + CreateSession/Join messages, the in-game join trigger, the Storm UDP packet framing, and the encryption determination. This map is the input to P2 (the server) and P3 (the relay).

- [ ] **Step 4: Commit**

```bash
cd /home/kalopsia/fc5mp
git add scripts/launch-capture.sh docs/superpowers/research/2026-10-13-p1-protocol-map.md
git commit -m "feat(p1): capture launch helper and 2-player protocol map"
```

---

## Notes for the executor

- Configure the host build once: `cmake -G Ninja -B build-host -DCMAKE_CXX_COMPILER=clang++`; the mingw build once: `cmake -G Ninja -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake`. Re-run `cmake --build` per task.
- Tasks 1–8 are fully autonomous and buildable now. **Task 9 is the hard stop** — it needs the user and the game.
- Task 8's signatures are the one place real reverse-engineering (not transcription) is required; the host `targets` test is the gate that keeps unverified addresses out of the build.
- Everything stays read-only on the wire in P1; redirection and the fake server are P2+.
