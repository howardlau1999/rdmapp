#pragma once

#include "rdma_util.h"
#include "connector.h"
#include <rdmapp/rdmapp.h>
#include <memory>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

namespace RDMA_EC {

class RDMAReceiver {
public:
    RDMAReceiver(std::shared_ptr<rdmapp::connector> connector,
                 std::shared_ptr<rdmapp::cq> recv_cq,
                 const Config& config = Config{});
    ~RDMAReceiver();
    
    // Receive data from sender
    rdmapp::task<std::vector<uint8_t>> receive_data(size_t expected_size);
    
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
    
    std::shared_ptr<rdmapp::connector> connector_;
    std::shared_ptr<rdmapp::qp> qp_;
    std::shared_ptr<rdmapp::cq> recv_cq_;
    Config config_;
    
    // Receive buffer
    std::vector<uint8_t> recv_buffer_;
    std::shared_ptr<rdmapp::local_mr> local_mr_;
    
    // Packet tracking bitmaps
    std::vector<std::atomic<uint16_t>> packet_bitmap_;
    std::atomic<uint64_t> chunk_bitmap_{0};
    size_t total_packets_{0};
    size_t total_chunks_{0};
    size_t expected_size_{0};
    
    // Background threads for processing
    std::thread completion_thread_;
    std::thread frontend_thread_;
    std::atomic<bool> stop_thread_{false};
    
    // Synchronization
    mutable std::mutex completion_mutex_;
    std::condition_variable completion_cv_;
    std::atomic<bool> reception_complete_{false};
    
    // Message ID
    uint16_t current_msg_id_{0};
    
    // Statistics
    std::atomic<size_t> packets_received_{0};
    std::atomic<size_t> bytes_received_{0};
    
    // Dummy buffer for receives (immediate data comes out-of-band)
    std::vector<uint8_t> dummy_recv_buffer_;
    std::shared_ptr<rdmapp::local_mr> dummy_recv_mr_;
};

} // namespace RDMA_EC
