#pragma once

#include "socket/tcp_connection.h"

#include <rdmapp/qp.h>
#include <rdmapp/task.h>

namespace rdmapp {

task<deserialized_qp> recv_qp(socket::tcp_connection &connection);

task<void> send_qp(qp const &qp, socket::tcp_connection &connection);

} // namespace rdmapp