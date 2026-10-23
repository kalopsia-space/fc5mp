# FC5MP Implementation Plan

Target: Far Cry 5 campaign co-op expanded beyond 2 players, running against a custom server, launched via Steam on Linux/Wine.

## Reference Repository Discovery (2026-10-18)

Eleven open-source projects cloned into `vendor/` cover every layer of fc5mp's stack. Together they eliminate ~40-60 hours of reverse engineering — every major component now has a working reference implementation to port from.

### Vendor Reference Map

| fc5mp Component | Primary Reference | Language | What It Gives Us |
|---|---|---|---|
| `server/src/prudp/` | **nex-go** `prudp_packet_v0.go` | Go | Cleanest V0 impl, Quazal-mode flags, enhanced checksum, signature calc |
| RMC dispatch (to build) | **AlcatrazServer** `QNetZ/RMC/RMC.cs` | C# | Numeric-ID dispatch, attribute-based service registration |
| DDL serialization | **AlcatrazServer** `QNetZ/DDL/DDLSerializer.cs` | C# | Type-recursive read/write, AnyData, all DDL primitives |
| Kerberos/tickets | **AlcatrazServer** `QNetZ/Kerberos/` + **nex-go** `kerberos.go` | C#/Go | RC4+HMAC pattern, dummy keys work for private servers |
| `server/src/demux/` | **Powder** `transport/demux/` | Python | Complete demux: TCP framing, protobuf, auth, ownership, 9 services |
| `client/src/uplay_stub/` | **UplayServer** `Dll/upc_r1/Exports/` | C# | All 135 UPC exports, working Save implementation |
| Protocol specs | **acr-docs** `RMC/*.md` | Markdown | Method signatures, param types, protocol IDs for shared Quazal protocols |
| PRUDP internals | **prudp** `docs/*.pdf` + `ghidra-generated/` | C/docs | Original Quazal Net-Z 2.0 documentation + decompiled PRUDP library |
| Full server pattern | **Powder** (Steep server) | Python | End-to-end: HTTPS + Demux + NEX for a modern Ubisoft game |
| RDV services | **AlcatrazServer** `RDVServices/` | C# | TicketGranting, SecureConnection, NATTraversal, Notifications |
| Game services pattern | **AlcatrazServer** `DSFServices/` | C# | How to implement game-specific protocols on top of RDV |

### Key Technical Discoveries

**RMC format:** Two flavors exist across Ubisoft games. FC5 uses the **numeric-ID** format (confirmed by our binary RE): `[u32 size][u8 proto_id|0x80][u32 call_id][u32 method_id][params]`. Newer games like Steep use string-based method names.

**PRUDP corrections needed:**
- DATA packet signatures must use **HMAC-MD5** of `[session_key || seq_id(2 LE) || 0x00 || fragment_id || payload]` with `MD5(access_key)` as HMAC key (currently using plain MD5 — fine for handshake, wrong for authenticated DATA)
- RC4 uses **separate cipher instances per direction**, not a shared stream
- **HAS_SIZE flag** (bit 6): when set, 2-byte LE payload size prefix before DATA payload
- Connection signature for SYN-ACK: `MD5(client_ip(4) || client_port(2 BE))[:4]` reversed — matches current impl

**Kerberos auth flow:**
1. Client → Auth Server (protocol 0x0A): `Login` / `LoginWithToken`
2. Server returns Kerberos ticket encrypted with user's key (RC4)
3. Client calls `RequestTicket` → gets ticket for secure server
4. Client sends ticket in CONNECT payload
5. Server decrypts with RC4, verifies HMAC-MD5 (last 16 bytes), recovers session key
6. Session key derives RC4 ciphers for all subsequent DATA encryption
7. **Dummy constants work** — AlcatrazServer uses hardcoded keys, fc5mp can do the same

**Demux protocol:**
- TCP with 4-byte big-endian length prefix + protobuf body
- Auth: `AuthenticateReq { ClientId="uplay_pc", Token }` before service connections
- Multiplexing: `OpenConnectionReq { ServiceName }` → `connectionId`, then `Push { Data { ConnectionId, Data } }`
- 9 services: `ownership_service`, `friends_service`, `party_service`, `cloudsave_service`, `download_service`, `store_service`, `ach_frontend`, `playtime_service`, `pcbang_service`

