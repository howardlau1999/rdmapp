#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <sys/epoll.h>
#include <unordered_map>

#include "rdmapp/socket/channel.h"

namespace rdmapp {
namespace socket {

class event_loop {
  int epoll_fd_;
  int close_event_fd_;
  const size_t max_events_;
  std::unordered_map<int, std::weak_ptr<channel>> channels_;

  void register_channel(std::shared_ptr<channel> channel, struct epoll_event *event);
public:
  event_loop(size_t max_events = 10);
  static std::shared_ptr<event_loop> new_loop(size_t max_events = 10);
  void loop();
  void close();
  void register_read(std::shared_ptr<channel> channel);
  void register_write(std::shared_ptr<channel> channel);
  void deregister(socket::channel &channel);
  ~event_loop();
};

} // namespace socket
} // namespace rdmapp