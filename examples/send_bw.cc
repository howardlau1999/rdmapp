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

#include "rdmapp/executor.h"
#include "rdmapp/mr.h"
#include "rdmapp/qp.h"

using namespace std::literals::chrono_literals;

rdmapp::task<void> server_worker(std::shared_ptr<rdmapp::qp> qp) {
  std::vector<uint8_t> buffer;
  buffer.resize(2 * 1024 * 1024);
  std::shared_ptr<rdmapp::local_mr> local_mr =
      qp->pd_ptr()->reg_mr(&buffer[0], buffer.size());
  for (size_t i = 0; i < 8 * 1024; ++i) {
    co_await qp->send(local_mr);
    if (i % 1024 == 0) {
      std::cout << i << std::endl;
    }
  }
  co_return;
}

rdmapp::task<void> server(rdmapp::acceptor &acceptor) {
  auto qp = co_await acceptor.accept();
  std::vector<std::shared_future<void>> futures;
  auto tik = std::chrono::high_resolution_clock::now();
  for (size_t i = 0; i < 4; ++i) {
    auto task = server_worker(qp);
    futures.emplace_back(task.get_future());
    task.detach();
  }
  for (auto &fut : futures) {
    fut.get();
  }
  auto tok = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> seconds = tok - tik;
  std::cout << seconds.count() << "s" << std::endl;
  co_return;
}

rdmapp::task<void> client_worker(std::shared_ptr<rdmapp::qp> qp) {
  std::vector<uint8_t> buffer;
  buffer.resize(2 * 1024 * 1024);
  std::shared_ptr<rdmapp::local_mr> local_mr =
      qp->pd_ptr()->reg_mr(&buffer[0], buffer.size());
  for (size_t i = 0; i < 8 * 1024; ++i) {
    co_await qp->recv(local_mr);
    if (i % 1024 == 0) {
      std::cout << i << std::endl;
    }
  }
  co_return;
}

rdmapp::task<void> client(rdmapp::connector &connector) {
  auto qp = co_await connector.connect();
  std::vector<std::shared_future<void>> futures;
  for (size_t i = 0; i < 4; ++i) {
    auto task = client_worker(qp);
    futures.emplace_back(task.get_future());
    task.detach();
  }
  auto tik = std::chrono::high_resolution_clock::now();
  for (auto &fut : futures) {
    fut.get();
  }
  auto tok = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> seconds = tok - tik;
  std::cout << seconds.count() << "s" << std::endl;
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