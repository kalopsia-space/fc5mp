# fc5mp — FiveM-style custom servers for Far Cry 5 (design)

- **Date:** 2026-10-13
- **Status:** approved
- **Working name:** `fc5mp` (placeholder)
- **Prior work:** `docs/research/far-cry-5-4player-re.md` (string-level RE notes; see §11 for corrections)

## 1. Goal

A FiveM-style platform for Far Cry 5: an injected client plus a custom server that **replaces
Ubisoft's online stack** (UbiServices, Rendez-Vous, Storm punch/relay) for campaign co-op, so
that more than two players can play the campaign together.

### v1 success criteria
- Up to **4 players** in the **host's Hope County campaign**; all can participate in missions,
  outposts and story events.
- **Story progress saves for the host only** (same as vanilla co-op; guests keep money/gear/perks).
- All co-op online traffic goes to `fc5mp-server`, never to Ubisoft online services.
- Every player legitimately owns the game; DRM is untouched (§2).

### Non-goals (v1)
- Per-player campaign progress.
- Host migration (session ends when the host leaves).
- Versus/Arcade support.
- Server-authoritative simulation (approach 3) — v1 is a relay hub *designed to grow into it*.
- Scripting API, server browser, in-game UI.

## 2. Constraints

- **No DRM bypass.** Steam, Ubisoft Connect (`uplay_r1_loader64.dll`) and Denuvo run normally.
  fc5mp never touches license/entitlement checks. The server additionally requires a valid
  Steam ownership ticket (app 552520).
- **No game files modified on disk.** All changes are runtime hooks from an injected DLL.
  `FC_m64.dll` carries Denuvo markers (`denuvo_dl`, `denuvo_atd`); static patching is out.
