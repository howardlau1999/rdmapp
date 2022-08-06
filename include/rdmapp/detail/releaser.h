#pragma once

#include <deque>
#include <functional>

namespace rdmapp {
namespace detail {

class Releaser {
  std::deque<std::function<void()>> release_callbacks_;

public:
  ~Releaser() { release(); }
  void register_release_callback(std::function<void()> &&cb) {
    release_callbacks_.emplace_front(std::move(cb));
  }
  void clear() { release_callbacks_.clear(); }
  void release() {
    for (auto &&cb : release_callbacks_) {
      cb();
    }
    release_callbacks_.clear();
  }
};

} // namespace detail
} // namespace rdmapp