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
  std::unique_ptr<socket::tcp_listener> listener_;
  std::shared_ptr<pd> pd_;
  std::shared_ptr<cq> recv_cq_;
  std::shared_ptr<cq> send_cq_;
  std::shared_ptr<srq> srq_;

public:
  acceptor(std::shared_ptr<socket::event_loop> loop, uint16_t port,
           std::shared_ptr<pd> pd, std::shared_ptr<cq> cq,
           std::shared_ptr<srq> srq = nullptr);
  acceptor(std::shared_ptr<socket::event_loop> loop, uint16_t port,
           std::shared_ptr<pd> pd, std::shared_ptr<cq> recv_cq,
           std::shared_ptr<cq> send_cq, std::shared_ptr<srq> srq = nullptr);
  acceptor(std::shared_ptr<socket::event_loop> loop,
           std::string const &hostname, uint16_t port, std::shared_ptr<pd> pd,
           std::shared_ptr<cq> cq, std::shared_ptr<srq> srq = nullptr);
  acceptor(std::shared_ptr<socket::event_loop> loop,
           std::string const &hostname, uint16_t port, std::shared_ptr<pd> pd,
           std::shared_ptr<cq> recv_cq, std::shared_ptr<cq> send_cq,
           std::shared_ptr<srq> srq = nullptr);
  task<std::shared_ptr<qp>> accept();
  ~acceptor();
};

} // namespace rdmapp