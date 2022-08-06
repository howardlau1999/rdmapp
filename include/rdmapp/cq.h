#pragma once

#include <memory>

#include <infiniband/verbs.h>

#include "rdmapp/detail/noncopyable.h"
#include "rdmapp/device.h"

namespace rdmapp {

class qp;

class cq : public noncopyable {
  std::shared_ptr<device> device_;
  struct ibv_cq *cq_;
  friend class qp;

public:
  cq(std::shared_ptr<device> device);
  bool poll(struct ibv_wc &wc);
  ~cq();
};

} // namespace rdmapp