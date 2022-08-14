#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

#include <infiniband/verbs.h>

#include "rdmapp/cq.h"
#include "rdmapp/executor.h"

namespace rdmapp {

/**
 * @brief This class is used to poll a completion queue.
 *
 */
class cq_poller {
  std::shared_ptr<cq> cq_;
  std::atomic<bool> stopped_;
  std::thread poller_thread_;
  std::shared_ptr<executor> executor_;
  std::vector<struct ibv_wc> wc_vec_;
  void worker();

public:
  /**
   * @brief Construct a new cq poller object. A new executor will be created.
   *
   * @param cq The completion queue to poll.
   * @param batch_size The number of completion entries to poll at a time.
   */
  cq_poller(std::shared_ptr<cq> cq, size_t batch_size = 16);

  /**
   * @brief Construct a new cq poller object.
   *
   * @param cq The completion queue to poll.
   * @param executor The executor to use to process the completion entries.
   * @param batch_size The number of completion entries to poll at a time.
   */
  cq_poller(std::shared_ptr<cq> cq, std::shared_ptr<executor> executor,
            size_t batch_size = 16);

  ~cq_poller();
};

} // namespace rdmapp