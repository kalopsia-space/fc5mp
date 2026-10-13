from fc5analyze.peimage import PEImage
from fc5analyze.xref import XrefScanner

def test_known_lea_to_string():
    sc = XrefScanner(PEImage())
    # VA 0x1841fb128 ("Calling RDV CreateSession") is LEA-referenced from 0x187ba5245
    hits = sc.lea_targets_to({0x1841FB128})
    assert 0x187BA5245 in hits[0x1841FB128]

def test_absent_target_empty():
    sc = XrefScanner(PEImage())
    # 0x1 is unreachable by any single RIP-relative LEA from the code section
    # (the disp32 required would exceed signed-int32 range), so it has no xrefs.
    hits = sc.lea_targets_to({0x1})
    assert hits[0x1] == []
