#pragma once

#include <memory>

#include <infiniband/verbs.h>

#include "rdmapp/detail/noncopyable.h"
#include "rdmapp/device.h"
#include "rdmapp/mr.h"

namespace rdmapp {

class qp;

class pd : public noncopyable, public std::enable_shared_from_this<pd> {
  std::shared_ptr<device> device_;
  struct ibv_pd *pd_;
  friend class qp;
  friend class srq;

public:
  pd(std::shared_ptr<device> device);
  std::unique_ptr<mr> reg_mr(void *addr, size_t length);
  std::unique_ptr<mr> reg_mr(void *addr, size_t length, int flags);
  ~pd();
};

} // namespace rdmapp