#pragma once
#include <cstdint>
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

// Phase 2: REST redirect support
// Server IP in network byte order.
uint32_t server_ip_nbo();
// Server IP as a dotted-quad string (thread-local buffer).
const char* server_ip_str();
// REST port in host byte order.
uint16_t rest_port();
// True if hostname should be redirected (*.ubi.com, *.ubisoft.com).
bool is_redirect_hostname(const char* name);
// Mark a socket as REST-redirected (called from connect hook).
void mark_redirected(uint64_t sock);
// True if socket was marked as REST-redirected.
bool is_redirected(uint64_t sock);
// Extract socket fd from an OpenSSL 1.0.2 SSL* context.
// Returns -1 if the context or its BIO is null.
int ssl_get_fd(void* ssl);
}
