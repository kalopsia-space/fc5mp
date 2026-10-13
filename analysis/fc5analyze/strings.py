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
