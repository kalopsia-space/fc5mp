import struct
from .peimage import PEImage

_REX = (0x48, 0x4C, 0x49, 0x4D)                 # REX.W with optional R/B bits
# mod=00, rm=101 (RIP-relative), reg field 0..7; REX.R selects r8..r15 with same modrm
_MODRM_RIP = {0x05 | (reg << 3) for reg in range(8)}

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
