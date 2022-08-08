#include <algorithm>
#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <rdmapp/rdmapp.h>
#include <string>
#include <thread>

#include "rdmapp/acceptor.h"
#include "rdmapp/socket/channel.h"
#include "rdmapp/socket/tcp_connection.h"

using namespace std::literals::chrono_literals;

rdmapp::task<void> server(rdmapp::acceptor &acceptor) {
  auto qp = co_await acceptor.accept();
  char buffer[6] = "hello";
  co_await qp->send(buffer, sizeof(buffer));
  std::cout << "Sent to client: " << buffer << std::endl;
  co_await qp->recv(buffer, sizeof(buffer));
  std::cout << "Received from client: " << buffer << std::endl;
  co_return;
}

rdmapp::task<int> client(std::shared_ptr<rdmapp::pd> pd,
                         std::shared_ptr<rdmapp::cq> cq,
                         std::shared_ptr<rdmapp::socket::event_loop> loop,
                         std::string const &hostname, uint16_t port) {
  auto connection =
      co_await rdmapp::socket::tcp_connection::connect(loop, hostname, port);
  auto remote_qp = co_await rdmapp::qp::recv_qp(*connection);
  auto qp = std::make_shared<rdmapp::qp>(
      remote_qp.header.lid, remote_qp.header.qp_num, remote_qp.header.sq_psn, pd, cq);
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
  auto loop = rdmapp::socket::event_loop::new_loop();
  auto looper = std::thread([loop]() { loop->loop(); });
  if (argc == 2) {
    rdmapp::acceptor acceptor(pd, cq, loop, std::stoi(argv[1]));
    auto coro = server(acceptor);
    while (!coro.h_.done()) {
      std::this_thread::yield();
    }
    if (auto exception = coro.h_.promise().exception_) {
      std::rethrow_exception(exception);
    }
  } else if (argc == 3) {
    auto coro = client(pd, cq, loop, argv[1], std::stoi(argv[2]));
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
  loop->close();
  looper.join();
  return 0;
}