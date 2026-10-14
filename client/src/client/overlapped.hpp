#pragma once
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>
namespace fc5mp::net {
// Recorded at WSARecvFrom time so the datagram can be logged once the game
// reaps the completion via GetOverlappedResult. The buffer and sockaddr
// pointers are the caller's; they stay valid until the overlapped op completes
// (i.e. until the reap we act on). We copy only the small WSABUF array, never
// the datagram bytes.
struct PendingRecv {
  uint64_t sock = 0;
  std::vector<std::pair<const uint8_t*, size_t>> bufs;
  const uint8_t* from = nullptr;   // sockaddr out-ptr, filled by completion; may be null
  const int* fromlen = nullptr;    // sockaddr length out-ptr; may be null
  uint64_t seq = 0;                // insertion order, for bounded eviction
};
// Thread-safe registry of in-flight overlapped recvs, keyed by the OVERLAPPED*
// the game passed. Bounded: past kMax live entries (completions reaped some way
// other than GetOverlappedResult, so never taken) the oldest is evicted, so
// memory can't grow without bound.
class PendingRecvs {
public:
  static constexpr size_t kMax = 512;
  void record(const void* ov, PendingRecv e);     // overwrite if ov already present
  std::optional<PendingRecv> take(const void* ov); // remove + return, or nullopt
  size_t size() const;
  // Lock-free "nothing in flight" hint. When true, a caller can skip take()
  // (and its mutex) entirely — the common case for GetOverlappedResult reaps of
  // non-socket I/O while no overlapped recv is outstanding. Never false-empty:
  // the counter is bumped before the map under the same lock.
  bool empty_hint() const { return count_.load(std::memory_order_acquire) == 0; }
private:
  mutable std::mutex m_;
  std::unordered_map<const void*, PendingRecv> map_;
  std::atomic<size_t> count_{0};
  uint64_t next_seq_ = 0;
};
}
