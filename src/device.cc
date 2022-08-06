#include "rdmapp/device.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include <infiniband/verbs.h>

#include "rdmapp/detail/debug.h"
#include "rdmapp/error.h"

namespace rdmapp {

device::device(uint16_t device_num, uint16_t port_num) : device_(nullptr) {
  int32_t nr_devices = 0;
  struct ::ibv_device **ibv_device_list = ibv_get_device_list(&nr_devices);
  if (nr_devices == 0) {
    throw std::runtime_error("no Infiniband devices found");
  }
  check_ptr(ibv_device_list, "failed to get Infiniband devices");
  if (device_num >= nr_devices) {
    char buffer[256] = {0};
    ::snprintf(buffer, sizeof(buffer),
               "requested device number %d out of range, %d devices available",
               device_num, nr_devices);
    throw std::invalid_argument(buffer);
  }
  device_ = ibv_device_list[device_num];
  ctx_ = ::ibv_open_device(device_);
  check_ptr(ctx_, "failed to open device");
  ::ibv_free_device_list(ibv_device_list);
  check_rc(::ibv_query_port(ctx_, port_num, &port_attr_),
           "failed to query port");
  port_num_ = port_num;
  const char *link_layer = "unspecified";
  if (port_attr_.link_layer == IBV_LINK_LAYER_ETHERNET) {
    link_layer = "ethernet";
  } else if (port_attr_.link_layer == IBV_LINK_LAYER_INFINIBAND) {
    link_layer = "infiniband";
  }
  RDMAPP_LOG_DEBUG("opened Inifiband device lid=%d link_layer=%s",
                   port_attr_.lid, link_layer);
}

uint16_t device::port_num() { return port_num_; }

uint16_t device::lid() { return port_attr_.lid; }

device::~device() {
  if (ctx_) {
    if (auto rc = ::ibv_close_device(ctx_); rc != 0) {
      RDMAPP_LOG_ERROR("failed to close device lid=%d: %s", port_attr_.lid,
                       strerror(rc));
    } else {
      RDMAPP_LOG_DEBUG("closed device lid=%d", port_attr_.lid);
    }
  }
}

} // namespace rdmapp