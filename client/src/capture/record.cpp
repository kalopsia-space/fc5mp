#include "record.hpp"
#include <chrono>
#include <cstring>
#include <vector>
namespace fc5mp::cap {
Writer::Writer(std::string path){ f_ = std::fopen(path.c_str(), "ab"); }
Writer::~Writer(){ if(f_) std::fclose(f_); }
void Writer::write(uint8_t kind, const uint8_t* data, size_t n) noexcept {
  if(!f_) return;                              // fail-closed
  uint64_t ts = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  uint32_t len = (uint32_t)n;
  // build the whole frame, then emit it atomically under the lock, so
  // concurrent callers cannot interleave the four fields.
  std::vector<uint8_t> frame; frame.reserve(13 + n);
  frame.insert(frame.end(), (uint8_t*)&ts, (uint8_t*)&ts + 8);
  frame.push_back(kind);
  frame.insert(frame.end(), (uint8_t*)&len, (uint8_t*)&len + 4);
  if(n) frame.insert(frame.end(), data, data + n);
  {
    std::lock_guard<std::mutex> g(mu_);
    std::fwrite(frame.data(), 1, frame.size(), f_);
    std::fflush(f_);
  }
}
}
