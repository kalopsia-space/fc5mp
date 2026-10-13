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

  // many records must each parse back intact (framing stays aligned)
  { const char* mp="/tmp/fc5mp_test_capture_multi.bin"; std::remove(mp);
    { Writer w(mp); assert(w.ok());
      for(int i=0;i<100;i++){ uint8_t p[4]={(uint8_t)i,1,2,3}; w.write((uint8_t)(10+(i%3)), p, 4); } }
    std::ifstream mf(mp,std::ios::binary);
    std::vector<uint8_t> mv((std::istreambuf_iterator<char>(mf)),{});
    size_t off=0, count=0;
    while(off+13<=mv.size()){
      uint32_t l; std::memcpy(&l,&mv[off+9],4);
      assert(off+13+l<=mv.size());            // frame fits
      assert(l==4);                            // our payload size
      off+=13+l; count++;
    }
    assert(off==mv.size());                    // no trailing garbage
    assert(count==100);                        // all frames present & aligned
    std::remove(mp);
  }
  return 0;
}