**Protocol IDs (stable across Ubisoft Quazal games):**

| ID | Protocol | FC5 Status |
|----|----------|------------|
| 0x0A (10) | TicketGranting (Auth) | Must implement |
| 0x0B (11) | SecureConnection | Must implement |
| 0x0E (14) | Notifications | Needed |
| 0x12 (18) | Health | Confirmed in binary |
| 0x13 (19) | Monitoring | Confirmed in binary |
| 0x15 (21) | MatchMaking | Needed for MP |
| 0x03 (3) | NATTraversal | Needed for MP |

### Prior Discoveries (preserved)

- 5th Echelon `quazal` crate exists but requires Rust nightly — not adopting as dependency, using as reference only
- FC5's 11 protocols / 100 RPC methods extracted from FC_m64.dll (full map in scratchpad `fc5_protocols.md`)
- FC5 does NOT embed DDL binary definitions (no DDL magic `0xCD652312` found)
- 6 protocols shared with SC:BL (Health, Monitoring, OfflineGameNotifications, SecureConnection overlap; LoginProtocol differs)

---

## Phase 1 — Two-Player Co-op Foundation

Get standard 2-player co-op working through our custom server.

### Block 0 — RMC + Kerberos + Demux (Parallel Agents, ~4-6h)

**Goal:** Build the three missing server layers using vendor references. Two independent work streams run in parallel.

**Stream A — PRUDP Services (RMC + Auth):**

*Reference: AlcatrazServer `QNetZ/`, nex-go `rmc_message.go` + `kerberos.go`*

- [ ] Build RMC message decoder/encoder in `server/src/rmc/` — numeric protocol/method ID format
- [ ] Build trait-based RMC dispatch system (protocol trait + method handler registration)
- [ ] Implement TicketGranting service (protocol 0x0A): `LoginWithToken` → return dummy Kerberos ticket, `RequestTicket` → return session ticket
- [ ] Implement Kerberos ticket generation with dummy keys (RC4 + HMAC-MD5)
- [ ] Implement SecureConnection service (protocol 0x0B): `Register`/`RegisterEx` → echo station URL
- [ ] Fix DATA packet signatures to use HMAC-MD5 with session key
- [ ] Handle HAS_SIZE flag in packet parsing
- [ ] Wire RMC dispatch into existing PRUDP connection handler

*Key reference files:*
- `vendor/AlcatrazServer/QNetZ/RMC/RMC.cs` — dispatch pattern
- `vendor/AlcatrazServer/QNetZ/Kerberos/KerberosTicket.cs` — ticket format
- `vendor/AlcatrazServer/RDVServices/Services/TicketGrantingService.cs` — auth flow
- `vendor/AlcatrazServer/RDVServices/Services/SecureConnectionService.cs` — register flow
- `vendor/nex-go/prudp_packet_v0.go` — signature calculation
- `vendor/nex-go/kerberos.go` — RC4+HMAC encryption

**Stream B — Demux + Ownership:**

*Reference: Powder `transport/demux/`, UplayServer `Libs/ClientKit/Demux/`*

