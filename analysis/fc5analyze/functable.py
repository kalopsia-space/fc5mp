import struct, bisect
from .peimage import PEImage, VAError

UNW_FLAG_CHAININFO = 0x4
IMAGE_SCN_MEM_EXECUTE = 0x20000000

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

    def _is_executable(self, va: int) -> bool:
        try:
            s = self.pe._sec_for_va(va)
        except VAError:
            return False
        return bool(s.chars & IMAGE_SCN_MEM_EXECUTE)

    def _chained_parent(self, unwind_va: int) -> int | None:
        # UNWIND_INFO: byte0 = Version(3b)|Flags(5b); byte2 = CountOfCodes
        try:
            hdr = self.pe.read_va(unwind_va, 4)
        except VAError:
            return None
        if len(hdr) < 4:
            return None
        flags = hdr[0] >> 3
        if not (flags & UNW_FLAG_CHAININFO):
            return None
        count = hdr[2]
        slots = count + (count & 1)                 # pad unwind codes to even
        chain_off = 4 + slots * 2                    # RUNTIME_FUNCTION follows codes
        try:
            cb, ce, cu = struct.unpack("<III", self.pe.read_va(unwind_va + chain_off, 12))
        except (VAError, struct.error):
            return None
        return self.pe.image_base + cb

    def primary_for(self, va: int) -> int | None:
        if not self._is_executable(va):
            return None
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
