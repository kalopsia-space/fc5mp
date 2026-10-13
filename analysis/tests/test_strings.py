from fc5analyze.peimage import PEImage
from fc5analyze.functable import FunctionTable
from fc5analyze.xref import XrefScanner
from fc5analyze.strings import find_string_va, anchor_functions

def test_find_known_string():
    pe = PEImage()
    vas = find_string_va(pe, b"Calling RDV CreateSession")
    assert 0x1841FB128 in vas

def test_anchor_resolves_to_function():
    pe = PEImage(); ft = FunctionTable(pe); sc = XrefScanner(pe)
    r = anchor_functions(pe, ft, sc, b"Calling RDV CreateSession")
    assert r.status == "ok"
    assert 0x187BA5245 in r.code_xrefs
    assert any(f <= 0x187BA5245 for f in r.functions)

def test_missing_string_status():
    pe = PEImage(); ft = FunctionTable(pe); sc = XrefScanner(pe)
    r = anchor_functions(pe, ft, sc, b"this string is not in the binary zzz")
    assert r.status == "no-string"

def test_string_present_but_unreferenced_is_no_xref():
    # This log string exists in .xtls but no .xpdata LEA targets the copy that
    # find_string_va keeps, so the anchor is reported no-xref (a status, never a
    # silent guess at a function).
    pe = PEImage(); ft = FunctionTable(pe); sc = XrefScanner(pe)
    r = anchor_functions(pe, ft, sc, b"CGRStateCampaignInGame::OnEveryoneInATeamDead()")
    assert r.string_vas != []          # the string is present
    assert r.status == "no-xref"
    assert r.functions == []
