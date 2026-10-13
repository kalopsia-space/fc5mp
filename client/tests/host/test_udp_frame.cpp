#include "../../src/capture/udp_frame.hpp"
#include "../../src/capture/kinds.hpp"
#include <cassert>
#include <cstring>
#include <vector>
using namespace fc5mp::cap;

static std::vector<uint8_t> sin4(uint8_t a,uint8_t b,uint8_t c,uint8_t d,uint16_t port){
  std::vector<uint8_t> s(16,0);
  s[0]=2; s[1]=0;                               // AF_INET, LE u16
  s[2]=(uint8_t)(port>>8); s[3]=(uint8_t)port;  // network order
  s[4]=a; s[5]=b; s[6]=c; s[7]=d;
  return s;
}

int main(){
  // kinds keep their P1 values
  static_assert(kind::RDV_CREATESESSION==10 && kind::COOP_LEASH==11 && kind::SPAWN_PLAYER==12);
  static_assert(kind::MODULE_TIMEOUT==250 && kind::SIG_MISSING==251);

  // IPv4 round-trip
  { auto sa = sin4(192,168,1,7,3074);
    Peer p = peer_from_sockaddr(sa.data(), sa.size());
    assert(p.family==2 && p.addr_len==4 && p.addr[0]==192 && p.addr[3]==7);
    assert(p.port[0]==(3074>>8) && p.port[1]==(3074&0xff));
    const uint8_t msg[]={1,2,3};
    Span sp{msg,3};
    auto rec = encode_udp(0x1234, p, &sp, 1, 3);
    auto d = decode_udp(rec.data(), rec.size());
    assert(d && d->sock==0x1234 && d->peer.family==2 && d->peer.addr_len==4);
    assert(std::memcmp(d->peer.addr, p.addr, 4)==0 && d->peer.port[0]==p.port[0] && d->peer.port[1]==p.port[1]);
    assert(d->orig_len==3 && d->payload==std::vector<uint8_t>({1,2,3}));
  }
  // IPv6 round-trip (Windows AF_INET6 = 23; addr at offset 8)
  { std::vector<uint8_t> s(28,0); s[0]=23; s[2]=0x0c; s[3]=0x02;
    for(int i=0;i<16;i++) s[8+i]=(uint8_t)(0xa0+i);
    Peer p = peer_from_sockaddr(s.data(), s.size());
    assert(p.family==23 && p.addr_len==16 && p.addr[0]==0xa0 && p.addr[15]==0xaf);
    auto rec = encode_udp(9, p, nullptr, 0, 0);
    auto d = decode_udp(rec.data(), rec.size());
    assert(d && d->peer.addr_len==16 && d->peer.addr[15]==0xaf && d->payload.empty());
  }
  // peer_null: connected socket, no address
  { Peer p = peer_from_sockaddr(nullptr, 16); assert(p.family==0 && p.addr_len==0); }
  // peer_short: salen too small for the family's address → no address bytes, no OOB
  { auto sa = sin4(1,2,3,4,80);
    Peer p = peer_from_sockaddr(sa.data(), 6);
    assert(p.family==2 && p.addr_len==0);
    Peer q = peer_from_sockaddr(sa.data(), 1);
    assert(q.family==0 && q.addr_len==0);
    std::vector<uint8_t> s6(20,0); s6[0]=23;
    Peer r = peer_from_sockaddr(s6.data(), s6.size());
    assert(r.family==23 && r.addr_len==0);
  }
  // peer_unknown_family: keep family, no address
  { std::vector<uint8_t> s(16,0); s[0]=99; s[2]=1; s[3]=2;
    Peer p = peer_from_sockaddr(s.data(), s.size());
    assert(p.family==99 && p.addr_len==0 && p.port[0]==1 && p.port[1]==2);
  }
  // gather_partial_multi: only `total` bytes, across buffers, in order
  { const uint8_t a[]={'a','b','c'}, b[]={'d','e','f','g'};
    Span sp[2]={{a,3},{b,4}};
    auto rec = encode_udp(1, Peer{}, sp, 2, 5);
    auto d = decode_udp(rec.data(), rec.size());
    assert(d && d->orig_len==5 && d->payload==std::vector<uint8_t>({'a','b','c','d','e'}));
    // total larger than the buffers hold → stored = what the buffers hold
    auto rec2 = encode_udp(1, Peer{}, sp, 2, 100);
    auto d2 = decode_udp(rec2.data(), rec2.size());
    assert(d2 && d2->orig_len==100 && d2->payload.size()==7);
  }
  // truncates_to_cap
  { std::vector<uint8_t> big(kMaxPayload+10, 0x5a);
    Span sp{big.data(), big.size()};
    auto rec = encode_udp(1, Peer{}, &sp, 1, big.size());
    auto d = decode_udp(rec.data(), rec.size());
    assert(d && d->orig_len==big.size() && d->payload.size()==kMaxPayload);
  }
  // decode rejects truncated headers
  { auto sa = sin4(1,2,3,4,80); Peer p = peer_from_sockaddr(sa.data(), sa.size());
    auto rec = encode_udp(1, p, nullptr, 0, 0);
    for(size_t cut=0; cut<rec.size(); cut++) assert(!decode_udp(rec.data(), cut));
    std::vector<uint8_t> bad(rec); bad[12]=17;   // addr_len > 16
    assert(!decode_udp(bad.data(), bad.size()));
  }
  return 0;
}
