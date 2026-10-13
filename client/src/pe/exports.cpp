#include "exports.hpp"
#include <cstring>
namespace {
template<class T> bool rd(const uint8_t* d, size_t sz, size_t off, T& out){
  if(off + sizeof(T) > sz) return false; std::memcpy(&out, d+off, sizeof(T)); return true;
}
bool rva_to_off(const uint8_t* d, size_t sz, uint32_t rva, uint32_t secCount,
                size_t secTableOff, uint32_t& off){
  for(uint32_t i=0;i<secCount;i++){
    size_t s = secTableOff + i*40;
    uint32_t vaddr, vsize, praw, sraw;
    if(!rd(d,sz,s+12,vaddr)||!rd(d,sz,s+8,vsize)||!rd(d,sz,s+20,praw)||!rd(d,sz,s+16,sraw)) return false;
    uint32_t span = vsize>sraw?vsize:sraw;
    if(rva>=vaddr && rva<vaddr+span){ off = praw + (rva - vaddr); return true; }
  }
  return false;
}
}
namespace fc5mp::pe {
std::vector<std::string> export_names(const uint8_t* d, size_t sz){
  std::vector<std::string> out;
  uint32_t e_lfanew; if(!rd(d,sz,0x3C,e_lfanew)) return out;
  uint32_t sig; if(!rd(d,sz,e_lfanew,sig) || sig!=0x00004550) return out;
  size_t fileHdr = e_lfanew + 4;
  uint16_t nsec, optSize; if(!rd(d,sz,fileHdr+2,nsec)||!rd(d,sz,fileHdr+16,optSize)) return out;
  size_t optHdr = fileHdr + 20;
  uint16_t magic; if(!rd(d,sz,optHdr,magic)) return out;
  size_t dataDirs = optHdr + (magic==0x20b ? 112 : 96);
  uint32_t expRva, expSize; if(!rd(d,sz,dataDirs,expRva)||!rd(d,sz,dataDirs+4,expSize)) return out;
  if(!expRva) return out;
  size_t secTable = optHdr + optSize;
  uint32_t expOff; if(!rva_to_off(d,sz,expRva,nsec,secTable,expOff)) return out;
  uint32_t nNames, namePtrRva;
  if(!rd(d,sz,expOff+24,nNames)||!rd(d,sz,expOff+32,namePtrRva)) return out;
  uint32_t namePtrOff; if(!rva_to_off(d,sz,namePtrRva,nsec,secTable,namePtrOff)) return out;
  for(uint32_t i=0;i<nNames;i++){
    uint32_t strRva; if(!rd(d,sz,namePtrOff+i*4,strRva)) break;
    uint32_t strOff; if(!rva_to_off(d,sz,strRva,nsec,secTable,strOff)) break;
    std::string s; while(strOff<sz && d[strOff]) s.push_back((char)d[strOff++]);
    out.push_back(std::move(s));
  }
  return out;
}
}
