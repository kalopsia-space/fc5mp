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

def probe_function(pe, ft, func_va: int, max_bytes: int = 0x4000) -> ProbeResult:
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64); md.detail = True
    r = ProbeResult(func_va=func_va)
    window_end = func_va + max_bytes
    for b, e in ft.body_chunks(func_va):
        e = min(e, window_end)
        if b >= e:
            continue
        code = pe.read_va(b, e - b)
        for ins in md.disasm(code, b):
            if ins.id == x86.X86_INS_CMP:
                ops = ins.operands
                if len(ops) == 2 and ops[1].type == x86.X86_OP_IMM:
                    if 1 <= ops[1].imm <= 8:
                        r.small_imm_compares.append((ins.address, ops[1].imm))
            for op in ins.operands:
                if op.type == x86.X86_OP_MEM and op.mem.index != 0 and op.mem.scale in (2, 4, 8):
                    r.scaled_indexed.append((ins.address, ins.op_str))
                    break
    return r
