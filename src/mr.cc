#include "rdmapp/mr.h"

#include <cstdint>
#include <vector>

#include <infiniband/verbs.h>

#include "rdmapp/detail/debug.h"
#include "rdmapp/detail/serdes.h"

namespace rdmapp {

mr<tags::mr::local>::mr(std::shared_ptr<pd> pd, struct ibv_mr *mr)
    : pd_(pd), mr_(mr) {}

mr<tags::mr::local>::mr(mr<tags::mr::local> &&other)
    : mr_(other.mr_), pd_(std::move(other.pd_)) {
  other.mr_ = nullptr;
}

mr<tags::mr::local> &
mr<tags::mr::local>::operator=(mr<tags::mr::local> &&other) {
  mr_ = other.mr_;
  pd_ = std::move(other.pd_);
  other.mr_ = nullptr;
  return *this;
}

mr<tags::mr::local>::~mr() {
  auto addr = mr_->addr;
  if (auto rc = ::ibv_dereg_mr(mr_); rc != 0) {
    RDMAPP_LOG_ERROR("failed to dereg mr %p addr=%p", mr_, addr);
  } else {
    RDMAPP_LOG_TRACE("dereg mr %p addr=%p", mr_, addr);
  }
}

std::vector<uint8_t> mr<tags::mr::local>::serialize() const {
  std::vector<uint8_t> buffer;
  auto it = std::back_inserter(buffer);
  detail::serialize(reinterpret_cast<uint64_t>(mr_->addr), it);
  detail::serialize(mr_->length, it);
  detail::serialize(mr_->rkey, it);
  return buffer;
}

void *mr<tags::mr::local>::addr() const { return mr_->addr; }

size_t mr<tags::mr::local>::length() const { return mr_->length; }

uint32_t mr<tags::mr::local>::rkey() const { return mr_->rkey; }

uint32_t mr<tags::mr::local>::lkey() const { return mr_->lkey; }

mr<tags::mr::remote>::mr(void *addr, uint32_t length, uint32_t rkey)
    : addr_(addr), length_(length), rkey_(rkey) {}

void *mr<tags::mr::remote>::addr() { return addr_; }

uint32_t mr<tags::mr::remote>::length() { return length_; }

uint32_t mr<tags::mr::remote>::rkey() { return rkey_; }

} // namespace rdmapp