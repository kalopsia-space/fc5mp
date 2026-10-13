#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
namespace fc5mp::cap {
// Mask credentials in captured plaintext, in place, with '*' (same length, so
// HTTP Content-Length / framing stays valid):
//   - the value of any `Authorization:` header (case-insensitive name), to CR/LF
//   - JSON string values of "ticket", "sessionKey", "rememberMeTicket",
//     "password", "twoFactorAuthenticationTicket"; a value cut off by the end of
//     the buffer is masked to the end.
// Best-effort: a key split across two buffers, gzip bodies and XOR-masked wss
// client frames are NOT redacted, so captures must still be treated as secret.
// Returns the number of bytes masked.
size_t redact(uint8_t* p, size_t n);
// TLS_READ / TLS_WRITE payload: [u64 SSL*][redacted copy of the bytes, capped
// at kMaxPayload]. The caller's buffer is never modified.
std::vector<uint8_t> encode_tls(uint64_t ssl, const uint8_t* p, size_t n);
}
