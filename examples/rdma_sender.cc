#include "rdma_sender.h"

#include "rdma_logger.h"
#include <chrono>
#include <cstring>
#include <future>
#include <iostream>
#include <random>
#include <thread>

namespace RDMA_EC {

RDMASender::RDMASender(std::shared_ptr<rdmapp::connector> data_connector,
                       std::shared_ptr<rdmapp::connector> ctrl_connector,
                       const Config &config)
    : data_connector_(data_connector), ctrl_connector_(ctrl_connector),
      config_(config) {
  Logger::set_enabled(config_.enable_logging);
  Logger::info() << "Sender: Initialized with MTU=" << config_.mtu
                 << ", chunk_size=" << config_.chunk_size;
}

rdmapp::task<void> RDMASender::send_data(const void *data, size_t size) {
  Logger::info() << "Sender: Connecting...";
  qp_ = co_await data_connector_->connect();
  Logger::info() << "Sender: Connected (data QP)";

  // If selective repeat is enabled, create a separate control QP for ACKs.
  if (config_.enable_selective_repeat) {
    if (!ctrl_connector_) {
      throw std::runtime_error("Sender: enable_selective_repeat is true but "
                               "ctrl_connector_ is null");
    }
    Logger::info()
        << "Sender: Connecting control QP (RC) for selective repeat...";
    ctrl_qp_ = co_await ctrl_connector_->connect();
    Logger::info() << "Sender: Control QP connected";
  }

  co_await wait_for_cts();
  Logger::info() << "Sender: Received CTS - remote_addr=0x" << std::hex
                 << cts_info_.remote_addr << ", rkey=0x" << cts_info_.rkey
                 << std::dec << ", packets=" << cts_info_.total_packets;

  auto pd = qp_->pd_ptr();
  local_mr_ = std::make_shared<rdmapp::local_mr>(
      pd->reg_mr(const_cast<void *>(data), size));

  const uint8_t *data_ptr = static_cast<const uint8_t *>(data);
  size_t num_packets = calculate_num_packets(size, config_.mtu);

  // Ensure total number of packets doesn't exceed 24-bit limit
  if (num_packets > 0xFFFFFF) {
    throw std::runtime_error(
        "Total number of packets exceeds maximum value (2^24 - 1)");
  }

  size_t num_chunks = calculate_num_chunks(num_packets, config_.chunk_size);

  // Initialize selective repeat state and start SR background thread if enabled
  if (config_.enable_selective_repeat && ctrl_qp_) {
    // Create retransmit queue with num_chunks size (timestamps initialized to
    // 0)
    retransmit_queue_ = std::make_unique<RetransmitQueue>(num_chunks);

    // Start background ACK receiver thread that will run concurrently with
    // the sending loop and remove chunks from retransmit queue when ACKed.
    ack_thread_started_ = true;
    ack_thread_ = std::thread([this, num_chunks]() {
      try {
        auto task = receive_acks(num_chunks);
        auto &fut = task.get_future();
        // Do NOT detach the task here; we block on its future so that
        // the coroutine frame stays alive until completion.
        fut.get();
      } catch (const std::exception &e) {
        Logger::error() << "Sender: ACK thread exception: " << e.what();
      } catch (...) {
        Logger::error() << "Sender: ACK thread unknown exception";
      }
    });
  }

  // Note: current_msg_id_ is set from CTS message, not incremented here

  Logger::info() << "Sender: Sending " << size << " bytes in " << num_packets
                 << " packets across " << num_chunks << " chunks";

  using clock = std::chrono::high_resolution_clock;
  const auto rto = std::chrono::milliseconds(config_.sr_rto_ms);

  if (config_.enable_selective_repeat && retransmit_queue_) {
    // First pass: send all chunks once and set timestamps
    for (size_t chunk_idx = 0; chunk_idx < num_chunks; ++chunk_idx) {
      size_t chunk_start_offset = chunk_idx * config_.chunk_size * config_.mtu;
      size_t packets_in_chunk = std::min(
          config_.chunk_size, num_packets - chunk_idx * config_.chunk_size);
      Logger::info() << "Sender: Sending chunk " << chunk_idx << " with "
                     << packets_in_chunk << " packets";

      // Send the chunk
      co_await send_chunk(chunk_idx, data_ptr, chunk_start_offset,
                          packets_in_chunk);

      // Add to retransmit queue with current timestamp
      auto now = clock::now();
      auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          now.time_since_epoch());
      retransmit_queue_->add(static_cast<uint32_t>(chunk_idx), now_ms);
    }

    // Retransmission loop: continue until all chunks are acknowledged
    while (retransmit_queue_->pending() > 0) {
      auto now = clock::now();
      auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          now.time_since_epoch());

      // Iterate through all chunks and check for retransmission
      for (size_t chunk_idx = 0; chunk_idx < num_chunks; ++chunk_idx) {
        if (retransmit_queue_->is_pending(static_cast<uint32_t>(chunk_idx))) {
          auto timestamp_ms = retransmit_queue_->get_timestamp(
              static_cast<uint32_t>(chunk_idx));
          auto elapsed = now_ms - timestamp_ms;

          if (elapsed >= rto) {
            Logger::info() << "Sender: Retransmitting chunk " << chunk_idx;

            size_t chunk_start_offset =
                chunk_idx * config_.chunk_size * config_.mtu;
            size_t packets_in_chunk =
                std::min(config_.chunk_size,
                         num_packets - chunk_idx * config_.chunk_size);

            // Retransmit the chunk
            co_await send_chunk(chunk_idx, data_ptr, chunk_start_offset,
                                packets_in_chunk);

            // Update timestamp for retransmission
            retransmit_queue_->update(static_cast<uint32_t>(chunk_idx), now_ms);
          }
        }
      }

      // Small sleep to avoid busy-waiting
      std::this_thread::sleep_for(rto / 4);
    }

