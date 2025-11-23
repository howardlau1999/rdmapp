#include "rdma_sender.h"
#include "rdma_receiver.h"
#include "rdma_util.h"
#include "acceptor.h"
#include "connector.h"
#include <rdmapp/rdmapp.h>
#include <iostream>
#include <thread>
#include <random>
#include <cstring>
#include <chrono>
#include <atomic>

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
bool verify_data(const std::vector<uint8_t>& sent, 
                 const std::vector<uint8_t>& received) {
    if (sent.size() != received.size()) {
        std::cerr << "Size mismatch: sent " << sent.size() 
                  << " vs received " << received.size() << std::endl;
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
        std::cerr << "Data mismatch at byte " << first_mismatch 
                  << ": expected " << static_cast<int>(sent[first_mismatch]) 
                  << " got " << static_cast<int>(received[first_mismatch]) << std::endl;
        std::cerr << "Showing first 10 mismatches:" << std::endl;
        size_t shown = 0;
        for (size_t i = first_mismatch; i < sent.size() && shown < 10; ++i) {
            if (sent[i] != received[i]) {
                std::cerr << "  Byte " << i << ": expected " 
                          << static_cast<int>(sent[i]) 
                          << " got " << static_cast<int>(received[i]) << std::endl;
                shown++;
            }
        }
        return false;
    }
    
    return true;
}

