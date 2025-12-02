#include "rdma_sender.h"
#include "rdma_logger.h"
#include <iostream>
#include <cstring>
#include <chrono>
#include <random>

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
    Logger::info() << "Sender: Connected (data QP)";

    // If selective repeat is enabled, create a separate control QP for ACKs.
    if (config_.enable_selective_repeat) {
        Logger::info() << "Sender: Connecting control QP for selective repeat...";
        ctrl_qp_ = co_await connector_->connect();
        Logger::info() << "Sender: Control QP connected";
    }
    
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
    
    // Initialize selective repeat state if enabled
    if (config_.enable_selective_repeat) {
        {
            std::lock_guard<std::mutex> lock(sr_mutex_);
            sr_chunks_.clear();
            sr_chunks_.resize(num_chunks);
        }

        // Start ACK receiver in the background BEFORE sending chunks so that
        // ACKs are processed concurrently with transmission.
        if (ctrl_qp_) {
            auto ack_task = receive_acks(num_chunks);
            ack_task.detach();
        }
    }
    
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

        // Record this chunk in the retransmission queue if selective repeat is enabled.
        if (config_.enable_selective_repeat) {
            auto now = std::chrono::high_resolution_clock::now();
            std::lock_guard<std::mutex> lock(sr_mutex_);
            if (chunk_idx < sr_chunks_.size()) {
                sr_chunks_[chunk_idx].first_sent = now;
                sr_chunks_[chunk_idx].acked = false;
            }
        }

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

rdmapp::task<void> RDMASender::receive_acks(size_t num_chunks) {
    size_t acked_chunks = 0;

    while (acked_chunks < num_chunks) {
        ChunkAck ack{};
        auto [bytes, imm_opt] = co_await ctrl_qp_->recv(&ack, sizeof(ack));

        if (bytes != sizeof(ack)) {
            Logger::error() << "Sender: Invalid ACK size: " << bytes;
            continue;
        }

        std::lock_guard<std::mutex> lock(sr_mutex_);
        if (ack.chunk_idx < sr_chunks_.size()) {
            if (!sr_chunks_[ack.chunk_idx].acked) {
                sr_chunks_[ack.chunk_idx].acked = true;
                ++acked_chunks;
                Logger::debug() << "Sender: ACK received for chunk " << ack.chunk_idx;
            }
        } else {
            Logger::error() << "Sender: ACK for out-of-range chunk " << ack.chunk_idx;
        }
    }

    Logger::info() << "Sender: All " << num_chunks << " chunks ACKed";
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

        // For testing selective repeat, we can intentionally drop individual
        // packets with a small probability. This is only enabled when
        // selective repeat is on.
        if (config_.enable_selective_repeat) {
            static thread_local std::mt19937 rng(std::random_device{}());
            // 1% chance to drop a packet
            std::uniform_real_distribution<double> dist(0.0, 1.0);
            if (dist(rng) < 0.01) {
                Logger::info() << "Sender: Intentionally dropping packet "
                               << global_packet_idx << " (chunk " << chunk_idx
                               << ") for selective-repeat testing";
                continue;
            }
        }

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
