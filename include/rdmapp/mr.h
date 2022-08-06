#pragma once

#include <cstdint>
#include <memory>

#include <infiniband/verbs.h>

namespace rdmapp {
class pd;
class mr {
  struct ibv_mr *mr_;
  std::shared_ptr<pd> pd_;

public:
  mr(std::shared_ptr<pd> pd, struct ibv_mr *mr);
  ~mr();
  void *addr();
  uint32_t length();
  uint32_t rkey();
  uint32_t lkey();
};

} // namespace rdmapp