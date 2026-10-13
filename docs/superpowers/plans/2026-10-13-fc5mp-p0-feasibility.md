# fc5mp P0 — Co-op Widening Feasibility Spike: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Decide, read-only, whether Far Cry 5 co-op stores players in a generic N-player manager (GO: widening is mostly logic patches) or in fixed 2-slot structures (STOP/RE-PLAN), and record the decision with evidence.

**Architecture:** A small Python analysis toolkit runs against the on-disk `FC_m64.dll`. It builds a function table from the PE exception directory, maps game log-strings to the functions that reference them (via RIP-relative LEA scanning), walks the static call graph, and probes operands in partner/player functions. The toolkit emits JSON artifacts; a final human-read report states the gate decision. Nothing is injected, nothing runs the game, no game file is written.

**Tech Stack:** Python 3.14, `pefile` 2024.8.26 (installed), `capstone` 5.x (to be installed via `uv`), `pytest` (to be installed via `uv`). Package manager: `uv`.

**Spec:** `docs/superpowers/specs/2026-10-13-fc5mp-design.md` (see §6.1 for the amended, RTTI-free method; this plan implements P0 of §9).

## Global Constraints

- **Read-only on game files.** The toolkit opens `FC_m64.dll` for reading only. No step may write, move, or patch any file under the game install. Verify mtime unchanged at the end.
- **Game install path:** `/home/kalopsia/.local/share/Steam/steamapps/common/FarCry5/bin/FC_m64.dll`. Tests must not hard-code it elsewhere; read it from env var `FC5_DLL` with that path as default.
- **DLL facts (verified 2026-10-13):** ImageBase `0x180000000`; real code section `.xpdata` (RWX, chars `0xE0000020`) holds `.text`; strings in `.xtls` (chars `0x40000040`); exception directory at RVA `0xE6840D0`, 369,061 entries, located inside section `.L~g`. File-offset→VA: `VA = ImageBase + section.VirtualAddress + (fileOffset - section.PointerToRawData)`.
- **No RTTI for game classes.** Only 856 `.?AV` descriptors exist and all are third-party (CryptoPP/Havok/std). Class grouping comes from string anchors and vtables, never from RTTI.
- **Build artifacts** (strings dump, xref/callgraph JSON, report) go under `analysis/out/` in the repo, which is git-ignored except the final committed report. Do NOT re-dump into `~/.hermes/cache/scratch/` (prunes after ~24h).
- **Capstone API pins (5.x):** operand mem type = `capstone.CS_OP_MEM`; RIP reg = `capstone.x86.X86_REG_RIP`; instruction end = `insn.address + insn.size` (no `next_address`).

## Review Focus

- **DLL at a different build than analyzed** → every tool must assert the DLL SHA-256 matches a recorded value and refuse (clear message) on mismatch, so a Steam update can't silently produce wrong offsets. (Task 1 owns this; test in Task 1.)
- **A log-string anchor appears zero times or many times in code** → `find_xrefs` must return a count, and the report step must treat 0-xref and >8-xref anchors as "inconclusive," never silently pick one. (Task 4 owns; test in Task 4.)
- **LEA displacement arithmetic sign/overflow** → disp32 is signed; a wrong sign points into the wrong section. Scanner must compute `target = instr_va + 7 + signed_disp` and the self-test must recover a known-good anchor (`0x187ba5245 → 0x1841fb128`). (Task 3 owns; test in Task 3.)
- **Exception-directory fragments** → ~369k entries include unwind fragments (chained info) that are not function starts; resolving a code VA to its *primary* function must follow `UNW_FLAG_CHAININFO`, or callers get mis-attributed. (Task 2 owns; test in Task 2.)
- **A VA that falls outside any section** (bad input, corrupt displacement) → `va_to_off`/`read_va` must raise a typed error, not index past buffer end or silently return adjacent bytes. (Task 1 owns; test in Task 1.)

---

## File Structure

```
analysis/
  pyproject.toml            # uv project: deps capstone, pytest, pefile
  fc5analyze/
    __init__.py
    peimage.py              # Task 1: PEImage — load, section map, va<->off, read_va, sha guard
    functable.py            # Task 2: FunctionTable — parse .pdata, resolve VA->primary func
    xref.py                 # Task 3: LEA xref scanner over .xpdata
    strings.py              # Task 4: locate anchor strings in .xtls, map string->code xrefs
    callgraph.py            # Task 5: static direct-call edges within a function
    probe.py                # Task 6: operand probing (cmp imm, scaled index) in a function
  tests/
    test_peimage.py
    test_functable.py
    test_xref.py
    test_strings.py
    test_callgraph.py
    test_probe.py
  anchors.py                # Task 7: curated co-op vs versus anchor string list + runner
  out/                      # git-ignored working artifacts
docs/superpowers/research/
  2026-10-13-p0-findings.md # Task 8: the committed gate report
```

---

### Task 1: PEImage — load, section map, VA/offset, guarded read

