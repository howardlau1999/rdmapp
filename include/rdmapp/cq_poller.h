#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

#include <infiniband/verbs.h>

#include "rdmapp/cq.h"
#include "rdmapp/executor.h"

namespace rdmapp {

class cq_poller {
  std::shared_ptr<cq> cq_;
  std::atomic<bool> stopped_;
  std::thread poller_thread_;
  std::shared_ptr<executor> executor_;
  std::vector<struct ibv_wc> wc_vec_;
  void worker();

public:
  cq_poller(std::shared_ptr<cq> cq, size_t batch_size = 16);
  cq_poller(std::shared_ptr<cq> cq, std::shared_ptr<executor> executor, size_t batch_size = 16);
  ~cq_poller();
};

} // namespace rdmapp