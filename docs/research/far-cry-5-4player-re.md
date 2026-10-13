# Far Cry 5 — 4-Player Co-op: Reverse-Engineering Notes

- **Date:** 2026-10-13
- **System:** Arch Linux (kernel 7.2.9-arch1), home `/home/kalopsia`
- **Game install:** `/home/kalopsia/.local/share/Steam/steamapps/common/FarCry5` (Steam, PC)
- **Goal:** get campaign co-op from 2 players (host + 1 guest) to 4 players.
- **Bottom line (verdict):** NOT achievable by modifying the shipped client — and not by a
  simple proxy/relay. See §5 and §6. The authoritative simulation is P2P and hardwired to
  one partner on the host; there is no dedicated game-server mode to point at.

> No game files were modified during this work. All analysis was read-only; outputs went to
> `~/.hermes/cache/scratch/`. (Verified: FC_m64.dll & FarCry5.exe mtime unchanged, 0 game
> files touched.)

---

## 1. Install & binary layout

- `bin/FarCry5.exe` — 210 KB launcher stub only. **Not** where the logic is.
- `bin/FC_m64.dll` — **~238 MB PE32+ (x86-64) DLL. This is the whole AnvilNext 2.0 engine +
  all game logic + the entire online stack. This is THE file to reverse-engineer.**
  - ImageBase `0x180000000`, SizeOfImage `0xEEA9000`, **0 exports** (pure internal).
- `bin/uplay_r1_loader64.dll` — Ubisoft Connect (Uplay) loader.
- `bin/steam_api64.dll` — Steam.
- `data_final/pc/*.dat` + `*.fat` — AnvilNext asset archives (worlds, DLC, per-language
  voice/text). `patch.dat` is ~8.9 GB. **All compressed/binary** (a small XML-looking
  preamble is present but the bulk is not editable plaintext). No editable max-player config
  found in them.
- **No anti-cheat DLL present.** Easy Anti-Cheat was removed from FC5 in Nov 2019, so nothing
  blocks a modified client at the integrity layer. (Relevant to the legal/account note §9.)

## 2. Section map (sections are RENAME-obfuscated)

The PE section names are misleading (anti-RE). Identify by **flags + sample disassembly**,
not by name. (`.text` is tiny/renamed; the real code is huge and mislabeled.)

| pefile name | VA          | Size       | Flags | What it actually is            |
|-------------|-------------|------------|-------|--------------------------------|
| `.xpdata`   | 0x1857CE000 | ~146.7 MB  | **RWX** | **CODE** (real `.text`)       |
| `.xtls`     | 0x184109000 | 0xBB0000   | R--   | read-only data / **strings**  |
| `.reloc`    | 0x180001000 | ~67 MB     | R-X   | jump/thunk table + reloc data |
| `.sbss`     | 0x184CB9000 | 0x688000   | RW-   | BSS (uninitialized)          |
| `.data`     | 0x18EDCD000 | 0xDB9E0    | R--   | data                          |
| `.code`     | 0x18EABE000 | ~3.2 MB    | R--   | secondary code-ish blob       |
| others      |             |            |       | `.pdata/.arch/.edata/.sdata/.L~g/...` |

File-offset → VA: `VA = ImageBase + section.VirtualAddress + (fileOffset - section.PointerToRawData)`.

## 3. Multiplayer architecture (grounded in strings/structure of FC_m64.dll)

- **Netcode = Ubisoft "Storm"** (proprietary), with three subsystems:
  - **RdV (Rendez-Vous)** — a *signaling/registry* service (NOT a game server). Creates
    sessions, matchmaking, reservations; on join it hands the client a **`p2pURL`**.
    Strings: `RendezVous session created`, `rdv_session_id`, `[P2PUrl=%s] Joined RdV session`,
    `Calling RDV CreateSession ... gamemode nb_slots visibility`.
  - **Punch** — NAT traversal. `PunchServerDetect`, `TraversalServerIp`.
  - **Relay / dedicated router** — *transport fallback* when a direct P2P connection fails.
    `Link::CanSend - RELAY`, `RelayPokeResult`, `EnableDynamicDedicatedRouter`,
    `SetDedicatedRouterInfo`. ("dedicated" in this binary ONLY ever means this Storm
    router/relay — there is NO dedicated game server.)
- **Topology = host-authoritative P2P.** The session brokers a P2P connection; the
  **authoritative simulation runs on the host peer's own PC** (FC_m64.dll in that process).
  `P2PChannel`, `P2PObject`, `ONLINE_P2P_CONNECTION_LOST`, `MigrateSession on RDV`.
