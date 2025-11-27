#include "acceptor.h"
#include "connector.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <rdmapp/rdmapp.h>
#include <infiniband/verbs.h>

constexpr size_t DEFAULT_BUFFER_SIZE = 8192;
constexpr size_t DEFAULT_CHUNK_SIZE = 8192;

static enum ibv_qp_type parse_qp_type(const std::string &qp_type_str) {
  if (qp_type_str == "RC" || qp_type_str == "rc") {
    return IBV_QPT_RC;
  } else if (qp_type_str == "UC" || qp_type_str == "uc") {
    return IBV_QPT_UC;
  } else {
    std::cerr << "Invalid QP type: " << qp_type_str << ". Must be RC or UC." << std::endl;
    exit(1);
  }
}

rdmapp::task<void> handle_qp(std::shared_ptr<rdmapp::qp> qp,
                             size_t buffer_size,
                             size_t chunk_size) {
  std::vector<uint8_t> recv_buffer(buffer_size);
  auto recv_mr = std::make_shared<rdmapp::local_mr>(
      qp->pd_ptr()->reg_mr(recv_buffer.data(), recv_buffer.size()));

  auto recv_mr_serialized = recv_mr->serialize();
  co_await qp->send(recv_mr_serialized.data(), recv_mr_serialized.size());
  std::cout << "Sent receive buffer MR to client: addr=" << recv_mr->addr()
            << " length=" << recv_mr->length() << " rkey=" << recv_mr->rkey()
            << std::endl;

  char remote_mr_serialized[rdmapp::remote_mr::kSerializedSize];
  co_await qp->recv(remote_mr_serialized, sizeof(remote_mr_serialized));
  auto remote_mr = rdmapp::remote_mr::deserialize(remote_mr_serialized);
  std::cout << "Received remote MR from client: addr=" << remote_mr.addr()
            << " length=" << remote_mr.length() << " rkey=" << remote_mr.rkey()
            << std::endl;

  size_t num_chunks =
      (buffer_size + chunk_size - 1) / chunk_size;
  std::cout << "Receiving " << buffer_size << " bytes in " << num_chunks
            << " chunks via one-sided RDMA writes" << std::endl;

  auto [_, imm] = co_await qp->recv(recv_mr);
  if (imm.has_value()) {
    std::cout << "Received completion signal: " << imm.value() << std::endl;
  }

  std::cout << "All chunks received! Buffer size: " << recv_buffer.size()
            << " bytes" << std::endl;

  std::cout << "First 16 bytes of received buffer: ";
  for (size_t i = 0; i < std::min(16UL, recv_buffer.size()); ++i) {
    std::cout << std::hex << static_cast<int>(recv_buffer[i]) << " ";
  }
  std::cout << std::dec << std::endl;

  co_return;
}

rdmapp::task<void> server(rdmapp::acceptor &acceptor, size_t buffer_size, size_t chunk_size) {
  while (true) {
    auto qp = co_await acceptor.accept();
    handle_qp(qp, buffer_size, chunk_size).detach();
  }
  co_return;
}