**Files:**
- Create: `analysis/pyproject.toml`
- Create: `analysis/fc5analyze/__init__.py` (empty)
- Create: `analysis/fc5analyze/peimage.py`
- Test: `analysis/tests/test_peimage.py`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `class PEImage(path: str)` — attrs `image_base:int`, `sha256:str`, `sections:list[Section]` where `Section` has `name:str, va:int, vsize:int, raw_off:int, raw_size:int, chars:int`.
  - `PEImage.va_to_off(va:int) -> int` — raises `VAError` if `va` is in no section.
  - `PEImage.read_va(va:int, n:int) -> bytes` — raises `VAError` on out-of-range.
  - `PEImage.section_by_name(name:str) -> Section`.
  - `PEImage.assert_sha(expected_hex:str) -> None` — raises `BuildMismatch` if mismatch.
  - Module constant `DEFAULT_DLL = os.environ.get("FC5_DLL", "/home/kalopsia/.local/share/Steam/steamapps/common/FarCry5/bin/FC_m64.dll")`.
  - Exceptions `VAError(Exception)`, `BuildMismatch(Exception)`.

- [ ] **Step 1: Scaffold the uv project**

Run:
```bash
cd /home/kalopsia/fc5mp/analysis 2>/dev/null || (mkdir -p /home/kalopsia/fc5mp/analysis && cd /home/kalopsia/fc5mp/analysis)
cd /home/kalopsia/fc5mp/analysis
cat > pyproject.toml <<'EOF'
[project]
name = "fc5analyze"
version = "0.0.0"
requires-python = ">=3.12"
dependencies = ["pefile==2024.8.26", "capstone>=5.0.1,<6"]

[dependency-groups]
dev = ["pytest>=8"]

[tool.pytest.ini_options]
testpaths = ["tests"]
EOF
mkdir -p fc5analyze tests out
touch fc5analyze/__init__.py
uv sync
```
Expected: `uv sync` creates `.venv` with capstone + pytest. (`out/` already git-ignored by repo `.gitignore` `scratch/`-style rule; add `analysis/out/` to `.gitignore` in Step 7.)

- [ ] **Step 2: Write the failing test**

```python
# analysis/tests/test_peimage.py
import pytest
from fc5analyze.peimage import PEImage, VAError, BuildMismatch, DEFAULT_DLL

def img():
    return PEImage(DEFAULT_DLL)

def test_image_base_and_sections():
    pe = img()
    assert pe.image_base == 0x180000000
    xp = pe.section_by_name(".xpdata")
    assert xp.chars == 0xE0000020          # RWX code
    xt = pe.section_by_name(".xtls")
    assert xt.chars == 0x40000040          # readable data/strings

def test_va_roundtrip_known_string():
    pe = img()
    # "Calling RDV CreateSession" lives at VA 0x1841fb128 (.xtls)
    data = pe.read_va(0x1841FB128, 25)
    assert data == b"Calling RDV CreateSession"

def test_va_out_of_range_raises():
    pe = img()
    with pytest.raises(VAError):
        pe.va_to_off(0x1FFFFFFFF)          # far above image

def test_sha_guard():
    pe = img()
    pe.assert_sha(pe.sha256)               # self matches
    with pytest.raises(BuildMismatch):
        pe.assert_sha("00" * 32)
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cd /home/kalopsia/fc5mp/analysis && uv run pytest tests/test_peimage.py -q`
Expected: FAIL — `ModuleNotFoundError: No module named 'fc5analyze.peimage'`.

- [ ] **Step 4: Write minimal implementation**

```python
# analysis/fc5analyze/peimage.py
import hashlib, os
from dataclasses import dataclass
import pefile

DEFAULT_DLL = os.environ.get(
    "FC5_DLL",
    "/home/kalopsia/.local/share/Steam/steamapps/common/FarCry5/bin/FC_m64.dll",
)

class VAError(Exception): pass
class BuildMismatch(Exception): pass

@dataclass
class Section:
    name: str; va: int; vsize: int; raw_off: int; raw_size: int; chars: int

class PEImage:
    def __init__(self, path: str = DEFAULT_DLL):
        self.path = path
        self._data = open(path, "rb").read()          # read-only
        self.sha256 = hashlib.sha256(self._data).hexdigest()
        pe = pefile.PE(data=self._data, fast_load=True)
        self.image_base = pe.OPTIONAL_HEADER.ImageBase
        self.sections = []
        for s in pe.sections:
            self.sections.append(Section(
                name=s.Name.rstrip(b"\0").decode("latin1"),
                va=self.image_base + s.VirtualAddress,
                vsize=max(s.Misc_VirtualSize, s.SizeOfRawData),
                raw_off=s.PointerToRawData,
                raw_size=s.SizeOfRawData,
                chars=s.Characteristics,
            ))
        self._pe = pe

    def section_by_name(self, name: str) -> Section:
        for s in self.sections:
            if s.name == name:
                return s
        raise VAError(f"no section {name!r}")

    def _sec_for_va(self, va: int) -> Section:
        for s in self.sections:
            if s.va <= va < s.va + s.vsize:
                return s
        raise VAError(f"VA {va:#x} in no section")

    def va_to_off(self, va: int) -> int:
        s = self._sec_for_va(va)
        off = s.raw_off + (va - s.va)
        if not (s.raw_off <= off < s.raw_off + s.raw_size):
            raise VAError(f"VA {va:#x} not backed by raw data")
        return off

    def read_va(self, va: int, n: int) -> bytes:
        off = self.va_to_off(va)
        return self._data[off:off + n]

    def assert_sha(self, expected_hex: str) -> None:
        if self.sha256.lower() != expected_hex.lower():
            raise BuildMismatch(f"DLL sha {self.sha256} != expected {expected_hex}")
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cd /home/kalopsia/fc5mp/analysis && uv run pytest tests/test_peimage.py -q`
Expected: PASS (4 passed).