- **Co-op model = host + exactly 1 guest.** The campaign co-op sim is
  `CGameRulesCampaign` / `CGRStateCampaignInGame`, built around a single **`bCoopPartner`**
  with a **"co-op leash"** (`BreakingCoopLeash`) tethering that one guest. There is **no code
  path for a 2nd or 3rd remote player.**
- **Online config is server-delivered.** Per-gamemode session params load at login from
  Ubisoft: `ClientConfigLoadAtLogin`, `OnlineConfiguration`, `population config`,
  per-gamemode "matchmaking settings" (`no matchmaking settings for the requested gamemode`).

### Co-op / session class inventory (in FC_m64.dll)
- `CCreateSessionOperation`, `CCreateRdVSessionOperation` — session creation/validation.
- `CCoopServiceNetObject`, `CCoopServiceNetObjectP2P`, `CCoopHostStateNetObjectComponent`,
  `CCoopRPCNetObjectComponent`, `CCoopVotingEvent`.
- `CFCXCoopService`, `CFCXGameModeCoop` (+ `...DLCMars`, `...DLCVietnam`,
  `...DLCZombies`, `...CoopFatherMode`, `...ValidationCoop`).
- `CInGamePartyService`, `CInGamePartySession`, `CInGamePartySessionNetObjectP2P`,
  `CInGamePartyMatchmakingComponent` (the "Open In-game Party" friend-group system — it does
  NOT unlock a 2nd/3rd playable partner).
- `CGameRulesCampaign`, `CGRStateCampaignInGame` (campaign co-op state machine:
  `DoSpawnPlayer() playerId: %d`, respawn conditions, `OnEveryoneInATeamDead`, etc.).
- Player-capacity property names: `nMaxNumPlayers`, `nMinNumPlayers`, `MaxPlayers`,
  `MaxPlayersForCaptureBonus`, `iMaxPlayersRound`, `iMinPlayersRound`.

## 4. Session-creation code path (localized)

`CCreateSessionOperation` validates and then calls RdV:
- Format string **`[nbSlots=%d] [minPlayers=%d] [maxPlayers=%d] Invalid number of slot
  requested.`** — the requested `nbSlots` is checked against `[minPlayers, maxPlayers]`.
  Out of range → error. (`INTERNAL_ERROR_MATCHMAKING_NBSLOTS_INVALID`.)
- `gameModeSession=%s nbSlots=%d layer=%s visibility=%s groupId=%s`
- Then `Calling RDV CreateSession ... gamemode nb_slots visibility` → RdV returns a
  `p2pURL`; peers connect P2P.

Localized anchors (VA in FC_m64.dll, for future work):
- `CCreateSessionOperation` string VA `0x1841FB270` → xref code `0x187a0d351` (class registration).
- `Calling RDV CreateSession` string VA `0x1841FB128` → xref code **`0x187ba5245`** (the RdV
  call site — inside the session-creation function).
- `gameModeSession=` string VA `0x184217C40` → xref code `0x18802ff94`.
- Session-creation function region: **~`0x187ba5xxx`**. Several fields are set to `2`
  (`mov byte ptr [rbp+0xbc],2`, `[rbp+0x1dc],2`, `[rbp+0x204],2`, `[rbp+0x22c],2`,
  `[rbp+0x1b4],2`) inside/near this function — candidates for the nbSlots/maxPlayers values,
  but the function is large and the authoritative per-gamemode cap is (see §5) ultimately the
  server-delivered config, not a single client constant.

## 5. Why 4-player isn't feasible (three independent blockers; any one is fatal)

