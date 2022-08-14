#pragma once

#include <memory>

#include "rdmapp/cq.h"
#include "rdmapp/pd.h"
#include "rdmapp/qp.h"
#include "rdmapp/socket/event_loop.h"
#include "rdmapp/task.h"

#include "rdmapp/detail/noncopyable.h"

namespace rdmapp {

class connector : public noncopyable {
  std::shared_ptr<pd> pd_;
  std::shared_ptr<cq> recv_cq_;
  std::shared_ptr<cq> send_cq_;
  std::shared_ptr<srq> srq_;
  std::shared_ptr<socket::event_loop> loop_;
  std::string hostname_;
  uint16_t port_;

public:
  connector(std::shared_ptr<socket::event_loop> loop,
            std::string const &hostname, uint16_t port, std::shared_ptr<pd> pd,
            std::shared_ptr<cq> recv_cq, std::shared_ptr<cq> send_cq,
            std::shared_ptr<srq> srq = nullptr);
  connector(std::shared_ptr<socket::event_loop> loop,
            std::string const &hostname, uint16_t port, std::shared_ptr<pd> pd,
            std::shared_ptr<cq> cq, std::shared_ptr<srq> srq = nullptr);
  task<std::shared_ptr<qp>> connect();
};

} // namespace rdmapp