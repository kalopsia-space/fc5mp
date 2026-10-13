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
    for addr, size, mnem, _ in md.disasm_lite(code, func_va):
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
