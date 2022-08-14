#pragma once

#include <memory>

#include "rdmapp/cq.h"
#include "rdmapp/pd.h"
#include "rdmapp/qp.h"
#include "rdmapp/socket/event_loop.h"
#include "rdmapp/task.h"

#include "rdmapp/detail/noncopyable.h"

namespace rdmapp {

/**
 * @brief This class is used to actively connect to a remote endpoint and
 * establish a Queue Pair.
 *
 */
class connector : public noncopyable {
  std::shared_ptr<pd> pd_;
  std::shared_ptr<cq> recv_cq_;
  std::shared_ptr<cq> send_cq_;
  std::shared_ptr<srq> srq_;
  std::shared_ptr<socket::event_loop> loop_;
  std::string hostname_;
  uint16_t port_;

public:
  /**
   * @brief Construct a new connector object.
   *
   * @param loop The event loop to use.
   * @param hostname The hostname to connect to.
   * @param port The port to connect to.
   * @param recv_cq The recv completion queue to use for new Queue Pairs.
   * @param send_cq The send completion queue to use for new Queue Pairs.
   * @param srq (Optional) The shared receive queue to use for new Queue Pairs.
   */
  connector(std::shared_ptr<socket::event_loop> loop,
            std::string const &hostname, uint16_t port, std::shared_ptr<pd> pd,
            std::shared_ptr<cq> recv_cq, std::shared_ptr<cq> send_cq,
            std::shared_ptr<srq> srq = nullptr);

  /**
   * @brief Construct a new connector object.
   *
   * @param loop The event loop to use.
   * @param hostname The hostname to connect to.
   * @param port The port to connect to.
   * @param recv_cq The send/recv completion queue to use for new Queue Pairs.
   * @param srq (Optional) The shared receive queue to use for new Queue Pairs.
   */
  connector(std::shared_ptr<socket::event_loop> loop,
            std::string const &hostname, uint16_t port, std::shared_ptr<pd> pd,
            std::shared_ptr<cq> cq, std::shared_ptr<srq> srq = nullptr);

  /**
   * @brief This function is used to connect to a remote endpoint and establish
   * a Queue Pair.
   *
   * @return task<std::shared_ptr<qp>>
   */
  task<std::shared_ptr<qp>> connect();
};

} // namespace rdmapp