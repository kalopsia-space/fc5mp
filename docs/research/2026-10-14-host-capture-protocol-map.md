# FC5 host capture — protocol map (P1.5b first live capture)

**Source:** `~/.fc5mp/captures/session-host-20261014-011117.bin` (host role, solo→co-op, ~30 min,
132,738 frames, 26 MB). First capture with TLS plaintext (P1.5b). Credentials verified masked
(67 Authorization headers, 0 leaks). **This file documents structure only — the capture itself is
sensitive and stays local.**

## TL;DR — the architecture P2 must emulate

Far Cry 5 co-op is **Quazal Rendezvous (NEX / PRUDP v0)** for login + matchmaking, plus a
Ubisoft "Storm" service layer and a P2P game-sync channel. Four network tiers:

1. **UbiServices REST (HTTPS)** — account/profile/auth bootstrap + telemetry. Fully captured.
2. **Quazal Rendezvous (PRUDP, UDP :6570)** — the matchmaking brain: PRUDP transport carrying
   NEX RMC calls, authenticated with a Quazal Kerberos ticket. **Client→server captured
   (incl. the ticket + encrypted RMC); server→client NOT captured** (overlapped `WSARecvFrom`).
3. **Storm service (UDP :11005 + :12000)** — plaintext opcode protocol, session/relay
   coordination + keepalive. Both directions captured.
4. **P2P game sync (UDP :9000)** — encrypted gameplay state, host↔peer, high volume. Both
   directions captured (opaque).

The boot markers show the harness went live (TLS hooks) at **+43 s**; login landed at **+64 s**,
so nothing was missed this session.

---

## 1. UbiServices REST (HTTPS) — fully captured

28 TLS streams to `*.ubi.com`. Account space `f40e304d-8e8d-4343-8270-d06487c35add`
(FCZ_PC_A sandbox), profile `fe525702-…`. Bootstrap sequence (first-seen time):

| t (s) | Request | Host | Purpose |
|---|---|---|---|
| 64.3 | `POST /v3/profiles/sessions` | public-ubiservices | **Login** → session ticket |
| 65.4 | `GET /v4/spaces/{space}/configs/events` | msr-public-ubiservices | App config |
| 65.4 | `GET /v2/websocket?NotificationTypes=…` (→ **101 Upgrade**) | public-ws-ubiservices | Push notification socket |
| 65.7 | `GET /v1/spaces/{space}/sandboxes/FCZ_PC_A/rendezvous/configuration` | msr-public-ubiservices | **→ PRUDP endpoint + public key** |
| 65.9 | `PUT /v2/connections/{id}/…/notifications` | public-im-ubiservices | IM/presence |
| 67.6–68.2 | `GET /v1/router/19` ×3 | {apac,ap-southeast-2,ncsa}-storm.ubi.com | **Storm router lookup** |
| 74–84 | stats, items/all, inventory, rewards, challenges | *-ubiservices | Profile data |
| 367 s → end | `POST /v3/profiles/me/events` every ~90 s | msr-public-ubiservices | Telemetry (ignorable for P2) |

**The key response** (`/rendezvous/configuration` and `/v1/router/19`, identical body):

```json
{"http_urls": [],
 "prudp_loadbalancer_url": "prudp:/address=lb-rdv-as-prod01.ubisoft.com;port=6570",
 "prudp_urls": [],
 "http_loadbalancer_url": "lb-rdv-ne1-prod-http.ubi.com/FCZ_PC_LNCH_A",
 "public_key": "RUNTMSAAAAAvpoE5TVE96o8WllgXStdPorODCIxDZ/RQiBTg+yefGbdcflm0isSu4PJ3KIUF4XqMAzRzcCKyGEDjpXmzOMMT"}
```
and the second router hop: `{"router":"3.73.55.101:12000"}`.

`public_key` base64 begins `RUNTMSAA…` = ASCII `ECS1 \x00\x00…` — a Quazal/NEX key blob. This is
the server identity the client authenticates the Rendezvous session against.

**For P2:** all of these are plain request/response we can reproduce. The server must mint a
session ticket at `POST /v3/profiles/sessions`, serve a config pointing `prudp_loadbalancer_url`
at **our** Rendezvous server, and serve **our** `public_key`.

