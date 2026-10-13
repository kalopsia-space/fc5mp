#pragma once
#include <cstdint>
#include <cstddef>
#include <optional>
#include <vector>
namespace fc5mp::cap {
// Payload of a UDP_SEND / UDP_RECV frame, little-endian:
//   [u64 socket][u16 family][2 bytes port, network order as on the wire]
//   [u8 addr_len][addr_len bytes][u32 orig_len][stored payload]
// orig_len is the datagram's real size; the stored payload is capped at
// kMaxPayload so one oversized buffer can't bloat the capture.
constexpr size_t kMaxPayload = 65536;
struct Peer { uint16_t family=0; uint8_t port[2]={}; uint8_t addr_len=0; uint8_t addr[16]={}; };
struct Span { const uint8_t* p; size_t n; };
struct UdpRecord { uint64_t sock; Peer peer; uint32_t orig_len; std::vector<uint8_t> payload; };
// Parse a raw Windows sockaddr (AF_INET=2, AF_INET6=23). Null-safe; never reads
// past salen. Unknown family or short salen → no address bytes.
Peer peer_from_sockaddr(const uint8_t* sa, size_t salen);
// Gather the first `total` bytes of bufs (in order) into one record.
std::vector<uint8_t> encode_udp(uint64_t sock, const Peer& peer,
                                const Span* bufs, size_t nbufs, size_t total);
std::optional<UdpRecord> decode_udp(const uint8_t* p, size_t n);
}