- [ ] **Step 6: Record the build SHA for later guards**

Run:
```bash
cd /home/kalopsia/fc5mp/analysis
uv run python -c "from fc5analyze.peimage import PEImage; print(PEImage().sha256)" | tee out/dll.sha256
```
Expected: a 64-hex string written to `out/dll.sha256`. (Later tools/report assert against it.)

- [ ] **Step 7: Git-ignore analysis working dir and commit**

```bash
cd /home/kalopsia/fc5mp
grep -q '^analysis/out/' .gitignore || printf 'analysis/out/\nanalysis/.venv/\n' >> .gitignore
git add .gitignore analysis/pyproject.toml analysis/fc5analyze/__init__.py analysis/fc5analyze/peimage.py analysis/tests/test_peimage.py
git commit -m "feat(p0): PEImage loader with VA mapping and build-SHA guard"
```

---

### Task 2: FunctionTable — exception-directory functions and primary-function resolution

**Files:**
- Create: `analysis/fc5analyze/functable.py`
- Test: `analysis/tests/test_functable.py`

**Interfaces:**
- Consumes: `PEImage` from Task 1.
- Produces:
  - `class FunctionTable(pe: PEImage)` — parses the exception directory into sorted function-start VAs.
  - `.starts: list[int]` — sorted unique `RUNTIME_FUNCTION.BeginAddress` VAs that are *primary* (not chained fragments).
  - `.primary_for(va:int) -> int | None` — the start VA of the function containing `va`, following `UNW_FLAG_CHAININFO` to the primary; `None` if `va` is in no known function.
  - `.count: int` — number of raw RUNTIME_FUNCTION entries (expected 369061).

- [ ] **Step 1: Write the failing test**

```python
# analysis/tests/test_functable.py
from fc5analyze.peimage import PEImage
from fc5analyze.functable import FunctionTable

def test_entry_count():
    ft = FunctionTable(PEImage())
    assert ft.count == 369061

def test_known_code_resolves_to_a_function():
    ft = FunctionTable(PEImage())
    # 0x187ba5245 is a LEA inside the RDV CreateSession function (verified)
    fn = ft.primary_for(0x187BA5245)
    assert fn is not None
    assert fn <= 0x187BA5245
    assert fn in ft.starts            # primary is itself a listed start

def test_non_code_va_returns_none():
    ft = FunctionTable(PEImage())
    assert ft.primary_for(0x1841FB128) is None   # a string VA, not code
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd /home/kalopsia/fc5mp/analysis && uv run pytest tests/test_functable.py -q`
Expected: FAIL — `ModuleNotFoundError`.

- [ ] **Step 3: Write minimal implementation**

