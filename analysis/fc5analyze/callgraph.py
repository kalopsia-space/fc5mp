import struct
import capstone
from .peimage import PEImage
from .functable import FunctionTable

def direct_callees(pe, ft, func_va: int, max_bytes: int = 0x20000) -> list[int]:
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    out = set()
    budget = max_bytes
    for b, e in ft.body_chunks(func_va):
        if budget <= 0:
            break
        e = min(e, b + budget)
        budget -= (e - b)
        code = pe.read_va(b, e - b)
        for addr, size, mnem, _ in md.disasm_lite(code, b):
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
