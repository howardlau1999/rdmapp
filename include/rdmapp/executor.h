#pragma once

#include <functional>
#include <vector>

#include <infiniband/verbs.h>

#include "rdmapp/detail/blocking_queue.h"

namespace rdmapp {

class executor {
  using work_queue = detail::blocking_queue<struct ibv_wc>;
  std::vector<std::thread> workers_;
  work_queue work_queue_;
  void worker_fn(size_t worker_id);

public:
  using queue_closed_error = work_queue::queue_closed_error;
  using callback_fn = std::function<void(struct ibv_wc const &wc)>;
  using callback_ptr = callback_fn *;
  executor(size_t nr_worker = 4);
  void process_wc(struct ibv_wc const &wc);
  void shutdown();
  ~executor();

  template <class T> static callback_ptr make_callback(T const &cb) {
    return new executor::callback_fn(cb);
  }
};

} // namespace rdmapp