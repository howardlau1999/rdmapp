#pragma once

#include <arpa/inet.h>
#include <cstdint>
#include <memory>
#include <sys/socket.h>

#include "rdmapp/device.h"
#include "rdmapp/pd.h"
#include "rdmapp/qp.h"
#include "rdmapp/socket/channel.h"
#include "rdmapp/socket/tcp_listener.h"

#include "rdmapp/detail/noncopyable.h"

namespace rdmapp {

/**
 * @brief This class is used to accept incoming connections and queue pairs.
 *
 */
class acceptor : public noncopyable {
  std::unique_ptr<socket::tcp_listener> listener_;
  std::shared_ptr<pd> pd_;
  std::shared_ptr<cq> recv_cq_;
  std::shared_ptr<cq> send_cq_;
  std::shared_ptr<srq> srq_;

public:
  /**
   * @brief Construct a new acceptor object.
   *
   * @param loop The event loop to use.
   * @param port The port to listen on.
   * @param recv_cq The recv completion queue to use for incoming Queue Pairs.
   * @param send_cq The send completion queue to use for incoming Queue Pairs.
   * @param srq (Optional) The shared receive queue to use for incoming Queue
   * Pairs.
   */
  acceptor(std::shared_ptr<socket::event_loop> loop, uint16_t port,
           std::shared_ptr<pd> pd, std::shared_ptr<cq> cq,
           std::shared_ptr<srq> srq = nullptr);

  /**
   * @brief Construct a new acceptor object.
   *
   * @param loop The event loop to use.
   * @param port The port to listen on.
   * @param pd The protection domain for all new Queue Pairs.
   * @param recv_cq The recv completion queue to use for incoming Queue Pairs.
   * @param send_cq The send completion queue to use for incoming Queue Pairs.
   * @param srq (Optional) The shared receive queue to use for incoming Queue
   * Pairs.
   */
  acceptor(std::shared_ptr<socket::event_loop> loop, uint16_t port,
           std::shared_ptr<pd> pd, std::shared_ptr<cq> recv_cq,
           std::shared_ptr<cq> send_cq, std::shared_ptr<srq> srq = nullptr);

  /**
   * @brief Construct a new acceptor object.
   *
   * @param loop The event loop to use.
   * @param hostname The hostname to listen on.
   * @param port The port to listen on.
   * @param pd The protection domain for all new Queue Pairs.
   * @param cq The send/recv completion queue to use for all new Queue Pairs.
   * @param srq (Optional) The shared receive queue to use for all new Queue
   * Pairs.
   */
  acceptor(std::shared_ptr<socket::event_loop> loop,
           std::string const &hostname, uint16_t port, std::shared_ptr<pd> pd,
           std::shared_ptr<cq> cq, std::shared_ptr<srq> srq = nullptr);

  /**
   * @brief Construct a new acceptor object.
   *
   * @param loop The event loop to use.
   * @param hostname The hostname to listen on.
   * @param port The port to listen on.
   * @param pd The protection domain for all new Queue Pairs.
   * @param recv_cq The recv completion queue to use for incoming Queue Pairs.
   * @param send_cq The send completion queue to use for incoming Queue Pairs.
   * @param srq (Optional) The shared receive queue to use for incoming Queue
   * Pairs.
   */
  acceptor(std::shared_ptr<socket::event_loop> loop,
           std::string const &hostname, uint16_t port, std::shared_ptr<pd> pd,
           std::shared_ptr<cq> recv_cq, std::shared_ptr<cq> send_cq,
           std::shared_ptr<srq> srq = nullptr);

  /**
   * @brief This function is used to accept an incoming connection and queue
   * pair. This should be called in a loop.
   *
   * @return task<std::shared_ptr<qp>> A completion task that returns a shared
   * pointer to the new queue pair. It will be in the RTS state.
   */
  task<std::shared_ptr<qp>> accept();
  ~acceptor();
};

} // namespace rdmapp