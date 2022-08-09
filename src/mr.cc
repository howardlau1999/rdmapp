#include "rdmapp/mr.h"

#include <cstdint>

#include <infiniband/verbs.h>

#include "rdmapp/detail/debug.h"

namespace rdmapp {

mr<tags::mr::local>::mr(std::shared_ptr<pd> pd, struct ibv_mr *mr)
    : pd_(pd), mr_(mr) {}

mr<tags::mr::local>::~mr() {
  auto addr = mr_->addr;
  if (auto rc = ::ibv_dereg_mr(mr_); rc != 0) {
    RDMAPP_LOG_ERROR("failed to dereg mr %p addr=%p", mr_, addr);
  } else {
    RDMAPP_LOG_TRACE("dereg mr %p addr=%p", mr_, addr);
  }
}

void *mr<tags::mr::local>::addr() { return mr_->addr; }

uint32_t mr<tags::mr::local>::length() { return mr_->length; }

uint32_t mr<tags::mr::local>::rkey() { return mr_->rkey; }

uint32_t mr<tags::mr::local>::lkey() { return mr_->lkey; }

mr<tags::mr::remote>::mr(void *addr, uint32_t length, uint32_t rkey)
    : addr_(addr), length_(length), rkey_(rkey) {}

void *mr<tags::mr::remote>::addr() { return addr_; }

uint32_t mr<tags::mr::remote>::length() { return length_; }

uint32_t mr<tags::mr::remote>::rkey() { return rkey_; }

} // namespace rdmapp