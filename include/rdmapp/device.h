#pragma once

#include <cstdint>
#include <iterator>
#include <string>

#include <infiniband/verbs.h>

#include "rdmapp/detail/noncopyable.h"

namespace rdmapp {

/**
 * @brief This class holds a list of devices available on the system.
 *
 */
class device_list : public noncopyable {
  struct ibv_device **devices_;
  size_t nr_devices_;

public:
  class iterator
      : public std::iterator<std::forward_iterator_tag, struct ibv_device *> {
    friend class device_list;
    size_t i_;
    struct ibv_device **devices_;
    iterator(struct ibv_device **devices, size_t i);

  public:
    struct ibv_device *&operator*();
    bool operator==(device_list::iterator const &other) const;
    bool operator!=(device_list::iterator const &other) const;
    device_list::iterator &operator++();
    device_list::iterator &operator++(int);
  };
  device_list();
  size_t size();
  struct ibv_device *at(size_t i);
  iterator begin();
  iterator end();
  ~device_list();
};

/**
 * @brief This class is an abstraction of an Infiniband device.
 *
 */
class device : public noncopyable {
  struct ibv_device *device_;
  struct ibv_context *ctx_;
  struct ibv_port_attr port_attr_;
  struct ibv_device_attr_ex device_attr_ex_;
  union ibv_gid gid_;

  int gid_index_;
  uint16_t port_num_;
  friend class pd;
  friend class cq;
  friend class qp;
  friend class srq;
  void open_device(struct ibv_device *target, uint16_t port_num);

public:
  /**
   * @brief Construct a new device object.
   *
   * @param target The target device.
   * @param port_num The port number of the target device.
   */
  device(struct ibv_device *target, uint16_t port_num = 1);

  /**
   * @brief Construct a new device object.
   *
   * @param device_name The name of the target device.
   * @param port_num The port number of the target device.
   */
  device(std::string const &device_name, uint16_t port_num = 1);

  /**
   * @brief Construct a new device object.
   *
   * @param device_num The index of the target device.
   * @param port_num The port number of the target device.
   */
  device(uint16_t device_num = 0, uint16_t port_num = 1);

  /**
   * @brief Get the device port number.
   *
   * @return uint16_t The port number.
   */
  uint16_t port_num() const;

  /**
   * @brief Get the lid of the device.
   *
   * @return uint16_t The lid.
   */
  uint16_t lid() const;

  /**
   * @brief Get the gid of the device.
   *
   * @return union ibv_gid The gid.
   */
  union ibv_gid gid() const;

  /**
   * @brief Checks if the device supports fetch and add.
   *
   * @return true Supported.
   * @return false Not supported.
   */
  bool is_fetch_and_add_supported() const;

  /**
   * @brief Checks if the device supports compare and swap.
   *
   * @return true Supported.
   * @return false Not supported.
   */
  bool is_compare_and_swap_supported() const;

  int gid_index() const;

  static std::string gid_hex_string(union ibv_gid const &gid);

  ~device();
};

} // namespace rdmapp