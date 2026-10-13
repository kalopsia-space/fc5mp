#include "udp_frame.hpp"
#include <algorithm>
#include <cstring>
namespace fc5mp::cap {
Peer peer_from_sockaddr(const uint8_t* sa, size_t salen){
  Peer p;
  if(!sa || salen < 4) return p;
  p.family = (uint16_t)(sa[0] | (sa[1] << 8));
  p.port[0] = sa[2]; p.port[1] = sa[3];
  size_t off = 0, len = 0;
  if(p.family == 2)       { off = 4; len = 4;  }   // sockaddr_in
  else if(p.family == 23) { off = 8; len = 16; }   // sockaddr_in6 (after flowinfo)
  if(len && salen >= off + len){
    std::memcpy(p.addr, sa + off, len);
    p.addr_len = (uint8_t)len;
  }
  return p;
}
std::vector<uint8_t> encode_udp(uint64_t sock, const Peer& peer,
                                const Span* bufs, size_t nbufs, size_t total){
  size_t want = std::min(total, kMaxPayload);
  uint32_t orig = (uint32_t)std::min<size_t>(total, UINT32_MAX);
  std::vector<uint8_t> out; out.reserve(8 + 2 + 2 + 1 + peer.addr_len + 4 + want);
  out.insert(out.end(), (const uint8_t*)&sock, (const uint8_t*)&sock + 8);
  out.push_back((uint8_t)peer.family); out.push_back((uint8_t)(peer.family >> 8));
  out.push_back(peer.port[0]); out.push_back(peer.port[1]);
  out.push_back(peer.addr_len);
  out.insert(out.end(), peer.addr, peer.addr + peer.addr_len);
  out.insert(out.end(), (const uint8_t*)&orig, (const uint8_t*)&orig + 4);
  for(size_t i = 0; i < nbufs && want; i++){
    size_t take = std::min(want, bufs[i].n);
    if(take && bufs[i].p) out.insert(out.end(), bufs[i].p, bufs[i].p + take);
    want -= take;
  }
  return out;
}
std::optional<UdpRecord> decode_udp(const uint8_t* p, size_t n){
  if(n < 13) return std::nullopt;
  UdpRecord r;
  std::memcpy(&r.sock, p, 8);
  r.peer.family = (uint16_t)(p[8] | (p[9] << 8));
  r.peer.port[0] = p[10]; r.peer.port[1] = p[11];
  r.peer.addr_len = p[12];
  if(r.peer.addr_len > 16 || n < 13 + (size_t)r.peer.addr_len + 4) return std::nullopt;
  std::memcpy(r.peer.addr, p + 13, r.peer.addr_len);
  size_t off = 13 + r.peer.addr_len;
  std::memcpy(&r.orig_len, p + off, 4);
  r.payload.assign(p + off + 4, p + n);
  return r;
}
}
