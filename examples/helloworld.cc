#include <algorithm>
#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <rdmapp/rdmapp.h>
#include <string>
#include <thread>

using namespace std::literals::chrono_literals;

rdmapp::task<void> server(std::shared_ptr<rdmapp::qp> qp) {
  char buffer[6] = "hello";
  co_await qp->send(buffer, sizeof(buffer));
  std::cout << "Sent to client: " << buffer << std::endl;
  co_await qp->recv(buffer, sizeof(buffer));
  std::cout << "Received from client: " << buffer << std::endl;
}

rdmapp::task<int> client(std::shared_ptr<rdmapp::qp> qp) {
  char buffer[6];
  co_await qp->recv(buffer, sizeof(buffer));
  std::cout << "Received from server: " << buffer << std::endl;
  std::copy_n("world", sizeof(buffer), buffer);
  co_await qp->send(buffer, sizeof(buffer));
  std::cout << "Sent to server: " << buffer << std::endl;
  co_return 0;
}

int main(int argc, char *argv[]) {
  auto device = std::make_shared<rdmapp::device>(0, 1);
  auto pd = std::make_shared<rdmapp::pd>(device);
  auto cq = std::make_shared<rdmapp::cq>(device);
  auto cq_poller = std::make_shared<rdmapp::cq_poller>(cq);
  if (argc == 2) {
    rdmapp::acceptor acceptor(pd, cq, std::stoi(argv[1]));
    auto qp = acceptor.accept();
    auto coro = server(qp);
    while (!coro.h_.done()) {
      std::this_thread::yield();
    }
    if (auto exception = coro.h_.promise().exception_) {
      std::rethrow_exception(exception);
    }
  } else if (argc == 3) {
    auto qp = std::make_shared<rdmapp::qp>(argv[1], std::stoi(argv[2]), pd, cq);
    auto coro = client(qp);
    while (!coro.h_.done()) {
      std::this_thread::yield();
    }
    if (auto exception = coro.h_.promise().exception_) {
      std::rethrow_exception(exception);
    }
  } else {
    std::cout << "Usage: " << argv[0] << " [port] for server and " << argv[0]
              << " [server_ip] [port] for client" << std::endl;
  }
  return 0;
}