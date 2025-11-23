#include "acceptor.h"
#include "connector.h"
#include "rdma_receiver.h"
#include "rdma_sender.h"
#include "rdma_util.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <random>
#include <thread>

#include <rdmapp/rdmapp.h>

using namespace RDMA_EC;

// Generate test data with pattern
std::vector<uint8_t> generate_test_data(size_t size) {
  std::vector<uint8_t> data(size);

  // Create a pattern for verification
  for (size_t i = 0; i < size; ++i) {
    data[i] = static_cast<uint8_t>((i * 7 + 42) % 256);
  }

  return data;
}

// Verify received data
bool verify_data(const std::vector<uint8_t> &sent,
                 const std::vector<uint8_t> &received) {
  if (sent.size() != received.size()) {
    std::cerr << "Size mismatch: sent " << sent.size() << " vs received "
              << received.size() << std::endl;
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
    std::cerr << "Data mismatch at byte " << first_mismatch << ": expected "
              << static_cast<int>(sent[first_mismatch]) << " got "
              << static_cast<int>(received[first_mismatch]) << std::endl;
    std::cerr << "Showing first 10 mismatches:" << std::endl;
    size_t shown = 0;
    for (size_t i = first_mismatch; i < sent.size() && shown < 10; ++i) {
      if (sent[i] != received[i]) {
        std::cerr << "  Byte " << i << ": expected "
                  << static_cast<int>(sent[i]) << " got "
                  << static_cast<int>(received[i]) << std::endl;
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
        std::cout << "Loaded configuration from " << config_file << std::endl;
        config.print();
      } else {
        std::cout << "Warning: Failed to load config file, using defaults"
                  << std::endl;
      }
    }

    size_t buffer_size = config.buffer_size;

    if (!is_client_mode) {
      // Server mode: [port] [config_file] - acts as receiver (listens for
      // connections)
      int port = std::stoi(argv[1]);
      std::cout << "Starting as RECEIVER on port " << port << std::endl;

      // Override buffer_size for receiver (double size for safety)
      config.buffer_size = buffer_size * 2;

      // Create CQs with larger size to handle more completions
      auto send_cq = std::make_shared<rdmapp::cq>(device, 2048);
      auto recv_cq = std::make_shared<rdmapp::cq>(device, 2048);

      // Create cq_poller for receiver's SEND completions (for CTS message)
      // NOTE: The receiver's completion thread polls recv_cq for receive
      // completions (packets). But we need a cq_poller on send_cq to process
      // send completions (CTS message)
      send_cq_poller = std::make_shared<rdmapp::cq_poller>(send_cq);

      auto acceptor =
          std::make_shared<rdmapp::acceptor>(loop, port, pd, recv_cq, send_cq);

      rdmapp::task<void> receiver_task = [acceptor, recv_cq, buffer_size,
                                          config]() -> rdmapp::task<void> {
        RDMAReceiver receiver(acceptor, recv_cq, config);

        std::cout << "\n=== RECEIVER STARTING ===" << std::endl;
        std::cout << "Expecting " << buffer_size << " bytes" << std::endl;
        std::cout << "MTU: " << config.mtu
                  << ", Chunk size: " << config.chunk_size << std::endl;

        auto start_time = std::chrono::high_resolution_clock::now();

        auto received_data = co_await receiver.receive_data(buffer_size);

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                            end_time - start_time)
                            .count();

        std::cout << "=== RECEIVER COMPLETE ===" << std::endl;
        std::cout << "Received " << receiver.get_packets_received()
                  << " packets, " << receiver.get_bytes_received() << " bytes"
                  << std::endl;
        std::cout << "Transfer time: " << duration << " ms" << std::endl;
        if (duration > 0) {
          std::cout << "Throughput: "
                    << (receiver.get_bytes_received() * 1000.0 / duration /
                        1024 / 1024)
                    << " MB/s" << std::endl;
        }
        co_return;
      }();

      receiver_task.detach();
    } else if (argc >= 3) {
      // Client mode: [receiver_ip] [port] [config_file] - acts as sender
      // (connects to receiver)
      std::string receiver_ip = argv[1];
      int port_idx = 2;

      // If config file was provided as last arg, port is still at index 2
      // Format is always: receiver_ip port [config_file]
      int port = std::stoi(argv[port_idx]);
      std::cout << "Starting as SENDER connecting to " << receiver_ip << ":"
                << port << std::endl;

      // Override buffer_size for sender (double size for safety)
      config.buffer_size = buffer_size * 2;

      // Create CQs with larger size to handle more completions
      auto send_cq = std::make_shared<rdmapp::cq>(device, 2048);
      auto recv_cq = std::make_shared<rdmapp::cq>(device, 2048);

      // Create cq_poller for sender's send completions (packets)
      send_cq_poller = std::make_shared<rdmapp::cq_poller>(send_cq);
      // Create cq_poller for sender's receive completions (CTS from receiver)
      recv_cq_poller = std::make_shared<rdmapp::cq_poller>(recv_cq);

      auto connector = std::make_shared<rdmapp::connector>(
          loop, receiver_ip, port, pd, recv_cq, send_cq);

      rdmapp::task<void> sender_task = [connector, buffer_size,
                                        config]() -> rdmapp::task<void> {
        RDMASender sender(connector, config);

        auto large_data_buffer = generate_test_data(buffer_size);

        std::cout << "\n=== SENDER STARTING ===" << std::endl;
        std::cout << "Sending " << buffer_size << " bytes" << std::endl;
        std::cout << "MTU: " << config.mtu
                  << ", Chunk size: " << config.chunk_size << std::endl;

        auto start_time = std::chrono::high_resolution_clock::now();

        co_await sender.send_data(large_data_buffer.data(),
                                  large_data_buffer.size());

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                            end_time - start_time)
                            .count();

        std::cout << "=== SENDER COMPLETE ===" << std::endl;
        std::cout << "Sent " << sender.get_packets_sent() << " packets, "
                  << sender.get_bytes_sent() << " bytes" << std::endl;
        std::cout << "Transfer time: " << duration << " ms" << std::endl;
        if (duration > 0) {
          std::cout << "Throughput: "
                    << (sender.get_bytes_sent() * 1000.0 / duration / 1024 /
                        1024)
                    << " MB/s" << std::endl;
        }
        co_return;
      }();

      sender_task.detach();
    } else {
      std::cerr << "Usage:" << std::endl;
      std::cerr << "  Receiver (server): " << argv[0] << " <port> [config_file]"
                << std::endl;
      std::cerr << "  Sender (client):   " << argv[0]
                << " <receiver_ip> <port> [config_file]" << std::endl;
      std::cerr << std::endl;
      std::cerr << "  config_file: Optional path to .config file (default: use "
                   "built-in defaults)"
                << std::endl;
      return 1;
    }
    std::this_thread::sleep_for(std::chrono::seconds(10));
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  loop->close();
  looper.join();

  return 0;
}
