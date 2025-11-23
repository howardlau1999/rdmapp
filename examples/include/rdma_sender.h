#pragma once

#include "rdma_util.h"
#include "acceptor.h"
#include <rdmapp/rdmapp.h>
#include <memory>
#include <vector>

namespace RDMA_EC {

class RDMASender {
public:
    RDMASender(std::shared_ptr<rdmapp::acceptor> acceptor, 
               const Config& config = Config{});
    
    // Send data to connected receiver
    rdmapp::task<void> send_data(const void* data, size_t size);
    
    // Get statistics
    size_t get_packets_sent() const { return packets_sent_; }
    size_t get_bytes_sent() const { return bytes_sent_; }
    
private:
    // Wait for CTS from receiver
    rdmapp::task<void> wait_for_cts();
    
    // Send a single chunk of packets
    rdmapp::task<void> send_chunk(size_t chunk_idx, 
                                   const uint8_t* data,
                                   size_t chunk_start_offset,
                                   size_t packets_in_chunk);
    
    // Send a single packet
    rdmapp::task<void> send_packet(size_t packet_idx,
                                    const uint8_t* data,
                                    size_t offset,
                                    size_t packet_size);
    
    std::shared_ptr<rdmapp::acceptor> acceptor_;
    std::shared_ptr<rdmapp::qp> qp_;
    Config config_;
    
    // CTS information from receiver
    CTSInfo cts_info_;
   
    // Current message ID
    std::atomic<uint16_t> current_msg_id_{0};
    
    // Statistics
    std::atomic<size_t> packets_sent_{0};
    std::atomic<size_t> bytes_sent_{0};
    
    // Local memory region
    std::shared_ptr<rdmapp::local_mr> local_mr_;
};

} // namespace RDMA_EC
