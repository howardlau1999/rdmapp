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
  std::thread worker_thread_;
  std::shared_ptr<executor> executor_;
  void worker();

public:
  cq_poller(std::shared_ptr<cq> cq);
  cq_poller(std::shared_ptr<cq> cq, std::shared_ptr<executor> executor);
  ~cq_poller();
};

} // namespace rdmapp