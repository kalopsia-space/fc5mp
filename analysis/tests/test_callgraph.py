from fc5analyze.peimage import PEImage
from fc5analyze.functable import FunctionTable
from fc5analyze.callgraph import direct_callees, callee_closure

def test_rdv_function_has_callees():
    pe = PEImage(); ft = FunctionTable(pe)
    fn = ft.primary_for(0x187BA5245)
    callees = direct_callees(pe, ft, fn)
    assert isinstance(callees, list)
    assert len(callees) >= 1
    assert all(c in ft.starts for c in callees)

def test_closure_superset_of_direct():
    pe = PEImage(); ft = FunctionTable(pe)
    fn = ft.primary_for(0x187BA5245)
    direct = set(direct_callees(pe, ft, fn))
    clo = callee_closure(pe, ft, [fn], depth=2)
    assert direct <= clo
