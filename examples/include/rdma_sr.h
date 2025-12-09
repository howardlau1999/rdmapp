#pragma once

#include <cstdint>

namespace RDMA_EC {

// Control-plane structures and constants for selective-repeat reliability.

// ACK message for a completed chunk.
// Sent over the control QP from receiver -> sender.
struct ChunkAck {
  uint8_t msg_id;     // Message identifier (matches data path msg_id)
  uint32_t chunk_idx; // Chunk index that has been fully received
};

} // namespace RDMA_EC
