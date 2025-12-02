#include "rdma_sender.h"
#include "rdma_logger.h"
#include <iostream>
#include <cstring>
#include <chrono>

namespace RDMA_EC {

RDMASender::RDMASender(std::shared_ptr<rdmapp::connector> connector, 
                       const Config& config)
    : connector_(connector), config_(config) {
    Logger::set_enabled(config_.enable_logging);
    Logger::info() << "Sender: Initialized with MTU=" << config_.mtu 
              << ", chunk_size=" << config_.chunk_size;
}

rdmapp::task<void> RDMASender::send_data(const void* data, size_t size) {
    Logger::info() << "Sender: Connecting...";
    qp_ = co_await connector_->connect();
    Logger::info() << "Sender: Connected";
    
    co_await wait_for_cts();
    Logger::info() << "Sender: Received CTS - remote_addr=0x" << std::hex 
              << cts_info_.remote_addr << ", rkey=0x" << cts_info_.rkey
              << std::dec << ", packets=" << cts_info_.total_packets;
    
    auto pd = qp_->pd_ptr();
    local_mr_ = std::make_shared<rdmapp::local_mr>(
        pd->reg_mr(const_cast<void*>(data), size));
    
    const uint8_t* data_ptr = static_cast<const uint8_t*>(data);
    size_t num_packets = calculate_num_packets(size, config_.mtu);
    
    // Ensure total number of packets doesn't exceed 24-bit limit
    if (num_packets > 0xFFFFFF) {
        throw std::runtime_error("Total number of packets exceeds maximum value (2^24 - 1)");
    }
    
    size_t num_chunks = calculate_num_chunks(num_packets, config_.chunk_size);
    
    // Note: current_msg_id_ is set from CTS message, not incremented here
    
    Logger::info() << "Sender: Sending " << size << " bytes in " 
              << num_packets << " packets across " 
              << num_chunks << " chunks";
    
    // Measure pure data-path throughput on the sender:
    // from first RDMA write until the last write completion.
    auto data_start = std::chrono::high_resolution_clock::now();

    for (size_t chunk_idx = 0; chunk_idx < num_chunks; ++chunk_idx) {
        size_t chunk_start_offset = chunk_idx * config_.chunk_size * config_.mtu;
        size_t packets_in_chunk = std::min(config_.chunk_size,
                                          num_packets - chunk_idx * config_.chunk_size);
        Logger::info() << "Sender: Sending chunk " << chunk_idx << " with " << packets_in_chunk << " packets";
        co_await send_chunk(chunk_idx, data_ptr, chunk_start_offset, packets_in_chunk);
    }

    auto data_end = std::chrono::high_resolution_clock::now();
    double seconds = std::chrono::duration<double>(data_end - data_start).count();
    if (seconds > 0.0) {
        double bytes = static_cast<double>(size);
        double mb_per_sec = bytes / (seconds * 1024.0 * 1024.0);
        double mbit_per_sec = (bytes * 8.0) / (seconds * 1e6);
        std::cout << "Sender (data-path): " << mb_per_sec << " MB/s ("
                       << mbit_per_sec << " Mbit/sec) over " << seconds
                       << " seconds";
    }
    
    packets_sent_ += num_packets;
    bytes_sent_ += size;
    
    Logger::info() << "Sender: Transfer complete. Sent " << num_packets 
              << " packets (" << size << " bytes)";
        
    co_return;
}

rdmapp::task<void> RDMASender::wait_for_cts() {
    auto [bytes, imm_opt] = co_await qp_->recv(&cts_info_, sizeof(CTSInfo));
    
    if (bytes != sizeof(CTSInfo)) {
        throw std::runtime_error("Invalid CTS message size");
    }
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
    rdmapp::remote_mr remote_mr(
        reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(cts_info_.remote_addr) + offset),
        static_cast<uint32_t>(packet_size),
        cts_info_.rkey);
    
    uint32_t imm = encode_immediate(current_msg_id_.load(), static_cast<uint32_t>(packet_idx));
    
    Logger::debug() << "Sender: Sending packet " << packet_idx << " offset=" << offset 
                << " size=" << packet_size << " imm=0x" << std::hex << imm << std::dec;

    co_await qp_->write_with_imm(
        remote_mr,
        const_cast<uint8_t*>(data + offset),
        packet_size,
        imm
    );
    
    co_return;
}

} // namespace RDMA_EC
