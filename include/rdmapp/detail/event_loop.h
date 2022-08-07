#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>

#include "rdmapp/detail/channel.h"

namespace rdmapp {
namespace detail {

class event_loop {
  int epoll_fd_;
  int close_event_fd_;
  const size_t max_events_;
  std::unordered_map<int, std::shared_ptr<channel>> channels_;

public:
  event_loop(size_t max_events = 10);
  void loop();
  void close();
  ~event_loop();
};

} // namespace detail
} // namespace rdmapp