int main(int argc, char *argv[]) {
    srand(42);
    
    size_t buffer_size = DEFAULT_BUFFER_SIZE;
    size_t chunk_size = DEFAULT_CHUNK_SIZE;
    size_t mtu = DEFAULT_MTU;
    
    // Initialize RDMA device with GID index 3 (as in chunked_transmission.cc)
    auto device = std::make_shared<rdmapp::device>(0, 1, 3);
    auto pd = std::make_shared<rdmapp::pd>(device);
    // Use a single shared CQ for both sends and receives
    auto cq = std::make_shared<rdmapp::cq>(device);
    // cq_poller will be created conditionally on receiver side
    std::shared_ptr<rdmapp::cq_poller> cq_poller;
    auto loop = rdmapp::socket::event_loop::new_loop();
    auto looper = std::thread([loop]() { loop->loop(); });
    
    try {
        if (argc == 2) {
            if (std::string(argv[1]) == "loopback") {
                // Loopback mode: uses default buffer_size and chunk_size
                std::cout << "Starting LOOPBACK test with buffer_size=" 
                          << buffer_size << ", chunk_size=" << chunk_size << std::endl;
                
                // Generate test data for verification
                auto test_data = generate_test_data(buffer_size);
                
                Config config;
                config.mtu = mtu;
                config.chunk_size = chunk_size;
                config.buffer_size = buffer_size * 2;
                
                // For loopback mode, use separate CQs for sender and receiver
                // This avoids competition between cq_poller and receiver's completion thread
                auto send_cq = std::make_shared<rdmapp::cq>(device);
                auto recv_cq = std::make_shared<rdmapp::cq>(device);
                
                // Create cq_poller for sender's send completions
                cq_poller = std::make_shared<rdmapp::cq_poller>(send_cq);
                
                // Create acceptor and connector for loopback with separate CQs
                int port = 12345;
                auto acceptor = std::make_shared<rdmapp::acceptor>(loop, port, pd, recv_cq, send_cq);
                auto connector = std::make_shared<rdmapp::connector>(loop, "128.110.219.158", port, pd, recv_cq, send_cq);
                
                // Start sender in background
                std::thread sender_thread([acceptor, buffer_size, config]() {
                    rdmapp::task<void> sender_task = [acceptor, buffer_size, config]() -> rdmapp::task<void> {
                        RDMASender sender(acceptor, config);
                        auto large_data_buffer = generate_test_data(buffer_size);
                        co_await sender.send_data(large_data_buffer.data(), large_data_buffer.size());
                        co_return;
                    }();
                    sender_task.detach();
                });
                
                // Small delay to ensure sender is ready
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                
                // Run receiver and wait for completion
                std::atomic<bool> receiver_done{false};
                rdmapp::task<void> receiver_task = [connector, recv_cq, buffer_size, config, &test_data, &receiver_done]() -> rdmapp::task<void> {
                    RDMAReceiver receiver(connector, recv_cq, config);
                    auto received_data = co_await receiver.receive_data(buffer_size);
                    
                    bool success = verify_data(test_data, received_data);
                    if (success) {
                        std::cout << "✓ Data verification PASSED" << std::endl;
                    } else {
                        std::cout << "✗ Data verification FAILED" << std::endl;
                    }
                    receiver_done = true;
                    co_return;
                }();
                
                receiver_task.detach();
                
                // Wait for receiver to complete (sender should be done by then)
                while (!receiver_done.load()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                
                // Give sender thread a moment to finish cleanup
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                sender_thread.join();
                
                std::cout << "\n=== LOOPBACK TEST COMPLETE ===" << std::endl;
            } else {
                // Server mode: [port] - acts as sender
                int port = std::stoi(argv[1]);
                std::cout << "Starting as SENDER on port " << port << std::endl;
                
                Config config;
                config.mtu = mtu;
                config.chunk_size = chunk_size;
                config.buffer_size = buffer_size * 2;
                
                auto acceptor = std::make_shared<rdmapp::acceptor>(loop, port, pd, cq);
                
                // Directly use RDMASender
                rdmapp::task<void> sender_task = [acceptor, buffer_size, config]() -> rdmapp::task<void> {
                    RDMASender sender(acceptor, config);
                    
                    // Generate test data
                    auto large_data_buffer = generate_test_data(buffer_size);
                    
                    std::cout << "\n=== SENDER STARTING ===" << std::endl;
                    std::cout << "Sending " << buffer_size << " bytes" << std::endl;
                    std::cout << "MTU: " << config.mtu << ", Chunk size: " << config.chunk_size << std::endl;
                    
                    auto start_time = std::chrono::high_resolution_clock::now();
                    
                    co_await sender.send_data(large_data_buffer.data(), large_data_buffer.size());
                    
                    auto end_time = std::chrono::high_resolution_clock::now();
                    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                        end_time - start_time).count();
                    
                    std::cout << "=== SENDER COMPLETE ===" << std::endl;
                    std::cout << "Sent " << sender.get_packets_sent() << " packets, "
                              << sender.get_bytes_sent() << " bytes" << std::endl;
                    std::cout << "Transfer time: " << duration << " ms" << std::endl;
                    if (duration > 0) {
                        std::cout << "Throughput: " << (sender.get_bytes_sent() * 1000.0 / duration / 1024 / 1024)
                                  << " MB/s" << std::endl;
                    }
                    co_return;
                }();
                
                sender_task.detach();
            }
        } else if (argc == 3) {
            // Client mode: [server_ip] [port] - acts as receiver
            std::string server_ip = argv[1];
            int port = std::stoi(argv[2]);
            std::cout << "Starting as RECEIVER connecting to " 
                      << server_ip << ":" << port << std::endl;
            
            Config config;
            config.mtu = mtu;
            config.chunk_size = chunk_size;
            config.buffer_size = buffer_size * 2;
            
            // Create cq_poller on receiver side
            cq_poller = std::make_shared<rdmapp::cq_poller>(cq);
            
            auto connector = std::make_shared<rdmapp::connector>(loop, server_ip, port, pd, cq);
            
            // Directly use RDMAReceiver
            rdmapp::task<void> receiver_task = [connector, cq, buffer_size, config]() -> rdmapp::task<void> {
                RDMAReceiver receiver(connector, cq, config);
                
                std::cout << "\n=== RECEIVER STARTING ===" << std::endl;
                std::cout << "Expecting " << buffer_size << " bytes" << std::endl;
                std::cout << "MTU: " << config.mtu << ", Chunk size: " << config.chunk_size << std::endl;
                
                auto start_time = std::chrono::high_resolution_clock::now();
                
                auto received_data = co_await receiver.receive_data(buffer_size);
                
                auto end_time = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    end_time - start_time).count();
                
                std::cout << "=== RECEIVER COMPLETE ===" << std::endl;
                std::cout << "Received " << receiver.get_packets_received() << " packets, "
                          << receiver.get_bytes_received() << " bytes" << std::endl;
                std::cout << "Transfer time: " << duration << " ms" << std::endl;
                if (duration > 0) {
                    std::cout << "Throughput: " << (receiver.get_bytes_received() * 1000.0 / duration / 1024 / 1024)
                              << " MB/s" << std::endl;
                }
                co_return;
            }();
            
            receiver_task.detach();
        } else if (argc == 4) {
            if (std::string(argv[1]).find('.') != std::string::npos ||
                std::string(argv[1]).find(':') != std::string::npos) {
                // Client mode: [server_ip] [port] [buffer_size]
                std::string server_ip = argv[1];
                int port = std::stoi(argv[2]);
                buffer_size = std::stoull(argv[3]);
                
                std::cout << "Starting as RECEIVER connecting to " 
                          << server_ip << ":" << port 
                          << " with buffer_size=" << buffer_size << std::endl;
                
                Config config;
                config.mtu = mtu;
                config.chunk_size = chunk_size;
                config.buffer_size = buffer_size * 2;
                
                // Create cq_poller on receiver side
                cq_poller = std::make_shared<rdmapp::cq_poller>(cq);
                
                auto connector = std::make_shared<rdmapp::connector>(loop, server_ip, port, pd, cq);
                
                rdmapp::task<void> receiver_task = [connector, cq, buffer_size, config]() -> rdmapp::task<void> {
                    RDMAReceiver receiver(connector, cq, config);
                    auto received_data = co_await receiver.receive_data(buffer_size);
                    std::cout << "Received " << receiver.get_bytes_received() << " bytes" << std::endl;
                    co_return;
                }();
                
                receiver_task.detach();
                
            } else {
                // Server mode: [port] [buffer_size] [chunk_size]
                int port = std::stoi(argv[1]);
                buffer_size = std::stoull(argv[2]);
                chunk_size = std::stoull(argv[3]);
                
                std::cout << "Starting as SENDER on port " << port 
                          << " with buffer_size=" << buffer_size 
                          << ", chunk_size=" << chunk_size << std::endl;
                
                Config config;
                config.mtu = mtu;
                config.chunk_size = chunk_size;
                config.buffer_size = buffer_size * 2;
                
                auto acceptor = std::make_shared<rdmapp::acceptor>(loop, port, pd, cq);
                
                rdmapp::task<void> sender_task = [acceptor, buffer_size, config]() -> rdmapp::task<void> {
                    RDMASender sender(acceptor, config);
                    auto large_data_buffer = generate_test_data(buffer_size);
                    co_await sender.send_data(large_data_buffer.data(), large_data_buffer.size());
                    co_return;
                }();
                
                sender_task.detach();
            }
        } else if (argc == 5) {
            // Client mode: [server_ip] [port] [buffer_size] [chunk_size]
            std::string server_ip = argv[1];
            int port = std::stoi(argv[2]);
            buffer_size = std::stoull(argv[3]);
            chunk_size = std::stoull(argv[4]);
            
            std::cout << "Starting as RECEIVER connecting to " 
                      << server_ip << ":" << port 
                      << " with buffer_size=" << buffer_size 
                      << ", chunk_size=" << chunk_size << std::endl;
            
            Config config;
            config.mtu = mtu;
            config.chunk_size = chunk_size;
            config.buffer_size = buffer_size * 2;
            
            // Create cq_poller on receiver side
            cq_poller = std::make_shared<rdmapp::cq_poller>(cq);
            
            auto connector = std::make_shared<rdmapp::connector>(loop, server_ip, port, pd, cq);
            
            rdmapp::task<void> receiver_task = [connector, cq, buffer_size, config]() -> rdmapp::task<void> {
                RDMAReceiver receiver(connector, cq, config);
                auto received_data = co_await receiver.receive_data(buffer_size);
                std::cout << "Received " << receiver.get_bytes_received() << " bytes" << std::endl;
                co_return;
            }();
            
            receiver_task.detach();
        } else {
            std::cerr << "Usage:" << std::endl;
            std::cerr << "  Server mode:    " << argv[0] << " <port>" << std::endl;
            std::cerr << "  Client mode:    " << argv[0] << " <server_ip> <port>" << std::endl;
            std::cerr << "  Loopback mode:  " << argv[0] << " loopback" << std::endl;
            std::cerr << "  Server custom:  " << argv[0] << " <port> <buffer_size> <chunk_size>" << std::endl;
            std::cerr << "  Client custom:  " << argv[0] << " <server_ip> <port> <buffer_size> [chunk_size]" << std::endl;
            std::cerr << "  Defaults: buffer_size=" << DEFAULT_BUFFER_SIZE 
                      << ", chunk_size=" << DEFAULT_CHUNK_SIZE 
                      << ", mtu=" << DEFAULT_MTU << std::endl;
            return 1;
        }
        
        // Wait for tasks to complete
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    // Cleanup
    loop->close();
    looper.join();
    
    return 0;
}
