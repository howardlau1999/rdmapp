#pragma once

#include <cstdint>
#include <memory>

#include <infiniband/verbs.h>

namespace rdmapp {

namespace tags {
namespace mr {
struct local {};
struct remote {};
} // namespace mr
} // namespace tags

class pd;

template<class Tag>
class mr;

template<>
class mr<tags::mr::local> {
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

template<>
class mr<tags::mr::remote> {
  void *addr_;
  uint32_t length_;
  uint32_t rkey_;
public:
  mr() = default;
  mr(void *addr, uint32_t length, uint32_t rkey);
  mr(mr<tags::mr::remote> const& other) = default;
  void *addr();
  uint32_t length();
  uint32_t rkey();
};


} // namespace rdmapp