#pragma once
namespace fc5mp::tls {
// Hooked OpenSSL entry points; the id is the payload of HOOK_FAILED.
enum Api : unsigned char { API_SSL_READ = 4, API_SSL_WRITE = 5, API_SSL_CONNECT = 9 };
// Windows: create MinHook detours on the game's statically linked
// SSL_read / SSL_write / SSL_connect, given their resolved addresses.
// ssl_read and ssl_write are captured (observe-only); ssl_connect is
// intercepted only when redirect mode is active (skip TLS handshake for
// redirected sockets so the server receives plain HTTP). Any address may
// be null; the corresponding hook is skipped and HOOK_FAILED is logged.
// The caller enables them via hook::enable_all. No-op on non-Windows.
void install_at(void* ssl_read, void* ssl_write, void* ssl_connect);
}
