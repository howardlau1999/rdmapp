# RDMA++

This library encapsulates the details of building IB queue pairs and provides a user-friendly modern C++ interface, featuring coroutines.

## Quick Example

Initialize the device, create a protection domain and create a completion queue with a corresponding poller:

```cpp
#include <rdmapp/rdmapp.h>

auto device = std::make_shared<rdmapp::device>(0, 1);
auto pd = std::make_shared<rdmapp::pd>(device);
auto cq = std::make_shared<rdmapp::cq>(device);
auto cq_poller = std::make_shared<rdmapp::cq_poller>(cq);
```

On the server side, create an acceptor to accept QPs:

```cpp
rdmapp::task<void> server(std::shared_ptr<rdmapp::qp> qp) {
  char buffer[6] = "hello";
  co_await qp->send(buffer, sizeof(buffer));
  std::cout << "Sent to client: " << buffer << std::endl;
  co_await qp->recv(buffer, sizeof(buffer));
  std::cout << "Received from client: " << buffer << std::endl;
}

rdmapp::acceptor acceptor(pd, cq, 2333);
auto qp = acceptor.accept();
auto coro = server(qp);
while (!coro.h_.done());
```

On the client side, connect to server:

```cpp
rdmapp::task<void> client(std::shared_ptr<rdmapp::qp> qp) {
  char buffer[6];
  co_await qp->recv(buffer, sizeof(buffer));
  std::cout << "Received from server: " << buffer << std::endl;
  std::copy_n("world", sizeof(buffer), buffer);
  co_await qp->send(buffer, sizeof(buffer));
  std::cout << "Sent to server: " << buffer << std::endl;
}

auto qp = std::make_shared<rdmapp::qp>("127.0.0.1", 2333, pd, cq);
auto coro = client(qp);
while (!coro.h_.done());
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
