#include "rdmapp/pd.h"

#include <cstdio>
#include <cstring>
#include <memory>

#include <infiniband/verbs.h>

#include "rdmapp/device.h"
#include "rdmapp/error.h"

#include "rdmapp/detail/debug.h"

namespace rdmapp {

pd::pd(std::shared_ptr<rdmapp::device> device) : device_(device) {
  pd_ = ::ibv_alloc_pd(device->ctx_);
  check_ptr(pd_, "failed to alloc pd");
  RDMAPP_LOG_TRACE("alloc pd %p", pd_);
}

std::shared_ptr<device> pd::device_ptr() const { return device_; }

local_mr pd::reg_mr(void *buffer, size_t length, int flags) {
  auto mr = ::ibv_reg_mr(pd_, buffer, length, flags);
  check_ptr(mr, "failed to reg mr");
  return rdmapp::local_mr(this->shared_from_this(), mr);
}

pd::~pd() {
  if (pd_ != nullptr) {
    if (auto rc = ::ibv_dealloc_pd(pd_); rc != 0) [[unlikely]] {
      RDMAPP_LOG_ERROR("failed to dealloc pd %p: %s", pd_, strerror(errno));
    } else {
      RDMAPP_LOG_TRACE("dealloc pd %p", pd_);
    }
  }
}

} // namespace rdmapp