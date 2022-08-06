#include "rdmapp/acceptor.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <netdb.h>
#include <netinet/in.h>
#include <stdexcept>
#include <strings.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#include "rdmapp/detail/debug.h"
#include "rdmapp/detail/socket.h"
#include "rdmapp/device.h"
#include "rdmapp/error.h"
#include "rdmapp/qp.h"

namespace rdmapp {

static inline void *get_in_addr(struct sockaddr *sa) {
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in *)sa)->sin_addr);
  }

  return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

uint16_t get_in_port(struct sockaddr *sa) {
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

acceptor::acceptor(std::shared_ptr<pd> pd, std::shared_ptr<cq> cq,
                   uint16_t port)
    : acceptor(pd, cq, cq, port) {}

acceptor::acceptor(std::shared_ptr<pd> pd, std::shared_ptr<cq> recv_cq,
                   std::shared_ptr<cq> send_cq, uint16_t port)
    : acceptor(pd, recv_cq, send_cq, "", port) {}

acceptor::acceptor(std::shared_ptr<pd> pd, std::shared_ptr<cq> cq,
                   std::string const &hostname, uint16_t port)
    : acceptor(pd, cq, cq, hostname, port) {}

acceptor::acceptor(std::shared_ptr<pd> pd, std::shared_ptr<cq> recv_cq,
                   std::shared_ptr<cq> send_cq, std::string const &hostname,
                   uint16_t port)
    : pd_(pd), recv_cq_(recv_cq), send_cq_(send_cq), fd_(-1) {
  std::string port_str = std::to_string(port);
  fd_ = ::socket(AF_INET, SOCK_STREAM, 0);

  check_errno(fd_, "failed to create socket");
  {
    int32_t yes = 1;
    check_rc(::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)),
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
      check_errno(::bind(fd_, p->ai_addr, p->ai_addrlen), "failed to bind");
    } catch (std::runtime_error &e) {
      RDMAPP_LOG_ERROR("%s", e.what());
      continue;
    }
    break;
  }
  ::freeaddrinfo(servinfo);
  check_ptr(p, "failed to bind");
  check_errno(::listen(fd_, 128), "failed to listen");
  RDMAPP_LOG_DEBUG("acceptor fd %d listening on %d", fd_, port);
}

std::shared_ptr<qp> acceptor::accept() {
  struct sockaddr_storage client_addr = {};
  socklen_t client_addr_len = sizeof(client_addr);
  int client_fd =
      ::accept4(fd_, reinterpret_cast<struct sockaddr *>(&client_addr),
                &client_addr_len, SOCK_CLOEXEC);
  check_errno(client_fd, "failed to accept");
  auto const &client_ip = get_in_addr_string(&client_addr);
  RDMAPP_LOG_DEBUG(
      "accepted connection from %s:%d fd=%d", client_ip.c_str(),
      get_in_port(reinterpret_cast<struct sockaddr *>(&client_addr)),
      client_fd);
  detail::socket::tcp remote(client_fd);
  auto remote_qp = remote.recv_qp();
  auto local_qp =
      std::make_shared<qp>(remote_qp.header.lid, remote_qp.header.qp_num,
                           remote_qp.header.sq_psn, pd_, send_cq_, recv_cq_);
  local_qp->user_data() = std::move(remote_qp.user_data);
  remote.send_qp(*local_qp);
  return local_qp;
}

acceptor::~acceptor() {
  if (fd_ > 0) {
    if (auto rc = ::close(fd_); rc != 0) {
      RDMAPP_LOG_ERROR("failed to close acceptor fd: %s(%d)", strerror(rc), rc);
    } else {
      RDMAPP_LOG_DEBUG("closed acceptor fd: %d", fd_);
    }
  }
}

} // namespace rdmapp