#pragma once

#include <arpa/inet.h>
#include <cstdint>
#include <memory>
#include <sys/socket.h>

#include "rdmapp/detail/noncopyable.h"
#include "rdmapp/device.h"
#include "rdmapp/pd.h"
#include "rdmapp/qp.h"
#include "rdmapp/socket/channel.h"
#include "rdmapp/socket/tcp_listener.h"

namespace rdmapp {

class acceptor : public noncopyable {
  int fd_;
  std::shared_ptr<socket::event_loop> loop_;
  socket::tcp_listener listener_;
  std::shared_ptr<pd> pd_;
  std::shared_ptr<cq> recv_cq_;
  std::shared_ptr<cq> send_cq_;

public:
  acceptor(std::shared_ptr<pd> pd, std::shared_ptr<cq> cq, uint16_t port);
  acceptor(std::shared_ptr<pd> pd, std::shared_ptr<cq> recv_cq,
           std::shared_ptr<cq> send_cq, uint16_t port);
  acceptor(std::shared_ptr<pd> pd, std::shared_ptr<cq> cq,
           std::string const &hostname, uint16_t port);
  acceptor(std::shared_ptr<pd> pd, std::shared_ptr<cq> recv_cq,
           std::shared_ptr<cq> send_cq, std::string const &hostname,
           uint16_t port);
  task<std::shared_ptr<qp>> accept();
  ~acceptor();
};

} // namespace rdmapp