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
