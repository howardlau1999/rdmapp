#include "rdmapp/cq_poller.h"

#include <memory>
#include <stdexcept>

#include <infiniband/verbs.h>

#include "rdmapp/detail/debug.h"
#include "rdmapp/executor.h"

namespace rdmapp {

cq_poller::cq_poller(std::shared_ptr<cq> cq, size_t batch_size)
    : cq_poller(cq, std::make_shared<executor>(), batch_size) {}

cq_poller::cq_poller(std::shared_ptr<cq> cq, std::shared_ptr<executor> executor,
                     size_t batch_size)
    : cq_(cq), stopped_(false), executor_(executor), wc_vec_(batch_size),
      poller_thread_(&cq_poller::worker, this) {}

cq_poller::~cq_poller() {
  stopped_ = true;
  poller_thread_.join();
}

void cq_poller::worker() {
  while (!stopped_) {
    try {
      auto nr_wc = cq_->poll(wc_vec_);
      for (size_t i = 0; i < nr_wc; ++i) {
        auto &wc = wc_vec_[i];
        RDMAPP_LOG_TRACE("polled cqe wr_id=%p status=%d",
                         reinterpret_cast<void *>(wc.wr_id), wc.status);
        executor_->process_wc(wc);
      }
    } catch (std::runtime_error &e) {
      RDMAPP_LOG_ERROR("%s", e.what());
      stopped_ = true;
      return;
    } catch (executor::queue_closed_error &) {
      stopped_ = true;
      return;
    }
  }
}

} // namespace rdmapp