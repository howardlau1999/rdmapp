#pragma once

#include <coroutine>
#include <functional>
#include <memory>

namespace rdmapp {
namespace socket {

class event_loop;

/**
 * @brief This class represents a pollable channel.
 *
 */
class channel : public std::enable_shared_from_this<channel> {
public:
  static std::function<void()> noop_callback;
  using callback_fn = std::function<void()>;

private:
  int fd_;
  std::shared_ptr<event_loop> loop_;
  callback_fn readable_callback_;
  callback_fn writable_callback_;

public:
  static void set_nonblocking(int fd);
  channel(int fd, std::shared_ptr<event_loop> loop);
  int fd();
  void set_nonblocking();
  void wait_readable();
  void wait_writable();
  void readable_callback();
  void writable_callback();
  void set_readable_callback(callback_fn &&callback);
  void set_writable_callback(callback_fn &&callback);
  std::shared_ptr<event_loop> loop();
  ~channel();
};

} // namespace socket
} // namespace rdmapp