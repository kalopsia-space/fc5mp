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
