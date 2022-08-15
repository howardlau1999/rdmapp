# RDMA++

[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)

[Documentation](https://liuhaohua.com/rdmapp/annotated.html) | [Examples](./examples)

This library encapsulates the details of building IB queue pairs and provides a user-friendly modern C++ interface, featuring coroutines.

## Quick Example

Initialize the device, create a protection domain and create a completion queue with a corresponding poller, plus an event loop for QP exchanges with sockets:

```cpp
#include <rdmapp/rdmapp.h>

int main(int argc, char *argv[]) {
  auto device = std::make_shared<rdmapp::device>(0, 1);
  auto pd = std::make_shared<rdmapp::pd>(device);
  auto cq = std::make_shared<rdmapp::cq>(device);
  auto cq_poller = std::make_shared<rdmapp::cq_poller>(cq);
  auto loop = rdmapp::socket::event_loop::new_loop();
  auto looper = std::thread([loop]() { loop->loop(); });
  looper.detach();
}
```

On the server side, create an acceptor to accept QPs:

```cpp
rdmapp::task<void> handle_qp(std::shared_ptr<rdmapp::qp> qp) {
  char buffer[6] = "hello";
  co_await qp->send(buffer, sizeof(buffer));
  std::cout << "Sent to client: " << buffer << std::endl;
  co_await qp->recv(buffer, sizeof(buffer));
  std::cout << "Received from client: " << buffer << std::endl;
  co_return;
}

rdmapp::task<void> server(rdmapp::acceptor &acceptor) {
  while (true) {
    auto qp = co_await acceptor.accept();
    handle_qp(qp).detach();
  }
  co_return;
}

int main() {
  // ...
  rdmapp::acceptor acceptor(pd, cq, loop, 2333);
  auto coro = server(acceptor);
  coro.get_future().get();
}
```

On the client side, connect to server:

```cpp
rdmapp::task<void> client(rdmapp::connector &connector) {
  auto qp = co_await connector.connect();
  char buffer[6];
  co_await qp->recv(buffer, sizeof(buffer));
  std::cout << "Received from server: " << buffer << std::endl;
  std::copy_n("world", sizeof(buffer), buffer);
  co_await qp->send(buffer, sizeof(buffer));
  std::cout << "Sent to server: " << buffer << std::endl;
  co_return;
}


int main(int argc, char *argv[]) {
  // ...
  rdmapp::connector connector(loop, "127.0.0.1", 2333, pd, cq);
  auto coro = client(connector);
  coro.get_future().get();
}
```

Browse [`examples`](/examples) to learn more about this library.

## Building

Requires: C++ compiler with C++20 standard support and `libibverbs` development headers installed.

```bash
git clone https://github.com/howardlau1999/rdmapp && cd rdmapp
cmake -Bbuild -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR .
cmake --build build

# To install
cmake --install build
```

## Developing

Install `clang-format` and `pre-commit`. 

```bash
pip install pre-commit
pre-commit install
```