```python
# analysis/fc5analyze/functable.py
import struct, bisect
from .peimage import PEImage, VAError

UNW_FLAG_CHAININFO = 0x4

class FunctionTable:
    def __init__(self, pe: PEImage):
        self.pe = pe
        d = pe._pe.OPTIONAL_HEADER.DATA_DIRECTORY[3]   # IMAGE_DIRECTORY_ENTRY_EXCEPTION
        base = pe.image_base
        blob = pe.read_va(base + d.VirtualAddress, d.Size)
        self.count = d.Size // 12
        self._begin = []      # begin VA per entry
        self._unwind = []     # unwind-info VA per entry
        for i in range(self.count):
            b, e, u = struct.unpack_from("<III", blob, i * 12)
            self._begin.append(base + b)
            self._unwind.append(base + u)
        order = sorted(range(self.count), key=lambda i: self._begin[i])
        self._begin = [self._begin[i] for i in order]
        self._unwind = [self._unwind[i] for i in order]
        self.starts = sorted(set(self._begin))

    def _chained_parent(self, unwind_va: int) -> int | None:
        # UNWIND_INFO: byte0 = Version(3b)|Flags(5b); byte2 = CountOfCodes
        try:
            hdr = self.pe.read_va(unwind_va, 4)
        except VAError:
            return None
        flags = hdr[0] >> 3
        if not (flags & UNW_FLAG_CHAININFO):
            return None
        count = hdr[2]
        slots = count + (count & 1)                 # pad unwind codes to even
        chain_off = 4 + slots * 2                    # RUNTIME_FUNCTION follows codes
        try:
            cb, ce, cu = struct.unpack("<III", self.pe.read_va(unwind_va + chain_off, 12))
        except VAError:
            return None
        return self.pe.image_base + cb

    def primary_for(self, va: int) -> int | None:
        idx = bisect.bisect_right(self._begin, va) - 1
        if idx < 0:
            return None
        begin = self._begin[idx]
        unwind = self._unwind[idx]
        seen = set()
        while True:
            parent = self._chained_parent(unwind)
            if parent is None:
                return begin
            if parent in seen:
                return begin
            seen.add(parent)
            j = bisect.bisect_left(self._begin, parent)
            if j >= len(self._begin) or self._begin[j] != parent:
                return parent
            begin, unwind = self._begin[j], self._unwind[j]
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd /home/kalopsia/fc5mp/analysis && uv run pytest tests/test_functable.py -q`
Expected: PASS (3 passed). If `test_non_code_va_returns_none` fails because a string VA falls after the last function start, that means `.xtls` sits above `.xpdata` — acceptable; adjust the assertion to pick a VA below the first function start (`ft.starts[0] - 0x10`) instead.

- [ ] **Step 5: Commit**

```bash
cd /home/kalopsia/fc5mp
git add analysis/fc5analyze/functable.py analysis/tests/test_functable.py
git commit -m "feat(p0): FunctionTable from PE exception dir with chain-info resolution"
```

---

### Task 3: LEA xref scanner

**Files:**
- Create: `analysis/fc5analyze/xref.py`
- Test: `analysis/tests/test_xref.py`

**Interfaces:**
- Consumes: `PEImage` (Task 1).
- Produces:
  - `class XrefScanner(pe: PEImage)` — pre-scans `.xpdata` once.
  - `.lea_targets_to(target_vas: set[int]) -> dict[int, list[int]]` — maps each target VA to the list of code VAs whose `REX 8D /r [rip+disp32]` instruction points at it.
  - Internally scans for opcodes: byte0 in `{0x48,0x4C,0x49,0x4D}` (REX.W variants), byte1 `0x8D`, modrm `0x05 | (reg<<3)` with mod=00 rm=101 (RIP-relative), for reg 0..15.

- [ ] **Step 1: Write the failing test**

```python
# analysis/tests/test_xref.py
from fc5analyze.peimage import PEImage
from fc5analyze.xref import XrefScanner

def test_known_lea_to_string():
    sc = XrefScanner(PEImage())
    # VA 0x1841fb128 ("Calling RDV CreateSession") is LEA-referenced from 0x187ba5245
    hits = sc.lea_targets_to({0x1841FB128})
    assert 0x187BA5245 in hits[0x1841FB128]

def test_absent_target_empty():
    sc = XrefScanner(PEImage())
    hits = sc.lea_targets_to({0x180000000})   # image base, not a string
    assert hits[0x180000000] == []
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd /home/kalopsia/fc5mp/analysis && uv run pytest tests/test_xref.py -q`
Expected: FAIL — `ModuleNotFoundError`.

- [ ] **Step 3: Write minimal implementation**

```python
# analysis/fc5analyze/xref.py
import struct
from .peimage import PEImage

_REX = (0x48, 0x4C, 0x49, 0x4D)                 # REX.W with optional R/B bits
_MODRM_RIP = {0x05 | (reg << 3) for reg in range(8)}   # mod=00, rm=101, reg 0..7
# reg 8..15 need REX.R (0x4C/0x4D) but modrm reg field is still 0..7 → same bytes

class XrefScanner:
    def __init__(self, pe: PEImage):
        self.pe = pe
        s = pe.section_by_name(".xpdata")
        self.code = pe._data[s.raw_off:s.raw_off + s.raw_size]
        self.code_va = s.va

    def lea_targets_to(self, target_vas: set[int]) -> dict[int, list[int]]:
        out = {t: [] for t in target_vas}
        code = self.code
        base_va = self.code_va
        n = len(code) - 7
        i = 0
        while i < n:
            if code[i] in _REX and code[i + 1] == 0x8D and code[i + 2] in _MODRM_RIP:
                disp = struct.unpack_from("<i", code, i + 3)[0]
                instr_va = base_va + i
                tgt = instr_va + 7 + disp
                if tgt in out:
                    out[tgt].append(instr_va)
                i += 7
            else:
                i += 1
        return out
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd /home/kalopsia/fc5mp/analysis && uv run pytest tests/test_xref.py -q`
Expected: PASS (2 passed). This pins the signed-displacement arithmetic against the known-good `0x187ba5245 → 0x1841fb128` pair.

- [ ] **Step 5: Commit**

