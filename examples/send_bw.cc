#include <algorithm>
#include <cassert>
#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <ratio>
#include <rdmapp/rdmapp.h>
#include <string>
#include <thread>

using namespace std::literals::chrono_literals;

constexpr size_t kBufferSize = 2 * 1024 * 1024; // 2 MB
constexpr size_t kQPCount = 1;
constexpr size_t kWorkerCount = 4;
constexpr size_t kSendCount = 8 * 1024;
constexpr size_t kTotalSize = kBufferSize * kSendCount * kWorkerCount;

rdmapp::task<void> server_worker(size_t id, std::shared_ptr<rdmapp::qp> qp) {
  std::vector<uint8_t> buffer;
  buffer.resize(kBufferSize);
  std::shared_ptr<rdmapp::local_mr> local_mr =
      qp->pd_ptr()->reg_mr(&buffer[0], buffer.size());
  std::cout << "Worker " << id << " started" << std::endl;
  for (size_t i = 0; i < kSendCount; ++i) {
    co_await qp->send(local_mr);
    if ((i + 1) % 1024 == 0) {
      std::cout << "Worker " << id << " sent " << (i + 1) << " times"
                << std::endl;
    }
  }
  std::cout << "Worker " << id << " exited" << std::endl;
  co_return;
}

rdmapp::task<void> server(rdmapp::acceptor &acceptor) {
  auto qp = co_await acceptor.accept();
  std::vector<std::shared_future<void>> futures;
  auto tik = std::chrono::high_resolution_clock::now();
  for (size_t i = 0; i < kWorkerCount; ++i) {
    auto task = server_worker(i, qp);
    futures.emplace_back(task.get_future());
    task.detach();
  }
  for (auto &fut : futures) {
    fut.get();
  }
  auto tok = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> seconds = tok - tik;
  double gb = static_cast<double>(kTotalSize) / 1024 / 1024 / 1024;
  double throughput = gb / seconds.count();
  std::cout << "Total: " << gb << " GB, Elapsed: " << seconds.count()
            << "s, Throughput: " << throughput << "GB/s" << std::endl;
  co_return;
}

rdmapp::task<void> client_worker(size_t id, std::shared_ptr<rdmapp::qp> qp) {
  std::vector<uint8_t> buffer;
  buffer.resize(kBufferSize);
  std::shared_ptr<rdmapp::local_mr> local_mr =
      qp->pd_ptr()->reg_mr(&buffer[0], buffer.size());
  std::cout << "Worker " << id << " started" << std::endl;
  for (size_t i = 0; i < kSendCount; ++i) {
    co_await qp->recv(local_mr);
    if ((i + 1) % 1024 == 0) {
      std::cout << "Worker " << id << " recv " << (i + 1) << " times"
                << std::endl;
    }
  }
  std::cout << "Worker " << id << " exited" << std::endl;
  co_return;
}

rdmapp::task<void> client(rdmapp::connector &connector) {
  auto qp = co_await connector.connect();
  std::vector<std::shared_future<void>> futures;
  for (size_t i = 0; i < kWorkerCount; ++i) {
    auto task = client_worker(i, qp);
    futures.emplace_back(task.get_future());
    task.detach();
  }
  auto tik = std::chrono::high_resolution_clock::now();
  for (auto &fut : futures) {
    fut.get();
  }
  auto tok = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> seconds = tok - tik;
  double gb = static_cast<double>(kTotalSize) / 1024 / 1024 / 1024;
  double throughput = gb / seconds.count();
  std::cout << "Total: " << gb << " GB, Elapsed: " << seconds.count()
            << "s, Throughput: " << throughput << "GB/s" << std::endl;
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
    coro.get_future().wait();
    if (auto exception = coro.get_exception()) {
      std::rethrow_exception(exception);
    }
  } else if (argc == 3) {
    rdmapp::connector connector(loop, argv[1], std::stoi(argv[2]), pd, cq);
    auto coro = client(connector);
    coro.get_future().wait();
    if (auto exception = coro.get_exception()) {
      std::rethrow_exception(exception);
    }
  } else {
    std::cout << "Usage: " << argv[0] << " [port] for server and " << argv[0]
              << " [server_ip] [port] for client" << std::endl;
  }
  loop->close();
  looper.join();
  return 0;
}
