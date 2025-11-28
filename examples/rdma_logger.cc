#include "rdma_logger.h"

namespace RDMA_EC {

std::atomic<bool> Logger::enabled_{true}; // Default to enabled

} // namespace RDMA_EC

