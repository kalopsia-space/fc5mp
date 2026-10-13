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

def test_executable_va_in_table_gap_returns_none():
    # 0x1800014b1 is an executable VA that is PAST one function's EndAddress
    # but BEFORE the next function's BeginAddress (a gap). Without an end-bound,
    # primary_for would silently mis-attribute it to the nearest-below function.
    ft = FunctionTable(PEImage())
    assert ft.primary_for(0x1800014B1) is None

def test_chained_fragment_resolves_to_parent():
    # 0x1800013cd is a chained unwind fragment whose primary function is
    # 0x1800013c0. primary_for must follow UNW_FLAG_CHAININFO to the parent,
    # not stop at the fragment's own begin.
    ft = FunctionTable(PEImage())
    assert ft.primary_for(0x1800013CD) == 0x1800013C0