```bash
cd /home/kalopsia/fc5mp
git add analysis/fc5analyze/xref.py analysis/tests/test_xref.py
git commit -m "feat(p0): RIP-relative LEA xref scanner over .xpdata"
```

---

### Task 4: String anchors → referencing functions

**Files:**
- Create: `analysis/fc5analyze/strings.py`
- Test: `analysis/tests/test_strings.py`

**Interfaces:**
- Consumes: `PEImage` (1), `FunctionTable` (2), `XrefScanner` (3).
- Produces:
  - `find_string_va(pe: PEImage, needle: bytes) -> list[int]` — all VAs in `.xtls` where `needle` begins (preceded by NUL or section start, to avoid mid-string hits).
  - `anchor_functions(pe, ft, sc, needle: bytes) -> AnchorResult` where `AnchorResult` has `string_vas:list[int]`, `code_xrefs:list[int]` (LEA sites), `functions:list[int]` (distinct primary funcs), `status:str` in `{"ok","no-string","no-xref","ambiguous"}`. `ambiguous` when `len(code_xrefs) > 8`.

- [ ] **Step 1: Write the failing test**

```python
# analysis/tests/test_strings.py
from fc5analyze.peimage import PEImage
from fc5analyze.functable import FunctionTable
from fc5analyze.xref import XrefScanner
from fc5analyze.strings import find_string_va, anchor_functions

def test_find_known_string():
    pe = PEImage()
    vas = find_string_va(pe, b"Calling RDV CreateSession")
    assert 0x1841FB128 in vas

def test_anchor_resolves_to_function():
    pe = PEImage(); ft = FunctionTable(pe); sc = XrefScanner(pe)
    r = anchor_functions(pe, ft, sc, b"Calling RDV CreateSession")
    assert r.status == "ok"
    assert 0x187BA5245 in r.code_xrefs
    assert any(f <= 0x187BA5245 for f in r.functions)

def test_missing_string_status():
    pe = PEImage(); ft = FunctionTable(pe); sc = XrefScanner(pe)
    r = anchor_functions(pe, ft, sc, b"this string is not in the binary zzz")
    assert r.status == "no-string"
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd /home/kalopsia/fc5mp/analysis && uv run pytest tests/test_strings.py -q`
Expected: FAIL — `ModuleNotFoundError`.

- [ ] **Step 3: Write minimal implementation**

```python
# analysis/fc5analyze/strings.py
from dataclasses import dataclass, field
from .peimage import PEImage
from .functable import FunctionTable
from .xref import XrefScanner

AMBIGUOUS_XREF_LIMIT = 8

def find_string_va(pe: PEImage, needle: bytes) -> list[int]:
    s = pe.section_by_name(".xtls")
    blob = pe._data[s.raw_off:s.raw_off + s.raw_size]
    out = []
    start = 0
    while True:
        j = blob.find(needle, start)
        if j < 0:
            break
        if j == 0 or blob[j - 1] == 0:        # string start, not mid-string
            out.append(s.va + j)
        start = j + 1
    return out

@dataclass
class AnchorResult:
    needle: bytes
    string_vas: list[int] = field(default_factory=list)
    code_xrefs: list[int] = field(default_factory=list)
    functions: list[int] = field(default_factory=list)
    status: str = "ok"

def anchor_functions(pe, ft, sc, needle: bytes) -> AnchorResult:
    r = AnchorResult(needle=needle)
    r.string_vas = find_string_va(pe, needle)
    if not r.string_vas:
        r.status = "no-string"; return r
    hits = sc.lea_targets_to(set(r.string_vas))
    for t in r.string_vas:
        r.code_xrefs.extend(hits[t])
    r.code_xrefs.sort()
    if not r.code_xrefs:
        r.status = "no-xref"; return r
    funcs = {ft.primary_for(x) for x in r.code_xrefs}
    r.functions = sorted(f for f in funcs if f is not None)
    if len(r.code_xrefs) > AMBIGUOUS_XREF_LIMIT:
        r.status = "ambiguous"
    return r
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd /home/kalopsia/fc5mp/analysis && uv run pytest tests/test_strings.py -q`
Expected: PASS (3 passed).

- [ ] **Step 5: Commit**

```bash
cd /home/kalopsia/fc5mp
git add analysis/fc5analyze/strings.py analysis/tests/test_strings.py
git commit -m "feat(p0): anchor-string resolution to referencing functions"
```

---

### Task 5: Static direct-call graph within a function

**Files:**
- Create: `analysis/fc5analyze/callgraph.py`
- Test: `analysis/tests/test_callgraph.py`

**Interfaces:**
- Consumes: `PEImage` (1), `FunctionTable` (2).
- Produces:
  - `direct_callees(pe, ft, func_va: int, max_bytes: int = 0x4000) -> list[int]` — primary-function VAs reached by a direct `E8 rel32` call from within the function body (body bounded by the next function start or `max_bytes`). Uses capstone linear disassembly.
  - `callee_closure(pe, ft, roots: list[int], depth: int = 2) -> set[int]` — union of callees to the given depth.

