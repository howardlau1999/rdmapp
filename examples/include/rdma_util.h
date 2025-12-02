#pragma once

#include <cstdint>
#include <string>
#include <stdexcept>
#include <infiniband/verbs.h>

namespace RDMA_EC {

// Default configuration values
constexpr size_t DEFAULT_MTU = 1024;
constexpr size_t DEFAULT_CHUNK_SIZE = 16;  // packets per chunk
constexpr size_t DEFAULT_BUFFER_SIZE = 1024 * 1024;  // 1MB
constexpr int DEFAULT_RECEIVER_TIMEOUT_SECONDS = 10;
constexpr bool DEFAULT_ENABLE_LOGGING = true;
extern enum ibv_qp_type DEFAULT_RDMA_TRANSPORT;

// Configuration for RDMA transport
class Config {
public:
    size_t mtu = DEFAULT_MTU;
    size_t chunk_size = DEFAULT_CHUNK_SIZE;
    size_t buffer_size = DEFAULT_BUFFER_SIZE;
    int cpu_core_id = 2;  // -1 means no CPU pinning
    int receiver_timeout_seconds = DEFAULT_RECEIVER_TIMEOUT_SECONDS;
    enum ibv_qp_type transport_type = DEFAULT_RDMA_TRANSPORT;
    bool enable_logging = DEFAULT_ENABLE_LOGGING;
    // Optional selective repeat reliability
    bool enable_selective_repeat = false;
    int sr_rto_ms = 10;  // retransmission timeout in milliseconds


    bool load_from_file(const std::string& filepath);

    bool save_to_file(const std::string& filepath) const;

    void print() const;

private:
    std::string trim(const std::string& str) const;
    bool parse_line(const std::string& line);
};

// Clear-To-Send message structure
struct CTSInfo {
    uint64_t remote_addr;
    uint32_t rkey;
    size_t buffer_size;
    size_t total_packets;
    uint8_t msg_id;
};

// Utility functions for immediate value encoding/decoding
// msg_id: 8 bits (upper 8 bits of uint32_t)
// packet_idx: 24 bits (lower 24 bits of uint32_t)
// Maximum packet_idx value: 2^24 - 1 = 16,777,215
inline uint32_t encode_immediate(uint8_t msg_id, uint32_t packet_idx) {
    return (static_cast<uint32_t>(msg_id) << 24) | (packet_idx & 0xFFFFFF);
}

inline std::pair<uint8_t, uint32_t> decode_immediate(uint32_t imm) {
    uint8_t msg_id = (imm >> 24) & 0xFF;
    uint32_t packet_idx = imm & 0xFFFFFF;
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
