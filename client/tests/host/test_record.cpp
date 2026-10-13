#include "../../src/capture/record.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
using namespace fc5mp::cap;
int main(){
  const char* path="/tmp/fc5mp_test_capture.bin"; std::remove(path);
  { Writer w(path); assert(w.ok());
    const uint8_t msg[]={'G','E','T'}; w.write(1,msg,3); }
  std::ifstream f(path,std::ios::binary);
  std::vector<uint8_t> v((std::istreambuf_iterator<char>(f)),{});
  assert(v.size()==8+1+4+3);
  assert(v[8]==1);
  uint32_t len; std::memcpy(&len,&v[9],4); assert(len==3);
  assert(v[13]=='G'&&v[14]=='E'&&v[15]=='T');
  Writer bad("/nonexistent-dir-xyz/cap.bin"); assert(!bad.ok());
  bad.write(1,(const uint8_t*)"x",1);          // must not throw
  std::remove(path);
  return 0;
}
