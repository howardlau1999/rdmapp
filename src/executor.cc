#include "rdmapp/executor.h"

#include "rdmapp/detail/blocking_queue.h"
#include "rdmapp/detail/debug.h"

namespace rdmapp {

executor::executor(size_t nr_worker) {
  for (size_t i = 0; i < nr_worker; ++i) {
    workers_.emplace_back(&executor::worker_fn, this, i);
  }
}

void executor::worker_fn(size_t worker_id) {
  try {
    while (true) {
      auto wc = work_queue_.pop();
      auto cb = reinterpret_cast<callback_ptr>(wc.wr_id);
      (*cb)(wc);
      destroy_callback(cb);
    }
  } catch (work_queue::queue_closed_error &) {
    RDMAPP_LOG_TRACE("executor worker %lu exited", worker_id);
  }
}

void executor::process_wc(struct ibv_wc const &wc) { work_queue_.push(wc); }

void executor::shutdown() { work_queue_.close(); }

void executor::destroy_callback(callback_ptr cb) { delete cb; }

executor::~executor() {
  shutdown();
  for (auto &&worker : workers_) {
    worker.join();
  }
}

} // namespace rdmapp