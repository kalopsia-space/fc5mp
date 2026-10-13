from fc5analyze.peimage import PEImage
from fc5analyze.functable import FunctionTable

def test_entry_count():
    ft = FunctionTable(PEImage())
    assert ft.count == 369061

def test_known_code_resolves_to_a_function():
    ft = FunctionTable(PEImage())
    # 0x187ba5245 is a LEA inside the RDV CreateSession function (verified)
    fn = ft.primary_for(0x187BA5245)
    assert fn is not None
    assert fn <= 0x187BA5245
    assert fn in ft.starts            # primary is itself a listed start

def test_non_code_va_returns_none():
    ft = FunctionTable(PEImage())
    assert ft.primary_for(0x1841FB128) is None   # a string VA, not code
