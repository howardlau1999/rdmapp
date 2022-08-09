#include "rdmapp/socket/tcp_connection.h"

#include <arpa/inet.h>
#include <memory>
#include <netdb.h>
#include <netinet/in.h>

#include "rdmapp/detail/debug.h"
#include "rdmapp/error.h"
#include "rdmapp/socket/channel.h"

namespace rdmapp {
namespace socket {

tcp_connection::tcp_connection(std::shared_ptr<channel> channel)
    : channel_(channel) {}

tcp_connection::connect_awaitable
tcp_connection::connect(std::shared_ptr<event_loop> loop,
                        const std::string &hostname, uint16_t port) {
  return connect_awaitable(loop, hostname, port);
}

tcp_connection::connect_awaitable::connect_awaitable(
    std::shared_ptr<event_loop> loop, std::string const &hostname,
    uint16_t port)
    : rc_(-1) {
  struct addrinfo hints, *servinfo, *p;
  auto const port_str = std::to_string(port);
  char s[INET6_ADDRSTRLEN];
  bzero(&hints, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  if (auto rc =
          ::getaddrinfo(hostname.c_str(), port_str.c_str(), &hints, &servinfo);
      rc != 0) {
    throw_with("failed to getaddrinfo: %s", ::gai_strerror(rc));
  }
  for (p = servinfo; p != nullptr; p = p->ai_next) {
    int fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (fd == -1) {
      RDMAPP_LOG_ERROR("failed to create socket: %s(errno=%d)", strerror(errno),
                       errno);
      continue;
    }
    auto channel = std::make_shared<socket::channel>(fd, loop);
    try {
      channel->set_nonblocking();
    } catch (std::runtime_error &e) {
      RDMAPP_LOG_ERROR("%s", e.what());
      continue;
    }
    rc_ = ::connect(channel->fd(), p->ai_addr, p->ai_addrlen);
    if (rc_ < 0) {
      if (errno != EINPROGRESS) {
        RDMAPP_LOG_ERROR("failed to connect: %s(errno=%d)", strerror(errno),
                         errno);
        continue;
      }
    }
    channel_ = channel;
    break;
  }
  freeaddrinfo(servinfo);
  if (p == nullptr) {
    throw_with("failed to connect");
  }
}

bool tcp_connection::connect_awaitable::await_ready() { return rc_ == 0; }

void tcp_connection::connect_awaitable::await_suspend(
    std::coroutine_handle<> h) {
  channel_->set_writable_callback([h]() { h.resume(); });
  channel_->wait_writable();
}

std::shared_ptr<tcp_connection>
tcp_connection::connect_awaitable::await_resume() {
  int err = 0;
  socklen_t len = sizeof(err);
  int status = ::getsockopt(channel_->fd(), SOL_SOCKET, SO_ERROR, &err, &len);
  check_errno(status, "failed to get socket error");
  check_rc(err, "failed to connect");
  RDMAPP_LOG_DEBUG("fd %d connected", channel_->fd());
  return std::make_shared<tcp_connection>(channel_);
}

tcp_connection::rw_awaitable tcp_connection::recv(void *buffer, size_t length) {
  return rw_awaitable(channel_, false, buffer, length);
}

tcp_connection::rw_awaitable tcp_connection::send(const void *buffer,
                                                  size_t length) {
  return rw_awaitable(channel_, true, const_cast<void *>(buffer), length);
}

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