#pragma once

#include <cstdint>

#include <infiniband/verbs.h>

#include "rdmapp/detail/noncopyable.h"

namespace rdmapp {

class device : public noncopyable {
  struct ibv_device *device_;
  struct ibv_context *ctx_;
  struct ibv_port_attr port_attr_;

  uint16_t port_num_;
  friend class pd;
  friend class cq;
  friend class qp;
  friend class srq;

public:
  device(uint16_t device_num = 0, uint16_t port_num = 1);
  uint16_t port_num();
  uint16_t lid();
  ~device();
};

} // namespace rdmapp