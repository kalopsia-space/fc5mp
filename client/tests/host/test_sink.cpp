#include "../../src/client/sink.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>
int main(){
  const char* path="/tmp/fc5mp_test_sink.bin"; std::remove(path);
  setenv("FC5MP_CAPTURE", path, 1);
  auto& a = fc5mp::client::sink();
  auto& b = fc5mp::client::sink();
  assert(&a == &b);                 // one process-wide writer
  assert(a.ok());
  const uint8_t x[]={7,8};
  a.write(20, x, 2);
  b.write(21, x, 1);
  std::ifstream f(path,std::ios::binary);
  std::vector<uint8_t> v((std::istreambuf_iterator<char>(f)),{});
  assert(v.size()==(13+2)+(13+1));  // both frames, same file
  assert(v[8]==20 && v[15+8]==21);
  std::remove(path);
  return 0;
}