- [ ] **Step 1: Write the failing test**

```python
# analysis/tests/test_callgraph.py
from fc5analyze.peimage import PEImage
from fc5analyze.functable import FunctionTable
from fc5analyze.callgraph import direct_callees, callee_closure

def test_rdv_function_has_callees():
    pe = PEImage(); ft = FunctionTable(pe)
    fn = ft.primary_for(0x187BA5245)
    callees = direct_callees(pe, ft, fn)
    assert isinstance(callees, list)
    assert len(callees) >= 1
    assert all(c in ft.starts for c in callees)

def test_closure_superset_of_direct():
    pe = PEImage(); ft = FunctionTable(pe)
    fn = ft.primary_for(0x187BA5245)
    direct = set(direct_callees(pe, ft, fn))
    clo = callee_closure(pe, ft, [fn], depth=2)
    assert direct <= clo
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd /home/kalopsia/fc5mp/analysis && uv run pytest tests/test_callgraph.py -q`
Expected: FAIL — `ModuleNotFoundError`.

- [ ] **Step 3: Write minimal implementation**

```python
# analysis/fc5analyze/callgraph.py
import bisect, struct
import capstone
from .peimage import PEImage
from .functable import FunctionTable

def _func_end(ft: FunctionTable, func_va: int, max_bytes: int) -> int:
    i = bisect.bisect_right(ft.starts, func_va)
    nxt = ft.starts[i] if i < len(ft.starts) else func_va + max_bytes
    return min(nxt, func_va + max_bytes)

def direct_callees(pe, ft, func_va: int, max_bytes: int = 0x4000) -> list[int]:
    end = _func_end(ft, func_va, max_bytes)
    code = pe.read_va(func_va, end - func_va)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    out = set()
    for ins in md.disasm_lite(code, func_va):
        addr, size, mnem, _ = ins
        if mnem == "call":
            raw = pe.read_va(addr, size)
            if raw and raw[0] == 0xE8 and size == 5:
                rel = struct.unpack_from("<i", raw, 1)[0]
                tgt = addr + 5 + rel
                p = ft.primary_for(tgt)
                if p is not None:
                    out.add(p)
    return sorted(out)

def callee_closure(pe, ft, roots, depth: int = 2) -> set[int]:
    seen, frontier = set(roots), list(roots)
    for _ in range(depth):
        nxt = []
        for f in frontier:
            for c in direct_callees(pe, ft, f):
                if c not in seen:
                    seen.add(c); nxt.append(c)
        frontier = nxt
    return seen
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd /home/kalopsia/fc5mp/analysis && uv run pytest tests/test_callgraph.py -q`
Expected: PASS (2 passed).

- [ ] **Step 5: Commit**

```bash
cd /home/kalopsia/fc5mp
git add analysis/fc5analyze/callgraph.py analysis/tests/test_callgraph.py
git commit -m "feat(p0): static direct-call graph and callee closure"
```

---

### Task 6: Operand probing (fixed-width storage signals)

**Files:**
- Create: `analysis/fc5analyze/probe.py`
- Test: `analysis/tests/test_probe.py`

**Interfaces:**
- Consumes: `PEImage` (1), `FunctionTable` (2).
- Produces:
  - `probe_function(pe, ft, func_va: int, max_bytes: int = 0x4000) -> ProbeResult` where `ProbeResult` has:
    - `small_imm_compares: list[tuple[int,int]]` — `(insn_va, imm)` for `cmp r/m, imm8/imm32` with `imm` in 1..8 (candidate player-count caps).
    - `scaled_indexed: list[tuple[int,str]]` — `(insn_va, op_str)` for memory operands with an index scale of 4/8/16 (candidate per-player array strides).
  - Interpretation guidance lives in Task 8; this task only extracts signals.

- [ ] **Step 1: Write the failing test**

```python
# analysis/tests/test_probe.py
from fc5analyze.peimage import PEImage
from fc5analyze.functable import FunctionTable
from fc5analyze.probe import probe_function

def test_probe_returns_structured_signals():
    pe = PEImage(); ft = FunctionTable(pe)
    fn = ft.primary_for(0x187BA5245)
    r = probe_function(pe, ft, fn)
    assert hasattr(r, "small_imm_compares")
    assert hasattr(r, "scaled_indexed")
    assert all(1 <= imm <= 8 for _, imm in r.small_imm_compares)

def test_probe_is_read_only_and_bounded():
    pe = PEImage(); ft = FunctionTable(pe)
    fn = ft.starts[1000]
    r = probe_function(pe, ft, fn, max_bytes=0x400)
    # every reported VA lies within the probed window
    for va, _ in r.small_imm_compares + r.scaled_indexed:
        assert fn <= va < fn + 0x400
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd /home/kalopsia/fc5mp/analysis && uv run pytest tests/test_probe.py -q`
Expected: FAIL — `ModuleNotFoundError`.

