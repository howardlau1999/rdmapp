#pragma once

#include "acceptor.h"
#include "rdma_util.h"
#include "rdma_sr.h"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <chrono>

#include <rdmapp/rdmapp.h>

namespace RDMA_EC {

class RDMAReceiver {
public:
  // data_acceptor is typically UC; ctrl_acceptor can be RC for ACKs.
  RDMAReceiver(std::shared_ptr<rdmapp::acceptor> data_acceptor,
               std::shared_ptr<rdmapp::acceptor> ctrl_acceptor,
               std::shared_ptr<rdmapp::cq> recv_cq,
               const Config &config = Config{});
  ~RDMAReceiver();

  // Receive data from sender
  rdmapp::task<void> receive_data(size_t expected_size);

  // Get statistics
  size_t get_packets_received() const { return packets_received_; }
  size_t get_bytes_received() const { return bytes_received_; }

private:
  // Send CTS to sender
  rdmapp::task<void> send_cts(size_t buffer_size);

  // Post receive requests for immediates (initial batch)
  rdmapp::task<void> post_receives(size_t count);

  // Post a single receive (used to repost after consumption)
  void post_single_receive();

  // Process incoming completions (runs in background thread)
  void process_completions();

  // Frontend polling thread that updates chunk bitmap
  void frontend_poller();

  // Check if reception is complete
  bool is_complete() const;

  // Send ACKs for completed chunks over the control QP (selective repeat).
  rdmapp::task<void> send_acks();

  // Separate acceptors for data (UC) and control (RC) paths.
  std::shared_ptr<rdmapp::acceptor> data_acceptor_;
  std::shared_ptr<rdmapp::acceptor> ctrl_acceptor_;
  std::shared_ptr<rdmapp::qp> qp_;
  std::shared_ptr<rdmapp::qp> ctrl_qp_;  // optional control QP for ACKs
  std::shared_ptr<rdmapp::cq> recv_cq_;
  Config config_;

  // Receive buffer (page-aligned for RDMA)
  void *recv_buffer_{
      nullptr}; // Page-aligned buffer allocated with posix_memalign
  size_t recv_buffer_size_{
      0}; // Actual allocated size (may be larger than requested)
  std::shared_ptr<rdmapp::local_mr> local_mr_;

  // Packet tracking bitmaps
  std::vector<std::atomic<uint16_t>>
      packet_bitmap_; // templating stuff here maybe
  // Chunk completion bitmap: one bit per chunk, packed into uint8_t (8 chunks per byte)
  std::vector<std::atomic<uint8_t>> chunk_bitmap_;
  size_t total_packets_{0};
  size_t total_chunks_{0};
  size_t expected_size_{0};

  // Track which chunks we've already ACKed (to avoid duplicates)
  std::vector<bool> chunk_acked_;

  // Number of chunks for which we've successfully sent an ACK.
  std::atomic<size_t> chunks_acked_{0};

  // Background threads for processing
  std::thread completion_thread_;
  std::thread frontend_thread_;
  std::atomic<bool> stop_thread_{false};

  // Synchronization
  mutable std::mutex completion_mutex_;
  std::condition_variable completion_cv_;
  std::atomic<bool> reception_complete_{false};

  // Message ID
  uint8_t current_msg_id_{0};

  // Statistics
  std::atomic<size_t> packets_received_{0};
  std::atomic<size_t> bytes_received_{0};

  // Dummy buffer for receives (immediate data comes out-of-band)
  std::vector<uint8_t> oob_buffer_;
  std::shared_ptr<rdmapp::local_mr> oob_buffer_mr_;
};

} // namespace RDMA_EC
