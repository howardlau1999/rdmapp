#include "rdma_logger.h"
#include "rdma_receiver.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>

#include <infiniband/verbs.h>

namespace RDMA_EC {

RDMAReceiver::RDMAReceiver(std::shared_ptr<rdmapp::acceptor> acceptor,
                           std::shared_ptr<rdmapp::cq> recv_cq,
                           const Config &config)
    : acceptor_(acceptor), recv_cq_(recv_cq), config_(config) {
  Logger::set_enabled(config_.enable_logging);
  Logger::info() << "Receiver: Initialized with MTU=" << config_.mtu
                 << ", chunk_size=" << config_.chunk_size;
  dummy_recv_buffer_.resize(1);
}

RDMAReceiver::~RDMAReceiver() {
  stop_thread_ = true;
  completion_cv_.notify_all();
  if (completion_thread_.joinable()) {
    completion_thread_.join();
  }
  if (frontend_thread_.joinable()) {
    frontend_thread_.join();
  }

  // Free page-aligned receive buffer
  if (recv_buffer_) {
    free(recv_buffer_);
    recv_buffer_ = nullptr;
  }
}

rdmapp::task<void> RDMAReceiver::receive_data(size_t expected_size) {
    expected_size_ = expected_size;
    
    Logger::info() << "Receiver: Waiting for connection...";
    qp_ = co_await acceptor_->accept();
    Logger::info() << "Receiver: Connection accepted";
    
    size_t page_size = sysconf(_SC_PAGESIZE);
    size_t aligned_size = ((config_.buffer_size + page_size - 1) / page_size) * page_size;
    
    if (posix_memalign(&recv_buffer_, page_size, aligned_size)) {
        perror("posix_memalign");
        throw std::runtime_error("Failed to allocate page-aligned receive buffer");
    }
    recv_buffer_size_ = aligned_size;
    
    Logger::info() << "Receiver: Allocated page-aligned buffer: size=" << config_.buffer_size
              << ", aligned_size=" << aligned_size 
              << ", page_size=" << page_size 
              << ", addr=0x" << std::hex << reinterpret_cast<uintptr_t>(recv_buffer_) 
              << std::dec;
    
    auto pd = qp_->pd_ptr();
    local_mr_ = std::make_shared<rdmapp::local_mr>(
        pd->reg_mr(recv_buffer_, recv_buffer_size_));
    
    total_packets_ = calculate_num_packets(expected_size, config_.mtu);
    
    // Ensure total number of packets doesn't exceed 24-bit limit
    if (total_packets_ > 0xFFFFFF) {
        throw std::runtime_error("Total number of packets exceeds maximum value (2^24 - 1)");
    }
    
    total_chunks_ = calculate_num_chunks(total_packets_, config_.chunk_size);
    
    // Initialize packet bitmap: each element is atomic<uint16_t> representing 16 packets
    // Note: atomic types are not copyable/movable, so we must construct with the right size
    // from the start. The constructor will default-construct each element (initialized to 0)
    size_t bitmap_size = (total_packets_ + 15) / 16;  // Round up to nearest 16
    packet_bitmap_ = std::vector<std::atomic<uint16_t>>(bitmap_size);
    
    // Initialize chunk bitmap
    chunk_bitmap_.store(0, std::memory_order_relaxed);
    
    Logger::info() << "Receiver: Expecting " << total_packets_ << " packets in " 
              << total_chunks_ << " chunks for " << expected_size << " bytes";
    
    // Post receives for immediate values
    // Must post before sending CTS
    co_await post_receives(total_packets_ + 10); // Extra for safety
    
    // Verify dummy_recv_mr_ is set
    if (!dummy_recv_mr_) {
        Logger::error() << "Receiver: ERROR - dummy_recv_mr_ not set after post_receives!";
        throw std::runtime_error("dummy_recv_mr_ not initialized");
    }
    Logger::info() << "Receiver: Verified dummy_recv_mr_ is set (addr=0x" << std::hex 
              << reinterpret_cast<uint64_t>(dummy_recv_mr_->addr()) << std::dec 
              << ", length=" << dummy_recv_mr_->length() << ")";
    
    // Verify all member variables are ready before starting threads
    Logger::info() << "Receiver: Pre-thread checks - packet_bitmap_.size()=" << packet_bitmap_.size()
              << ", total_packets_=" << total_packets_ 
              << ", total_chunks_=" << total_chunks_;
    
    // Start background threads BEFORE sending CTS
    // This ensures the completion thread is ready to poll receive completions
    // before cq_poller can consume them (if cq_poller is being used)
    Logger::info() << "Receiver: Starting backend thread...";
    completion_thread_ = std::thread(&RDMAReceiver::process_completions, this);
    Logger::info() << "Receiver: Backend thread started successfully";
    
    Logger::info() << "Receiver: Starting frontend thread...";
    frontend_thread_ = std::thread(&RDMAReceiver::frontend_poller, this);
    Logger::info() << "Receiver: Frontend thread started successfully";
    
    // Give threads a moment to start polling before we send CTS
    // This helps ensure the receiver's thread can get receive completions
    // before cq_poller (if used) starts consuming them
    //std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Send CTS to sender (this requires cq_poller to process send completion)
    co_await send_cts(expected_size);
    
    // Wait for all packets to arrive
    {
        std::unique_lock<std::mutex> lock(completion_mutex_);
        auto timeout = std::chrono::seconds(config_.receiver_timeout_seconds);
        bool success = completion_cv_.wait_for(lock, timeout, [this] {
            return reception_complete_.load();
        });
        
        if (!success) {
            Logger::info() << "Receiver: Timeout waiting for packets (timeout: " 
                      << config_.receiver_timeout_seconds << " seconds)";
        }
    }
    
    // Stop background threads
    stop_thread_ = true;
    if (completion_thread_.joinable()) {
        completion_thread_.join();
    }
    if (frontend_thread_.joinable()) {
        frontend_thread_.join();
    }
    
    // Update statistics
    bytes_received_ = expected_size;
    
    // Return the received data (copy from page-aligned buffer)
    // uint8_t* recv_data = static_cast<uint8_t*>(recv_buffer_);
    // std::vector<uint8_t> result(recv_data, recv_data + expected_size);
    
    Logger::info() << "Receiver: Transfer complete. Received " 
              << packets_received_.load() << " packets (" 
              << expected_size << " bytes)";

    co_return;
}

rdmapp::task<void> RDMAReceiver::send_cts(size_t buffer_size) {
  CTSInfo cts;
  cts.remote_addr = reinterpret_cast<uint64_t>(recv_buffer_);
  cts.rkey = local_mr_->rkey();
  cts.buffer_size = buffer_size;
  cts.total_packets = total_packets_;
  cts.msg_id = current_msg_id_++;

  co_await qp_->send(&cts, sizeof(cts));

  Logger::info() << "Receiver: Sent CTS - addr=0x" << std::hex
                 << cts.remote_addr << ", rkey=0x" << cts.rkey << std::dec;

  co_return;
}

rdmapp::task<void> RDMAReceiver::post_receives(size_t count) {
  // Post receives to catch immediate values from RDMA Write with Immediate
  // In RDMA Write with Immediate, the immediate value comes in the receive
  // completion The actual data is written directly to memory via RDMA Write

  // Register a memory region for the dummy receive buffer
  auto pd = qp_->pd_ptr();
  dummy_recv_mr_ = std::make_shared<rdmapp::local_mr>(
      pd->reg_mr(dummy_recv_buffer_.data(), dummy_recv_buffer_.size()));

  // Post initial batch of receives - we post just enough to handle the transfer
  // QP capacity is 1024, but we need total_packets_ + small buffer for the
  // transfer Don't overpost (which could cause duplicate completion loop)
  constexpr size_t max_qp_capacity = 1024;
  // Add a small buffer (5%) to handle potential races/duplicates
  size_t needed_receives = std::min(count + count / 20, count + 100UL);
  size_t initial_count = std::min(needed_receives, max_qp_capacity);

  Logger::info() << "Receiver: Posting " << initial_count
                 << " receives (needed: " << needed_receives
                 << ", QP capacity: " << max_qp_capacity << "...";

  for (size_t i = 0; i < initial_count; ++i) {
    post_single_receive();
  }

  Logger::info() << "Receiver: Posted " << initial_count << " initial receives";

  co_return;
}

void RDMAReceiver::post_single_receive() {
  auto mr = dummy_recv_mr_;
  auto qp = qp_;

  if (!mr) {
    Logger::error() << "Receiver: Error - dummy_recv_mr_ not initialized!";
    return;
  }

  if (!qp) {
    Logger::error() << "Receiver: Error - qp_ not initialized!";
    return;
  }

  struct ibv_sge recv_sge;
  recv_sge.addr = reinterpret_cast<uint64_t>(mr->addr());
  recv_sge.length = mr->length();
  recv_sge.lkey = mr->lkey();

  struct ibv_recv_wr recv_wr = {};
  struct ibv_recv_wr *bad_recv_wr = nullptr;
  recv_wr.next = nullptr;
  recv_wr.num_sge = 1;
  // Use a special marker value that's clearly not a valid callback pointer
  // This helps us filter our receive completions from send completions
  // Note: cq_poller will still try to process this and crash, but at least we
  // can identify it
  recv_wr.wr_id =
      0xFFFFFFFFFFFFFFFFULL; // Special marker for our receive completions
  recv_wr.sg_list = &recv_sge;

  try {
    qp->post_recv(recv_wr, bad_recv_wr);
  } catch (const std::exception &e) {
    Logger::error() << "Receiver: Failed to post receive: " << e.what();
  } catch (...) {
    Logger::error() << "Receiver: Unknown exception in post_recv!";
  }
}

void RDMAReceiver::process_completions() {
  if (config_.cpu_core_id >= 0) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(1, &cpuset);
    int ret =
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (ret != 0) {
      Logger::error() << "Receiver: Warning - failed to set CPU affinity for "
                         "backend thread: "
                      << ret;
    } else {
      Logger::info() << "Receiver: Pinned backend thread to CPU "
                     << sched_getcpu();
    }
  }

