#include "rdmapp/socket/channel.h"

#include <cassert>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <memory>
#include <unistd.h>

#include "rdmapp/detail/debug.h"
#include "rdmapp/error.h"
#include "rdmapp/socket/event_loop.h"

namespace rdmapp {
namespace socket {

channel::channel(int fd, std::shared_ptr<event_loop> loop)
    : fd_(fd), loop_(loop) {
}

int channel::fd() { return fd_; }

std::function<void()> channel::noop_callback = []() {};

void channel::set_nonblocking() {
  int opts = fcntl(fd_, F_GETFL);
  check_errno(opts, "failed to get fcntl flags");
  opts |= O_NONBLOCK;
  check_errno(fcntl(fd_, F_SETFL, opts), "failed to set fcntl flags");
}

void channel::writable_callback() {
  writable_callback_();
  writable_callback_ = noop_callback;
  loop_->deregister(this->shared_from_this());
}

void channel::readable_callback() {
  readable_callback_();
  readable_callback_ = noop_callback;
  loop_->deregister(this->shared_from_this());
}

void channel::wait_readable() {
  loop_->register_read(this->shared_from_this());
}

void channel::wait_writable() {
  loop_->register_write(this->shared_from_this());
}

void channel::set_writable_callback(callback_fn &&callback) {
  writable_callback_ = callback;
}

void channel::set_readable_callback(callback_fn &&callback) {
  readable_callback_ = callback;
}

channel::~channel() {
  assert(fd_ > 0);
  if (auto rc = ::close(fd_); rc != 0) {
    RDMAPP_LOG_ERROR("failed to close fd %d: %s (errno=%d)", fd_,
                     strerror(errno), errno);
  } else {
    RDMAPP_LOG_DEBUG("closed fd %d", fd_);
  }
}

} // namespace socket
} // namespace rdmapp