#include "rdmapp/cq_poller.h"

#include <memory>
#include <stdexcept>

#include <infiniband/verbs.h>

#include "rdmapp/detail/debug.h"
#include "rdmapp/executor.h"

namespace rdmapp {

cq_poller::cq_poller(std::shared_ptr<cq> cq)
    : cq_poller(cq, std::make_shared<executor>()) {}

cq_poller::cq_poller(std::shared_ptr<cq> cq, std::shared_ptr<executor> executor)
    : cq_(cq), stopped_(false), executor_(executor),
      worker_thread_(&cq_poller::worker, this) {}

cq_poller::~cq_poller() {
  stopped_ = true;
  worker_thread_.join();
}

void cq_poller::worker() {
  struct ibv_wc wc;
  while (!stopped_) {
    try {
      if (cq_->poll(wc)) {
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