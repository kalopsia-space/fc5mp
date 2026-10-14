#pragma once
struct sockaddr;
namespace fc5mp::redirect {
// Load redirect config from $FC5MP_REDIRECT or ~/.fc5mp/redirect.toml.
// Returns true if redirect is now active. Safe to call once at boot.
bool load();
// If redirect is active and dest targets a known Ubisoft PRUDP endpoint
// (port 6570), rewrites dest in-place to point at our server. Returns true
// if rewritten.
bool maybe_rewrite_dest(sockaddr* dest, int destlen);
// Whether redirect mode is active (config was loaded successfully).
bool active();
}
