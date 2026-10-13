#include "signature.hpp"
#include <sstream>
#include <cstdlib>
namespace fc5mp::sig {
std::optional<Sig> parse(const std::string& pattern){
  std::istringstream is(pattern); std::string tok; Sig s; bool anyFixed=false;
  while(is>>tok){
    if(tok=="??"||tok=="?"){ s.bytes.push_back(0); s.mask.push_back(false); }
    else {
      if(tok.size()!=2) return std::nullopt;
      char* end=nullptr; long v=strtol(tok.c_str(),&end,16);
      if(end!=tok.c_str()+2||v<0||v>255) return std::nullopt;
      s.bytes.push_back((uint8_t)v); s.mask.push_back(true); anyFixed=true;
    }
  }
  if(s.bytes.empty()||!anyFixed) return std::nullopt;
  return s;
}
std::vector<size_t> find_all(const uint8_t* d, size_t n, const Sig& s){
  std::vector<size_t> out; size_t m=s.bytes.size();
  if(m==0||n<m) return out;
  for(size_t i=0;i+m<=n;i++){
    bool ok=true;
    for(size_t j=0;j<m;j++){ if(s.mask[j] && d[i+j]!=s.bytes[j]){ ok=false; break; } }
    if(ok) out.push_back(i);
  }
  return out;
}
std::optional<size_t> find_unique(const uint8_t* d, size_t n, const Sig& s){
  auto all=find_all(d,n,s); if(all.size()==1) return all[0]; return std::nullopt;
}
}
