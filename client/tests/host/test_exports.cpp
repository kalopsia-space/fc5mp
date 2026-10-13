#include "../../src/pe/exports.hpp"
#include <cassert>
#include <vector>
#include <string>
#include <fstream>
static std::vector<uint8_t> slurp(const char* p){
  std::ifstream f(p, std::ios::binary); return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),{});
}
int main(int argc, char** argv){
  assert(argc==2);
  auto buf = slurp(argv[1]); assert(!buf.empty());
  auto names = fc5mp::pe::export_names(buf.data(), buf.size());
  assert(names.size()==16);
  bool hasVQV=false; for(auto&n:names) if(n=="VerQueryValueW") hasVQV=true;
  assert(hasVQV);
  return 0;
}
