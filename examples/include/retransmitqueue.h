#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>

namespace RDMA_EC {
class RetransmitQueue {
public:
  struct Entry {
    int64_t timestamp;
    std::atomic<bool> pending{false};
  };

  explicit RetransmitQueue(size_t capacity)
      : capacity_{capacity}, entries_{std::make_unique<Entry[]>(capacity)},
        pending_count_{0} {}

  void add(uint32_t id, std::chrono::milliseconds timestamp) {
    entries_[id].timestamp = timestamp.count();
    entries_[id].pending.store(true, std::memory_order_release);
    pending_count_.fetch_add(1, std::memory_order_relaxed);
  }

  void update(uint32_t id, std::chrono::milliseconds timestamp) {
    entries_[id].timestamp = timestamp.count();
  }

  bool remove(uint32_t id) {
    bool expected = true;
    if (entries_[id].pending.compare_exchange_strong(
            expected, false, std::memory_order_acq_rel)) {
      pending_count_.fetch_sub(1, std::memory_order_relaxed);
      return true;
    }
    return false;
  }

  [[nodiscard]] bool is_pending(uint32_t id) const {
    return entries_[id].pending.load(std::memory_order_acquire);
  }

  [[nodiscard]] std::chrono::milliseconds get_timestamp(uint32_t id) const {
    return std::chrono::milliseconds{entries_[id].timestamp};
  }

  [[nodiscard]] bool all_acknowledged() const {
    return pending_count_.load(std::memory_order_acquire) == 0;
  }

  [[nodiscard]] size_t pending() const {
    return pending_count_.load(std::memory_order_relaxed);
  }

private:
  size_t capacity_;
  std::unique_ptr<Entry[]> entries_;
  alignas(64) std::atomic<size_t> pending_count_;
};
} // namespace RDMA_EC