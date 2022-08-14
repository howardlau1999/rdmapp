#pragma once

#include <memory>

#include <infiniband/verbs.h>

#include "rdmapp/device.h"
#include "rdmapp/mr.h"

#include "rdmapp/detail/noncopyable.h"

namespace rdmapp {

class qp;

/**
 * @brief This class is an abstraction of a Protection Domain.
 *
 */
class pd : public noncopyable, public std::enable_shared_from_this<pd> {
  std::shared_ptr<device> device_;
  struct ibv_pd *pd_;
  friend class qp;
  friend class srq;

public:
  /**
   * @brief Construct a new pd object
   *
   * @param device The device to use.
   */
  pd(std::shared_ptr<device> device);

  /**
   * @brief Get the device object pointer.
   *
   * @return std::shared_ptr<device> The device object pointer.
   */
  std::shared_ptr<device> device_ptr() const;

  /**
   * @brief Register a local memory region.
   *
   * @param addr The address of the memory region.
   * @param length The length of the memory region.
   * @param flags The access flags to use.
   * @return local_mr The local memory region handle.
   */
  local_mr reg_mr(void *addr, size_t length,
                  int flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE |
                              IBV_ACCESS_REMOTE_READ |
                              IBV_ACCESS_REMOTE_ATOMIC);
  /**
   * @brief Destroy the pd object and the associated protection domain.
   *
   */
  ~pd();
};

} // namespace rdmapp