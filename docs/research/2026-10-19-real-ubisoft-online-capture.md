# Real Ubisoft online session — captured answer key (2026-10-19)

**Source:** `fc5mp-capture.bin` in the game `bin/` dir is **append-mode** and
accumulates across runs. It contains a full, successful **real-Ubisoft**
passthrough session captured 2026-10-19 ~11:26 GMT (redirect OFF), alongside
later failed redirect attempts. Decode with
`analysis/capdump.py <cap> --dump <dir>`.

Real streams are unmistakable: `Server: openresty`, real `Ubi-TransactionId`
gateway headers, 200 OK login, and **real bidirectional UDP** to public IPs on
`:6570` (PRUDP/NEX) and `:12000` (storm relay). That run reached full online.

## REST sequence the game performs to go online (real hosts)

1. `POST /v3/profiles/sessions` (public-ubiservices) — login, 200, ~3.3KB JSON (ticket/sessionKey masked)
2. `GET /v1/spaces/{space}/items/all` (public-ubiservices)
3. `GET /v1/profiles/{pid}/rewards?...` (msr-public-ubiservices)
4. `GET /v4/spaces/{space}/configs/events` (msr-public-ubiservices)
5. `GET /v1/spaces/{space}/sandboxes/FCZ_PC_A/rendezvous/configuration` (msr-public-ubiservices) — **PRUDP locator, see below**
6. `GET /v2/websocket?NotificationTypes=...&SpaceIds=...` (public-ws-ubiservices) — 101 upgrade + US_CONNECTION_ESTABLISHED push
7. `PUT /v2/connections/{conn}/{n}/{id}/notifications` (public-im-ubiservices)
8. `GET /v1/router/19` (×3: ap-southeast-2-storm, apac-storm, ncsa-storm) — storm relay locator

Space id (FCZ / Far Cry 5): `f40e304d-8e8d-4343-8270-d06487c35add`
App id: `e0b29a98-ce38-4bc7-96ee-9d20e793e9e3`  Sandbox: `FCZ_PC_A`

## CRITICAL: rendezvous/configuration response (the bridge to PRUDP)

```json
{"http_urls": [],
 "prudp_loadbalancer_url": "prudp:/address=lb-rdv-as-prod01.ubisoft.com;port=6570",
 "prudp_urls": [],
 "http_loadbalancer_url": "lb-rdv-ne1-prod-http.ubi.com/FCZ_PC_LNCH_A",
 "public_key": "RUNTMSAAAAAvpoE5TVE96o8WllgXStdPorODCIxDZ/RQiBTg+yefGbdcflm0isSu4PJ3KIUF4XqMAzRzcCKyGEDjpXmzOMMT"}
```

- `prudp_loadbalancer_url` → the PRUDP/NEX game server host:port. We must return
  **our** host here (our server already listens on :6570).
- `public_key` → base64; prefix `RUNT...` ≈ "ECS1", the Quazal/NEX ECDSA public
  key the game uses to validate the PRUDP server identity during the handshake.
  **We must return the public key that matches OUR server's NEX private key.**
  We control both the rendezvous response and the server keypair, so they just
  need to be mutually consistent — no need to recover Ubisoft's private key.

## Storm router response (relay locator)

```json
{"router":"3.73.55.101:12000"}   // and 54.79.133.166:12000, ...
Server: Provisioning
```

## Websocket (step 6) — 101 then a server push frame

```
HTTP/1.1 101 Switching Protocols
Upgrade: websocket
Sec-WebSocket-Accept: <computed from client key>

<ws frame> {"content":{"connectionId":"...","contactUrl":".../notifications"},
 "notificationType":"US_CONNECTION_ESTABLISHED", ...}
```

## UDP transport observed (post-REST)

- PRUDP/NEX: `*:6570` (lb-rdv), bidirectional
- Storm relay: several `*:12000` (AWS IPs), bidirectional — NAT punch-through
- Game issues overlapped `WSARecvFrom` (64 pending) + `sendto`/`recvfrom`

## Known regression to fix separately

Later redirect run (14:28): game's `POST /v3/profiles/sessions` to our local
server got **0 bytes back** (6 retries, all `in=0`), causing the login→config
loop + stall. Local server was not answering that request in that run (down or
route mismatch) — verify server is up and the route returns a body.
