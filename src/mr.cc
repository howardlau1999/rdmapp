#include "rdmapp/mr.h"

#include <cstdint>
#include <utility>
#include <vector>

#include <infiniband/verbs.h>

#include "rdmapp/detail/debug.h"
#include "rdmapp/detail/serdes.h"

namespace rdmapp {

local_mr::mr(std::shared_ptr<pd> pd, struct ibv_mr *mr) : mr_(mr), pd_(pd) {}

local_mr::mr(local_mr &&other)
    : mr_(std::exchange(other.mr_, nullptr)), pd_(std::move(other.pd_)) {}

local_mr &local_mr::operator=(local_mr &&other) {
  mr_ = other.mr_;
  pd_ = std::move(other.pd_);
  other.mr_ = nullptr;
  return *this;
}

local_mr::~mr() {
  if (mr_ == nullptr) [[unlikely]] {
    // This mr is moved.
    return;
  }
  auto addr = mr_->addr;
  if (auto rc = ::ibv_dereg_mr(mr_); rc != 0) [[unlikely]] {
    RDMAPP_LOG_ERROR("failed to dereg mr %p addr=%p",
                     reinterpret_cast<void *>(mr_), addr);
  } else {
    RDMAPP_LOG_TRACE("dereg mr %p addr=%p", reinterpret_cast<void *>(mr_),
                     addr);
  }
}

std::vector<uint8_t> local_mr::serialize() const {
  std::vector<uint8_t> buffer;
  auto it = std::back_inserter(buffer);
  detail::serialize(reinterpret_cast<uint64_t>(mr_->addr), it);
  detail::serialize(mr_->length, it);
  detail::serialize(mr_->rkey, it);
  return buffer;
}

void *local_mr::addr() const { return mr_->addr; }

size_t local_mr::length() const { return mr_->length; }

uint32_t local_mr::rkey() const { return mr_->rkey; }

uint32_t local_mr::lkey() const { return mr_->lkey; }

remote_mr::mr(void *addr, uint32_t length, uint32_t rkey)
    : addr_(addr), length_(length), rkey_(rkey) {}

void *remote_mr::addr() { return addr_; }

uint32_t remote_mr::length() { return length_; }

uint32_t remote_mr::rkey() { return rkey_; }

} // namespace rdmapp