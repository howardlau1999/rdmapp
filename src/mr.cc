#include "rdmapp/mr.h"

#include <cstdint>

#include <infiniband/verbs.h>

#include "rdmapp/detail/debug.h"

namespace rdmapp {

mr::mr(std::shared_ptr<pd> pd, struct ibv_mr *mr) : pd_(pd), mr_(mr) {}

mr::~mr() {
  auto addr = mr_->addr;
  if (auto rc = ::ibv_dereg_mr(mr_); rc != 0) {
    RDMAPP_LOG_ERROR("failed to dereg mr %p addr=%p", mr_, addr);
  } else {
    RDMAPP_LOG_TRACE("dereg mr %p addr=%p", mr_, addr);
  }
}

void *mr::addr() { return mr_->addr; }

uint32_t mr::length() { return mr_->length; }

uint32_t mr::lkey() { return mr_->lkey; }

uint32_t mr::rkey() { return mr_->rkey; }

} // namespace rdmapp