#include "acceptor.h"
#include "connector.h"
#include "rdma_logger.h"
#include "rdma_receiver.h"
#include "rdma_sender.h"
#include "rdma_util.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <optional>
#include <random>
#include <thread>
#include <unistd.h>

#include <infiniband/verbs.h>

#include <rdmapp/rdmapp.h>

using namespace RDMA_EC;

void *allocate_test_data(size_t size) {
  void *buffer = nullptr;
  size_t page_size = sysconf(_SC_PAGESIZE);

  size_t aligned_size = ((size + page_size - 1) / page_size) * page_size;

  if (posix_memalign(&buffer, page_size, aligned_size)) {
    perror("posix_memalign");
    return nullptr;
  }

  uint8_t *data = static_cast<uint8_t *>(buffer);
  for (size_t i = 0; i < size; ++i) {
    data[i] = static_cast<uint8_t>((i * 7 + 42) % 256);
  }

  if (aligned_size > size) {
    memset(data + size, 0, aligned_size - size);
  }

  Logger::info() << "Allocated page-aligned buffer: size=" << size
                 << ", aligned_size=" << aligned_size
                 << ", page_size=" << page_size << ", addr=0x" << std::hex
                 << reinterpret_cast<uintptr_t>(buffer) << std::dec;

  return buffer;
}

std::vector<uint8_t> generate_test_data(size_t size) {
  std::vector<uint8_t> data(size);

  for (size_t i = 0; i < size; ++i) {
    data[i] = static_cast<uint8_t>((i * 7 + 42) % 256);
  }

  return data;
}

bool verify_data(const std::vector<uint8_t> &sent,
                 const std::vector<uint8_t> &received) {
  if (sent.size() != received.size()) {
    Logger::error() << "Size mismatch: sent " << sent.size() << " vs received "
                    << received.size();
    return false;
  }

  size_t first_mismatch = SIZE_MAX;
  for (size_t i = 0; i < sent.size(); ++i) {
    if (sent[i] != received[i]) {
      if (first_mismatch == SIZE_MAX) {
        first_mismatch = i;
      }
    }
  }

  if (first_mismatch != SIZE_MAX) {
    Logger::error() << "Data mismatch at byte " << first_mismatch
                    << ": expected " << static_cast<int>(sent[first_mismatch])
                    << " got " << static_cast<int>(received[first_mismatch]);
    Logger::error() << "Showing first 10 mismatches:";
    size_t shown = 0;
    for (size_t i = first_mismatch; i < sent.size() && shown < 10; ++i) {
      if (sent[i] != received[i]) {
        Logger::error() << "  Byte " << i << ": expected "
                        << static_cast<int>(sent[i]) << " got "
                        << static_cast<int>(received[i]);
        shown++;
      }
    }
    return false;
  }

  return true;
}

