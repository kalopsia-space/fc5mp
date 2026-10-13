#include "record.hpp"
#include <chrono>
namespace fc5mp::cap {
Writer::Writer(std::string path){ f_ = std::fopen(path.c_str(), "ab"); }
Writer::~Writer(){ if(f_) std::fclose(f_); }
void Writer::write(uint8_t kind, const uint8_t* data, size_t n) noexcept {
  if(!f_) return;                              // fail-closed
  uint64_t ts = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  uint32_t len = (uint32_t)n;
  std::fwrite(&ts,8,1,f_); std::fwrite(&kind,1,1,f_);
  std::fwrite(&len,4,1,f_); if(n) std::fwrite(data,1,n,f_);
  std::fflush(f_);
}
}
