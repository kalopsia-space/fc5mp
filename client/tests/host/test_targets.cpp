#include "../../src/client/targets.hpp"
#include "../../src/sig/signature.hpp"
#include <cassert>
#include <fstream>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <string>
// PE file offset -> RVA via the section table (0 if not inside a section).
static size_t off_to_rva(const std::vector<uint8_t>& d, size_t off){
  uint32_t pe; std::memcpy(&pe, d.data()+0x3C, 4);
  uint16_t nsec, optsz; std::memcpy(&nsec, d.data()+pe+6, 2); std::memcpy(&optsz, d.data()+pe+20, 2);
  size_t sh = pe + 24 + optsz;
  for(uint16_t i=0;i<nsec;i++, sh+=40){
    uint32_t va, raw_sz, raw_ptr;
    std::memcpy(&va, d.data()+sh+12, 4); std::memcpy(&raw_sz, d.data()+sh+16, 4); std::memcpy(&raw_ptr, d.data()+sh+20, 4);
    if(off >= raw_ptr && off < raw_ptr + raw_sz) return off - raw_ptr + va;
  }
  return 0;
}
static size_t index_of(const char* name){
  const auto& t = fc5mp::targets::table();
  for(size_t i=0;i<t.size();i++) if(std::string(t[i].name)==name) return i;
  assert(!"target missing"); return 0;
}
static std::vector<uint8_t> slurp(const char* p){ std::ifstream f(p,std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),{}); }
int main(){
  const auto& t = fc5mp::targets::table();
  assert(!t.empty());
  for(const auto& e : t){
    auto s = fc5mp::sig::parse(e.sig);
    assert(s.has_value());                 // every signature parses
    assert(e.kind>=10 && e.kind<=15);
  }
  // missing_indices: diagnostic helper for the live run. With all targets
  // resolved it is empty; with a hole it names the absent table index.
  // the static-OpenSSL TLS entry points are in the table with their marker kinds
  assert(t[index_of("ssl_read")].kind == 13);
  assert(t[index_of("ssl_write")].kind == 14);
  { std::vector<std::pair<size_t,size_t>> all;
    for(size_t i=0;i<t.size();i++) all.emplace_back(i,0);
    assert(fc5mp::targets::missing_indices(all).empty()); }
  { auto miss = fc5mp::targets::missing_indices({{0,0},{2,0}});  // index 1 absent
    assert(miss.size()==t.size()-2 && miss[0]==1); }
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
      // SSL_read / SSL_write / SSL_connect land on the RVAs identified by RE
      for(const auto& pr : r){
        if(pr.first==index_of("ssl_read"))    assert(off_to_rva(d, pr.second)==0x3dd0560);
        if(pr.first==index_of("ssl_write"))   assert(off_to_rva(d, pr.second)==0x3dd09c0);
        if(pr.first==index_of("ssl_connect")) assert(off_to_rva(d, pr.second)==0x3dcf330);
      }
    }
  }
  return 0;
}
