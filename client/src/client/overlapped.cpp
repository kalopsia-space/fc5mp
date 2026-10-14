#include "overlapped.hpp"
namespace fc5mp::net {
void PendingRecvs::record(const void* ov, PendingRecv e){
  std::lock_guard<std::mutex> g(m_);
  e.seq = next_seq_++;
  map_[ov] = std::move(e);                 // overwrite reuses the slot, no growth
  if(map_.size() > kMax){
    // evict the oldest (smallest seq); rare, only if reaps are missed
    auto oldest = map_.begin();
    for(auto it = map_.begin(); it != map_.end(); ++it)
      if(it->second.seq < oldest->second.seq) oldest = it;
    map_.erase(oldest);
  }
}
std::optional<PendingRecv> PendingRecvs::take(const void* ov){
  std::lock_guard<std::mutex> g(m_);
  auto it = map_.find(ov);
  if(it == map_.end()) return std::nullopt;
  PendingRecv e = std::move(it->second);
  map_.erase(it);
  return e;
}
size_t PendingRecvs::size() const {
  std::lock_guard<std::mutex> g(m_);
  return map_.size();
}
}
