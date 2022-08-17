#include "rdmapp/connector.h"

#include "rdmapp/pd.h"

namespace rdmapp {

connector::connector(std::shared_ptr<socket::event_loop> loop,
                     std::string const &hostname, uint16_t port,
                     std::shared_ptr<pd> pd, std::shared_ptr<cq> recv_cq,
                     std::shared_ptr<cq> send_cq, std::shared_ptr<srq> srq)
    : pd_(pd), recv_cq_(recv_cq), send_cq_(send_cq), srq_(srq), loop_(loop),
      hostname_(hostname), port_(port) {}

connector::connector(std::shared_ptr<socket::event_loop> loop,
                     std::string const &hostname, uint16_t port,
                     std::shared_ptr<pd> pd, std::shared_ptr<cq> cq,
                     std::shared_ptr<srq> srq)
    : connector(loop, hostname, port, pd, cq, cq, srq) {}

task<std::shared_ptr<qp>> connector::connect() {
  auto connection =
      co_await rdmapp::socket::tcp_connection::connect(loop_, hostname_, port_);
  auto qp = co_await rdmapp::qp::from_tcp_connection(*connection, pd_, recv_cq_,
                                                     send_cq_, srq_);
  co_return qp;
}

} // namespace rdmapp