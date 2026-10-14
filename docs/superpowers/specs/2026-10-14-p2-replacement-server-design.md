# P2 — Replacement Server Design

## TL;DR

A single Rust binary that pretends to be Ubisoft's online services so Far Cry 5 connects to
**us** instead for co-op matchmaking. Two modules inside: an HTTP server for the login/config
endpoints the game expects, and a UDP server that speaks the game's matchmaking protocol
(PRUDP/NEX). The game's injected DLL redirects traffic to our server at runtime — the server
address is read from a config file, never hardcoded. Once players are matched, the actual
gameplay flows directly between them (peer-to-peer); the server is only needed for setup.

**Success criteria:** the game connects to our server, authenticates, creates a co-op session,
and a second player joins that session — all without touching Ubisoft's servers for matchmaking.

---

## 1. Goal

Build an MVP replacement server that emulates enough of Ubisoft's services for Far Cry 5 to
create and join a co-op session through our server. This is the foundation that P5 (4-player
widening) will later build on — but the MVP only needs to support the existing 2-player co-op
flow through our infrastructure.

## 2. Background — what the game does today

When Far Cry 5 starts co-op, it talks to several Ubisoft servers in sequence:

1. **Login (HTTPS).** The game sends credentials to `public-ubiservices.ubi.com` and receives a
   session ticket — a permission slip that says "this player is authenticated."

2. **Get matchmaking address (HTTPS).** The game asks Ubisoft "where is the matchmaking server?"
   by hitting endpoints like `/rendezvous/configuration` and `/v1/router/19`. The response
   contains an IP/port and a cryptographic public key for the matchmaking server.

3. **Matchmaking (PRUDP/NEX over UDP).** The game connects to the matchmaking address using a
   protocol called **PRUDP** — a custom reliable protocol built on UDP (like a homemade TCP with
   sequence numbers, acknowledgments, and retransmissions). Inside PRUDP, it speaks **NEX RMC**
   (Remote Method Calls) — essentially function calls sent over the network, like "create a
   session" or "register my player."

   Before sending any RMC calls, the game authenticates using a **Kerberos-style ticket** — a
   cryptographic token that proves the player is allowed to connect. The ticket also establishes
   an **RC4 session key** that encrypts all subsequent RMC traffic.

4. **Session coordination (Storm, UDP).** A separate plaintext UDP protocol handles relay
   coordination and NAT traversal. **Not in MVP scope** — we'll add a stub if the game requires
   Storm responses to proceed past matchmaking.

5. **Gameplay (P2P, UDP).** Once matched, players communicate directly. The server is not
   involved.

## 3. Architecture

Single Rust binary with two internal modules sharing in-memory state.

```
fc5mp-server
├── REST module          (HTTP, default port 21000)
│   ├── /v3/profiles/sessions     → fake login, return a session ticket
│   ├── /rendezvous/configuration → tell the game "connect to ME for matchmaking"
│   └── /v1/router/19             → return our PRUDP endpoint address + public key
│
├── PRUDP/NEX module     (UDP, default port 6570)
│   ├── PRUDP v0 transport   → connection management, reliability, sequencing
│   ├── Kerberos auth        → issue + verify tickets (we own the secret key)
│   └── NEX RMC handlers    → respond to create-session, register-station, etc.
│
└── Shared state
    ├── Player registry      → who's connected, their session keys
    └── Session registry     → active co-op sessions, who's in each one
```

### 3.1 REST module

A standard HTTP server (no TLS — see §4 for why) that serves the specific endpoints the game
hits during login and configuration. We have full captured request/response pairs from the
P1.5b TLS capture, so we know exactly what the game sends and what format it expects back.

Key endpoints:

- **`POST /v3/profiles/sessions`** — the game sends credentials, we return a fake session ticket
  and profile data. The ticket format must match what the game parses (JSON with specific fields).
- **`GET /rendezvous/configuration`** — the game asks where the matchmaking server is. We respond
  with our own PRUDP address and port.
- **`GET /v1/router/19`** — the game fetches the PRUDP endpoint details including a public key
  for the Kerberos handshake. We respond with our own key.

### 3.2 PRUDP/NEX module

This is the harder part. It has three layers:

**PRUDP v0 transport.** A reliable protocol on top of UDP. Each packet has:
- Source/destination virtual ports (for FC5: bytes `0x3f 0x31`)
- A 4-byte connection signature
- Type/flags byte (SYN, CONNECT, DATA, DISCONNECT, ACK)
- Sequence number for reliability
- Payload

