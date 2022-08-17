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

/**
 * @brief A remote or local memory region.
 *
 * @tparam Tag Either `tags::mr::local` or `tags::mr::remote`.
 */
template <class Tag> class mr;

/**
 * @brief Represents a local memory region.
 *
 */
template <> class mr<tags::mr::local> : public noncopyable {
  struct ibv_mr *mr_;
  std::shared_ptr<pd> pd_;

public:
  /**
   * @brief Construct a new mr object
   *
   * @param pd The protection domain to use.
   * @param mr The ibverbs memory region handle.
   */
  mr(std::shared_ptr<pd> pd, struct ibv_mr *mr);

  /**
   * @brief Move construct a new mr object
   *
   * @param other The other mr object to move from.
   */
  mr(mr<tags::mr::local> &&other);

  /**
   * @brief Move assignment operator.
   *
   * @param other The other mr to move from.
   * @return mr<tags::mr::local>& This mr.
   */
  mr<tags::mr::local> &operator=(mr<tags::mr::local> &&other);

  /**
   * @brief Destroy the mr object and deregister the memory region.
   *
   */
  ~mr();

  /**
   * @brief Serialize the memory region handle to be sent to a remote peer.
   *
   * @return std::vector<uint8_t> The serialized memory region handle.
   */
  std::vector<uint8_t> serialize() const;

  /**
   * @brief Get the address of the memory region.
   *
   * @return void* The address of the memory region.
   */
  void *addr() const;

  /**
   * @brief Get the length of the memory region.
   *
   * @return size_t The length of the memory region.
   */
  size_t length() const;

  /**
   * @brief Get the remote key of the memory region.
   *
   * @return uint32_t The remote key of the memory region.
   */
  uint32_t rkey() const;

  /**
   * @brief Get the local key of the memory region.
   *
   * @return uint32_t The local key of the memory region.
   */
  uint32_t lkey() const;
};

/**
 * @brief Represents a remote memory region.
 *
 */
template <> class mr<tags::mr::remote> {
  void *addr_;
  size_t length_;
  uint32_t rkey_;

public:
  /**
   * @brief The size of a serialized remote memory region.
   *
   */
  static constexpr size_t kSerializedSize =
      sizeof(addr_) + sizeof(length_) + sizeof(rkey_);

  mr() = default;

  /**
   * @brief Construct a new remote mr object
   *
   * @param addr The address of the remote memory region.
   * @param length The length of the remote memory region.
   * @param rkey The remote key of the remote memory region.
   */
  mr(void *addr, uint32_t length, uint32_t rkey);

  /**
   * @brief Construct a new remote mr object copied from another
   *
   * @param other The other remote mr object to copy from.
   */
  mr(mr<tags::mr::remote> const &other) = default;

  /**
   * @brief Get the address of the remote memory region.
   *
   * @return void* The address of the remote memory region.
   */
  void *addr();

  /**
   * @brief Get the length of the remote memory region.
   *
   * @return uint32_t The length of the remote memory region.
   */
  uint32_t length();

  /**
   * @brief Get the remote key of the memory region.
   *
   * @return uint32_t The remote key of the memory region.
   */
  uint32_t rkey();

  /**
   * @brief Deserialize a remote memory region handle.
   *
   * @tparam It The iterator type.
   * @param it The iterator to deserialize from.
   * @return mr<tags::mr::remote> The deserialized remote memory region handle.
   */
  template <class It> static mr<tags::mr::remote> deserialize(It it) {
    mr<tags::mr::remote> remote_mr;
    detail::deserialize(it, remote_mr.addr_);
    detail::deserialize(it, remote_mr.length_);
    detail::deserialize(it, remote_mr.rkey_);
    return remote_mr;
  }
};

using local_mr = mr<tags::mr::local>;
using remote_mr = mr<tags::mr::remote>;

} // namespace rdmapp