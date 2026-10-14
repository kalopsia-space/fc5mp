# DLL Redirect Mode Design

## Goal

Add a "redirect" mode to the existing fc5mp injection DLL so the game's network traffic goes to our local replacement server instead of Ubisoft's. The DLL already hooks `sendto`, `WSASendTo`, `recvfrom`, `WSARecvFrom`, `SSL_read`, `SSL_write`, and `GetOverlappedResult` in observe-only mode. Redirect mode makes the UDP hooks rewrite destination addresses before calling the originals.

## Constraints

- Server address MUST come from a config file, not hardcoded into the DLL
- Existing capture-only mode must continue working when no redirect config is present
- The DLL cross-compiles with MinGW for Windows (runs under Proton/Wine)
- No anti-cheat to worry about (EAC was removed from FC5 in Nov 2019)

## Architecture

### Config file

Path: `%USERPROFILE%\.fc5mp\redirect.toml` (Windows path, resolved at DLL boot). Alternatively, the env var `FC5MP_REDIRECT` points at an explicit path.

Format:
```toml
server_ip = "127.0.0.1"
rest_port = 21000
prudp_port = 6570
```

If the file doesn't exist and the env var isn't set, redirect is disabled — the DLL works in capture-only mode exactly as before.

### Phase 1: UDP redirect (PRUDP)

This is the critical path. The game's PRUDP traffic (Quazal Rendezvous matchmaking) goes over UDP. The DLL already hooks `sendto` and `WSASendTo`.

**In `d_sendto` and `d_WSASendTo`:** before calling the original, check if the destination is a known Ubisoft PRUDP endpoint (port 6570 or any IP that was returned in the rendezvous config response). If so, rewrite the `sockaddr_in` to `{server_ip}:{prudp_port}` from the config. Log the original destination for diagnostics. Pass the rewritten address to the real Winsock call.

**In `d_recvfrom`, `d_WSARecvFrom`, and `d_GetOverlappedResult`:** the "from" address in received packets will already be our server's address (since the OS fills it from the actual sender). No rewriting needed on the receive path.

**Implementation:**

New files:
- `client/src/client/redirect.hpp` — declares the redirect config and address-rewriting API
- `client/src/client/redirect.cpp` — loads config from TOML file, provides `maybe_rewrite_dest()` function

```cpp
// redirect.hpp
#pragma once
#include <cstdint>
#include <optional>
struct sockaddr;

namespace fc5mp::redirect {
struct Config {
    uint32_t server_ip;   // network byte order
    uint16_t rest_port;   // host byte order
    uint16_t prudp_port;  // host byte order
};

// Load config from file. Returns nullopt if redirect is disabled.
std::optional<Config> load();

// If redirect is active and `dest` targets a known Ubisoft endpoint,
// rewrites dest in-place to point at our server. Returns true if rewritten.
bool maybe_rewrite_dest(sockaddr* dest, int destlen);

// Whether redirect mode is active (config was loaded successfully).
bool active();
}
```

Modified files:
- `client/src/client/net_hooks.cpp` — in `d_sendto` and `d_WSASendTo`, call `redirect::maybe_rewrite_dest()` on a copy of the destination sockaddr before passing it to the original
- `client/src/client/dllmain.cpp` — call `redirect::load()` early in the Boot thread (before hooks are installed)

**Known Ubisoft PRUDP destinations to match:**
- Any destination with port 6570 (the standard Quazal Rendezvous port)
- Port 11005 or 12000 (Storm service ports, if we decide to handle those later)

The match is port-based, not IP-based, because the game resolves Ubisoft's hostnames dynamically and the IPs can vary.

### Phase 2: REST redirect (deferred)

The game talks HTTPS to `public-ubiservices.ubi.com` (and related hostnames) for login, rendezvous config, etc. Redirecting this is more complex because it involves TLS.

**Recommended approach (simplest):** Use a hosts-file override on the host machine to resolve Ubisoft hostnames to 127.0.0.1, and configure our Rust server to optionally serve HTTPS with a self-signed cert (or use `rustls` with a generated cert). This avoids any DLL changes for REST redirect.

**Alternative (if hosts-file doesn't work under Proton):** Hook `getaddrinfo` in ws2_32.dll to resolve known Ubisoft hostnames to `server_ip` from config. This is one additional hook in `net_hooks.cpp`.

Phase 2 is deferred until Phase 1 is confirmed working. The hosts-file approach should be tried first.

### Redirect logic detail

```
d_sendto / d_WSASendTo
    │
    ├─ redirect::active()?
    │   ├─ NO → call original with untouched args (existing behavior)
    │   └─ YES → copy sockaddr
    │            redirect::maybe_rewrite_dest(&copy)?
    │            ├─ YES → log "redirecting from {orig} to {new}"
    │            │        call original with rewritten sockaddr
    │            └─ NO  → call original with untouched args
    │
    └─ capture/log as before (using the ORIGINAL dest for the capture record,
       so captures still show the real Ubisoft endpoints for protocol analysis)
```

### Testing

- **Unit test (cross-compiled):** `maybe_rewrite_dest()` on a sockaddr_in with port 6570 → rewritten. Port 443 → not rewritten. Redirect disabled → not rewritten.
- **Integration test:** Start the Rust server, launch the game with the DLL in redirect mode, confirm the server logs show SYN packets arriving on the PRUDP listener.
- **Regression:** With no redirect config file, the DLL must behave identically to the current capture-only mode.

### Config file parsing

The TOML parsing can be minimal — the file has 3 keys, all simple values. A hand-written parser (scan for `key = "value"` lines) avoids adding a TOML library dependency to the C++ build. The Rust server already uses the `toml` crate for its own config.

### Security notes

- The redirect config file is local-only, not committed to git
- Redirecting to a non-localhost address is supported (for LAN play) but the user is responsible for network security in that case
- The DLL does not modify game save data, progression, or anti-cheat state
