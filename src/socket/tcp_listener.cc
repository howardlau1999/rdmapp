#include "rdmapp/socket/tcp_listener.h"

#include <arpa/inet.h>
#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>
#include <cassert>
#include <cerrno>
#include <memory>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "rdmapp/detail/debug.h"
#include "rdmapp/error.h"
#include "rdmapp/socket/channel.h"

namespace rdmapp {
namespace socket {
static inline void *get_in_addr(struct sockaddr *sa) {
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in *)sa)->sin_addr);
  }

  return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

static inline uint16_t get_in_port(struct sockaddr *sa) {
  if (sa->sa_family == AF_INET) {
    return (((struct sockaddr_in *)sa)->sin_port);
  }

  return (((struct sockaddr_in6 *)sa)->sin6_port);
}

std::string get_in_addr_string(struct addrinfo *ai) {
  char s[INET6_ADDRSTRLEN];
  inet_ntop(ai->ai_family, get_in_addr(ai->ai_addr), s, sizeof(s));
  return s;
}

std::string get_in_addr_string(struct sockaddr_storage *ss) {
  char s[INET6_ADDRSTRLEN];
  inet_ntop(ss->ss_family, get_in_addr(reinterpret_cast<struct sockaddr *>(ss)),
            s, sizeof(s));
  return s;
}

int tcp_listener::accept_awaitable::do_io() {
  struct sockaddr_storage client_addr = {};
  socklen_t client_addr_len = sizeof(client_addr);
  int client_fd =
      ::accept4(fd_, reinterpret_cast<struct sockaddr *>(&client_addr),
                &client_addr_len, SOCK_CLOEXEC | SOCK_NONBLOCK);
  if (client_fd < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return client_fd;
    }
  }
  check_errno(client_fd, "failed to accept");
  auto const &client_ip = get_in_addr_string(&client_addr);
  RDMAPP_LOG_DEBUG(
      "accepted connection from %s:%d fd=%d", client_ip.c_str(),
      get_in_port(reinterpret_cast<struct sockaddr *>(&client_addr)),
      client_fd);
  return client_fd;
}

tcp_listener::accept_awaitable::accept_awaitable(
    std::shared_ptr<channel> channel)
    : channel_(channel) {}

bool tcp_listener::accept_awaitable::await_ready() {
  fd_ = do_io();
  return fd_ > 0;
}

void tcp_listener::accept_awaitable::await_suspend(std::coroutine_handle<> h) {
  channel_->set_readable_callback([h]() { h.resume(); });
  channel_->wait_readable();
}

std::shared_ptr<channel> tcp_listener::accept_awaitable::await_resume() {
  if (fd_ < 0) {
    fd_ = do_io();
  }
  check_errno(fd_, "could not accept after readable");
  auto channel_ptr = std::make_shared<channel>(fd_, channel_->loop());
  channel_ptr->set_nonblocking();
  return channel_ptr;
}

tcp_listener::tcp_listener(std::shared_ptr<event_loop> loop,
                           std::string const &hostname, uint16_t port) {
  std::string port_str = std::to_string(port);
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);

  check_errno(fd, "failed to create socket");
  {
    int32_t yes = 1;
    check_rc(::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)),
             "failed to set reuse address");
  }

  struct addrinfo hints, *servinfo, *p;
  ::bzero(&hints, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  if (auto rc = getaddrinfo(nullptr, port_str.c_str(), &hints, &servinfo);
      rc != 0) {
    throw_with("getaddrinfo: %s", gai_strerror(rc));
  }

  for (p = servinfo; p != nullptr; p = p->ai_next) {
    try {
      auto const &ip = get_in_addr_string(p);
      RDMAPP_LOG_DEBUG("binding %s:%d", ip.c_str(), port);
      check_errno(::bind(fd, p->ai_addr, p->ai_addrlen), "failed to bind");
    } catch (std::runtime_error &e) {
      RDMAPP_LOG_ERROR("%s", e.what());
      continue;
    }
    break;
  }
  ::freeaddrinfo(servinfo);
  check_ptr(p, "failed to bind");
  check_errno(::listen(fd, 128), "failed to listen");
  channel_ = std::make_shared<channel>(fd, loop);
  channel_->set_nonblocking();
  RDMAPP_LOG_DEBUG("acceptor fd %d listening on %d", fd, port);
}

tcp_listener::accept_awaitable tcp_listener::accept() {
  return accept_awaitable{channel_};
}

} // namespace socket
} // namespace rdmapp