#include "../../src/sig/signature.hpp"
#include <cassert>
#include <fstream>
#include <vector>
#include <cstdlib>
#include <cstring>
using namespace fc5mp::sig;
static std::vector<uint8_t> slurp(const char* p){ std::ifstream f(p,std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),{}); }
int main(){
  auto s = parse("DE AD ?? EF"); assert(s.has_value());
  assert(s->bytes.size()==4 && s->mask.size()==4 && s->mask[2]==false);
  assert(!parse("").has_value());
  assert(!parse("?? ??").has_value());
  std::vector<uint8_t> buf={0xDE,0xAD,0x01,0xEF, 0x00, 0xDE,0xAD,0x99,0xEF};
  assert(find_all(buf.data(),buf.size(),*s).size()==2);
  assert(!find_unique(buf.data(),buf.size(),*s).has_value());
  auto s2=parse("DE AD 01 EF");
  assert(find_unique(buf.data(),buf.size(),*s2).value()==0);
  if(const char* dll=std::getenv("FC5_DLL")){
    auto d=slurp(dll); if(!d.empty()){
      auto s3=parse("48 8D 15 DC 5E 65 FC");
      assert(find_unique(d.data(),d.size(),*s3).has_value());
    }
  }
  return 0;
}
