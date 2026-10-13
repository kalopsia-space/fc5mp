import json, os
from fc5analyze.peimage import PEImage
from fc5analyze.functable import FunctionTable
from fc5analyze.xref import XrefScanner
from fc5analyze.strings import anchor_functions
from fc5analyze.callgraph import callee_closure
from fc5analyze.probe import probe_function

# Co-op / campaign anchors (verified present in the strings dump).
COOP = [
    b"CGRStateCampaignInGame::DoSpawnPlayer() playerId: %d",
    b"CGRStateCampaignInGame::OnEveryoneInATeamDead()",
    b"CoopLeashCB : ignoring leash callback for player:%u entity:%llu leashState:%d",
    b"ERROR: CreateCoopLeash, player is null",
    b"CGREventOnCoopPartnerReady",
    b"CGREventOnCoopPartnerDetected",
]
# "Partner"-logic anchors we most want to probe for fixed-width storage.
PARTNER = [
    b"ERROR: CreateCoopLeash, player is null",
    b"CGRStateCampaignInGame::DoSpawnPlayer() playerId: %d",
]
# Versus / MP anchors for shared-callee comparison.
VERSUS = [
    b"CUserMapPvPGameModeSession::HighPingKickDoAction",
    b"CFCXGRStateEditorPvPInRound::ProcessGenericEvent",
]

def hx(xs): return [hex(x) for x in xs]

def main():
    pe = PEImage(); pe.assert_sha(open("out/dll.sha256").read().strip())
    ft = FunctionTable(pe); sc = XrefScanner(pe)
    report = {"anchors": {}, "shared_callees": [], "partner_probe": {}}

    def resolve(group):
        funcs = set()
        for needle in group:
            r = anchor_functions(pe, ft, sc, needle)
            report["anchors"][needle.decode("latin1")] = {
                "status": r.status, "string_vas": hx(r.string_vas),
                "code_xrefs": hx(r.code_xrefs), "functions": hx(r.functions),
            }
            if r.status in ("ok", "ambiguous"):
                funcs.update(r.functions)
        return funcs

    coop_funcs = resolve(COOP)
    versus_funcs = resolve(VERSUS)
    resolve(PARTNER)

    coop_clo = callee_closure(pe, ft, sorted(coop_funcs), depth=2)
    versus_clo = callee_closure(pe, ft, sorted(versus_funcs), depth=2)
    report["shared_callees"] = hx(sorted(coop_clo & versus_clo))
    report["coop_closure_size"] = len(coop_clo)
    report["versus_closure_size"] = len(versus_clo)

    for needle in PARTNER:
        r = anchor_functions(pe, ft, sc, needle)
        for fn in r.functions:
            pr = probe_function(pe, ft, fn)
            report["partner_probe"][hex(fn)] = {
                "small_imm_compares": [[hex(a), i] for a, i in pr.small_imm_compares],
                "scaled_indexed": [[hex(a), s] for a, s in pr.scaled_indexed],
            }

    os.makedirs("out", exist_ok=True)
    json.dump(report, open("out/p0.json", "w"), indent=2)
    print("anchors:", {k: v["status"] for k, v in report["anchors"].items()})
    print("shared callees:", len(report["shared_callees"]),
          "coop_clo:", report["coop_closure_size"], "versus_clo:", report["versus_closure_size"])

if __name__ == "__main__":
    main()