- **Platforms:** Linux/Proton (primary, the developer's machine) and Windows.
- **Languages:** client C++ (MinHook + own pattern scanner); server Rust.
- **Testing resources:** currently **2 owned copies** (2 accounts/PCs). Steam does not allow one
  copy to run twice concurrently.

## 3. Architecture

Approach chosen: **relay hub** (approach 2 of 3). The server brokers sessions *and* forwards
all peer-to-peer game traffic in a star topology. The host's game remains the authoritative
simulation. The relay's capture/decoder pipeline is the seed for a later OneSync-style
authoritative server (approach 3).

```
 ┌──────────── each player's PC (Proton or Windows) ─────────────┐
 │ Steam → Ubisoft Connect → FarCry5.exe → FC_m64.dll (unchanged)│
 │                                 ▲ runtime hooks               │
 │  version.dll (proxy loader) ──► fc5mp-client.dll              │
 │     ├─ hook core       pattern scanner + MinHook, Denuvo-aware│
 │     ├─ svc shim        UbiServices REST + RdV wss → our server│
 │     ├─ net shim        Storm UDP sendto/recvfrom → relay      │
 │     └─ coop module     4-player widening patches              │
 └───────────────────────────────┬───────────────────────────────┘
                     HTTPS / WSS / UDP
 ┌───────────────────────────────▼───────────────────────────────┐
 │ fc5mp-server (Rust, single binary)                            │
 │  ├─ auth        Steam ticket verify + whitelist               │
 │  ├─ svc emu     minimal UbiServices / RdV routes FC5 uses     │
 │  ├─ relay hub   UDP star forwarder                            │
 │  └─ capture     rotating packet logs + pluggable decoders     │
 └───────────────────────────────────────────────────────────────┘
```

## 4. Components

### 4.1 Client
- **Loader — `version.dll` proxy.** Forwards every export to the system `version.dll`, then
  loads `fc5mp-client.dll`. Enabled on Proton via Steam launch option
  `WINEDLLOVERRIDES="version=n,b" %command%`; on Windows by placing it in `bin/`.
- **Hook core.** Pattern scanner (FC_m64.dll has 0 exports) + MinHook. Hook targets are limited
  to statically linked libraries (OpenSSL, libcurl 7.54.1, Storm) and OS APIs; never
  Denuvo-protected game functions. If a target proves protected, move to the nearest library
  boundary.
- **svc shim.** Redirects `https://{env}public-ubiservices.ubi.com/{version}` and the RdV
  websocket to `fc5mp-server`, and makes the bundled OpenSSL trust the fc5mp server certificate
  (CA injection into the X509 store, or verify-callback hook — chosen in implementation).
- **net shim.** Hooks Storm's UDP socket calls; implements the virtual-address relay (§5 step 4).
- **coop module.** Registry of named widening patches (§6).

### 4.2 Server
- **auth** — verifies Steam auth session tickets via the Steam Web API (operator supplies a Web
  API key); enforces a SteamID whitelist; maps SteamID → stable fake Ubisoft profileId.
- **svc emu** — implements only the UbiServices/RdV routes FC5 is observed to call; unknown
  routes are logged and get a safe empty response.
- **relay hub** — UDP forwarder between peers; heartbeat/liveness tracking.
- **capture** — tags every packet (src, dst, timestamp) into rotating capture files; decoders
  are pluggable modules that parse message types. Growth path to approach 3:
  decoders → state model → arbitration.

## 5. Data flow

**One `fc5mp-server` = one campaign session.** Server config names the host SteamID.

1. **Auth.** Client obtains a Steam auth ticket from the already-loaded `steam_api64.dll`, sends
   it to the server, receives an fc5mp session token.
2. **Services.** Game's UbiServices login (`/profiles/sessions`) hits svc emu, which ignores the
   Ubisoft ticket (unverifiable; Ubisoft Connect already validated ownership locally) and returns
   a session for the mapped profile. `/applications/{applicationId}/configuration` returns
   config with co-op max players = 4. `public/rendezvous/v1/configuration` points RdV at us.
3. **Session.** Emulated RdV handles login, `CreateSession(nb_slots=4)` for the host, and joins
   for guests. The in-game join trigger (invite/presence/other) is determined by Phase 1
   capture; fallback is a client-side command invoking the game's `CJoinStormSessionOperation`.
4. **Relay.** RdV hands out a `p2pURL` listing peers as virtual addresses `10.77.0.N`. The net
   shim wraps any datagram to `10.77.0.N` in an fc5mp header
   `{version, src_vid, dst_vid, seq, hmac}` (HMAC keyed by the fc5mp token) and sends it to the
   relay, which verifies, taps to capture, and forwards. The receiving shim unwraps and presents
   the source as `10.77.0.M`. NAT-detection queries are answered as "open NAT" so Storm never
   falls back to its own relay. Storm believes it is talking direct; game code is unaltered at
   this layer.
5. **Teardown.** Host leaving ends the session for all.

**Encryption contingency:** if Storm encrypts its payloads, relaying still works (opaque bytes);
decoding then requires a client-side pre-encryption tap, reserved in the net shim design.

## 6. Co-op widening module

### 6.1 Phase 0 gate (read-only feasibility spike)
Determine whether co-op player storage is the **generic N-player manager shared with Versus**
(proceed: mostly logic patches) or **fixed 2-slot arrays** (stop and re-plan: hybrid approach C,
or target 3 players). Also compare co-op vs Versus code paths.

**Method (amended 2026-10-13):** game classes are compiled **without RTTI** — the only 856
`.?AV` descriptors belong to third-party libraries (CryptoPP, Havok, std, …). P0 therefore uses:
- **String anchors** — the game's debug/log format strings name their owning functions
  (e.g. `CGRStateCampaignInGame::DoSpawnPlayer() playerId: %d`); RIP-relative LEA xrefs locate
  the code that uses them.
- **Function table** — the PE exception directory (369,061 `RUNTIME_FUNCTION` entries, intact)
  gives function boundaries; chained unwind info resolves fragments to primary functions.
- **Shared-callee analysis** — callee closures of co-op-anchored vs Versus-anchored functions;
  shared player-list accessors indicate generic storage.
- **Operand probing** — small-constant compares (`cmp reg, 2`) and scaled indexed accesses in
  partner-related functions indicate fixed-width storage.
- **Vtable recovery** — runs of function-start pointers in data sections, to group functions
  into classes.

### 6.2 Limit taxonomy
1. **Admission caps** — session size (served by our config), join validation, session-full checks.
2. **Storage caps** — fixed-size player arrays / slot indices.
3. **"The partner" logic** — leash, revive, partner HUD marker, mission-start voting
   (`CCoopVotingEvent`), respawn, cutscenes, fast travel.

### 6.3 Patch structure
Each fix is a **named widening patch**: `{name, pattern, purpose, change, test}`, registered in
the coop module and individually toggleable from config for crash bisection.

### 6.4 v1 behaviour choices
- Leash: every guest tethered to the host.
- Mission-start voting: host decides; guests notified.
- Revive: anyone can revive anyone.
- HUD: markers for all other players.

### 6.5 Milestones
- **M1** — a 3rd player connects and is visible to host and guest #1 (ghost-like is acceptable).
- **M2** — 3 players move, shoot, drive; AI reacts to all.
- **M3** — missions start and complete with 3 players; objectives sync.
- **M4** — 4 players + stability pass.

With 2 copies, M1/M2 rely on the **fake-peer tool** (§8); M3 needs a real 3rd copy, M4 a 4th.

## 7. Error handling

- **Version lock.** Client hashes `FC_m64.dll` against a supported-build table. On mismatch or
  any required pattern missing, fc5mp disables itself with a clear message and the game runs
  vanilla. Never partially hooked.
- **No leak to Ubisoft.** svc shim is deny-by-default for Ubisoft online hosts (UbiServices, RdV,
  punch/detect/traversal): redirected to fc5mp or blocked, never passed through. The Ubisoft
  ticket is never forwarded anywhere.
- **Logs and crash dumps.** `fc5mp.log` (hooks installed, patches active, unknown routes); crash
  handler writes a minidump plus the active-patch list.
- **Relay liveness.** 1 s heartbeat, 10 s timeout → peer dropped, others notified via RdV emu;
  host drop → session end.
- **Auth failures** rejected with explicit reason (bad ticket / not whitelisted / server full).
- **Denuvo-suspect hooks** (tamper-style crashes) marked unsafe in the patch registry and moved.

## 8. Testing

- **Pattern tests (offline, Linux):** every pattern matches exactly once in the supported
  `FC_m64.dll`, run against the file on disk.
- **Server:** Rust unit tests; **replay tests** feeding captured real sessions through svc emu
  and relay with golden outputs.
- **Baseline regression (2 players):** vanilla 2-player co-op routed through fc5mp behaves
  identically to vanilla. Exit criterion for the transport work.
- **Fake-peer tool (required):** replays a captured guest join/handshake to the host as a
  synthetic 3rd peer, enabling M1/M2 debugging with only 2 copies.
- **Milestone checklists:** short manual scripts per M1–M4 (join, move, fight, start mission,
  complete mission).

## 9. Sub-projects and order

Each gets its own implementation plan.

| # | Sub-project | Needs | Exit criterion |
|---|-------------|-------|----------------|
| P0 | Feasibility spike: RTTI hierarchy + partner storage | DLL on disk | Gate decision recorded (§6.1) |
| P1 | Loader + hook core + **passive capture** of UbiServices/RdV/Storm traffic (read-only hooks against real Ubisoft, 2-player session) | 2 copies | Documented protocol map: routes, RdV messages, join flow, Storm packet framing, encryption yes/no |
| P2 | `fc5mp-server` auth + svc emu + RdV emu; client svc shim | P1 | Both clients log in and create/join a 2-slot session against fc5mp only |
| P3 | Relay hub + net shim + capture/decoders | P2 | Baseline regression passes |
| P4 | Fake-peer tool | P3 captures | Host accepts synthetic 3rd peer join handshake |
| P5 | Co-op widening M1→M4 | P0 gate, P4; real copies for M3/M4 | Milestones in §6.5 |

**P1 risk note:** passive capture talks to real Ubisoft servers with an injected client. Hooks
are read-only (no modified traffic), but it is still a modified client on a real account; the
user accepts this. Keep P1 sessions short and use the account they are least worried about.

## 10. Risks and open questions

- **Partner storage is fixed 2-wide** → large rework or reduced goal (P0 gate).
- **Denuvo protects a required hook target** → library-boundary fallback may not exist for every
  target.
- **Storm payload encryption** → decoding needs client-side taps (§5).
- **Join flow** may depend on Ubisoft friends/presence services we must also emulate.
- **Other Ubisoft services** the game requires before co-op is offered (clubs, stats, news) may
  need stub routes; discovered in P1.
- **Game updates** invalidate patterns (mitigated by version lock).
- **EULA/ToS**: modifying the client breaches the EULA; with EAC removed, ban risk is lower but
  non-zero. P2+ stops sending co-op traffic to Ubisoft entirely, which reduces exposure.
- **Code location**: separate repo `~/fc5mp` (`client/`, `server/`, `tools/`, `docs/`);
  confirmed: `/home/kalopsia/fc5mp`.

## 11. Corrections to prior RE notes

- FC5's engine is **Dunia 2**, not AnvilNext 2.0. Existing Dunia modding knowledge applies
  (e.g. Gibbed tools for `.fat`/`.dat`).
- `FC_m64.dll` contains **Denuvo markers**; the RWX `.xpdata` section and odd section layout are
  likely protection-related, so on-disk bytes may not equal runtime code. Prior §4 VAs need
  re-validation at runtime.
- `FC_m64.dll` statically links **OpenSSL** and **libcurl 7.54.1**; UbiServices REST base is
  `https://{env}public-ubiservices.ubi.com/{version}`; RdV uses a secure websocket.
- "EAC removed Nov 2019" is unverified.
- Prior claims of "no code path for a 2nd/3rd remote player" are string-level inference only;
  P0 tests this properly.
