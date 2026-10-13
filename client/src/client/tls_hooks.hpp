#pragma once
namespace fc5mp::tls {
// Hooked OpenSSL entry points; the id is the payload of HOOK_FAILED.
enum Api : unsigned char { API_SSL_READ = 4, API_SSL_WRITE = 5 };
// Windows: create observe-only MinHook detours on the game's statically linked
// SSL_read / SSL_write, given their resolved addresses. Each detour calls the
// original with untouched args, then logs the plaintext actually transferred
// (redacted) as TLS_READ / TLS_WRITE. Logs HOOK_FAILED(api) on failure.
// The caller enables them via hook::enable_all. No-op on non-Windows.
void install_at(void* ssl_read, void* ssl_write);
}
