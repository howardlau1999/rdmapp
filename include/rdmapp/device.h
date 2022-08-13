#pragma once

#include <cstdint>
#include <iterator>
#include <string>

#include <infiniband/verbs.h>

#include "rdmapp/detail/noncopyable.h"

namespace rdmapp {

class device_list : public noncopyable {
  struct ibv_device **devices_;
  int32_t nr_devices_;

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

class device : public noncopyable {
  struct ibv_device *device_;
  struct ibv_context *ctx_;
  struct ibv_port_attr port_attr_;
  struct ibv_device_attr attr_;
  struct ibv_device_attr_ex attr_ex_;

  uint16_t port_num_;
  friend class pd;
  friend class cq;
  friend class qp;
  friend class srq;
  void open_device(struct ibv_device *target, uint16_t port_num);

public:
  device(struct ibv_device *target, uint16_t port_num = 1);
  device(std::string const &device_name, uint16_t port_num = 1);
  device(uint16_t device_num = 0, uint16_t port_num = 1);
  uint16_t port_num();
  uint16_t lid();
  bool is_fetch_and_add_supported();
  bool is_compare_and_swap_supported();
  bool is_swap_supported();
  ~device();
};

} // namespace rdmapp