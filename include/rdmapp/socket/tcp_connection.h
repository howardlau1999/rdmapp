#pragma once

#include <memory>

#include "rdmapp/socket/channel.h"
#include "rdmapp/task.h"

namespace rdmapp {
namespace socket {

class tcp_connection {
  std::shared_ptr<channel> channel_;

public:
  class rw_awaitable {
    std::shared_ptr<channel> channel_;
    void *buffer_;
    int n_;
    size_t length_;
    bool write_;
    int do_io();

  public:
    rw_awaitable(std::shared_ptr<channel> channel, bool write, void *buffer,
                 size_t length);
    bool await_ready();
    void await_suspend(std::coroutine_handle<> h);
    int await_resume();
  };
  tcp_connection(std::shared_ptr<channel> channel);
  rw_awaitable recv(void *buffer, size_t length);
  rw_awaitable send(const void *buffer, size_t length);
};

} // namespace socket
} // namespace rdmapp