rdmapp::task<void> client(rdmapp::connector &connector, size_t buffer_size, size_t chunk_size) {
  auto qp = co_await connector.connect();

  char remote_mr_serialized[rdmapp::remote_mr::kSerializedSize];
  co_await qp->recv(remote_mr_serialized, sizeof(remote_mr_serialized));
  auto remote_mr = rdmapp::remote_mr::deserialize(remote_mr_serialized);
  std::cout << "Received remote MR from server: addr=" << remote_mr.addr()
            << " length=" << remote_mr.length() << " rkey=" << remote_mr.rkey()
            << std::endl;

  std::vector<uint8_t> send_buffer(buffer_size);
  for (size_t i = 0; i < buffer_size; ++i) {
    send_buffer[i] = static_cast<uint8_t>(rand() % 256);
  }

  auto send_mr = std::make_shared<rdmapp::local_mr>(
      qp->pd_ptr()->reg_mr(send_buffer.data(), send_buffer.size()));

  auto send_mr_serialized = send_mr->serialize();
  co_await qp->send(send_mr_serialized.data(), send_mr_serialized.size());
  std::cout << "Sent send buffer MR to server: addr=" << send_mr->addr()
            << " length=" << send_mr->length() << " rkey=" << send_mr->rkey()
            << std::endl;

  size_t num_chunks =
      (buffer_size + chunk_size - 1) / chunk_size;
  std::cout << "Sending " << buffer_size << " bytes in " << num_chunks
            << " chunks of " << chunk_size
            << " bytes each via one-sided RDMA writes" << std::endl;

  for (size_t chunk_idx = 0; chunk_idx < num_chunks; ++chunk_idx) {
    size_t chunk_offset = chunk_idx * chunk_size;
    size_t chunk_length =
        std::min(chunk_size, buffer_size - chunk_offset);

    rdmapp::remote_mr chunk_remote_mr(
        reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(remote_mr.addr()) +
                                 chunk_offset),
        static_cast<uint32_t>(chunk_length), remote_mr.rkey());

    // Single sided writes
    std::cout << "Writing chunk " << chunk_idx << ": remote_addr=" 
              << std::hex << reinterpret_cast<uintptr_t>(chunk_remote_mr.addr()) 
              << " rkey=" << chunk_remote_mr.rkey() << " length=" << std::dec 
              << chunk_length << std::endl;
    co_await qp->write(chunk_remote_mr, send_buffer.data() + chunk_offset,
                       chunk_length);
    std::cout << "Write completed for chunk " << chunk_idx << std::endl;

    std::cout << "Sent chunk " << chunk_idx << ": offset=" << chunk_offset
              << " length=" << chunk_length << std::endl;
  }

  rdmapp::remote_mr final_remote_mr(
      reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(remote_mr.addr())),
      static_cast<uint32_t>(1), remote_mr.rkey());
  uint8_t dummy = 0;
  auto dummy_mr = std::make_shared<rdmapp::local_mr>(
      qp->pd_ptr()->reg_mr(&dummy, 1));
  co_await qp->write_with_imm(final_remote_mr, dummy_mr, 0xDEADBEEF);

  std::cout << "All chunks sent!" << std::endl;
  co_return;
}

