#include "rdma_sender.h"
#include <iostream>
#include <cstring>

namespace RDMA_EC {

RDMASender::RDMASender(std::shared_ptr<rdmapp::acceptor> acceptor, 
                       const Config& config)
    : acceptor_(acceptor), config_(config) {
    std::cout << "Sender: Initialized with MTU=" << config_.mtu 
              << ", chunk_size=" << config_.chunk_size << std::endl;
}

rdmapp::task<void> RDMASender::send_data(const void* data, size_t size) {
    // Accept connection from receiver
    std::cout << "Sender: Waiting for connection..." << std::endl;
    qp_ = co_await acceptor_->accept();
    std::cout << "Sender: Connection accepted" << std::endl;
    
    // Wait for CTS message from receiver
    co_await wait_for_cts();
    std::cout << "Sender: Received CTS - remote_addr=0x" << std::hex 
              << cts_info_.remote_addr << ", rkey=0x" << cts_info_.rkey
              << std::dec << ", packets=" << cts_info_.total_packets << std::endl;
    
    // Register local memory
    auto pd = qp_->pd_ptr();
    local_mr_ = std::make_shared<rdmapp::local_mr>(
        pd->reg_mr(const_cast<void*>(data), size));
    
    // Calculate segmentation
    const uint8_t* data_ptr = static_cast<const uint8_t*>(data);
    size_t num_packets = calculate_num_packets(size, config_.mtu);
    size_t num_chunks = calculate_num_chunks(num_packets, config_.chunk_size);
    
    // Note: current_msg_id_ is set from CTS message, not incremented here
    
    std::cout << "Sender: Sending " << size << " bytes in " 
              << num_packets << " packets across " 
              << num_chunks << " chunks" << std::endl;
    
    // Send all chunks
    for (size_t chunk_idx = 0; chunk_idx < num_chunks; ++chunk_idx) {
        size_t chunk_start_offset = chunk_idx * config_.chunk_size * config_.mtu;
        size_t packets_in_chunk = std::min(config_.chunk_size,
                                          num_packets - chunk_idx * config_.chunk_size);
        
        co_await send_chunk(chunk_idx, data_ptr, chunk_start_offset, packets_in_chunk);
    }
    
    // Update statistics
    packets_sent_ += num_packets;
    bytes_sent_ += size;
    
    std::cout << "Sender: Transfer complete. Sent " << num_packets 
              << " packets (" << size << " bytes)" << std::endl;
    
    co_return;
}

rdmapp::task<void> RDMASender::wait_for_cts() {
    // Receive CTS message from receiver
    auto [bytes, imm_opt] = co_await qp_->recv(&cts_info_, sizeof(CTSInfo));
    
    if (bytes != sizeof(CTSInfo)) {
        throw std::runtime_error("Invalid CTS message size");
    }
    
    // Store message ID from CTS
    current_msg_id_ = cts_info_.msg_id;
    
    co_return;
}

rdmapp::task<void> RDMASender::send_chunk(size_t chunk_idx,
                                          const uint8_t* data,
                                          size_t /* chunk_start_offset */,
                                          size_t packets_in_chunk) {
    for (size_t pkt_idx = 0; pkt_idx < packets_in_chunk; ++pkt_idx) {
        size_t global_packet_idx = chunk_idx * config_.chunk_size + pkt_idx;
        size_t offset = global_packet_idx * config_.mtu;
        size_t packet_size = std::min(config_.mtu, 
                                     cts_info_.buffer_size - offset);
        
        co_await send_packet(global_packet_idx, data, offset, packet_size);
    }
    
    co_return;
}

rdmapp::task<void> RDMASender::send_packet(size_t packet_idx,
                                           const uint8_t* data,
                                           size_t offset,
                                           size_t packet_size) {
    // Create remote memory region for this packet at the offset
    rdmapp::remote_mr remote_mr(
        reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(cts_info_.remote_addr) + offset),
        static_cast<uint32_t>(packet_size),
        cts_info_.rkey);
    
    // Encode packet index in immediate value
    uint32_t imm = encode_immediate(current_msg_id_, packet_idx);
    
    std::cout << "Sender: Sending packet " << packet_idx << " offset=" << offset 
                << " size=" << packet_size << " imm=0x" << std::hex << imm << std::dec << std::endl;

    // Send packet with RDMA Write with Immediate
    co_await qp_->write_with_imm(
        remote_mr,
        const_cast<uint8_t*>(data + offset),
        packet_size,
        imm
    );
    
    co_return;
}

} // namespace RDMA_EC
