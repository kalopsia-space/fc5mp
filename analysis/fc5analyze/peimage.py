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
        with open(path, "rb") as fh:            # read-only
            self._data = fh.read()
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
        s = self._sec_for_va(va)
        off = s.raw_off + (va - s.va)
        end = off + n
        if not (s.raw_off <= off and end <= s.raw_off + s.raw_size):
            raise VAError(f"read of {n} bytes at {va:#x} overruns section {s.name!r}")
        return self._data[off:end]

    def assert_sha(self, expected_hex: str) -> None:
        if self.sha256.lower() != expected_hex.lower():
            raise BuildMismatch(f"DLL sha {self.sha256} != expected {expected_hex}")
