#include "rdmapp/socket/tcp_connection.h"

#include <memory>

#include "rdmapp/error.h"

namespace rdmapp {
namespace socket {

tcp_connection::tcp_connection(std::shared_ptr<channel> channel)
    : channel_(channel) {}

tcp_connection::rw_awaitable::rw_awaitable(std::shared_ptr<channel> channel,
                                           bool write, void *buffer,
                                           size_t length)
    : channel_(channel), write_(write), buffer_(buffer), length_(length),
      n_(-1) {}

int tcp_connection::rw_awaitable::do_io() {
  int n = -1;
  if (write_) {
    n = ::write(channel_->fd(), buffer_, length_);
  } else {
    n = ::read(channel_->fd(), buffer_, length_);
  }
  return n;
}

bool tcp_connection::rw_awaitable::await_ready() {
  n_ = do_io();
  if (n_ >= 0) {
    return true;
  }
  if (errno == EAGAIN || errno == EWOULDBLOCK) {
    return false;
  } else {
    check_errno(n_, "failed to read write");
  }
  return false;
}

void tcp_connection::rw_awaitable::await_suspend(std::coroutine_handle<> h) {
  auto &&callback = [h]() { h.resume(); };
  if (write_) {
    channel_->set_writable_callback(callback);
    channel_->wait_writable();
  } else {
    channel_->set_readable_callback(callback);
    channel_->wait_readable();
  }
}

int tcp_connection::rw_awaitable::await_resume() {
  if (n_ < 0) {
    n_ = do_io();
    check_errno(n_, "failed to io after readable or writable");
  }
  return n_;
}

} // namespace socket
} // namespace rdmapp