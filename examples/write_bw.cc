#include "acceptor.h"
#include "connector.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <ratio>
#include <string>
#include <thread>

#include <rdmapp/rdmapp.h>

using namespace std::literals::chrono_literals;

std::atomic<size_t> gSendCount = 0;

constexpr size_t kBufferSizeBytes = 8;
constexpr size_t kSendCount = 1024 * 1024 * 1024;

rdmapp::task<void> client_worker(std::shared_ptr<rdmapp::qp> qp) {
  std::vector<uint8_t> buffer;
  buffer.resize(kBufferSizeBytes);
  auto local_mr = std::make_shared<rdmapp::local_mr>(
      qp->pd_ptr()->reg_mr(&buffer[0], buffer.size()));
  char remote_mr_serialized[rdmapp::remote_mr::kSerializedSize];
  co_await qp->recv(remote_mr_serialized, sizeof(remote_mr_serialized));
  auto remote_mr = rdmapp::remote_mr::deserialize(remote_mr_serialized);
  std::cout << "Received mr addr=" << remote_mr.addr()
            << " length=" << remote_mr.length() << " rkey=" << remote_mr.rkey()
            << " from server" << std::endl;
  for (size_t i = 0; i < kSendCount; ++i) {
    co_await qp->write(remote_mr, local_mr);
    gSendCount.fetch_add(1);
  }
  co_await qp->write_with_imm(remote_mr, local_mr, 0xDEADBEEF);
  co_return;
}

rdmapp::task<void> server(rdmapp::acceptor &acceptor) {
  auto qp = co_await acceptor.accept();
  std::vector<uint8_t> buffer;
  buffer.resize(kBufferSizeBytes);
  auto local_mr = std::make_shared<rdmapp::local_mr>(
      qp->pd_ptr()->reg_mr(&buffer[0], sizeof(buffer)));
  auto local_mr_serialized = local_mr->serialize();
  co_await qp->send(local_mr_serialized.data(), local_mr_serialized.size());
  std::cout << "Sent mr addr=" << local_mr->addr()
            << " length=" << local_mr->length() << " rkey=" << local_mr->rkey()
            << " to client" << std::endl;
  auto imm = (co_await qp->recv(local_mr)).second;
  if (!imm.has_value()) {
    throw std::runtime_error("No imm received");
  }
  if (imm.value() != 0xDEADBEEF) {
    throw std::runtime_error("Wrong imm received");
  }
  co_return;
}

rdmapp::task<void> client(rdmapp::connector &connector) {
  auto qp = co_await connector.connect();
  co_await client_worker(qp);
  co_return;
}

int main(int argc, char *argv[]) {
  auto device = std::make_shared<rdmapp::device>(0, 1);
  auto pd = std::make_shared<rdmapp::pd>(device);
  auto cq = std::make_shared<rdmapp::cq>(device);
  auto cq_poller = std::make_shared<rdmapp::cq_poller>(cq);
  auto loop = rdmapp::socket::event_loop::new_loop();
  auto looper = std::thread([loop]() { loop->loop(); });
  auto reporter = std::thread([&]() {
    while (true) {
      std::this_thread::sleep_for(1s);
      std::cout << "IOPS: " << gSendCount.exchange(0) << std::endl;
    }
  });
  if (argc == 2) {
    rdmapp::acceptor acceptor(loop, std::stoi(argv[1]), pd, cq);
    server(acceptor);
  } else if (argc == 3) {
    rdmapp::connector connector(loop, argv[1], std::stoi(argv[2]), pd, cq);
    client(connector);
  } else {
    std::cout << "Usage: " << argv[0] << " [port] for server and " << argv[0]
              << " [server_ip] [port] for client" << std::endl;
  }
  loop->close();
  looper.join();
  reporter.join();
  return 0;
}