  Logger::info() << "Receiver: Backend thread started";

  // Wait a bit to ensure dummy_recv_mr_, recv_cq_, and packet_bitmap_ are
  // initialized This is a safety measure - post_receives() should complete
  // before threads start Use a shorter delay to start polling sooner and beat
  // cq_poller to completions
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  if (!dummy_recv_mr_) {
    Logger::error() << "Receiver: FATAL - dummy_recv_mr_ not initialized in "
                       "completion thread!";
    return;
  }

  if (!recv_cq_) {
    Logger::error()
        << "Receiver: FATAL - recv_cq_ not initialized in completion thread!";
    return;
  }

  if (packet_bitmap_.empty()) {
    Logger::error()
        << "Receiver: FATAL - packet_bitmap_ is empty in completion thread!";
    return;
  }

  Logger::info() << "Receiver: Backend thread ready - packet_bitmap_.size()="
                 << packet_bitmap_.size()
                 << ", total_packets_=" << total_packets_;

  constexpr size_t batch_size = 32;
  std::vector<struct ibv_wc> wc_vec(batch_size);
  size_t total_polled = 0;
  size_t total_with_imm = 0;
  size_t receives_to_repost = 0; // Track how many receives need reposting

  // Poll very aggressively - we need to get completions before cq_poller does
  // because cq_poller will consume them from the CQ even if it skips processing
  while (!stop_thread_) {
    // Poll the completion queue
    if (!recv_cq_) {
      Logger::error() << "Receiver: recv_cq_ is null!";
      break;
    }
    size_t num_completions = recv_cq_->poll(wc_vec);
    total_polled += num_completions;

    if (num_completions > 0) {
      Logger::debug() << "[BACKEND] Polled " << num_completions
                      << " completions (total polled: " << total_polled;
    }

    // Reset counter for this batch - we'll only count new packets
    receives_to_repost = 0;

    for (size_t i = 0; i < num_completions; ++i) {
      const auto &wc = wc_vec[i];

      // Process ALL completions since we're not using cq_poller
      // Receive completions have our marker, send completions have callback
      // pointers
      constexpr uint64_t RECV_MARKER = 0xFFFFFFFFFFFFFFFFULL;

      if (wc.wr_id != RECV_MARKER) {
        // This is a send completion with a callback pointer
        // Manually invoke the callback (since we're not using cq_poller)
        try {
          auto cb = reinterpret_cast<rdmapp::executor::callback_ptr>(wc.wr_id);
          (*cb)(wc);
          // Destroy the callback after invocation (same as executor does)
          rdmapp::executor::destroy_callback(cb);
        } catch (...) {
          // If callback invocation fails, log and continue
          Logger::error() << "[BACKEND] Warning - failed to invoke callback "
                             "for send completion";
        }
        continue; // Skip to next completion
      }

      // Verify this is actually a receive completion
      if (wc.opcode != IBV_WC_RECV && wc.opcode != IBV_WC_RECV_RDMA_WITH_IMM) {
        Logger::debug() << "[BACKEND] Warning - wr_id=RECV_MARKER but opcode="
                        << wc.opcode << ", skipping";
        continue;
      }

      // Always repost receives - the completion consumes the receive work
      // request Even if it's a duplicate packet, we need to repost to keep the
      // receive queue full receives_to_repost++;

      // Check completion status
      if (wc.status != IBV_WC_SUCCESS) {
        Logger::error() << "Receiver: Completion error: status=" << wc.status
                        << ", opcode=" << wc.opcode;
        receives_to_repost++; // Need to repost for error completions too
        continue;
      }

      // Check if this completion has an immediate value
      if (wc.wc_flags & IBV_WC_WITH_IMM) {
        total_with_imm++;
        uint32_t imm = wc.imm_data;

        // Decode immediate value to get packet index
        auto [msg_id, packet_idx] = decode_immediate(imm);

        Logger::debug() << "[BACKEND] Received packet " << packet_idx
                        << " (msg_id=" << msg_id << ", imm=0x" << std::hex
                        << imm << std::dec << ")";

        // Verify message ID matches
        if (msg_id != current_msg_id_ - 1) {
          Logger::error()
              << "Receiver: Warning - message ID mismatch: expected "
              << (current_msg_id_ - 1) << ", got " << msg_id;
          continue; // Skip - don't repost for invalid messages
        }

        // Verify packet index is valid
        if (packet_idx >= total_packets_) {
          Logger::error() << "Receiver: Warning - invalid packet index: "
                          << packet_idx << " (max: " << total_packets_ << ")";
          continue; // Skip - don't repost for invalid packet indices
        }

        // Get the bitmap entry index (packet_idx / 16)
        // Each bitmap entry represents 16 packets
        size_t bitmap_idx = packet_idx / 16;

        // Safety checks - ensure packet_bitmap_ is valid and index is in range
        if (packet_bitmap_.empty()) {
          Logger::error() << "[BACKEND] FATAL - packet_bitmap_ is empty!";
          continue; // Skip - don't repost for fatal errors
        }

        if (bitmap_idx >= packet_bitmap_.size()) {
          Logger::error() << "[BACKEND] FATAL - bitmap_idx " << bitmap_idx
                          << " >= packet_bitmap_.size() "
                          << packet_bitmap_.size()
                          << " (packet_idx=" << packet_idx << ")";
          continue; // Skip - don't repost for invalid bitmap indices
        }

        // Set the bit atomically using fetch_or
        uint16_t bit_mask = 1U << (packet_idx % 16);
        uint16_t old_val = packet_bitmap_[bitmap_idx].fetch_or(
            bit_mask, std::memory_order_release);

        if ((old_val & bit_mask) == 0) {
          // This is a new packet
          packets_received_.fetch_add(1, std::memory_order_relaxed);
          receives_to_repost++; // Only repost for new packets, not duplicates
          Logger::debug() << "[BACKEND] Marked packet " << packet_idx
                          << " in bitmap[" << bitmap_idx << "] (bitmask=0x"
                          << std::hex << bit_mask << ", old=0x" << old_val
                          << ", new=0x" << (old_val | bit_mask) << std::dec
                          << ")";
        } else {
          Logger::debug() << "[BACKEND] Packet " << packet_idx
                          << " already marked (duplicate completion?)";
        }
      } else {
        Logger::debug() << "[BACKEND] Completion without IMM: opcode="
                        << wc.opcode << ", byte_len=" << wc.byte_len
                        << " (skipping)";
        // Repost for completions without IMM (rare case, but we need to
        // maintain queue)
        receives_to_repost++;
      }
    }

    // Batch repost only receives for new packets
    // This breaks the feedback loop of duplicate completions
    // We're selective about reposting: only repost for new packets and error
    // completions
    if (receives_to_repost > 0 && dummy_recv_mr_) {
      for (size_t i = 0; i < receives_to_repost; ++i) {
        post_single_receive(); // May fail silently, but we'll try again next
                               // cycle
      }
      if (receives_to_repost > 1) {
        Logger::debug() << "[BACKEND] Reposted " << receives_to_repost
                        << " receives in batch";
      }
    }

    // Check if all packets have been received
    size_t received_count = packets_received_.load(std::memory_order_acquire);
    if (received_count >= total_packets_) {
      Logger::info() << "Receiver: All " << total_packets_
                     << " packets received! (" << received_count << " unique, "
                     << total_with_imm << " total with dupes)";
      std::lock_guard<std::mutex> lock(completion_mutex_);
      reception_complete_ = true;
      completion_cv_.notify_all();
      break;
    }

    // Poll aggressively to get completions before cq_poller does
    // If we don't get any completions, use a very short sleep to avoid
    // busy-waiting but still poll frequently enough to beat cq_poller
    if (num_completions == 0) {
      std::this_thread::sleep_for(std::chrono::microseconds(1));
    }
  }

