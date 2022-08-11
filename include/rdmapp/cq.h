#pragma once

#include <array>
#include <memory>
#include <vector>

#include <infiniband/verbs.h>

#include "rdmapp/detail/noncopyable.h"
#include "rdmapp/device.h"
#include "rdmapp/error.h"

namespace rdmapp {

class qp;

class cq : public noncopyable {
  std::shared_ptr<device> device_;
  struct ibv_cq *cq_;
  friend class qp;

public:
  cq(std::shared_ptr<device> device, size_t num_cqe = 128);
  bool poll(struct ibv_wc &wc);
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
  size_t poll(std::vector<struct ibv_wc> &wc_vec);
  ~cq();
};

} // namespace rdmapp