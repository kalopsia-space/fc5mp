#include "../../src/client/sha256.hpp"
#include <cassert>
#include <string>
#include <cstdlib>
using fc5mp::client::sha256_hex; using fc5mp::client::sha256_file_hex;
int main(){
  // NIST test vector: "abc"
  assert(sha256_hex((const uint8_t*)"abc",3)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  // empty string
  assert(sha256_hex((const uint8_t*)"",0)=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  // the real FC_m64.dll must hash to the recorded supported build
  if(const char* dll=std::getenv("FC5_DLL")){
    std::string h=sha256_file_hex(dll);
    if(!h.empty())
      assert(h=="00833fae4d5d70213158a146ca98439b28a8e962b934885ecccd906204880af2");
  }
  return 0;
}
