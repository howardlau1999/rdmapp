#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <infiniband/verbs.h>

#include "rdmapp/detail/noncopyable.h"
#include "rdmapp/detail/serdes.h"

namespace rdmapp {

namespace tags {
namespace mr {
struct local {};
struct remote {};
} // namespace mr
} // namespace tags

class pd;

template <class Tag> class mr;

template <> class mr<tags::mr::local> : public noncopyable {
  struct ibv_mr *mr_;
  std::shared_ptr<pd> pd_;

public:
  mr(std::shared_ptr<pd> pd, struct ibv_mr *mr);
  mr(mr<tags::mr::local> &&other);
  mr<tags::mr::local> &operator=(mr<tags::mr::local> &&other);
  ~mr();
  std::vector<uint8_t> serialize() const;
  void *addr() const;
  size_t length() const;
  uint32_t rkey() const;
  uint32_t lkey() const;
};

template <> class mr<tags::mr::remote> {
  void *addr_;
  size_t length_;
  uint32_t rkey_;

public:
  static constexpr size_t kSerializedSize =
      sizeof(addr_) + sizeof(length_) + sizeof(rkey_);
  mr() = default;
  mr(void *addr, uint32_t length, uint32_t rkey);
  mr(mr<tags::mr::remote> const &other) = default;
  void *addr();
  uint32_t length();
  uint32_t rkey();
  template <class It> static mr<tags::mr::remote> deserialize(It it) {
    mr<tags::mr::remote> remote_mr;
    detail::deserialize(it, reinterpret_cast<uint64_t &>(remote_mr.addr_));
    detail::deserialize(it, remote_mr.length_);
    detail::deserialize(it, remote_mr.rkey_);
    return remote_mr;
  }
};

using local_mr = mr<tags::mr::local>;
using remote_mr = mr<tags::mr::remote>;

} // namespace rdmapp