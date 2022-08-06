#pragma once

#include <arpa/inet.h>
#include <memory>
#include <netinet/in.h>

#include "rdmapp/qp.h"

namespace rdmapp {
namespace detail {
namespace socket {

class tcp {
  int fd_;

public:
  tcp(int fd);
  tcp(std::string const &hostname, uint16_t port);
  ~tcp();
  deserialized_qp recv_qp();
  void send_qp(qp const &qp);
};

} // namespace socket
} // namespace detail
} // namespace rdmapp