The transport handles connection setup (SYN/CONNECT handshake), packet acknowledgment,
retransmission of lost packets, and fragmentation of large messages. PRUDP v0 is well-documented
by projects like Pretendo (Nintendo server reimplementation) — we follow the same spec.

**Kerberos authentication.** During the CONNECT phase, the game presents a Kerberos ticket
(the `ECK1` blob seen in captures). Since *our server issues* the ticket (via the REST
`/v1/router/19` response), we know the secret key and can:
- Verify the ticket is one we issued
- Derive the RC4 session key for encrypting/decrypting all subsequent RMC traffic
- This is what makes the whole approach work — we control both sides of the key exchange

The Kerberos scheme uses **HMAC-MD5** for ticket authentication (legacy crypto, but matching
what the game expects).

**NEX RMC handlers.** Once authenticated, the game sends RMC calls — each identified by a
protocol ID and method ID. Expected calls include:
- Create session (host creates a co-op game)
- Register station (player registers their network address)
- Join session (guest joins an existing game)
- Matchmaking queries

We will discover the exact method IDs iteratively: connect the game to our server, log which
IDs it calls, then implement responses one at a time until the session flow succeeds.

### 3.3 Shared state

In-memory data structures — no database needed. The server handles a handful of players on a
local network, so simple `HashMap`s behind a mutex are sufficient.

- **Player registry:** maps player IDs to their session keys, connection state, and network
  addresses.
- **Session registry:** maps session IDs to the list of players in each session and its current
  state (waiting for players, in progress, etc.).

## 4. Game redirection — DLL redirect mode

The game currently connects to `public-ubiservices.ubi.com` over HTTPS. We need it to talk to
our server instead.

**Approach:** extend the existing injected DLL with a "redirect" mode. The DLL already hooks
Winsock and TLS functions for traffic capture (P1.5). For P2, it adds the ability to rewrite
the destination address before the game's own code sends the request. This happens *before*
TLS encryption, which means:

- **The server doesn't need TLS.** The DLL intercepts below the encryption layer, so our server
  receives plain HTTP. This dramatically simplifies the server — no certificates, no TLS config.
- **No system configuration needed.** No hosts file edits, no proxy setup. The DLL handles
  everything inside the game process.

**Server address is NOT hardcoded.** The DLL reads the server address from a config file at
runtime (e.g. `fc5mp-client.toml` beside the game, or an environment variable set by the
launch script). A future standalone configuration tool will make this easy to set without
editing files manually.

**Redirect targets:**
- REST traffic (`public-ubiservices.ubi.com:443`) → `<server_ip>:<rest_port>` (default
  `127.0.0.1:21000`)
- PRUDP traffic (the address returned by `/rendezvous/configuration`) → already points at our
  server since we control the REST response

## 5. Configuration

`fc5mp-server.toml`:

```toml
# All fields have sensible defaults — a bare file works
listen_ip = "0.0.0.0"      # accept connections from LAN
rest_port = 21000           # HTTP (no TLS needed)
prudp_port = 6570           # PRUDP/NEX matchmaking
max_players = 4             # the whole point
```

The server also accepts CLI overrides (e.g. `--rest-port 8080`) for quick testing.

## 6. Usage flow

A typical co-op session:

1. **Host** runs `./fc5mp-server` — it prints the listening addresses.
2. **Host** launches Far Cry 5 with the DLL in redirect mode (via the existing launch script,
   extended with a `redirect` mode alongside the current `capture` mode). The DLL reads the
   server address from config.
3. **Host** creates a co-op session in-game. The game talks to our server for login and
   matchmaking — the server creates the session and waits for a guest.
4. **Guest** launches Far Cry 5 with the DLL configured to point at the host's IP address.
5. **Guest** joins the session in-game. The server matches them to the host's session.
6. The game establishes a **direct P2P connection** between host and guest for actual gameplay.
   The server is no longer needed (though it stays running for reconnects or new sessions).

The server is a **matchmaker, not a relay** — it's only busy during login and session setup.
Gameplay traffic flows directly between players, just like it does with Ubisoft's servers.

## 7. Build order

Each step is buildable and testable independently:

1. **REST module** — fully unblocked. We have the captured request/response pairs from P1.5b
   showing exactly what the game sends and what it expects back. Can be tested with `curl`
   before involving the game at all.

