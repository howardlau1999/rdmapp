#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include <rdmapp/rdmapp.h>

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
  auto local_mr = std::make_shared<rdmapp::local_mr>(
      qp->pd_ptr()->reg_mr(&buffer[0], sizeof(buffer)));
  auto local_mr_serialized = local_mr->serialize();
  co_await qp->send(local_mr_serialized.data(), local_mr_serialized.size());
  std::cout << "Sent mr addr=" << local_mr->addr()
            << " length=" << local_mr->length() << " rkey=" << local_mr->rkey()
            << " to client" << std::endl;
  auto imm = co_await qp->recv(local_mr);
  assert(imm.has_value());
  std::cout << "Written by client (imm=" << imm.value() << "): " << buffer
            << std::endl;

  uint64_t counter = 42;
  auto counter_mr = std::make_shared<rdmapp::local_mr>(
      qp->pd_ptr()->reg_mr(&counter, sizeof(counter)));
  auto counter_mr_serialized = counter_mr->serialize();
  co_await qp->send(counter_mr_serialized.data(), counter_mr_serialized.size());
  std::cout << "Sent mr addr=" << counter_mr->addr()
            << " length=" << counter_mr->length()
            << " rkey=" << counter_mr->rkey() << " to client" << std::endl;
  imm = co_await qp->recv(local_mr);
  assert(imm.has_value());
  std::cout << "Fetched and added by client: " << counter << std::endl;
  imm = co_await qp->recv(local_mr);
  assert(imm.has_value());
  std::cout << "Compared and swapped by client: " << counter << std::endl;

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

  char counter_mr_serialized[rdmapp::remote_mr::kSerializedSize];
  co_await qp->recv(counter_mr_serialized, sizeof(counter_mr_serialized));
  auto counter_mr = rdmapp::remote_mr::deserialize(counter_mr_serialized);
  std::cout << "Received mr addr=" << counter_mr.addr()
            << " length=" << counter_mr.length()
            << " rkey=" << counter_mr.rkey() << " from server" << std::endl;
  uint64_t counter = 0;
  co_await qp->fetch_and_add(counter_mr, &counter, sizeof(counter), 1);
  std::cout << "Fetched and added from server: " << counter << std::endl;
  co_await qp->write_with_imm(remote_mr, buffer, sizeof(buffer), 1);
  co_await qp->compare_and_swap(counter_mr, &counter, sizeof(counter), 43,
                                4422);
  std::cout << "Compared and swapped from server: " << counter << std::endl;
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