#include "../../src/client/overlapped.hpp"
#include <cassert>
#include <cstdint>
#include <vector>
using namespace fc5mp::net;

static PendingRecv mk(uint64_t sock){
  PendingRecv e; e.sock = sock; e.from = nullptr; e.fromlen = nullptr; e.seq = 0;
  return e;
}
int main(){
  PendingRecvs reg;
  int k1 = 0, k2 = 0;                         // distinct OVERLAPPED* stand-ins
  // record then take returns it; a second take is empty
  reg.record(&k1, mk(0x11));
  assert(reg.size() == 1);
  auto a = reg.take(&k1);
  assert(a && a->sock == 0x11);
  assert(!reg.take(&k1));                      // removed
  assert(reg.size() == 0);
  // unknown key -> empty
  assert(!reg.take(&k2));
  // record same key twice -> take returns the second (overwrite, no growth)
  reg.record(&k1, mk(0xAA));
  reg.record(&k1, mk(0xBB));
  assert(reg.size() == 1);
  auto b = reg.take(&k1);
  assert(b && b->sock == 0xBB);
  // bounded: fill past kMax, size caps and the oldest is evicted
  std::vector<int> keys(PendingRecvs::kMax + 10);
  for(size_t i = 0; i < keys.size(); i++) reg.record(&keys[i], mk(i));
  assert(reg.size() == PendingRecvs::kMax);
  assert(!reg.take(&keys[0]));                 // oldest evicted
  assert(reg.take(&keys[keys.size() - 1]));    // newest still present
  return 0;
}