- [ ] **Step 3: Write minimal implementation**

```python
# analysis/fc5analyze/probe.py
import bisect
from dataclasses import dataclass, field
import capstone
from capstone import x86
from .peimage import PEImage
from .functable import FunctionTable

@dataclass
class ProbeResult:
    func_va: int
    small_imm_compares: list = field(default_factory=list)
    scaled_indexed: list = field(default_factory=list)

def _end(ft, func_va, max_bytes):
    i = bisect.bisect_right(ft.starts, func_va)
    nxt = ft.starts[i] if i < len(ft.starts) else func_va + max_bytes
    return min(nxt, func_va + max_bytes)

def probe_function(pe, ft, func_va: int, max_bytes: int = 0x4000) -> ProbeResult:
    end = _end(ft, func_va, max_bytes)
    code = pe.read_va(func_va, end - func_va)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64); md.detail = True
    r = ProbeResult(func_va=func_va)
    for ins in md.disasm(code, func_va):
        if ins.id == x86.X86_INS_CMP:
            ops = ins.operands
            if len(ops) == 2 and ops[1].type == x86.X86_OP_IMM:
                if 1 <= ops[1].imm <= 8:
                    r.small_imm_compares.append((ins.address, ops[1].imm))
        for op in ins.operands:
            if op.type == x86.X86_OP_MEM and op.mem.index != 0 and op.mem.scale in (4, 8, 16):
                r.scaled_indexed.append((ins.address, ins.op_str))
                break
    return r
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd /home/kalopsia/fc5mp/analysis && uv run pytest tests/test_probe.py -q`
Expected: PASS (2 passed).

- [ ] **Step 5: Commit**

```bash
cd /home/kalopsia/fc5mp
git add analysis/fc5analyze/probe.py analysis/tests/test_probe.py
git commit -m "feat(p0): operand probing for fixed-width storage signals"
```

---

### Task 7: Anchor set and analysis runner

**Files:**
- Create: `analysis/anchors.py`
- Test: none (runner/driver; its building blocks are already tested). Verified by producing `out/p0.json`.