1. **Server-side cap.** Per-gamemode max players live in Ubisoft's online config (loaded at
   login) and are enforced by their RdV server. You cannot change Ubisoft's server; a client
   that sends `nbSlots=4` for co-op is refused (or yields a session the server won't accept).
2. **Host sim is 1-wide.** Even if a 4-person P2P session connected, the host only simulates
   one partner (`bCoopPartner`). Players 3 & 4 have no simulation slot, no state replication,
   no rendering, no participation in AI/objectives → non-functional ghosts.
3. **Everything downstream is 2-wide.** AI/objective scaling, the co-op leash, HUD, and
   progression all assume exactly two.

So even a "perfect" client patch = 2 real players + 2 ghosts, not playable 4-player.

## 6. Why "our own server / proxy" doesn't work

- A **proxy in front of RdV** can only rewrite the client's outbound request (`nb_slots`) and
  the config download. It has **no game logic**, so it can't simulate players 3/4, and it
  can't change Ubisoft's server-side enforcement. The "server" in co-op is a *player's PC*
  (P2P), not a service you can replace.
- The idea is correct for **server-hosted** games (how private servers for dead games work),
  but FC5 co-op is the **P2P exception**: there is no game server to point at.
- **The only architecture that could truly work** = a **dedicated authoritative server**
  running the campaign, with all 4 as thin clients. That requires:
  1. Running the full campaign sim (world, Eden's Gate AI, factions, missions, vehicles,
     weather) **headlessly on a server** — but FC_m64.dll is a 238 MB *client* DLL with **no
     headless/server entry point** (0 hits for "dedicatedserver"/"gameserver"/"headless").
     You'd re-implement the whole campaign sim in a new program, not reuse the DLL.
  2. A full reimplementation of the **Storm / RdV / P2P protocol** (message formats, entity
     replication) so 4 modified clients talk to your server.
  3. Four modified clients.
  → A many-months-to-years, high-uncertainty, closed-source-engine reimplementation for a
  small experienced team. A project, not a proxy. Still voids the EULA (§9).

## 7. What actually serves "4 friends in a session"

- **Versus (PvP) supports up to 12 players** — the real large-multiplayer mode, no server
  needed (competitive, not campaign co-op). Whether it's P2P or relay-hosted is unconfirmed
  (`PVP_P2P_CHANNEL_TIMEOUT` hints P2P) — worth a separate check if a "private Versus" matters.
- **Campaign: 2-player is the ceiling.** For four friends: run two 2-player sessions, or rotate.

## 8. RE methodology used (reusable; no Ghidra required)

1. `file` + `readelf`/`pefile` to get section table; **identify real code by section
   EXECUTE flag + sample disassembly**, not by (obfuscated) section name.
2. Dump strings: `strings -n 5 FC_m64.dll > fc5_strings.txt` (~785K lines) for repeatable grep.
3. Map file-offset → VA via section table (`ImageBase + VA + (off - PointerToRawData)`).
4. **Find string xrefs in a 146 MB code section without full disassembly:** byte-scan for
   RIP-relative LEA encodings `48/4C 8D <05+8·r> disp32` (r = 0..15), compute
   `target = instrVA + 7 + disp`, keep only exact-VA matches. (~10 s vs. minutes for
   detail-mode capstone.) Capstone 5.x: operand type = `capstone.CS_OP_MEM`; RIP register =
   `capstone.x86.X86_REG_RIP` (=41); no `next_address` attr (use `i.address + i.size`).
5. Disassemble a window around a hit (`capstone`, detail off) to read the function; grep for
   small immediates / string LEAs / control flow.

Reusable helper script (xref finder) pattern:
```
pefile.PE(dll) -> base, sections
code = section with name '.xpdata' (or: the RWX one)
for i in range(len(codebytes)-7):
    if codebytes[i] in (0x48,0x4C) and codebytes[i+1]==0x8D and codebytes[i+2] in {0x05+8*r for r in range(16)}:
        disp = int.from_bytes(codebytes[i+3:i+7],'little',signed=True)
        tgt = codeVA + i + 7 + disp
        if tgt in TARGET_VAS: record(codeVA + i)
```

## 9. Legal / account

- Modifying FC_m64.dll to exceed the multiplayer limit **violates the EULA/ToS** (breach of
  contract). Reverse-engineering your own copy for personal/educational use is generally
  tolerated in many jurisdictions, but "play with more players than designed" is a weak
  interoperability claim.
- With EAC gone, ban risk is *lower* but not zero, and it's not "safe" for an account you
  care about. Proceeding is the user's informed call.

## 10. Artifacts / open items

- Scratch outputs (may be pruned after ~24h idle): `~/.hermes/cache/scratch/`
  - `fc5_strings.txt` (785K strings), `fc5_xrefs.json` (xref map), `fc5_func.txt` (disasm window).
- Open questions:
  - Is Versus P2P or relay-hosted? (determines if a "private Versus" is feasible.)
  - Full end-to-end RdV `p2pURL` / session-join protocol map (if pursuing the dedicated-server
    path — large effort).
  - Whether the per-gamemode min/max cap is *also* mirrored in any decompressed client data
    file (server remains the authority regardless).
