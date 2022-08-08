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
#include <thread>
#include <unistd.h>
#include <vector>

#include "rdmapp/detail/debug.h"
#include "rdmapp/device.h"
#include "rdmapp/error.h"
#include "rdmapp/qp.h"
#include "rdmapp/socket/channel.h"
#include "rdmapp/socket/tcp_connection.h"
#include "rdmapp/socket/tcp_listener.h"

namespace rdmapp {

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
    : pd_(pd), recv_cq_(recv_cq), send_cq_(send_cq),
      loop_(std::make_shared<socket::event_loop>()),
      listener_(loop_, hostname, port), fd_(-1) {
  auto looper = std::thread([this]() { loop_->loop(); });
  looper.detach();
}

task<std::shared_ptr<qp>> acceptor::accept() {
  auto fd = co_await listener_.accept();
  auto channel = std::make_shared<socket::channel>(fd, loop_);
  auto connection = socket::tcp_connection(channel);
  auto remote_qp = co_await qp::recv_qp(connection);
  auto local_qp =
      std::make_shared<qp>(remote_qp.header.lid, remote_qp.header.qp_num,
                           remote_qp.header.sq_psn, pd_, send_cq_, recv_cq_);
  local_qp->user_data() = std::move(remote_qp.user_data);
  co_await local_qp->send_qp(connection);
  co_return local_qp;
}

acceptor::~acceptor() { loop_->close(); }

} // namespace rdmapp