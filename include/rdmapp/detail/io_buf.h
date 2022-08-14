#pragma once

#include <cstdint>
#include <vector>

namespace rdmapp {
namespace detail {

class io_buf {
  std::vector<uint8_t> buffer_;
  size_t read_idx_;
  size_t write_idx_;

public:
  io_buf();
  size_t length();
  size_t read_idx();
  size_t write_idx();
  void consume(size_t n);
  void append(uint8_t *data, size_t length);
  ~io_buf();
};

} // namespace detail
} // namespace rdmapp