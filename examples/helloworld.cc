#include <algorithm>
#include <cassert>
#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <rdmapp/rdmapp.h>
#include <string>
#include <thread>

using namespace std::literals::chrono_literals;

rdmapp::task<void> handle_qp(std::shared_ptr<rdmapp::qp> qp) {
  /* Send/Recv */
  char buffer[6] = "hello";
  co_await qp->send(buffer, sizeof(buffer));
  std::cout << "Sent to client: " << buffer << std::endl;
  co_await qp->recv(buffer, sizeof(buffer));
  std::cout << "Received from client: " << buffer << std::endl;

  /* Read/Write */
  std::copy_n("hello", sizeof(buffer), buffer);
  auto local_mr = std::shared_ptr<rdmapp::local_mr>(
      qp->pd_ptr()->reg_mr(buffer, sizeof(buffer)));
  auto local_mr_serialized = local_mr->serialize();
  co_await qp->send(local_mr_serialized.data(), local_mr_serialized.size());
  std::cout << "Sent mr addr=" << local_mr->addr()
            << " length=" << local_mr->length() << " rkey=" << local_mr->rkey()
            << " to client" << std::endl;
  auto imm = co_await qp->recv(local_mr);
  assert(imm.has_value());
  std::cout << "Written by client (imm=" << imm.value() << "): " << buffer << std::endl;

  co_return;
}

rdmapp::task<void> server(rdmapp::acceptor &acceptor) {
  while (true) {
    auto qp = co_await acceptor.accept();
    handle_qp(qp).detach();
  }
  co_return;
}

rdmapp::task<void> client(rdmapp::connector &connector) {
  auto qp = co_await connector.connect();
  char buffer[6];

  /* Send/Recv */
  co_await qp->recv(buffer, sizeof(buffer));
  std::cout << "Received from server: " << buffer << std::endl;
  std::copy_n("world", sizeof(buffer), buffer);
  co_await qp->send(buffer, sizeof(buffer));
  std::cout << "Sent to server: " << buffer << std::endl;

  /* Read/Write */
  char remote_mr_serialized[rdmapp::remote_mr::kSerializedSize];
  co_await qp->recv(remote_mr_serialized, sizeof(remote_mr_serialized));
  auto remote_mr = rdmapp::remote_mr::deserialize(remote_mr_serialized);
  std::cout << "Received mr addr=" << remote_mr.addr()
            << " length=" << remote_mr.length() << " rkey=" << remote_mr.rkey()
            << " from server" << std::endl;
  co_await qp->read(remote_mr, buffer, sizeof(buffer));
  std::cout << "Read from server: " << buffer << std::endl;
  std::copy_n("world", sizeof(buffer), buffer);
  co_await qp->write_with_imm(remote_mr, buffer, sizeof(buffer), 1);

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