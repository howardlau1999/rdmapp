#pragma once

#include <memory>

#include <infiniband/verbs.h>

#include "rdmapp/pd.h"

namespace rdmapp {

class srq {
  struct ibv_srq *srq_;
  std::shared_ptr<pd> pd_;
  friend class qp;

public:
  srq(std::shared_ptr<pd> pd, size_t max_wr = 1024);
  ~srq();
};

} // namespace rdmapp