#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>
#include <stdexcept>

namespace rdmapp {
namespace detail {

template <class T> class blocking_queue {
  std::mutex mutex_;
  std::condition_variable cv_;
  std::queue<T> queue_;
  bool closed_;

public:
  struct queue_closed_error {};
  void push(T const &item) {
    std::lock_guard lock(mutex_);
    if (closed_) {
      throw queue_closed_error();
    }
    queue_.push(item);
    cv_.notify_one();
  }
  T pop() {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this]() { return !queue_.empty() || closed_; });
    if (queue_.empty()) {
      throw queue_closed_error();
    }
    T item = queue_.front();
    queue_.pop();
    return item;
  }
  void close() {
    std::lock_guard lock(mutex_);
    closed_ = true;
    cv_.notify_all();
  }
};

} // namespace detail
} // namespace rdmapp