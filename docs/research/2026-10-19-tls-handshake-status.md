# FC5MP — in-game TLS + REST login status (2026-10-19, overnight autonomous run)

## Headline

**The in-game HTTPS blocker is solved and REST login works end to end.** Far Cry 5
now launches to the main menu with the real Ubisoft DLLs + our hooks, the game's own
HTTPS connection to `public-ubiservices.ubi.com` is redirected to our local server,
the TLS handshake completes, and the game successfully sends
`POST /v3/profiles/sessions` — the server logs `player logged in`. This is the
furthest the project has ever gotten: every prior session was blocked before any
REST request reached our server.

**Remaining blocker:** after login the game requests
`GET /v2/applications/{app_id}/configuration`, reads our response, then stops and
shows `Error: Maine-118` without contacting any other host. Serving a full, real
configuration shape did not change this. See "Remaining blocker" below.

## How the TLS blocker was solved (the key result)

The game statically links libcurl over OpenSSL 1.0.2n inside `FC_m64.dll`. Three
things had to line up, in order:

1. **Install the TLS detours.** The boot path used to skip the OpenSSL signature
   scan entirely (too slow under Wine), so no TLS hook was ever installed. New
   `targets::install_tls_known()` hooks `SSL_read`/`SSL_write`/`SSL_connect` at the
   RVAs recorded for the version-locked build (`0x3dd0560` / `0x3dd09c0` /
   `0x3dcf330`), after confirming the on-disk `FC_m64.dll` hash equals
   `00833fae…af2`. Wired into `dllmain.cpp` boot + `enable_all()`.
2. **Complete the handshake.** The redirected `ssl_connect` detour busy-polls
   `o_ssl_connect` to completion (required — a transparent pass-through never
   finished the handshake on the non-blocking socket). Verify-mode is forced to
   NONE so the handshake does not abort on the self-signed cert.
3. **Pass libcurl's post-handshake check.** libcurl calls `SSL_get_verify_result`
   after the handshake and aborts before sending the request if it is not
   `X509_V_OK`. Our self-signed cert makes the handshake set `verify_result = 18`
   (DEPTH_ZERO_SELF_SIGNED_CERT), so curl dropped every connection with zero bytes
   written. Fix: force `ssl->verify_result = 0` after the handshake.

   The `verify_result` offset (**0x17c**) was found safely, not guessed:
   a read-only diagnostic in our own `ssl_connect` hook scanned the SSL object
   after the handshake and found error code 18 at 0x17c on every connection; this
   matched the static finding that `FC_m64` has a dedicated 32-bit getter+setter at
   0x17c (`SSL_get_verify_result` / `SSL_set_verify_result`). The patch is guarded
   two ways: it only runs on a `redir` fd, and it re-confirms via `getpeername`
   that the peer is `127.0.0.0/8` before writing, so cert verification can never be
   disabled for a non-loopback host.

Evidence: trace shows `ssl_connect OK … loopback=1 verify_result 18->0`, then
`ssl_write … POST /v3/profiles/sessions`, and the server logs the login.

## What else was changed this session

- `server/src/rest/`: added the `/v2/applications/{app_id}/configuration` route
  (game uses v2; we only had v1), and the handler now serves a full real config
  shape from `server/src/rest/fc5_app_config.json` (adapted from OpenQuazal's real
  `applications/.../configuration.json`: FC5 ids, `uplayGameCode=FCZ`, sandbox
  `FCZ_PC_A`, all service hosts pointed at `public-ubiservices.ubi.com` which we
  redirect; 19 gatewayResources, 23 featuresSwitches).
- `scripts/fc5mp-steam-wrapper.sh` (new): Steam launch option is now just
  `…/scripts/fc5mp-steam-wrapper.sh %command%`; all env lives in
  `~/.fc5mp/launch.env`, editable between runs without touching Steam.
- `scripts/game-interact.sh`: monitor position resolved dynamically from `hyprctl`.

## Remaining blocker — app configuration / launcher path

- The game sends exactly two requests (`login`, `configuration`), both handled,
  then stops and shows Maine-118 without resolving or contacting any other host
  (no rendezvous, no storm, no events, no websocket).
- In the **2026-10-14 host capture**, the game itself did NOT request
  `/v2/applications/{id}/configuration`; it went login → events-config →
  websocket → `rendezvous/configuration` → router → profile data. In that session
  the **Ubisoft launcher** (`upc.exe`, its own gnutls stack, which we do NOT
  redirect) supplied the app configuration via local IPC.
- So the current behaviour is a **degraded/fallback path**: because the launcher is
  not providing config, the game self-fetches it over HTTPS and then gives up. A
  full, correct config body did not help, which suggests the game wants the config
  to come from the launcher (or expects FC5-specific fields the Watch Dogs-derived
  template doesn't carry).

## Recommended next steps (in order)

1. **Redirect the launcher's traffic too.** The proven 2026-10-16 LD_PRELOAD gnutls
   bypass got the launcher talking to our server. If the launcher is "online" via
   our server, the game should follow the normal 2026-10-14 path (login → events →
   rendezvous) instead of the app-config fallback. This is the most likely unlock.
2. If staying on the game-self-fetch path: capture FC5's **real**
   `/v2/applications/{id}/configuration` response (it is not in the 2026-10-14
   capture because the launcher handled it) — e.g. a short re-capture, or memdump
   `upc.exe` as on 2026-10-16 — and reproduce it exactly.
3. Once the game proceeds past config, the server already serves
   `rendezvous/configuration` and `router/19`; point `prudp_loadbalancer_url` at our
   server and move to the PRUDP/RMC flow (Block 2). The PRUDP server and RMC
   services are already built and registered.

## Current machine state (left for the morning)

- Game: **stopped**. Harness deployed in `FarCry5/bin`: our `version.dll` +
  `fc5mp-client.dll` (TLS known-offset install + busy-poll + verify_result=0 with
  loopback guard) alongside the **real** Ubisoft `uplay_r1_loader64.dll` +
  `dbdata.dll`. Reaches the main menu and logs in to our server.
- Steam launch option: `…/scripts/fc5mp-steam-wrapper.sh %command%`;
  `~/.fc5mp/launch.env` holds `WINEDLLOVERRIDES=version=n,b`.
- Server: left **running** (socat TLS :21000 → axum :21001, PRUDP :6570). Restart
  with `scripts/fc5mp-server.sh`.
- SAN/CA cert in `server/certs/` (originals backed up as `*.fc5mp-local.bak`).
- Capture TLS-stream dump (real Ubisoft responses, for reference/reproduction) at
  the session scratchpad `…/scratchpad/capdump/` (sensitive — local only).
- All source changes are in the working tree, **not committed** (user did not ask
  to commit). `git status` lists them; `docs/research/2026-10-19-tls-handshake-status.md`
  and `server/src/rest/fc5_app_config.json` are new.