2. **PRUDP v0 transport** — the packet format is publicly documented (PRUDP v0 spec, Pretendo
   implementations). We build packet encode/decode, the connection handshake (SYN → SYN-ACK →
   CONNECT), reliability (ack tracking, retransmission), and fragmentation. Testable with a
   standalone client that sends PRUDP packets.

3. **Kerberos ticket issuance** — we design this ourselves using standard HMAC-MD5 crypto. The
   REST module serves the public key; the PRUDP module verifies tickets and derives session keys.
   Testable end-to-end without the game.

4. **NEX RMC handlers** — this is the learn-as-we-go phase. We connect the real game to our
   server and see which RMC method IDs it calls. The structured logging (`tracing`) is critical
   here: we log every incoming RMC call with its protocol ID, method ID, and decrypted payload,
   then implement responses iteratively until the game successfully creates a session.

**Key insight:** we don't need to decrypt the old Ubisoft captures. Since our server issues the
Kerberos ticket, we choose the encryption key, and can decrypt everything the game sends us.
The old captures are useful for understanding the REST responses but not required for the
PRUDP/NEX layer.

## 8. Technology

| Dependency | Purpose |
|---|---|
| `tokio` | Async runtime — drives both the HTTP server and UDP socket |
| `axum` | HTTP framework for the REST module |
| `rc4` | RC4 encryption/decryption for NEX session traffic |
| `hmac` + `md5` | HMAC-MD5 for Kerberos ticket authentication |
| `serde` + `toml` | Config file parsing |
| `tracing` + `tracing-subscriber` | Structured logging (critical for the iterative RMC phase) |

No external services — no database, no Redis, no separate TLS stack. Everything runs in-process.

## 9. Project structure

```
server/
├── Cargo.toml
├── src/
│   ├── main.rs              — startup, config loading, launch both modules
│   ├── config.rs            — TOML config parsing + CLI overrides
│   ├── state.rs             — shared player/session registries (Arc<Mutex<...>>)
│   ├── rest/
│   │   ├── mod.rs           — axum router setup
│   │   └── handlers.rs      — login, configuration, router endpoint handlers
│   └── prudp/
│       ├── mod.rs           — UDP socket loop, packet dispatch
│       ├── transport.rs     — PRUDP v0 packet encode/decode, reliability, fragmentation
│       ├── kerberos.rs      — ticket issuance, verification, session key derivation
│       └── rmc.rs           — NEX RMC call dispatch + per-method handlers
```

## 10. Open questions and risks

- **NEX RMC method IDs:** we discover these iteratively by connecting the game to our server.
  If the game calls methods we can't figure out responses for, we may need the validated P1.5c
  capture data (a live host capture showing `:6570` recv pkts > 0, confirming we can read
  Ubisoft's actual responses for reference).

- **Storm service:** not in MVP scope. If the game requires Storm responses to proceed past
  matchmaking (e.g. refuses to start the P2P connection without a Storm acknowledgment), we
  add a minimal Storm stub that replies with the expected opcodes. The captured Storm traffic
  is plaintext so this would be straightforward.

- **DLL redirect mode:** new code in the existing C++ client. Needs careful integration with
  the existing capture-mode hooks — redirect and capture should be selectable modes, not
  conflicting code paths. The launch script already has a mode argument (`host`/`guest`);
  `redirect` becomes a third mode.

- **Friend's setup:** the friend also needs the injected DLL and a config file pointing at the
  host's IP. The launch script and a future config tool handle this, but it's a UX surface
  that needs to be simple.

- **REST response fidelity:** the game may parse response fields we don't expect. The iterative
  approach (connect, see what breaks, fix) handles this, but early REST testing with the real
  game is important to surface surprises.

## 11. What is NOT in scope

- **4-player widening** — that's P5. The server is designed to support `max_players > 2` but
  the MVP only needs to handle the existing 2-player co-op flow through our infrastructure.
- **Storm service emulation** — added only if needed (see §10).
- **Relay/NAT traversal** — that's P3. Players must be able to reach each other directly (LAN
  or port-forwarded) for the MVP.
- **Game logic modification** — the server handles matchmaking only. The game's own co-op
  logic is unchanged.
- **Persistence** — no saved sessions, no player accounts. Everything is in-memory and resets
  when the server restarts.
