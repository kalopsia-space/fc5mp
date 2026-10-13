#include "../../src/client/targets.hpp"
#include "../../src/sig/signature.hpp"
#include <cassert>
#include <fstream>
#include <vector>
#include <cstdlib>
static std::vector<uint8_t> slurp(const char* p){ std::ifstream f(p,std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),{}); }
int main(){
  const auto& t = fc5mp::targets::table();
  assert(!t.empty());
  for(const auto& e : t){
    auto s = fc5mp::sig::parse(e.sig);
    assert(s.has_value());                 // every signature parses
    assert(e.kind>=10 && e.kind<=12);
  }
  // missing_indices: diagnostic helper for the live run. With all targets
  // resolved it is empty; with a hole it names the absent table index.
  assert(fc5mp::targets::missing_indices({{0,0},{1,0},{2,0}}).empty());
  { auto miss = fc5mp::targets::missing_indices({{0,0},{2,0}});  // index 1 absent
    assert(miss.size()==1 && miss[0]==1); }
  assert(fc5mp::targets::missing_indices({}).size()==t.size());  // none resolved

  // against the real DLL: every signature resolves exactly once
  if(const char* dll=std::getenv("FC5_DLL")){
    auto d=slurp(dll);
    if(!d.empty()){
      auto r = fc5mp::targets::resolve(d.data(), d.size());
      assert(r.size()==t.size());          // all resolved, each unique
      assert(fc5mp::targets::missing_indices(r).empty());  // nothing missing on-disk
      for(const auto& e: t){
        auto s=fc5mp::sig::parse(e.sig);
        assert(fc5mp::sig::find_unique(d.data(),d.size(),*s).has_value());
      }
    }
  }
  return 0;
}
