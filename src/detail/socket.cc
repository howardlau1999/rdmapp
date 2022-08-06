
#include "rdmapp/detail/socket.h"

#include <arpa/inet.h>
#include <cassert>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "rdmapp/detail/debug.h"
#include "rdmapp/error.h"
#include "rdmapp/qp.h"

namespace rdmapp {
namespace detail {
namespace socket {

tcp::tcp(int fd) : fd_(fd) {}

tcp::tcp(std::string const &hostname, uint16_t port) : fd_(-1) {
  struct addrinfo hints, *servinfo, *p;
  auto const port_str = std::to_string(port);
  char s[INET6_ADDRSTRLEN];
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  if (auto rc =
          getaddrinfo(hostname.c_str(), port_str.c_str(), &hints, &servinfo);
      rc != 0) {
    throw_with("failed to getaddrinfo: %s", gai_strerror(rc));
  }
  for (p = servinfo; p != nullptr; p = p->ai_next) {
    if ((fd_ = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      RDMAPP_LOG_ERROR("failed to create socket: %s(errno=%d)", strerror(errno),
                       errno);
      continue;
    }

    if (connect(fd_, p->ai_addr, p->ai_addrlen) == -1) {
      close(fd_);
      fd_ = -1;
      RDMAPP_LOG_ERROR("failed to connect: %s(errno=%d)", strerror(errno),
                       errno);
      continue;
    }
    break;
  }
  freeaddrinfo(servinfo);
  if (p == nullptr) {
    throw_with("failed to connect");
  }
}

tcp::~tcp() {
  if (fd_ > 0) {
    ::close(fd_);
  }
}

deserialized_qp tcp::recv_qp() {
  std::vector<uint8_t> buffer;
  buffer.resize(deserialized_qp::qp_header::kSerializedSize, 0);
  size_t header_read = 0;
  while (header_read < deserialized_qp::qp_header::kSerializedSize) {
    int n =
        ::recv(fd_, &buffer[header_read],
               deserialized_qp::qp_header::kSerializedSize - header_read, 0);
    if (n == 0) {
      throw_with("remote closed unexpectedly while receiving qp header");
    }
    check_errno(n, "failed to receive qp header");
    header_read += n;
  }

  auto remote_qp = deserialized_qp::deserialize(buffer.cbegin());
  size_t user_data_read = 0;
  if (remote_qp.header.user_data_size > 0) {
    remote_qp.user_data.resize(remote_qp.header.user_data_size, 0);
    while (user_data_read < remote_qp.header.user_data_size) {
      int n = ::recv(fd_, &buffer[user_data_read],
                     remote_qp.header.user_data_size - user_data_read, 0);
      if (n == 0) {
        throw_with("remote closed unexpectedly while receiving user data");
      }
      check_errno(n, "failed to receive user data");
      user_data_read += n;
    }
  }
  RDMAPP_LOG_DEBUG("received remote qp lid=%u qpn=%u psn=%u",
                   remote_qp.header.lid, remote_qp.header.qp_num,
                   remote_qp.header.sq_psn);
  return remote_qp;
}

void tcp::send_qp(qp const &qp) {
  auto local_qp_data = qp.serialize();
  assert(local_qp_data.size() != 0);
  size_t local_qp_sent = 0;
  while (local_qp_sent < local_qp_data.size()) {
    int n = ::send(fd_, &local_qp_data[local_qp_sent],
                   local_qp_data.size() - local_qp_sent, 0);
    if (n == 0) {
      throw_with("remote closed unexpectedly while sending qp");
    }
    check_errno(n, "failed to send qp");
    local_qp_sent += n;
  }
}

} // namespace socket
} // namespace detail
} // namespace rdmapp
