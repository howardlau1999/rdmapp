#pragma once

#include <array>
#include <memory>
#include <vector>

#include <infiniband/verbs.h>

#include "rdmapp/device.h"
#include "rdmapp/error.h"

#include "rdmapp/detail/noncopyable.h"

namespace rdmapp {

class qp;

/**
 * @brief This class is an abstraction of a Completion Queue.
 *
 */
class cq : public noncopyable {
  std::shared_ptr<device> device_;
  struct ibv_cq *cq_;
  friend class qp;

public:
  /**
   * @brief Construct a new cq object.
   *
   * @param device The device to use.
   * @param num_cqe The number of completion entries to allocate.
   */
  cq(std::shared_ptr<device> device, size_t num_cqe = 128);

  /**
   * @brief Poll the completion queue.
   *
   * @param wc If any, this will be filled with a completion entry.
   * @return true If there is a completion entry.
   * @return false If there is no completion entry.
   * @exception std::runtime_exception Error occured while polling the
   * completion queue.
   */
  bool poll(struct ibv_wc &wc);

  /**
   * @brief Poll the completion queue.
   *
   * @param wc_vec If any, this will be filled with completion entries up to the
   * size of the vector.
   * @return size_t The number of completion entries. 0 means no completion
   * entry.
   * @exception std::runtime_exception Error occured while polling the
   * completion queue.
   */
  size_t poll(std::vector<struct ibv_wc> &wc_vec);
  template <class It> size_t poll(It wc, int count) {
    int rc = ::ibv_poll_cq(cq_, count, wc);
    if (rc < 0) {
      throw_with("failed to poll cq: %s (rc=%d)", strerror(rc), rc);
    }
    return rc;
  }
  template <int N> size_t poll(std::array<struct ibv_wc, N> &wc_array) {
    return poll(&wc_array[0], N);
  }
  ~cq();
};

} // namespace rdmapp