  Logger::info() << "Receiver: Backend thread exiting (total polled: "
                 << total_polled << ", with IMM: " << total_with_imm
                 << ", unique packets: " << packets_received_.load() << ")";
}

void RDMAReceiver::frontend_poller() {
  if (config_.cpu_core_id >= 0) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(2, &cpuset);
    int ret =
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (ret != 0) {
      Logger::error() << "Receiver: Warning - failed to set CPU affinity for "
                         "frontend thread: "
                      << ret;
    } else {
      Logger::info() << "Receiver: Pinned frontend thread to CPU "
                     << sched_getcpu();
    }
  }

  // Wait a bit to ensure packet_bitmap_ is initialized
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  // Add a try-catch to catch any exceptions
  try {
    // Check if we can access member variables safely
    bool stop = stop_thread_.load(std::memory_order_acquire);
    size_t total = total_packets_;
    size_t bmp_size = packet_bitmap_.size();
    size_t chunk_size = config_.chunk_size;

    // Suppress unused variable warnings
    (void)stop;
    (void)total;
    (void)bmp_size;
    (void)chunk_size;

  } catch (const std::exception &e) {
    Logger::error() << "[FRONTEND] Exception in frontend_poller: " << e.what();
    return;
  } catch (...) {
    Logger::error() << "[FRONTEND] Unknown exception in frontend_poller!";
    return;
  }

  while (!stop_thread_.load(std::memory_order_acquire)) {

    // Safety check
    if (packet_bitmap_.empty() || total_packets_ == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    // Poll the packet bitmap and update chunk bitmap
    // Check each packet bitmap entry
    for (size_t i = 0; i < packet_bitmap_.size(); ++i) {
      // Read the packet bitmap entry atomically
      uint16_t packet_mask = packet_bitmap_[i].load(std::memory_order_acquire);

      // Check if the mask (0xFFFF & packet_bitmap_[i]) indicates all bits are
      // set This means all 16 packets in this bitmap entry are received
      if ((packet_mask & 0xFFFF) == 0xFFFF) {
        // Calculate which packets this bitmap entry covers
        size_t first_packet = i * 16;
        size_t last_packet = std::min(first_packet + 15, total_packets_ - 1);

        // For each chunk that overlaps with these packets, check if it's
        // complete and mark it atomically if so
        size_t first_chunk = first_packet / config_.chunk_size;
        size_t last_chunk = last_packet / config_.chunk_size;

        for (size_t chunk_idx = first_chunk;
             chunk_idx <= last_chunk && chunk_idx < total_chunks_;
             ++chunk_idx) {
          // Check if this chunk is already marked
          uint64_t chunk_bit = 1ULL << chunk_idx;
          uint64_t current_chunk_bitmap =
              chunk_bitmap_.load(std::memory_order_acquire);

          if (current_chunk_bitmap & chunk_bit) {
            continue; // Already marked
          }

          // Check if all packets in this chunk are received
          bool chunk_complete = true;
          size_t chunk_start_packet = chunk_idx * config_.chunk_size;
          size_t chunk_end_packet = std::min(
              chunk_start_packet + config_.chunk_size - 1, total_packets_ - 1);

          for (size_t p = chunk_start_packet; p <= chunk_end_packet; ++p) {
            size_t bmp_idx = p / 16;
            size_t bit_pos = p % 16;
            uint16_t bit_mask = 1U << bit_pos;

            // Safety check
            if (bmp_idx >= packet_bitmap_.size()) {
              chunk_complete = false;
              break;
            }

            uint16_t bmp_val =
                packet_bitmap_[bmp_idx].load(std::memory_order_acquire);

            if ((bmp_val & bit_mask) == 0) {
              chunk_complete = false;
              break;
            }
          }

          // If chunk is complete, mark it atomically
          if (chunk_complete) {
            chunk_bitmap_.fetch_or(chunk_bit, std::memory_order_release);
          }
        }
      }
    }

    if (reception_complete_.load(std::memory_order_acquire)) {
      break;
    }
    //std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
}

bool RDMAReceiver::is_complete() const {
  uint64_t expected_mask =
      (total_chunks_ == 64) ? UINT64_MAX : ((1ULL << total_chunks_) - 1);
  uint64_t current_mask = chunk_bitmap_.load(std::memory_order_acquire);
  return (current_mask & expected_mask) == expected_mask;
}

} // namespace RDMA_EC