    // Wait for ACK thread to finish
    if (ack_thread_started_ && ack_thread_.joinable()) {
      ack_thread_.join();
    }
  } else {
    // Non-SR mode: just send all chunks once
    for (size_t chunk_idx = 0; chunk_idx < num_chunks; ++chunk_idx) {
      size_t chunk_start_offset = chunk_idx * config_.chunk_size * config_.mtu;
      size_t packets_in_chunk = std::min(
          config_.chunk_size, num_packets - chunk_idx * config_.chunk_size);
      Logger::info() << "Sender: Sending chunk " << chunk_idx << " with "
                     << packets_in_chunk << " packets";

      co_await send_chunk(chunk_idx, data_ptr, chunk_start_offset,
                          packets_in_chunk);
    }
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
    // Wait for next ACK
    ChunkAck ack{};
    auto [bytes, imm_opt] = co_await ctrl_qp_->recv(&ack, sizeof(ack));

    if (bytes != sizeof(ack)) {
      Logger::error() << "Sender: Invalid ACK size: " << bytes;
      continue;
    }

    // Remove the ACKed chunk from retransmit queue
    if (retransmit_queue_ && ack.chunk_idx < num_chunks) {
      if (retransmit_queue_->remove(static_cast<uint32_t>(ack.chunk_idx))) {
        ++acked_chunks;
        Logger::debug() << "Sender: ACK received for chunk " << ack.chunk_idx
                        << " (" << acked_chunks << "/" << num_chunks << ")";
      }
    } else {
      Logger::error() << "Sender: ACK for out-of-range chunk " << ack.chunk_idx;
    }
  }

  Logger::info() << "Sender: All " << num_chunks << " chunks ACKed";
  co_return;
}

rdmapp::task<void> RDMASender::send_chunk(size_t chunk_idx, const uint8_t *data,
                                          size_t /* chunk_start_offset */,
                                          size_t packets_in_chunk) {
  for (size_t pkt_idx = 0; pkt_idx < packets_in_chunk; ++pkt_idx) {
    size_t global_packet_idx = chunk_idx * config_.chunk_size + pkt_idx;
    size_t offset = global_packet_idx * config_.mtu;
    size_t packet_size = std::min(config_.mtu, cts_info_.buffer_size - offset);

    // For testing selective repeat, we can intentionally drop individual
    // packets with a configurable probability. This is only enabled when
    // selective repeat is on and packet_loss_probability > 0.
    if (config_.enable_selective_repeat &&
        config_.packet_loss_probability > 0.0) {
      static thread_local std::mt19937 rng(std::random_device{}());
      std::uniform_real_distribution<double> dist(0.0, 1.0);
      if (dist(rng) < config_.packet_loss_probability) {
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
                                           const uint8_t *data, size_t offset,
                                           size_t packet_size) {
  rdmapp::remote_mr remote_mr(
      reinterpret_cast<void *>(
          reinterpret_cast<uintptr_t>(cts_info_.remote_addr) + offset),
      static_cast<uint32_t>(packet_size), cts_info_.rkey);

  uint32_t imm = encode_immediate(current_msg_id_.load(),
                                  static_cast<uint32_t>(packet_idx));

  Logger::debug() << "Sender: Sending packet " << packet_idx
                  << " offset=" << offset << " size=" << packet_size
                  << " imm=0x" << std::hex << imm << std::dec;

  co_await qp_->write_with_imm(remote_mr, const_cast<uint8_t *>(data + offset),
                               packet_size, imm);

  co_return;
}

} // namespace RDMA_EC
