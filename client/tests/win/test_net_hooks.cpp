// Windows integration test (run under Wine): install the real ws2_32 detours,
// push loopback datagrams through all four hooked APIs, then parse the capture
// and check each datagram was logged exactly once with the right direction,
// payload and peer, and that the caller-visible last-error survived logging.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "capture/kinds.hpp"
#include "capture/udp_frame.hpp"
#include "client/net_hooks.hpp"
#include "client/sink.hpp"
#include "hookcore/hookcore.hpp"

static int fails = 0;
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } }while(0)

struct Frame { uint8_t kind; std::vector<uint8_t> payload; };
static std::vector<Frame> read_frames(const char* path){
  std::vector<Frame> out;
  FILE* f = std::fopen(path, "rb"); if(!f) return out;
  std::vector<uint8_t> v; uint8_t buf[4096]; size_t n;
  while((n = std::fread(buf, 1, sizeof buf, f)) > 0) v.insert(v.end(), buf, buf + n);
  std::fclose(f);
  for(size_t off = 0; off + 13 <= v.size();){
    uint32_t len; std::memcpy(&len, &v[off + 9], 4);
    if(off + 13 + len > v.size()) break;
    out.push_back({v[off + 8], std::vector<uint8_t>(v.begin() + off + 13, v.begin() + off + 13 + len)});
    off += 13 + len;
  }
  return out;
}

int main(){
  char tmp[MAX_PATH]; GetTempPathA(MAX_PATH, tmp);
  std::string cap = std::string(tmp) + "fc5mp_test_net_hooks.bin";
  DeleteFileA(cap.c_str());
  SetEnvironmentVariableA("FC5MP_CAPTURE", cap.c_str());
  _putenv_s("FC5MP_CAPTURE", cap.c_str());

  WSADATA wd; CHECK(WSAStartup(MAKEWORD(2,2), &wd) == 0);
  CHECK(fc5mp::hook::init());
  fc5mp::net::install();
  CHECK(fc5mp::hook::enable_all());

  SOCKET a = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  SOCKET b = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  CHECK(bind(b, (sockaddr*)&addr, sizeof addr) == 0);
  int alen = sizeof addr; getsockname(b, (sockaddr*)&addr, &alen);

  // 1) sendto / recvfrom
  CHECK(sendto(a, "hello", 5, 0, (sockaddr*)&addr, sizeof addr) == 5);
  char rb[64]; sockaddr_in from{}; int flen = sizeof from;
  CHECK(recvfrom(b, rb, sizeof rb, MSG_PEEK, (sockaddr*)&from, &flen) == 5);   // peek: not logged
  flen = sizeof from;
  CHECK(recvfrom(b, rb, sizeof rb, 0, (sockaddr*)&from, &flen) == 5);

  // 2) WSASendTo (2 buffers) / WSARecvFrom (2 buffers, partial fill)
  char p1[] = "abc", p2[] = "defg";
  WSABUF sb[2] = {{3, p1}, {4, p2}}; DWORD sent = 0;
  CHECK(WSASendTo(a, sb, 2, &sent, 0, (sockaddr*)&addr, sizeof addr, nullptr, nullptr) == 0 && sent == 7);
  char r1[4], r2[16]; WSABUF rbufs[2] = {{4, r1}, {16, r2}}; DWORD got = 0, fl = 0; flen = sizeof from;
  CHECK(WSARecvFrom(b, rbufs, 2, &got, &fl, (sockaddr*)&from, &flen, nullptr, nullptr) == 0 && got == 7);

  // 3) last-error preserved across a failing call (non-blocking recv, nothing queued)
  u_long nb = 1; ioctlsocket(b, FIONBIO, &nb);
  CHECK(recvfrom(b, rb, sizeof rb, 0, nullptr, nullptr) == SOCKET_ERROR);
  CHECK(WSAGetLastError() == WSAEWOULDBLOCK);

  closesocket(a); closesocket(b); WSACleanup();

  auto frames = read_frames(cap.c_str());
  std::vector<fc5mp::cap::UdpRecord> sends, recvs; int seen = 0, failed = 0;
  for(auto& f : frames){
    if(f.kind == fc5mp::cap::kind::UDP_SEND || f.kind == fc5mp::cap::kind::UDP_RECV){
      auto r = fc5mp::cap::decode_udp(f.payload.data(), f.payload.size());
      CHECK(r.has_value());
      if(r) (f.kind == fc5mp::cap::kind::UDP_SEND ? sends : recvs).push_back(*r);
    }
    if(f.kind == fc5mp::cap::kind::API_SEEN) seen++;
    if(f.kind == fc5mp::cap::kind::HOOK_FAILED) failed++;
  }
  std::printf("frames=%zu sends=%zu recvs=%zu api_seen=%d hook_failed=%d\n",
              frames.size(), sends.size(), recvs.size(), seen, failed);
  CHECK(failed == 0);
  CHECK(sends.size() == 2);          // exactly once each
  CHECK(recvs.size() == 2);          // the MSG_PEEK read is not logged
  if(sends.size() == 2){
    CHECK(sends[0].payload == std::vector<uint8_t>({'h','e','l','l','o'}));
    CHECK(sends[1].payload == std::vector<uint8_t>({'a','b','c','d','e','f','g'}));
    CHECK(sends[0].peer.family == 2 && sends[0].peer.addr_len == 4 && sends[0].peer.addr[0] == 127);
    CHECK(std::memcmp(sends[0].peer.port, &addr.sin_port, 2) == 0);
  }
  if(recvs.size() == 2){
    CHECK(recvs[0].payload == std::vector<uint8_t>({'h','e','l','l','o'}));
    CHECK(recvs[1].orig_len == 7);
    CHECK(recvs[1].payload == std::vector<uint8_t>({'a','b','c','d','e','f','g'}));
    CHECK(recvs[0].peer.addr[0] == 127);
  }
  DeleteFileA(cap.c_str());
  std::printf(fails ? "FAILED (%d)\n" : "OK\n", fails);
  return fails ? 1 : 0;
}