int main(int argc, char *argv[]) {
  srand(42);

  size_t buffer_size = DEFAULT_BUFFER_SIZE;
  size_t chunk_size = DEFAULT_CHUNK_SIZE;
  enum ibv_qp_type qp_type = IBV_QPT_RC;

  auto device = std::make_shared<rdmapp::device>(0, 1, 3);
  auto pd = std::make_shared<rdmapp::pd>(device);
  auto cq = std::make_shared<rdmapp::cq>(device);
  auto cq_poller = std::make_shared<rdmapp::cq_poller>(cq);
  auto loop = rdmapp::socket::event_loop::new_loop();
  auto looper = std::thread([loop]() { loop->loop(); });

  if (argc == 2) {
    // Server: [port]
    rdmapp::acceptor acceptor(loop, std::stoi(argv[1]), pd, cq, nullptr, qp_type);
    server(acceptor, buffer_size, chunk_size);
  } else if (argc == 3) {
    if (std::string(argv[1]).find('.') != std::string::npos ||
        std::string(argv[1]).find(':') != std::string::npos) {
      // Client: [server_ip] [port]
      rdmapp::connector connector(loop, argv[1], std::stoi(argv[2]), pd, cq, nullptr, qp_type);
      client(connector, buffer_size, chunk_size);
    } else {
      // Server: [port] [buffer_size] OR [port] [qp_type]
      // Check if second arg is a number or QP type string
      if (std::string(argv[2]) == "RC" || std::string(argv[2]) == "rc" ||
          std::string(argv[2]) == "UC" || std::string(argv[2]) == "uc") {
        qp_type = parse_qp_type(argv[2]);
        rdmapp::acceptor acceptor(loop, std::stoi(argv[1]), pd, cq, nullptr, qp_type);
        server(acceptor, buffer_size, chunk_size);
      } else {
        buffer_size = std::stoull(argv[2]);
        rdmapp::acceptor acceptor(loop, std::stoi(argv[1]), pd, cq, nullptr, qp_type);
        server(acceptor, buffer_size, chunk_size);
      }
    }
  } else if (argc == 4) {
    if (std::string(argv[1]).find('.') != std::string::npos ||
        std::string(argv[1]).find(':') != std::string::npos) {
      // Client: [server_ip] [port] [buffer_size] OR [server_ip] [port] [qp_type]
      if (std::string(argv[3]) == "RC" || std::string(argv[3]) == "rc" ||
          std::string(argv[3]) == "UC" || std::string(argv[3]) == "uc") {
        qp_type = parse_qp_type(argv[3]);
        rdmapp::connector connector(loop, argv[1], std::stoi(argv[2]), pd, cq, nullptr, qp_type);
        client(connector, buffer_size, chunk_size);
      } else {
        buffer_size = std::stoull(argv[3]);
        rdmapp::connector connector(loop, argv[1], std::stoi(argv[2]), pd, cq, nullptr, qp_type);
        client(connector, buffer_size, chunk_size);
      }
    } else {
      // Server: [port] [buffer_size] [chunk_size] OR [port] [buffer_size] [qp_type]
      buffer_size = std::stoull(argv[2]);
      if (std::string(argv[3]) == "RC" || std::string(argv[3]) == "rc" ||
          std::string(argv[3]) == "UC" || std::string(argv[3]) == "uc") {
        qp_type = parse_qp_type(argv[3]);
        rdmapp::acceptor acceptor(loop, std::stoi(argv[1]), pd, cq, nullptr, qp_type);
        server(acceptor, buffer_size, chunk_size);
      } else {
        chunk_size = std::stoull(argv[3]);
        rdmapp::acceptor acceptor(loop, std::stoi(argv[1]), pd, cq, nullptr, qp_type);
        server(acceptor, buffer_size, chunk_size);
      }
    }
  } else if (argc == 5) {
    if (std::string(argv[1]).find('.') != std::string::npos ||
        std::string(argv[1]).find(':') != std::string::npos) {
      // Client: [server_ip] [port] [buffer_size] [chunk_size] OR [server_ip] [port] [buffer_size] [qp_type]
      buffer_size = std::stoull(argv[3]);
      if (std::string(argv[4]) == "RC" || std::string(argv[4]) == "rc" ||
          std::string(argv[4]) == "UC" || std::string(argv[4]) == "uc") {
        qp_type = parse_qp_type(argv[4]);
        rdmapp::connector connector(loop, argv[1], std::stoi(argv[2]), pd, cq, nullptr, qp_type);
        client(connector, buffer_size, chunk_size);
      } else {
        chunk_size = std::stoull(argv[4]);
        rdmapp::connector connector(loop, argv[1], std::stoi(argv[2]), pd, cq, nullptr, qp_type);
        client(connector, buffer_size, chunk_size);
      }
    } else {
      // Server: [port] [buffer_size] [chunk_size] [qp_type]
      buffer_size = std::stoull(argv[2]);
      chunk_size = std::stoull(argv[3]);
      qp_type = parse_qp_type(argv[4]);
      rdmapp::acceptor acceptor(loop, std::stoi(argv[1]), pd, cq, nullptr, qp_type);
      server(acceptor, buffer_size, chunk_size);
    }
  } else if (argc == 6) {
    // Client: [server_ip] [port] [buffer_size] [chunk_size] [qp_type]
    buffer_size = std::stoull(argv[3]);
    chunk_size = std::stoull(argv[4]);
    qp_type = parse_qp_type(argv[5]);
    rdmapp::connector connector(loop, argv[1], std::stoi(argv[2]), pd, cq, nullptr, qp_type);
    client(connector, buffer_size, chunk_size);
  } else {
    std::cout << "Usage: " << argv[0] << " [port] [buffer_size] [chunk_size] [qp_type] for server"
              << std::endl;
    std::cout << "       " << argv[0]
              << " [server_ip] [port] [buffer_size] [chunk_size] [qp_type] for client" << std::endl;
    std::cout << "       buffer_size defaults to " << DEFAULT_BUFFER_SIZE
              << " bytes" << std::endl;
    std::cout << "       chunk_size defaults to " << DEFAULT_CHUNK_SIZE
              << " bytes" << std::endl;
    std::cout << "       qp_type must be RC or UC (defaults to UC)" << std::endl;
  }

  loop->close();
  looper.join();
  return 0;
}
