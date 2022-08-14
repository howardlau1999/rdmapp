#pragma once

#include <memory>

#include <infiniband/verbs.h>

#include "rdmapp/pd.h"

namespace rdmapp {

/**
 * @brief This class represents a Shared Receive Queue.
 *
 */
class srq {
  struct ibv_srq *srq_;
  std::shared_ptr<pd> pd_;
  friend class qp;

public:
  /**
   * @brief Construct a new srq object
   *
   * @param pd The protection domain to use.
   * @param max_wr The maximum number of outstanding work requests.
   */
  srq(std::shared_ptr<pd> pd, size_t max_wr = 1024);

  /**
   * @brief Destroy the srq object and the associated shared receive queue.
   *
   */
  ~srq();
};

} // namespace rdmapp