## 2. Quazal Rendezvous — PRUDP v0 on UDP :6570 (`203.132.26.146`)

178 client→server packets captured; **0 server→client** (this socket uses overlapped
`WSARecvFrom`, which the harness does not yet capture — see §Gap).

Header: every packet starts `3f 31` (PRUDP source/dest virtual-port+stream bytes for this title),
then a 4-byte connection signature `9d1a89a0`, a 1-byte type/flags, a sequence counter, payload.

Observed client flow:
- **pkt 0** (18 B): `3f31 2000 00…00 3f31 2000` — SYN (connection request, zero signature).
- **pkt 1** (92 B): `3f31 b11c 9d1a89a0 0100 … 45 43 4b 31 20 ("ECK1 ") …` — CONNECT carrying the
  **Quazal Kerberos ticket** (`ECK1` = encrypted Kerberos blob).
- **pkts 2…N** (seq `02,03,04,…`, 47–799 B): DATA packets carrying **NEX RMC** calls, RC4-encrypted
  with the session key derived from the ticket. Fragmented across packets (sizes ~767–799).

This is where create-session / register-station / matchmaking happens. We have the requests
(encrypted) but not the responses that carry the assigned session id + station URLs a guest needs.

**For P2:** emulate a NEX/PRUDP server. Because an emulated server *issues* the Kerberos ticket, we
*own* the RC4 session key and can decrypt our own client's RMC calls — so the recv gap blocks
*reading Ubisoft's* responses, not our own server. Public NEX reimplementations (PRUDP v0 + RMC)
are strong prior art. First we need the RMC method/protocol ids, which requires the recv side.

## 3. Storm service — UDP :11005 (`203.132.26.108`, Ubisoft AU) + :12000 (`3.73.55.101`, the router)

**Plaintext** (entropy ~3.1). Opcode in byte 0:
- `:11005` send: `0x10` ×427, `0x11` ×1; recv: `0x13` ×212, `0x00` ×212, `0x12` ×2.
- Shape: `<op> <?> <seq16> <fields…>`; recurring ids `0045f150` and `2b1dc92d` (station / pid?).
  `0x10`↔`0x13` look like request↔ack pairs (~1/2 s over the session).
- `:12000` send/recv: 20-byte zero prefix + `1000 0000` + session id `96535800` + counter —
  a session-keyed probe/keepalive (STUN-ish NAT-keepalive to the RDV secure server).

**For P2:** decodable without crypto — worth fully reversing the opcode set once we decide whether
our server replaces Storm or we relay it.

## 4. P2P game sync — UDP :9000 (`87.121.209.175`)

83k sent / 48k recv, entropy ~7.8 (encrypted game state). Packets share a ~20-byte zero prefix
then a header (`…8000 0000 2010 4500 0002`). This is the actual co-op gameplay stream (the P5
widening target) — opaque for now; decoding needs a pre-encryption client tap, deferred.

## Gap that now blocks progress: overlapped `WSARecvFrom` recv capture

The Rendezvous **server→client** PRUDP is the one flow we must read to map the join/create-session
RMCs, and it is exactly the traffic the harness drops (overlapped `WSARecvFrom` on the `:6570`
socket — 178 sent, 0 received; the capture logs `RECV_PENDING` markers only). This was deferred in
P1.5a as "add completion-side capture if Phase A shows it's used." Phase A and now Phase B both
show it is used for the single most important socket.

**Next engineering step (new P1.5c):** capture overlapped-recv completions — hook
`WSAGetOverlappedResult` / `GetQueuedCompletionStatus` keyed by the `OVERLAPPED*` recorded at
`WSARecvFrom` time, and log the bytes once the completion delivers them. Then re-run a host
capture to obtain the PRUDP responses and begin the NEX RMC map.

## What P2 can start on now (unblocked)

1. UbiServices REST emulation — login ticket, config/router serving our Rendezvous + public key.
2. A PRUDP v0 + NEX RMC server skeleton (transport, Kerberos ticket issue, RC4) from public specs;
   we control the key, so our own client's RMC calls become readable for validation.

Blocked on the recv gap: the exact RMC protocol/method ids Ubisoft's RDV uses (needed to answer
the client the way it expects) and the join-flow responses.
