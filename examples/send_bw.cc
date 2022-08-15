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

constexpr size_t kBufferSizeBytes = 2 * 1024 * 1024; // 2 MB
constexpr size_t kQPCount = 1;
constexpr size_t kWorkerCount = 4;
constexpr size_t kSendCount = 8 * 1024;
constexpr size_t kPrintInterval = 1024;
constexpr size_t kTotalSizeBytes = kBufferSizeBytes * kSendCount * kWorkerCount;

template <bool Client = false>
rdmapp::task<void> worker(size_t id, std::shared_ptr<rdmapp::qp> qp) {
  std::vector<uint8_t> buffer;
  buffer.resize(kBufferSizeBytes);
  auto local_mr = std::make_shared<rdmapp::local_mr>(
      qp->pd_ptr()->reg_mr(&buffer[0], buffer.size()));
  std::cout << "Worker " << id << " started" << std::endl;
  for (size_t i = 0; i < kSendCount; ++i) {
    if constexpr (Client) {
      co_await qp->recv(local_mr);
    } else {
      co_await qp->send(local_mr);
    }
    if ((i + 1) % kPrintInterval == 0) {
      std::cout << "Worker " << id << (Client ? " recv " : " sent ") << (i + 1)
                << " times" << std::endl;
    }
  }
  std::cout << "Worker " << id << " exited" << std::endl;
  co_return;
}

template <bool Client = false>
rdmapp::task<void> handler(std::shared_ptr<rdmapp::qp> qp) {
  std::vector<std::shared_future<void>> futures;
  for (size_t i = 0; i < kWorkerCount; ++i) {
    auto task = worker<Client>(i, qp);
    futures.emplace_back(task.get_future());
    task.detach();
  }
  auto tik = std::chrono::high_resolution_clock::now();
  for (auto &fut : futures) {
    fut.get();
  }
  auto tok = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> seconds = tok - tik;
  double mb = static_cast<double>(kTotalSizeBytes) / 1024 / 1024;
  double throughput = mb / seconds.count();
  std::cout << "Total: " << mb << " MB, Elapsed: " << seconds.count()
            << " s, Throughput: " << throughput << " MB/s" << std::endl;
  co_return;
}

rdmapp::task<void> server(rdmapp::acceptor &acceptor) {
  auto qp = co_await acceptor.accept();
  co_await handler(qp);
  co_return;
}

rdmapp::task<void> client(rdmapp::connector &connector) {
  auto qp = co_await connector.connect();
  co_await handler<true>(qp);
  co_return;
}

int main(int argc, char *argv[]) {
  auto device = std::make_shared<rdmapp::device>(0, 1);
  auto pd = std::make_shared<rdmapp::pd>(device);
  auto cq = std::make_shared<rdmapp::cq>(device);
  auto cq_poller = std::make_shared<rdmapp::cq_poller>(cq);
  auto loop = rdmapp::socket::event_loop::new_loop();
  auto looper = std::thread([loop]() { loop->loop(); });
  if (argc == 2) {
    rdmapp::acceptor acceptor(loop, std::stoi(argv[1]), pd, cq);
    auto coro = server(acceptor);
    coro.get_future().get();
  } else if (argc == 3) {
    rdmapp::connector connector(loop, argv[1], std::stoi(argv[2]), pd, cq);
    auto coro = client(connector);
    coro.get_future().get();
  } else {
    std::cout << "Usage: " << argv[0] << " [port] for server and " << argv[0]
              << " [server_ip] [port] for client" << std::endl;
  }
  loop->close();
  looper.join();
  return 0;
}
