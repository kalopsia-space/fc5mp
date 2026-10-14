#include "redact.hpp"
#include "udp_frame.hpp"
#include <algorithm>
#include <cstring>
namespace fc5mp::cap {
namespace {
uint8_t lower(uint8_t c){ return (c >= 'A' && c <= 'Z') ? (uint8_t)(c + 32) : c; }
bool match_ci(const uint8_t* p, size_t n, size_t at, const char* lit){
  size_t l = std::strlen(lit);
  if(at + l > n) return false;
  for(size_t i = 0; i < l; i++) if(lower(p[at + i]) != (uint8_t)lit[i]) return false;
  return true;
}
bool match(const uint8_t* p, size_t n, size_t at, const char* lit){
  size_t l = std::strlen(lit);
  return at + l <= n && std::memcmp(p + at, lit, l) == 0;
}
bool space(uint8_t c){ return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// `Authorization:` value → '*' up to CR/LF or end of buffer.
size_t mask_header(uint8_t* p, size_t n, size_t at){
  size_t i = at + std::strlen("authorization:"), m = 0;
  while(i < n && (p[i] == ' ' || p[i] == '\t')) i++;
  for(; i < n && p[i] != '\r' && p[i] != '\n'; i++){ p[i] = '*'; m++; }
  return m;
}
// `"key" : "value"` → value chars '*' up to the unescaped closing quote or end.
size_t mask_json(uint8_t* p, size_t n, size_t i){
  while(i < n && space(p[i])) i++;
  if(i >= n || p[i] != ':') return 0;
  i++;
  while(i < n && space(p[i])) i++;
  if(i >= n || p[i] != '"') return 0;
  size_t m = 0;
  for(i++; i < n && p[i] != '"'; i++){
    bool esc = p[i] == '\\';
    p[i] = '*'; m++;
    if(esc && i + 1 < n){ p[++i] = '*'; m++; }
  }
  return m;
}
const char* const kJsonKeys[] = {"\"ticket\"", "\"sessionKey\"", "\"rememberMeTicket\"",
                                 "\"password\"", "\"twoFactorAuthenticationTicket\""};
}

size_t redact(uint8_t* p, size_t n){
  if(!p) return 0;
  size_t masked = 0;
  for(size_t i = 0; i < n; i++){
    if(match_ci(p, n, i, "authorization:")){ masked += mask_header(p, n, i); continue; }
    if(p[i] != '"') continue;
    for(const char* k : kJsonKeys)
      if(match(p, n, i, k)){ masked += mask_json(p, n, i + std::strlen(k)); break; }
  }
  return masked;
}

std::vector<uint8_t> encode_tls(uint64_t ssl, const uint8_t* p, size_t n){
  size_t keep = p ? std::min(n, kMaxPayload) : 0;
  std::vector<uint8_t> out(8 + keep);
  std::memcpy(out.data(), &ssl, 8);
  if(keep){ std::memcpy(out.data() + 8, p, keep); redact(out.data() + 8, keep); }
  return out;
}
}