int main(int argc, char *argv[]) {
  srand(42);

  auto device = std::make_shared<rdmapp::device>(0, 1, 3);
  auto pd = std::make_shared<rdmapp::pd>(device);
  std::shared_ptr<rdmapp::cq_poller> recv_cq_poller;
  std::shared_ptr<rdmapp::cq_poller> send_cq_poller;
  auto loop = rdmapp::socket::event_loop::new_loop();
  auto looper = std::thread([loop]() { loop->loop(); });

  std::optional<std::future<void>> receiver_future;
  std::optional<std::future<void>> sender_future;

  try {
    Config config;
    std::string config_file;

    bool is_client_mode =
        (argc >= 3 && std::string(argv[1]).find('.') != std::string::npos);

    if (argc >= 3) {
      std::string last_arg = argv[argc - 1];
      if (last_arg.find("config") != std::string::npos ||
          (last_arg.find('.') != std::string::npos &&
           last_arg.find('/') != std::string::npos)) {
        config_file = last_arg;
      }
    }

    if (!config_file.empty()) {
      if (config.load_from_file(config_file)) {
        Logger::info() << "Loaded configuration from " << config_file;
        config.print();
      } else {
        Logger::info() << "Warning: Failed to load config file, using defaults";
      }
    }

    Logger::set_enabled(config.enable_logging);

    size_t buffer_size = config.buffer_size;

    if (!is_client_mode) {
      // Server mode: [port] [config_file] - acts as receiver (listens for
      // connections)
      int port = std::stoi(argv[1]);
      Logger::info() << "Starting as RECEIVER on port " << port;

      config.buffer_size = buffer_size * 2;

      auto send_cq = std::make_shared<rdmapp::cq>(device, 2048);
      auto recv_cq = std::make_shared<rdmapp::cq>(device, 2048);

      // Create cq_poller for receiver's SEND completions (for CTS message)
      // NOTE: The receiver's completion thread polls recv_cq for receive
      // completions (packets). But we need a cq_poller on send_cq to process
      // send completions (CTS message)
      send_cq_poller = std::make_shared<rdmapp::cq_poller>(send_cq);

      auto acceptor = std::make_shared<rdmapp::acceptor>(
          loop, port, pd, recv_cq, send_cq, nullptr, config.transport_type);

      rdmapp::task<void> receiver_task = [acceptor, recv_cq, buffer_size,
                                          config]() -> rdmapp::task<void> {
        RDMAReceiver receiver(acceptor, recv_cq, config);

        Logger::info() << "\n=== RECEIVER STARTING ===" << std::endl;
        Logger::info() << "Expecting " << buffer_size << " bytes";
        Logger::info() << "MTU: " << config.mtu
                       << ", Chunk size: " << config.chunk_size;
        Logger::info() << ", Transport Type: "
                       << ((config.transport_type == IBV_QPT_RC) ? "RC" : "UC");

        auto start_time = std::chrono::high_resolution_clock::now();

        co_await receiver.receive_data(buffer_size);

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration_us =
            std::chrono::duration_cast<std::chrono::microseconds>(end_time -
                                                                  start_time)
                .count();
        auto duration_ms = duration_us / 1000.0;

        std::cout << "=== RECEIVER COMPLETE ===" << std::endl;
        std::cout << "Received " << receiver.get_packets_received()
                  << " packets, " << receiver.get_bytes_received() << " bytes"
                  << std::endl;
        std::cout << "Transfer time: " << duration_ms << " ms ("
                  << duration_us / 1000000.0 << " seconds)" << std::endl;
        if (duration_us > 0) {
          long long bytes = receiver.get_bytes_received();
          double mbits_per_sec = (bytes * 8.0) / duration_us;
          double mb_per_sec = (bytes * 1000.0) / duration_ms / 1024 / 1024;
          std::cout << "Throughput: " << mb_per_sec << " MB/s ("
                    << mbits_per_sec << " Mbit/sec)" << std::endl;
        }
        co_return;
      }();
      receiver_future = std::move(receiver_task.get_future());
      receiver_task.detach();
    } else if (argc >= 3) {
      // Client mode: [receiver_ip] [port] [config_file] - acts as sender
      // (connects to receiver)
      std::string receiver_ip = argv[1];
      int port_idx = 2;
      int port = std::stoi(argv[port_idx]);
      Logger::info() << "Starting as SENDER connecting to " << receiver_ip
                     << ":" << port;

      config.buffer_size = buffer_size * 2;

      auto send_cq = std::make_shared<rdmapp::cq>(device, 2048);
      auto recv_cq = std::make_shared<rdmapp::cq>(device, 2048);

      send_cq_poller = std::make_shared<rdmapp::cq_poller>(send_cq);
      recv_cq_poller = std::make_shared<rdmapp::cq_poller>(recv_cq);

      auto connector = std::make_shared<rdmapp::connector>(
          loop, receiver_ip, port, pd, recv_cq, send_cq, nullptr,
          config.transport_type);

      rdmapp::task<void> sender_task = [connector, buffer_size,
                                        config]() -> rdmapp::task<void> {
        RDMASender sender(connector, config);

        void *large_data_buffer = allocate_test_data(buffer_size);
        if (!large_data_buffer) {
          Logger::error() << "Failed to allocate page-aligned buffer";
          co_return;
        }

        Logger::info() << "\n=== SENDER STARTING ===";
        Logger::info() << "Sending " << buffer_size << " bytes";
        Logger::info() << "MTU: " << config.mtu
                       << ", Chunk size: " << config.chunk_size;

        auto start_time = std::chrono::high_resolution_clock::now();

        co_await sender.send_data(large_data_buffer, buffer_size);

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration_us =
            std::chrono::duration_cast<std::chrono::microseconds>(end_time -
                                                                  start_time)
                .count();
        auto duration_ms = duration_us / 1000.0;

        std::cout << "=== SENDER COMPLETE ===" << std::endl;
        std::cout << "Sent " << sender.get_packets_sent() << " packets, "
                  << sender.get_bytes_sent() << " bytes" << std::endl;
        std::cout << "Transfer time: " << duration_ms << " ms ("
                  << duration_us / 1000000.0 << " seconds)" << std::endl;

        free(large_data_buffer);
        co_return;
      }();
      sender_future = std::move(sender_task.get_future());
      sender_task.detach();
    } else {
      Logger::error() << "Usage:";
      Logger::error() << "  Receiver (server): " << argv[0]
                      << " <port> [config_file]";
      Logger::error() << "  Sender (client):   " << argv[0]
                      << " <receiver_ip> <port> [config_file]";
      Logger::error() << "";
      Logger::error()
          << "  config_file: Optional path to .config file (default: use "
             "built-in defaults)";
      return 1;
    }

    if (!is_client_mode && receiver_future.has_value()) {
      try {
        receiver_future->wait();
        receiver_future->get();
        Logger::info() << "Receiver task completed successfully";
      } catch (const std::exception &e) {
        Logger::error() << "Receiver task failed: " << e.what();
      }
    } else if (is_client_mode && sender_future.has_value()) {
      try {
        sender_future->wait();
        sender_future->get();
        Logger::info() << "Sender task completed successfully";
      } catch (const std::exception &e) {
        Logger::error() << "Sender task failed: " << e.what();
      }
    }
  } catch (const std::exception &e) {
    Logger::error() << "Error: " << e.what();
    return 1;
  }

  loop->close();
  looper.join();

  return 0;
}