**Interfaces:**
- Consumes: all of Tasks 1–6.
- Produces: `out/p0.json` — for every anchor: `{status, string_vas, code_xrefs, functions}`; plus `shared_callees` (functions in BOTH co-op and versus closures) and `partner_probe` (probe signals for each co-op-partner anchor's function).

- [ ] **Step 1: Write the runner**

```python
# analysis/anchors.py
import json, os
from fc5analyze.peimage import PEImage
from fc5analyze.functable import FunctionTable
from fc5analyze.xref import XrefScanner
from fc5analyze.strings import anchor_functions
from fc5analyze.callgraph import callee_closure
from fc5analyze.probe import probe_function

# Co-op / campaign anchors (verified present in the strings dump).
COOP = [
    b"CGRStateCampaignInGame::DoSpawnPlayer() playerId: %d",
    b"CGRStateCampaignInGame::OnEveryoneInATeamDead()",
    b"CoopLeashCB : ignoring leash callback for player:%u entity:%llu leashState:%d",
    b"ERROR: CreateCoopLeash, player is null",
    b"CGREventOnCoopPartnerReady",
    b"CGREventOnCoopPartnerDetected",
]
# "Partner"-logic anchors we most want to probe for fixed-width storage.
PARTNER = [
    b"ERROR: CreateCoopLeash, player is null",
    b"CGRStateCampaignInGame::DoSpawnPlayer() playerId: %d",
]
# Versus / MP anchors for shared-callee comparison.
VERSUS = [
    b"CUserMapPvPGameModeSession::HighPingKickDoAction",
    b"CFCXGRStateEditorPvPInRound::ProcessGenericEvent",
]

def hx(xs): return [hex(x) for x in xs]

def main():
    pe = PEImage(); pe.assert_sha(open("out/dll.sha256").read().strip())
    ft = FunctionTable(pe); sc = XrefScanner(pe)
    report = {"anchors": {}, "shared_callees": [], "partner_probe": {}}

    def resolve(group):
        funcs = set()
        for needle in group:
            r = anchor_functions(pe, ft, sc, needle)
            report["anchors"][needle.decode("latin1")] = {
                "status": r.status, "string_vas": hx(r.string_vas),
                "code_xrefs": hx(r.code_xrefs), "functions": hx(r.functions),
            }
            if r.status in ("ok", "ambiguous"):
                funcs.update(r.functions)
        return funcs

    coop_funcs = resolve(COOP)
    versus_funcs = resolve(VERSUS)
    resolve(PARTNER)

    coop_clo = callee_closure(pe, ft, sorted(coop_funcs), depth=2)
    versus_clo = callee_closure(pe, ft, sorted(versus_funcs), depth=2)
    report["shared_callees"] = hx(sorted(coop_clo & versus_clo))
    report["coop_closure_size"] = len(coop_clo)
    report["versus_closure_size"] = len(versus_clo)

    for needle in PARTNER:
        r = anchor_functions(pe, ft, sc, needle)
        for fn in r.functions:
            pr = probe_function(pe, ft, fn)
            report["partner_probe"][hex(fn)] = {
                "small_imm_compares": [[hex(a), i] for a, i in pr.small_imm_compares],
                "scaled_indexed": [[hex(a), s] for a, s in pr.scaled_indexed],
            }

    os.makedirs("out", exist_ok=True)
    json.dump(report, open("out/p0.json", "w"), indent=2)
    print("anchors:", {k: v["status"] for k, v in report["anchors"].items()})
    print("shared callees:", len(report["shared_callees"]),
          "coop_clo:", report["coop_closure_size"], "versus_clo:", report["versus_closure_size"])

if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run the analysis**

Run: `cd /home/kalopsia/fc5mp/analysis && uv run python anchors.py`
Expected: prints a per-anchor status map and the shared-callee count; writes `out/p0.json`. If many anchors report `no-xref`, retry those with a shortened needle (drop the trailing format specifiers — the xref may target the start of a longer merged string); note any that stay `no-xref`.

- [ ] **Step 3: Sanity-check the output**

Run: `cd /home/kalopsia/fc5mp/analysis && uv run python -c "import json; d=json.load(open('out/p0.json')); print('ok anchors:', sum(1 for a in d['anchors'].values() if a['status'] in ('ok','ambiguous')), '/', len(d['anchors'])); print('shared:', len(d['shared_callees']))"`
Expected: at least half the anchors resolve `ok`/`ambiguous`; prints a shared-callee count.

- [ ] **Step 4: Commit the runner**

```bash
cd /home/kalopsia/fc5mp
git add analysis/anchors.py
git commit -m "feat(p0): anchor set and analysis runner producing out/p0.json"
```

---

### Task 8: Gate report (the P0 deliverable)

**Files:**
- Create: `docs/superpowers/research/2026-10-13-p0-findings.md` (committed — the one analysis artifact that is NOT git-ignored)
- Test: none (human-read deliverable).

**Interfaces:**
- Consumes: `out/p0.json` from Task 7, read by a human/agent to write prose.

- [ ] **Step 1: Confirm game files were never modified**

Run:
```bash
stat -c '%n %Y' /home/kalopsia/.local/share/Steam/steamapps/common/FarCry5/bin/FC_m64.dll
```
Expected: mtime unchanged from the start of P0. Record it in the report.

- [ ] **Step 2: Write the findings report**

Create `docs/superpowers/research/2026-10-13-p0-findings.md` with these sections, filled from `out/p0.json` (no placeholders — quote actual VAs and counts):
- **DLL build:** SHA-256 (`out/dll.sha256`) and confirmed-unchanged mtime.
- **Anchors resolved:** table of anchor → status → function VA(s). Note any `no-string`/`no-xref`/`ambiguous`.
- **Shared-callee analysis:** size of co-op closure, versus closure, and intersection; list the shared functions. Interpretation: a shared player-enumeration/accessor callee argues for generic storage.
- **Partner-function probe:** for each partner anchor's function, the `small_imm_compares` (flag any `cmp …, 2` — a candidate hard 2-player cap) and `scaled_indexed` strides (a repeated stride argues a per-player array; note the stride).
- **GATE DECISION** — exactly one of:
  - **GO (generic storage):** co-op and versus share player-list accessors, and no hard `cmp …,2` / fixed 2-stride dominates partner code. Widening is primarily logic patches → proceed to P1.
  - **STOP (fixed 2-wide):** partner code is dominated by `cmp …,2` caps and/or fixed 2-element strides with no shared generic manager. Re-plan: hybrid approach C or reduce target to 3 players.
  - **INCONCLUSIVE:** too many anchors unresolved. List exactly what to disassemble by hand next (name the function VAs) before deciding.
- **Confidence and caveats:** static analysis only; Denuvo may relocate/decrypt at runtime, so any GO must be re-validated live in P1.

- [ ] **Step 3: Commit the deliverable**

```bash
cd /home/kalopsia/fc5mp
git add docs/superpowers/research/2026-10-13-p0-findings.md
git commit -m "docs(p0): co-op widening feasibility findings and gate decision"
```

---

## Notes for the executor

- Run everything from `/home/kalopsia/fc5mp/analysis` with `uv run` so the venv (capstone/pytest) is active.
- If `uv sync` can't fetch capstone offline, that is a blocker to report, not to work around — the xref self-tests (Task 3) run without capstone, but Tasks 5, 6 need it.
- Treat every `no-string` result as a possible dump-vs-DLL mismatch first: the strings came from a prior dump; re-confirm the needle exists via `find_string_va` against the live DLL before concluding it is absent.
- The gate decision in Task 8 is the whole point of P0. If the data is thin, choose **INCONCLUSIVE** and name the next manual step — do not force GO/STOP.
