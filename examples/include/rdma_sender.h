#pragma once

#include "connector.h"
#include "rdma_sr.h"
#include "rdma_util.h"
#include "retransmitqueue.h"
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <rdmapp/rdmapp.h>

namespace RDMA_EC {

class RDMASender {
public:
  // Data connector is typically UC, control connector can be RC for ACKs.
  RDMASender(std::shared_ptr<rdmapp::connector> data_connector,
             std::shared_ptr<rdmapp::connector> ctrl_connector,
             const Config &config = Config{});

  // Send data to connected receiver
  rdmapp::task<void> send_data(const void *data, size_t size);

  // Get statistics
  size_t get_packets_sent() const { return packets_sent_; }
  size_t get_bytes_sent() const { return bytes_sent_; }

private:
  // Wait for CTS from receiver
  rdmapp::task<void> wait_for_cts();

  // Send a single chunk of packets
  rdmapp::task<void> send_chunk(size_t chunk_idx, const uint8_t *data,
                                size_t chunk_start_offset,
                                size_t packets_in_chunk);

  // Send a single packet
  rdmapp::task<void> send_packet(size_t packet_idx, const uint8_t *data,
                                 size_t offset, size_t packet_size);

  // Receive ACKs on the control QP (selective repeat).
  rdmapp::task<void> receive_acks(size_t num_chunks);

  // Separate connectors for data (UC) and control (RC) paths.
  std::shared_ptr<rdmapp::connector> data_connector_;
  std::shared_ptr<rdmapp::connector> ctrl_connector_;
  std::shared_ptr<rdmapp::qp> qp_;
  Config config_;

  // CTS information from receiver
  CTSInfo cts_info_;

  // Current message ID
  std::atomic<uint8_t> current_msg_id_{0};

  // Statistics
  std::atomic<size_t> packets_sent_{0};
  std::atomic<size_t> bytes_sent_{0};

  // Local memory region
  std::shared_ptr<rdmapp::local_mr> local_mr_;

  // --- Selective repeat (optional) ---
  // Control QP used for ACKs (created only if enable_selective_repeat is true).
  std::shared_ptr<rdmapp::qp> ctrl_qp_;

  // Retransmission queue for selective repeat
  std::unique_ptr<RetransmitQueue> retransmit_queue_;

  // Background thread for selective repeat
  // - ack_thread_: waits for ACKs on control QP and removes chunks from
  // retransmit queue
  std::thread ack_thread_;
  bool ack_thread_started_{false};
};

} // namespace RDMA_EC
