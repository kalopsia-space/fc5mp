// Windows integration test (run under Wine): an overlapped WSARecvFrom reaped
// via GetOverlappedResult (kernel32) — the path FC_m64 uses — must now be
// captured as UDP_RECV with the right bytes and peer; a file-I/O OVERLAPPED
// reaped the same way must NOT be logged; an incomplete poll logs nothing.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cstdio>
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
  std::vector<Frame> out; FILE* f = std::fopen(path, "rb"); if(!f) return out;
  std::vector<uint8_t> v; uint8_t b[4096]; size_t n;
  while((n = std::fread(b,1,sizeof b,f))>0) v.insert(v.end(), b, b+n);
  std::fclose(f);
  for(size_t off=0; off+13<=v.size();){
    uint32_t len; std::memcpy(&len,&v[off+9],4);
    if(off+13+len>v.size()) break;
    out.push_back({v[off+8], std::vector<uint8_t>(v.begin()+off+13, v.begin()+off+13+len)});
    off += 13+len;
  }
  return out;
}

int main(){
  char tmp[MAX_PATH]; GetTempPathA(MAX_PATH, tmp);
  std::string cap = std::string(tmp) + "fc5mp_test_overlapped.bin";
  DeleteFileA(cap.c_str());
  _putenv_s("FC5MP_CAPTURE", cap.c_str());

  WSADATA wd; CHECK(WSAStartup(MAKEWORD(2,2), &wd) == 0);
  CHECK(fc5mp::hook::init());
  fc5mp::net::install();
  CHECK(fc5mp::hook::enable_all());

  SOCKET a = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  SOCKET b = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr_in addr{}; addr.sin_family=AF_INET; addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  CHECK(bind(b,(sockaddr*)&addr,sizeof addr)==0);
  int alen=sizeof addr; getsockname(b,(sockaddr*)&addr,&alen);

  // overlapped recv with an event, into 2 buffers (partial fill: 4+16 for 7 bytes)
  char r1[4], r2[16]; WSABUF rb[2]={{4,r1},{16,r2}};
  DWORD got=0, flags=0; sockaddr_in from{}; int flen=sizeof from;
  WSAOVERLAPPED ov{}; ov.hEvent=WSACreateEvent();
  int rr=WSARecvFrom(b, rb, 2, &got, &flags, (sockaddr*)&from, &flen, &ov, nullptr);
  CHECK(rr==0 || WSAGetLastError()==WSA_IO_PENDING);
  // poll before the datagram arrives: incomplete -> FALSE, last-error ERROR_IO_INCOMPLETE,
  // and the detour (ok==FALSE) must log nothing and keep the entry + error intact
  { DWORD x0=0; BOOL e0=GetOverlappedResult((HANDLE)b,(LPOVERLAPPED)&ov,&x0,FALSE);
    CHECK(!e0 && GetLastError()==ERROR_IO_INCOMPLETE); }
  // send after posting the recv
  CHECK(sendto(a,"abcdefg",7,0,(sockaddr*)&addr,sizeof addr)==7);
  // reap via kernel32 GetOverlappedResult (what the game does) — this is hooked
  DWORD xfer=0;
  BOOL ok=GetOverlappedResult((HANDLE)b, (LPOVERLAPPED)&ov, &xfer, TRUE);
  CHECK(ok && xfer==7);
  WSACloseEvent(ov.hEvent);

  // a non-socket overlapped reaped via GetOverlappedResult must NOT be logged
  { char path[MAX_PATH]; std::snprintf(path,sizeof path,"%sfc5mp_ov_file.tmp",tmp);
    HANDLE fh=CreateFileA(path,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_FLAG_OVERLAPPED,nullptr);
    if(fh!=INVALID_HANDLE_VALUE){
      OVERLAPPED fov{}; fov.hEvent=CreateEventA(nullptr,TRUE,FALSE,nullptr);
      WriteFile(fh,"xyz",3,nullptr,&fov);
      DWORD fx=0; GetOverlappedResult(fh,&fov,&fx,TRUE);
      CloseHandle(fov.hEvent); CloseHandle(fh); DeleteFileA(path);
    }
  }

  closesocket(a); closesocket(b); WSACleanup();
  fc5mp::hook::uninit();

  auto frames=read_frames(cap.c_str());
  std::vector<fc5mp::cap::UdpRecord> recvs;
  for(auto& f: frames) if(f.kind==fc5mp::cap::kind::UDP_RECV){
    auto u=fc5mp::cap::decode_udp(f.payload.data(), f.payload.size());
    if(u) recvs.push_back(*u);
  }
  std::printf("recvs=%zu\n", recvs.size());
  CHECK(recvs.size()==1);                              // the overlapped datagram, captured once
  if(recvs.size()==1){
    CHECK(recvs[0].orig_len==7);
    CHECK(std::string(recvs[0].payload.begin(), recvs[0].payload.end())=="abcdefg");
    // peer from the filled sockaddr: AF_INET, 127.0.0.1
    const auto& pr = recvs[0].peer;
    CHECK(pr.family==2 && pr.addr_len==4);
    CHECK(pr.addr[0]==127 && pr.addr[1]==0 && pr.addr[2]==0 && pr.addr[3]==1);
  }
  std::printf("%s (%d failures)\n", fails?"FAILURES":"OK", fails);
  return fails?1:0;
}