- [ ] Implement TCP listener with 4-byte BE length-prefix framing
- [ ] Implement protobuf message codec (hand-rolled, port from Powder's `pb.py`)
- [ ] Handle `AuthenticateReq` — accept any token
- [ ] Handle `ClientVersion` push
- [ ] Implement service connection multiplexing (`OpenConnectionReq` → assign connectionId)
- [ ] Implement `ownership_service` — return FC5 product ID as owned
- [ ] Stub remaining services (friends, party, cloudsave, etc.) with empty/success responses
- [ ] Wire into server's existing TCP/TLS listener

*Key reference files:*
- `vendor/powder/powder/server/transport/demux/server.py` — full demux server
- `vendor/powder/powder/server/transport/demux/pb.py` — protobuf codec
- `vendor/powder/powder/server/transport/demux/ownership.py` — ownership responses
- `vendor/powder/powder/server/transport/demux/services.py` — service dispatch
- `vendor/UplayServer/Libs/ClientKit/Demux/Socket.cs` — protocol details
- `vendor/UplayServer/Libs/ClientKit/Demux/Connection/OwnershipConnection.cs` — ownership init

**Deliverable:** Server handles RMC method calls over PRUDP with Kerberos auth stubs, AND handles demux connections with ownership verification. Both paths ready for game testing.

---

### Block 1 — Hooks + Steam Launch (~6-8h)

**Goal:** Game launches through Steam with our DLLs, hooks load, multiplayer traffic intercepted.

> **Status 2026-10-19:** Game launches to main menu with real Ubisoft DLLs + our hooks
> (stub-DLL path abandoned — Denuvo needs real Ubisoft Connect). **In-game HTTPS + REST
> login now WORK end to end:** TLS detours install at known offsets, the handshake
> completes, and the `verify_result=0x17c` patch gets libcurl past its post-handshake cert
> check, so the game sends `POST /v3/profiles/sessions` and the server logs the login.
> **Now blocked** one step later: the game fetches `/v2/applications/.../configuration`,
> reads our response, then stops at Maine-118 without contacting any other host.
> Full details: `docs/research/2026-10-19-tls-handshake-status.md`.
>
> **Update 2026-10-19 PM — "go after the real config" (Path A) results:**
> - ✅ **Config is now byte-correct.** Replaced the OpenQuazal-adapted template with the
>   genuine config extracted from the 2026-10-14 working capture (added the missing
>   `configuration.custom.featuresSwitches` block). Content-Length matches the real 12934.
>   (`server/src/rest/fc5_app_config.json`; `mercury.client_secret` blanked — inert, that
>   host is redirected to us.)
> - ✅ **Added the Ubisoft gateway response headers** the real server sends — `Ubi-TransactionId`,
>   `Ubi-Forwarded-By`, `Server: openresty`, HSTS, `X-Frame-Options`, `X-Content-Type-Options`,
>   `Content-Type: application/json; charset=utf-8` — via middleware in `server/src/rest/mod.rs`.
> - ❌ **Neither unblocked the game.** It still does exactly login (#1) + config (#2), then stops.
> - 🔎 **Port 4455 is a RED HERRING (corrected).** Earlier I suspected the launcher's local
>   IPC on `127.0.0.1:4455` was the blocker. An A/B passthrough test disproved it: with our
>   redirect OFF, the game goes **fully online to real Ubisoft** while 4455 stays
>   connection-refused the whole time. 4455 is just harmless background overlay polling —
>   **we do NOT need to emulate the launcher.**
> - 📋 **The real wall:** the game reads our config and its SDK simply decides not to advance
>   to request #3. The normal chronological order (from the capture) is: #1 login → #2 config →
>   **#3 `GET /v1/profiles/me/populations`** → entities → events (msr-) → websocket → users →
>   … → storm `/v1/router/19`. Our game never attempts #3.
> - ⚠️ **Open anomaly:** `socat E I/O error` is logged right after config every run. We cannot
>   yet tell if the game receives the *complete, valid* config body, because the client logs
>   `ssl_connect`/`ssl_write` but **not** `ssl_read`.
>
> **→ NEXT STEP (where to resume):** Add `ssl_read` logging to the client
> (`client/src/client/tls_hooks.cpp`) and relaunch. This single measurement disambiguates the
> two remaining hypotheses: **(a)** the config response is being truncated/broken on delivery
> (the socat error is real) vs **(b)** the game receives a perfect config and its SDK stops for
> a content/logic reason. Secondary leads if delivery is clean: force HTTP keep-alive (the game
> reused one fd for login+config but opened nothing for #3), or replace socat with the server's
> built-in OpenSSL TLS (`tls-openssl` feature already in `Cargo.toml`) to rule out the socat I/O
> error. Launch gotchas: repeated `kill -9` of FarCry5.exe wedges the Wine prefix — reset with
> `wineserver -k` (kill orphan wine procs by PID via `pgrep -f '[F]arCry5'`; never
> `pkill -f FarCry5`, it self-kills the shell).

- [x] Fix version.dll proxy for Steam/Ubisoft Connect launch path
- [x] Verify fc5mp-client.dll loads hooks without crashing (builds clean, runtime needs testing)
- [x] Update Uplay stub exports using UplayServer reference (137 exports — 135 reference + 2 extras)
- [x] Add detailed traffic logging to hooks (DNS, UDP redirect, connect, TLS traces)
- [x] Launch script updated for Steam+hooks mode (`scripts/fc5mp-launch.sh full`)
- [x] Fix sha256_file_hex to streaming (constant memory, no 250MB allocation)
- [x] Build and test all DLLs (13/13 host tests pass, Wine tests pass)
- [x] Install DLLs into game directory
- [x] Server infrastructure: socat TLS + REST + demux + PRUDP all operational (34 tests)
- [ ] Start game through Steam, reach main menu (needs user — see test instructions below)
- [ ] Confirm hooks intercept Ubisoft multiplayer traffic (needs user)
- [ ] Map all outbound connections (IPs, ports, protocols) (needs user)
- [ ] Redirect traffic to fc5mp-server, iterate on REST/demux responses (needs user)
- [ ] Document every endpoint the game hits (needs user)

#### Steam Launch Test Instructions

Everything is built, installed, and ready. To test:

**Terminal 1 — Start the server:**
```bash
bash ~/fc5mp/scripts/fc5mp-server.sh
```

**Terminal 2 — Launch Far Cry 5 through Steam:**

In Steam, right-click Far Cry 5 → Properties → Launch Options, paste:
```
WINEDLLOVERRIDES="uplay_r1_loader64=n;uplay_aux_r164=n;dbdata=n;version=n" %command%
```
Then click Play. The DLL overrides tell Wine to use our proxy DLLs from the game directory instead of built-in Wine stubs.

**What to check after launch:**
1. `fc5mp-boot.log` (in game's bin/ directory) — should show stages 0-3
2. `fc5mp-connect-trace.txt` — shows all connect() calls and redirects
3. `fc5mp-hooks-diag.txt` — DNS redirect log
4. `fc5mp-tls-trace.txt` — TLS connection log
5. `uplay_stub.log` — Uplay API call log
6. Server terminal — should show REST requests and demux connections

*Key reference files:*
- `vendor/UplayServer/Dll/upc_r1/Exports/Main.cs` — UPLAY_Init, UPLAY_Update, event pump
- `vendor/UplayServer/Dll/upc_r1/Exports/User.cs` — user/ownership exports
- `vendor/UplayServer/Dll/upc_r1/Exports/Save.cs` — complete save implementation (portable)
- `vendor/UplayServer/Dll/upc_r1/Structures.cs` — UPLAY_Overlapped, UPLAY_Event structs

**Deliverable:** Game reaches main menu in "online" state, all traffic flowing through our server.

---

### Block 2 — Single-Player Online Session (~2-3h)

**Goal:** Start a game/load a save with hooks active. Game thinks it's online and ready for co-op.

- [ ] Navigate past main menu into gameplay
- [ ] Handle LoginProtocol::LoginWithToken (method 1) via RMC dispatch
- [ ] Handle SecureConnectionProtocol::Register (method 1)
- [ ] Handle MatchmakingProtocol::CreateSession (method 1) — game creating its session on load
- [ ] Get co-op menu accessible ("Invite Friend")
- [ ] Log all RMC calls the game makes
- [ ] Document full connection sequence: REST → Demux → PRUDP → RMC services

*Key reference files:*
- `vendor/powder/powder/server/transport/nex/handlers/login.py` — login flow outputs
- `vendor/AlcatrazServer/DSFServices/Services/MatchMakingService.cs` — session creation pattern
- `vendor/acr-docs/RMC/MatchMakingProtocol.md` — method signatures

**Deliverable:** One player in-game, connected to our server, co-op UI functional.

---

### >>> YOU STEP IN — Traffic Capture (Optional, ~1-2h)

Possibly unnecessary now that we have protocol documentation from acr-docs/acb-docs. This becomes a *validation* step if we hit unexplained RMC calls.

---

### Block 3 — Protocol Implementation (~3-4h)

**Goal:** Implement server handlers for every co-op-critical RPC method.

- [ ] Implement MatchmakingProtocol handlers: CreateSession, JoinSession, JoinSession2, AddReservations, SendInvitation, AcceptInvitation
- [ ] Implement session state management (host/guest roles, player slots)
- [ ] Implement SecureConnectionProtocol::RequestConnectionData
- [ ] Handle OfflineGameNotificationsProtocol::PollNotifications
- [ ] Return defaults for non-critical methods (GetMatchmakingStats, QueryStats)
- [ ] DDL serialization for complex param types (port from AlcatrazServer's DDLSerializer)

*Key reference files:*
- `vendor/AlcatrazServer/DSFServices/Services/MatchMakingService.cs`
- `vendor/AlcatrazServer/QNetZ/DDL/DDLSerializer.cs`
- `vendor/acr-docs/RMC/MatchMakingProtocol.md`, `SecureConnectionProtocol.md`
- `vendor/acb-docs/RMC/GameSessionAdminProtocol.md`

**Deliverable:** Server implements the full co-op protocol flow. Single player can create a session.

---

### Block 4 — Two-Player Connection (~4-5h)

**Goal:** Two clients connect and establish a co-op session.

- [ ] Implement invitation flow: SendInvitation → NotificationProtocol → AcceptInvitation → JoinSession
- [ ] Implement peer-to-peer connection brokering via RequestConnectionData
- [ ] Handle PRUDP station URLs and connection tokens
- [ ] Multi-client connection tracking
- [ ] Test with simulated second client
- [ ] Fix timing, ordering, and state issues

*Key reference files:*
- `vendor/AlcatrazServer/DSFServices/Party/` — session/gathering management
- `vendor/AlcatrazServer/RDVServices/Services/NotificationService.cs`
- `vendor/acr-docs/RMC/NATTraversalProtocol.md`

**Deliverable:** Server brokers connection between two clients. Ready for live test.

---

### >>> YOU STEP IN — Two-Player Test (~2-3h)

Launch two game instances with hooks pointing at our server. Iterate together in real-time until co-op works.

---

### Block 5 — Polish + Stability (~8-10h)

**Goal:** Handle edge cases, disconnections, errors. Make it reliable.

- [ ] Graceful disconnect and reconnect
- [ ] Host migration (MatchmakingProtocol::MigrateSession)
- [ ] Packet ordering and reliability
- [ ] Save game sync (port UplayServer's save implementation if needed)
- [ ] Error recovery (connection drops, timeouts)
- [ ] Launcher script (starts server + installs hooks + launches game)
- [ ] Test a full 30-min play session

**Deliverable:** Stable 2-player co-op through our server. Phase 1 complete.

---

## Phase 2 — Expand to N-Player Campaign Co-op

### Block 6 — Player Limit Investigation (~10h)

Unpack .dat/.fat archives, find where the 2-player co-op limit lives (data configs vs compiled code). Check Arcade mode's 12-player PvP for reusable patterns.

### >>> YOU STEP IN — Feasibility Review (~30min)

Read Block 6 report. Decide target player count and accept tradeoffs.

### Block 7 — Apply Player Limit Changes (~10h)

Data-level patches (configs) or code-level patches (runtime binary patching) depending on Block 6 findings.

### Block 8 — N-Player Server Protocol (~7-8h)

Extend session state, N-way peer introduction, multi-client PRUDP handling.

### Block 9 — State Replication Patches (~10h)

Patch replication for N players, spawn points, AI scaling, serialization.

### >>> YOU STEP IN — Multi-Player Test (~3-4h)

3+ players testing. Multiple rounds of fixes.

### Block 10 — Multi-Player Stability (~10h)

Fix crashes, desync, optimize N² traffic, graceful degradation for scripted events.

---

## Phase 3 — Server Plugins + Mod Compatibility

### Block 11 — Plugin System (~10h)
Lua/JS plugins with event hooks (on_player_join, on_kill, etc.), hot-reload, example plugins.

### Block 12 — Mod Compatibility (~10h)
Test with popular Dunia mods, handle mismatched mod states, whitelist/blacklist system.

### Block 13 — Internet Play + NAT Traversal (~10h)
STUN/TURN, direct P2P with relay fallback, mixed IPv4/IPv6.

### Block 14 — Save System + Installer (~10h)
Server-side saves, one-click installer, server browser, auto version pinning.

---

## Summary

### Phase 1 — Two-Player Foundation

| Block | Type | Est. Hours | Depends On |
|-------|------|-----------|------------|
| 0 — RMC + Kerberos + Demux | Parallel agents | **~4-6h** | Nothing |
| 1 — Hooks + Steam Launch | Solo | ~6-8h | Block 0 |
| 2 — Single-Player Online | Solo | ~2-3h | Block 1 |
| **Traffic Capture** | **You + friend (optional)** | **~1-2h** | Block 2 (if needed) |
| 3 — Protocol Implementation | Solo | ~3-4h | Block 2 |
| 4 — Two-Player Connection | Solo | ~4-5h | Block 3 |
| **Two-Player Test** | **You + friend** | **~2-3h** | Block 4 |
| 5 — Polish + Stability | Solo | ~8-10h | Two-player test |

**Phase 1 total: ~28-37h solo + ~3-5h your time**

### Phase 2 — N-Player Expansion

| Block | Type | Est. Hours | Depends On |
|-------|------|-----------|------------|
| 6 — Player Slot RE | Solo | ~10h | Phase 1 complete |
| **Feasibility Review** | **You** | **~30min** | Block 6 |
| 7 — Binary Patching | Solo | ~10h | Review |
| 8 — N-Player Server | Solo | ~7-8h | Block 7 |
| 9 — State Replication | Solo | ~10h | Block 8 |
| **Multi-Player Test** | **You + friends** | **~3-4h** | Block 9 |
| 10 — Stability | Solo | ~10h | Test |

### Phase 3 — Plugins + Polish

| Block | Type | Est. Hours |
|-------|------|-----------|
| 11 — Plugin System | Solo | ~10h |
| 12 — Mod Compat | Solo | ~10h |
| 13 — NAT Traversal | Solo | ~10h |
| 14 — Saves + Installer | Solo | ~10h |

### Totals

| | Phase 1 | Phase 2 | Phase 3 | Total |
|-|---------|---------|---------|-------|
| **Solo compute** | ~28-37h | ~47-48h | ~40h | **~115-125h** |
| **Your time** | ~3-5h | ~4-5h | ~0h | **~7-10h** |

*Revised 2026-10-18 after vendor repo discovery. Phase 1 down ~15-20h from previous estimate (~42-46h) due to reference implementations eliminating RE guesswork.*

---

## Vision

FiveM-style platform for Far Cry 5: community-run servers with plugins, mod compatibility, N-player campaign co-op, and custom game modes.

## Risk Factors

- **Phase 1 risk: low.** Every component now has a working reference implementation. Remaining risk is FC5-specific quirks (access key, protocol version differences) and the Steam/Wine launch path.
- **Phase 2 risk: high.** Block 6 report is the go/no-go gate. Player limit may be in data (easy) or in code/VM-protected sections (hard/impossible).
- **Biggest unknown:** FC5's access key for PRUDP checksum calculation. May need to extract from binary or test known Ubisoft keys.

## Dependencies

- `vendor/` — 11 reference repositories (see Reference Map above)
- Protocol reference: `fc5_protocols.md` in scratchpad (11 protocols, 100 methods)
- Existing server code: `server/src/prudp/` (working V0 handshake), `server/src/rest/` (REST endpoints), `server/src/demux/` (started)
- Existing client code: `client/src/client/` (hooks), `client/src/uplay_stub/` (started), `client/src/version_proxy/` (DLL proxy)
