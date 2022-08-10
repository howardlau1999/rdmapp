#pragma once

namespace rdmapp {

class noncopyable {
public:
  noncopyable() = default;
  noncopyable(noncopyable &&) = default;
  noncopyable(noncopyable const &) = delete;
  noncopyable &operator=(noncopyable const &) = delete;
  noncopyable &operator=(noncopyable &&) = default;
};

} // namespace rdmapp