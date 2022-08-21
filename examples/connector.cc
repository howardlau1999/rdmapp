#include "connector.h"

#include "qp_transmission.h"
#include "socket/tcp_connection.h"

#include <rdmapp/pd.h>

namespace rdmapp {

/**
 * @brief This function is used to receive a Queue Pair from a remote peer.
 *
 * @param connection The TCP connection to the remote peer.
 * @param pd The protection domain of the new Queue Pair.
 * @param recv_cq The completion queue of recv work completions.
 * @param send_cq The completion queue of send work completions.
 * @param srq (Optional) If set, all recv work requests will be posted to this
 * SRQ.
 * @return task<std::shared_ptr<qp>> A coroutine that returns a shared pointer
 * to the new Queue Pair.
 */
static task<std::shared_ptr<qp>>
from_tcp_connection(socket::tcp_connection &connection, std::shared_ptr<pd> pd,
                    std::shared_ptr<cq> recv_cq, std::shared_ptr<cq> send_cq,
                    std::shared_ptr<srq> srq = nullptr) {
  auto qp_ptr = std::make_shared<qp>(pd, recv_cq, send_cq, srq);
  co_await send_qp(*qp_ptr, connection);
  auto remote_qp = co_await recv_qp(connection);
  qp_ptr->rtr(remote_qp.header.lid, remote_qp.header.qp_num,
              remote_qp.header.sq_psn);
  qp_ptr->user_data() = std::move(remote_qp.user_data);
  qp_ptr->rts();
  co_return qp_ptr;
}

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
  auto qp =
      co_await from_tcp_connection(*connection, pd_, recv_cq_, send_cq_, srq_);
  co_return qp;
}

} // namespace rdmapp