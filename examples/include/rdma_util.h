#pragma once

#include <cstdint>
#include <vector>
#include <atomic>
#include <bitset>

namespace RDMA_EC {

// Default configuration values
constexpr size_t DEFAULT_MTU = 1024;
constexpr size_t DEFAULT_CHUNK_SIZE = 16;  // packets per chunk
constexpr size_t DEFAULT_BUFFER_SIZE = 1024 * 1024;  // 1MB

// Configuration for RDMA transport
struct Config {
    size_t mtu = DEFAULT_MTU;
    size_t chunk_size = DEFAULT_CHUNK_SIZE;
    size_t buffer_size = DEFAULT_BUFFER_SIZE;
    int cpu_core_id = 2;  // -1 means no CPU pinning
};

// Clear-To-Send message structure
struct CTSInfo {
    uint64_t remote_addr;
    uint32_t rkey;
    size_t buffer_size;
    size_t total_packets;
    uint16_t msg_id;
};

// Utility functions for immediate value encoding/decoding
inline uint32_t encode_immediate(uint16_t msg_id, uint16_t packet_idx) {
    return (static_cast<uint32_t>(msg_id) << 16) | packet_idx;
}

inline std::pair<uint16_t, uint16_t> decode_immediate(uint32_t imm) {
    uint16_t msg_id = (imm >> 16) & 0xFFFF;
    uint16_t packet_idx = imm & 0xFFFF;
    return {msg_id, packet_idx};
}

// Calculate number of packets needed for a given size
inline size_t calculate_num_packets(size_t data_size, size_t mtu) {
    return (data_size + mtu - 1) / mtu;
}

// Calculate number of chunks for given packets
inline size_t calculate_num_chunks(size_t num_packets, size_t chunk_size) {
    return (num_packets + chunk_size - 1) / chunk_size;
}

} // namespace RDMA_EC
