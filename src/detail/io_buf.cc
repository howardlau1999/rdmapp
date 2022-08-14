#include "rdmapp/detail/io_buf.h"

#include <cstddef>
#include <stdexcept>

namespace rdmapp {

namespace detail {

io_buf::io_buf() : read_idx_(0), write_idx_(0) {}

size_t io_buf::length() { return write_idx_ - read_idx_; }

size_t io_buf::read_idx() { return read_idx_; }

size_t io_buf::write_idx() { return write_idx_; }

void io_buf::consume(size_t n) {
  if (n > length()) {
    throw std::out_of_range("consume more bytes than available");
  }
  read_idx_ += n;
  if (read_idx_ == write_idx_) {
    read_idx_ = 0;
    write_idx_ = 0;
  }
}

void io_buf::append(uint8_t *data, size_t length) {
  auto available = buffer_.size() - write_idx_;
  if (available < length) {
    buffer_.resize(buffer_.size() + length);
  }
  std::copy_n(data, length, &buffer_[write_idx_]);
  write_idx_ += length;
}

io_buf::~io_buf() {}

} // namespace detail
} // namespace rdmapp