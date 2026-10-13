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

  // 3) zero-length datagram (logged by sendto/recvfrom like any other)
  CHECK(sendto(a, "", 0, 0, (sockaddr*)&addr, sizeof addr) == 0);
  flen = sizeof from;
  CHECK(recvfrom(b, rb, sizeof rb, 0, (sockaddr*)&from, &flen) == 0);

  // 4) overlapped: send is logged (encoded before the call); recv only marked
  { char px[] = "xyz"; WSABUF ob{3, px}; DWORD osent = 0, oflags = 0;
    WSAOVERLAPPED ov{}; ov.hEvent = WSACreateEvent();
    int r = WSASendTo(a, &ob, 1, nullptr, 0, (sockaddr*)&addr, sizeof addr, &ov, nullptr);
    CHECK(r == 0 || WSAGetLastError() == WSA_IO_PENDING);
    CHECK(WSAGetOverlappedResult(a, &ov, &osent, TRUE, &oflags) && osent == 3);
    WSACloseEvent(ov.hEvent);
    char ro[16]; WSABUF rob{16, ro}; DWORD ogot = 0; oflags = 0;
    WSAOVERLAPPED rov{}; rov.hEvent = WSACreateEvent(); flen = sizeof from;
    r = WSARecvFrom(b, &rob, 1, nullptr, &oflags, (sockaddr*)&from, &flen, &rov, nullptr);
    CHECK(r == 0 || WSAGetLastError() == WSA_IO_PENDING);
    CHECK(WSAGetOverlappedResult(b, &rov, &ogot, TRUE, &oflags) && ogot == 3);
    WSACloseEvent(rov.hEvent);
  }

  // 5) last-error preserved across a failing call (non-blocking recv, nothing queued)
  u_long nb = 1; ioctlsocket(b, FIONBIO, &nb);
  CHECK(recvfrom(b, rb, sizeof rb, 0, nullptr, nullptr) == SOCKET_ERROR);
  CHECK(WSAGetLastError() == WSAEWOULDBLOCK);

  closesocket(a); closesocket(b); WSACleanup();

  auto frames = read_frames(cap.c_str());
  std::vector<fc5mp::cap::UdpRecord> sends, recvs; int seen = 0, failed = 0, pending = 0;
  for(auto& f : frames){
    if(f.kind == fc5mp::cap::kind::UDP_SEND || f.kind == fc5mp::cap::kind::UDP_RECV){
      auto r = fc5mp::cap::decode_udp(f.payload.data(), f.payload.size());
      CHECK(r.has_value());
      if(r) (f.kind == fc5mp::cap::kind::UDP_SEND ? sends : recvs).push_back(*r);
    }
    if(f.kind == fc5mp::cap::kind::API_SEEN) seen++;
    if(f.kind == fc5mp::cap::kind::HOOK_FAILED) failed++;
    if(f.kind == fc5mp::cap::kind::RECV_PENDING){
      pending++;
      CHECK(f.payload.size() == 12);
      uint32_t n = 0; if(f.payload.size() == 12) std::memcpy(&n, &f.payload[8], 4);
      CHECK(n == (uint32_t)pending);           // running count
    }
  }
  std::printf("frames=%zu sends=%zu recvs=%zu api_seen=%d hook_failed=%d pending=%d\n",
              frames.size(), sends.size(), recvs.size(), seen, failed, pending);
  CHECK(failed == 0);
  CHECK(sends.size() == 4);          // exactly once each, incl. 0-byte + overlapped
  CHECK(recvs.size() == 3);          // MSG_PEEK and the overlapped recv are not logged
  CHECK(pending == 1);               // ...the overlapped recv leaves a marker instead
  if(sends.size() == 4){
    CHECK(sends[2].payload.empty() && sends[2].orig_len == 0);
    CHECK(sends[3].payload == std::vector<uint8_t>({'x','y','z'}));
    CHECK(sends[0].payload == std::vector<uint8_t>({'h','e','l','l','o'}));
    CHECK(sends[1].payload == std::vector<uint8_t>({'a','b','c','d','e','f','g'}));
    CHECK(sends[0].peer.family == 2 && sends[0].peer.addr_len == 4 && sends[0].peer.addr[0] == 127);
    CHECK(std::memcmp(sends[0].peer.port, &addr.sin_port, 2) == 0);
  }
  if(recvs.size() == 3){
    CHECK(recvs[2].payload.empty() && recvs[2].orig_len == 0);
    CHECK(recvs[0].payload == std::vector<uint8_t>({'h','e','l','l','o'}));
    CHECK(recvs[1].orig_len == 7);
    CHECK(recvs[1].payload == std::vector<uint8_t>({'a','b','c','d','e','f','g'}));
    CHECK(recvs[0].peer.addr[0] == 127);
  }
  DeleteFileA(cap.c_str());
  std::printf(fails ? "FAILED (%d)\n" : "OK\n", fails);
  return fails ? 1 : 0;
}
