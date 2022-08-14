#include "rdmapp/cq.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

#include <infiniband/verbs.h>

#include "rdmapp/error.h"

#include "rdmapp/detail/debug.h"

namespace rdmapp {

cq::cq(std::shared_ptr<device> device, size_t nr_cqe) : device_(device) {
  cq_ = ::ibv_create_cq(device->ctx_, nr_cqe, this, nullptr, 0);
  check_ptr(cq_, "failed to create cq");
  RDMAPP_LOG_TRACE("created cq: %p", cq_);
}

bool cq::poll(struct ibv_wc &wc) {
  if (auto rc = ::ibv_poll_cq(cq_, 1, &wc); rc < 0) [[unlikely]] {
    check_rc(-rc, "failed to poll cq");
  } else if (rc == 0) {
    return false;
  } else {
    return true;
  }
  return false;
}

size_t cq::poll(std::vector<struct ibv_wc> &wc_vec) {
  return poll(&wc_vec[0], wc_vec.size());
}

cq::~cq() {
  if (cq_ != nullptr) {
    if (auto rc = ::ibv_destroy_cq(cq_); rc != 0) [[unlikely]] {
      RDMAPP_LOG_ERROR("failed to destroy cq %p: %s", cq_, strerror(errno));
    } else {
      RDMAPP_LOG_TRACE("destroyed cq: %p", cq_);
    }
  }
}

} // namespace rdmapp