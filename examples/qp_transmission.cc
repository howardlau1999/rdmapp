#include "qp_transmission.h"

namespace rdmapp {

task<void> send_qp(qp const &qp, socket::tcp_connection &connection) {
  auto local_qp_data = qp.serialize();
  assert(local_qp_data.size() != 0);
  size_t local_qp_sent = 0;
  while (local_qp_sent < local_qp_data.size()) {
    int n = co_await connection.send(&local_qp_data[local_qp_sent],
                                     local_qp_data.size() - local_qp_sent);
    if (n == 0) {
      throw_with("remote closed unexpectedly while sending qp");
    }
    check_errno(n, "failed to send qp");
    local_qp_sent += n;
  }
  co_return;
}

task<deserialized_qp> recv_qp(socket::tcp_connection &connection) {
  size_t header_read = 0;
  uint8_t header[deserialized_qp::qp_header::kSerializedSize];
  while (header_read < deserialized_qp::qp_header::kSerializedSize) {
    int n = co_await connection.recv(
        &header[header_read],
        deserialized_qp::qp_header::kSerializedSize - header_read);
    if (n == 0) {
      throw_with("remote closed unexpectedly while receiving qp header");
    }
    header_read += n;
  }

  auto remote_qp = deserialized_qp::deserialize(header);
  RDMAPP_LOG_TRACE("received header lid=%u qpn=%u psn=%u user_data_size=%u",
                   remote_qp.header.lid, remote_qp.header.qp_num,
                   remote_qp.header.sq_psn, remote_qp.header.user_data_size);
  remote_qp.user_data.resize(remote_qp.header.user_data_size);

  if (remote_qp.header.user_data_size > 0) {
    size_t user_data_read = 0;
    remote_qp.user_data.resize(remote_qp.header.user_data_size, 0);
    while (user_data_read < remote_qp.header.user_data_size) {
      int n = co_await connection.recv(&remote_qp.user_data[user_data_read],
                                       remote_qp.header.user_data_size -
                                           user_data_read);
      if (n == 0) {
        throw_with("remote closed unexpectedly while receiving user data");
      }
      check_errno(n, "failed to receive user data");
      user_data_read += n;
    }
  }
  RDMAPP_LOG_TRACE("received user data");
  co_return remote_qp;
}

} // namespace rdmapp