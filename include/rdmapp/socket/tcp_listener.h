#pragma once

#include <memory>

#include "rdmapp/socket/event_loop.h"

namespace rdmapp {
namespace socket {

class tcp_listener {
  std::shared_ptr<channel> channel_;
  std::shared_ptr<event_loop> loop_;
public:
  struct accept_awaitable {
        std::shared_ptr<channel> channel_;
    void *buffer_;
    int fd_;
    int do_io();
  public:
    accept_awaitable(std::shared_ptr<channel> channel);
    bool await_ready();
    void await_suspend(std::coroutine_handle<> h);
    int await_resume();
  };
  tcp_listener(std::shared_ptr<event_loop> loop, std::string const &hostname, uint16_t port);
  accept_awaitable accept();
};

} // namespace detail
} // namespace rdmapp