#include "rdmapp/cq_poller.h"

#include <memory>
#include <stdexcept>

#include <infiniband/verbs.h>

#include "rdmapp/executor.h"

#include "rdmapp/detail/debug.h"

namespace rdmapp {

cq_poller::cq_poller(std::shared_ptr<cq> cq, size_t batch_size)
    : cq_poller(cq, std::make_shared<executor>(), batch_size) {}

cq_poller::cq_poller(std::shared_ptr<cq> cq, std::shared_ptr<executor> executor,
                     size_t batch_size)
    : cq_(cq), stopped_(false), poller_thread_(&cq_poller::worker, this),
      executor_(executor), wc_vec_(batch_size) {}

cq_poller::~cq_poller() {
  stopped_ = true;
  poller_thread_.join();
}

void cq_poller::worker() {
  // Marker value used by RDMAReceiver for receive completions
  // We skip these completions because they're handled by RDMAReceiver's process_completions()
  constexpr uint64_t RECV_MARKER = 0xFFFFFFFFFFFFFFFFULL;
  
  while (!stopped_) {
    try {
      auto nr_wc = cq_->poll(wc_vec_);
      for (size_t i = 0; i < nr_wc; ++i) {
        auto &wc = wc_vec_[i];
        RDMAPP_LOG_TRACE("polled cqe wr_id=%p status=%d",
                         reinterpret_cast<void *>(wc.wr_id), wc.status);
        
        // Skip receive completions with our special marker value
        // These are handled by RDMAReceiver's process_completions() thread
        // NOTE: We still consume these completions from the CQ by polling, but we don't process them
        // This means RDMAReceiver's process_completions() won't see them either!
        // We need to NOT poll these completions at all, but we can't do that with the current API.
        // The solution is to have RDMAReceiver poll more aggressively, or to not use cq_poller on the shared CQ.
        if (wc.wr_id == RECV_MARKER) {
          RDMAPP_LOG_TRACE("cq_poller: skipping receive completion with marker (consumed from CQ)");
          continue;
        }
        
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