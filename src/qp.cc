#include "rdmapp/qp.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <endian.h>
#include <iterator>
#include <memory>
#include <netdb.h>
#include <netinet/in.h>
#include <stdexcept>
#include <strings.h>
#include <sys/socket.h>
#include <vector>

#include <infiniband/verbs.h>

#include "rdmapp/cq_poller.h"
#include "rdmapp/detail/debug.h"
#include "rdmapp/detail/serdes.h"
#include "rdmapp/error.h"
#include "rdmapp/executor.h"
#include "rdmapp/pd.h"
#include "rdmapp/srq.h"

namespace rdmapp {

std::atomic<uint32_t> qp::next_sq_psn = 1;
qp::qp(uint16_t remote_device_id, uint32_t remote_qpn, uint32_t remote_psn,
       std::shared_ptr<pd> pd, std::shared_ptr<cq> cq, std::shared_ptr<srq> srq)
    : qp(remote_device_id, remote_qpn, remote_psn, pd, cq, cq, srq) {}
qp::qp(uint16_t remote_device_id, uint32_t remote_qpn, uint32_t remote_psn,
       std::shared_ptr<pd> pd, std::shared_ptr<cq> recv_cq,
       std::shared_ptr<cq> send_cq, std::shared_ptr<srq> srq)
    : qp(pd, recv_cq, send_cq, srq) {
  rtr(remote_device_id, remote_qpn, remote_psn);
  rts();
}

qp::qp(std::shared_ptr<rdmapp::pd> pd, std::shared_ptr<cq> cq,
       std::shared_ptr<srq> srq)
    : qp(pd, cq, cq, srq) {}

qp::qp(std::shared_ptr<rdmapp::pd> pd, std::shared_ptr<cq> recv_cq,
       std::shared_ptr<cq> send_cq, std::shared_ptr<srq> srq)
    : qp_(nullptr), pd_(pd), recv_cq_(recv_cq), send_cq_(send_cq), srq_(srq) {
  create();
  init();
}

std::vector<uint8_t> &qp::user_data() { return user_data_; }

std::vector<uint8_t> qp::serialize() const {
  std::vector<uint8_t> buffer;
  auto it = std::back_inserter(buffer);
  detail::serialize(pd_->device_->lid(), it);
  detail::serialize(qp_->qp_num, it);
  detail::serialize(sq_psn_, it);
  detail::serialize(static_cast<uint32_t>(user_data_.size()), it);
  std::copy(user_data_.cbegin(), user_data_.cend(), std::back_inserter(buffer));
  return buffer;
}

void qp::create() {
  struct ibv_qp_init_attr qp_init_attr = {};
  ::bzero(&qp_init_attr, sizeof(qp_init_attr));
  qp_init_attr.qp_type = IBV_QPT_RC;
  qp_init_attr.recv_cq = recv_cq_->cq_;
  qp_init_attr.send_cq = send_cq_->cq_;
  qp_init_attr.cap.max_recv_sge = 1;
  qp_init_attr.cap.max_send_sge = 1;
  qp_init_attr.cap.max_recv_wr = 128;
  qp_init_attr.cap.max_send_wr = 128;
  qp_init_attr.sq_sig_all = 0;
  if (srq_ != nullptr) {
    qp_init_attr.srq = srq_->srq_;
  }

  qp_ = ::ibv_create_qp(pd_->pd_, &qp_init_attr);
  check_ptr(qp_, "failed to create qp");
  RDMAPP_LOG_DEBUG("created qp %p", qp_);
  sq_psn_ = next_sq_psn.fetch_add(1);
}

void qp::init() {
  struct ibv_qp_attr qp_attr = {};
  ::bzero(&qp_attr, sizeof(qp_attr));
  qp_attr.qp_state = IBV_QPS_INIT;
  qp_attr.pkey_index = 0;
  qp_attr.port_num = pd_->device_->port_num();
  qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ |
                            IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_ATOMIC;

  check_rc(ibv_modify_qp(qp_, &(qp_attr),
                         IBV_QP_STATE | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS |
                             IBV_QP_PKEY_INDEX),
           "failed to transition qp to init state");
}

void qp::rtr(uint16_t remote_lid, uint32_t remote_qpn, uint32_t remote_psn) {
  struct ibv_qp_attr qp_attr = {};
  ::bzero(&qp_attr, sizeof(qp_attr));
  qp_attr.qp_state = IBV_QPS_RTR;
  qp_attr.path_mtu = IBV_MTU_4096;
  qp_attr.dest_qp_num = remote_qpn;
  qp_attr.rq_psn = remote_psn;
  qp_attr.max_dest_rd_atomic = 1;
  qp_attr.min_rnr_timer = 12;
  qp_attr.ah_attr.is_global = 0;
  qp_attr.ah_attr.dlid = remote_lid;
  qp_attr.ah_attr.sl = 0;
  qp_attr.ah_attr.src_path_bits = 0;
  qp_attr.ah_attr.port_num = pd_->device_->port_num();
  check_rc(::ibv_modify_qp(qp_, &qp_attr,
                           IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU |
                               IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
                               IBV_QP_MIN_RNR_TIMER |
                               IBV_QP_MAX_DEST_RD_ATOMIC),
           "failed to transition qp to rtr state");
}

void qp::rts() {
  struct ibv_qp_attr qp_attr = {};
  ::bzero(&qp_attr, sizeof(qp_attr));
  qp_attr.qp_state = IBV_QPS_RTS;
  qp_attr.timeout = 14;
  qp_attr.retry_cnt = 7;
  qp_attr.rnr_retry = 7;
  qp_attr.max_rd_atomic = 1;
  qp_attr.sq_psn = sq_psn_;

  check_rc(::ibv_modify_qp(qp_, &qp_attr,
                           IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
                               IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN |
                               IBV_QP_MAX_QP_RD_ATOMIC),
           "failed to transition qp to rts state");
}
task<deserialized_qp> qp::recv_qp(socket::tcp_connection &connection) {
  int read = 0;
  uint8_t header[deserialized_qp::qp_header::kSerializedSize];
  while (read < deserialized_qp::qp_header::kSerializedSize) {
    int n = co_await connection.recv(
        &header[read], deserialized_qp::qp_header::kSerializedSize - read);
    if (n == 0) {
      throw_with("remote closed unexpectedly while receiving qp header");
    }
    read += n;
  }

  auto remote_qp = deserialized_qp::deserialize(header);
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
  co_return remote_qp;
}

task<void> qp::send_qp(socket::tcp_connection &connection) {
  auto local_qp_data = serialize();
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

void qp::post_send(struct ibv_send_wr &send_wr,
                   struct ibv_send_wr *&bad_send_wr) {
  check_rc(::ibv_post_send(qp_, &send_wr, &bad_send_wr), "failed to post send");
}

void qp::post_recv(struct ibv_recv_wr &recv_wr,
                   struct ibv_recv_wr *&bad_recv_wr) {
  if (srq_) {
    check_rc(::ibv_post_srq_recv(srq_->srq_, &recv_wr, &bad_recv_wr),
             "failed to post srq recv");
  } else {
    check_rc(::ibv_post_recv(qp_, &recv_wr, &bad_recv_wr),
             "failed to post recv");
  }
}

qp::send_awaitable::send_awaitable(std::shared_ptr<qp> qp, void *buffer,
                                   size_t length, enum ibv_wr_opcode opcode)
    : qp_(qp), buffer_(buffer), length_(length), mr_(nullptr), wc_(),
      opcode_(opcode), remote_addr_(nullptr), rkey_(0) {}
qp::send_awaitable::send_awaitable(std::shared_ptr<qp> qp, void *buffer,
                                   size_t length, enum ibv_wr_opcode opcode,
                                   void *remote_addr, uint32_t rkey)
    : qp_(qp), buffer_(buffer), length_(length), opcode_(opcode),
      remote_addr_(remote_addr), rkey_(rkey) {}
bool qp::send_awaitable::await_ready() const noexcept { return false; }
void qp::send_awaitable::await_suspend(std::coroutine_handle<> h) {
  mr_ = qp_->pd_->reg_mr(buffer_, length_);
  auto callback = executor::make_callback([h, this](struct ibv_wc const &wc) {
    wc_ = wc;
    h.resume();
  });
  struct ibv_sge send_sge = {};
  send_sge.addr = reinterpret_cast<uint64_t>(mr_->addr());
  send_sge.length = mr_->length();
  send_sge.lkey = mr_->lkey();

  struct ibv_send_wr send_wr = {};
  struct ibv_send_wr *bad_send_wr = nullptr;
  send_wr.opcode = opcode_;
  send_wr.next = nullptr;
  send_wr.num_sge = 1;
  send_wr.wr_id = reinterpret_cast<uint64_t>(callback);
  send_wr.send_flags = IBV_SEND_SIGNALED;
  send_wr.sg_list = &send_sge;
  if (opcode_ == IBV_WR_RDMA_READ || opcode_ == IBV_WR_RDMA_WRITE ||
      opcode_ == IBV_WR_RDMA_WRITE_WITH_IMM) {
    assert(remote_addr_ != nullptr);
    send_wr.wr.rdma.remote_addr = reinterpret_cast<uint64_t>(remote_addr_);
    send_wr.wr.rdma.rkey = rkey_;
  }

  try {
    qp_->post_send(send_wr, bad_send_wr);
  } catch (std::runtime_error &e) {
    delete callback;
    throw;
  }
}

void qp::send_awaitable::await_resume() {
  check_rc(wc_.status, "failed to send");
}

qp::send_awaitable qp::send(void *buffer, size_t length) {
  return qp::send_awaitable(this->shared_from_this(), buffer, length,
                            IBV_WR_SEND);
}

qp::send_awaitable qp::write(void *remote_addr, uint32_t rkey, void *buffer,
                             size_t length) {
  return qp::send_awaitable(this->shared_from_this(), buffer, length,
                            IBV_WR_RDMA_WRITE, remote_addr, rkey);
}
qp::send_awaitable qp::read(void *remote_addr, uint32_t rkey, void *buffer,
                            size_t length) {
  return qp::send_awaitable(this->shared_from_this(), buffer, length,
                            IBV_WR_RDMA_READ, remote_addr, rkey);
}

qp::recv_awaitable::recv_awaitable(std::shared_ptr<qp> qp, void *buffer,
                                   size_t length)
    : qp_(qp), buffer_(buffer), length_(length), mr_(nullptr), wc_() {}

bool qp::recv_awaitable::await_ready() const noexcept { return false; }
void qp::recv_awaitable::await_suspend(std::coroutine_handle<> h) {
  mr_ = qp_->pd_->reg_mr(buffer_, length_);
  auto callback = executor::make_callback([h, this](struct ibv_wc const &wc) {
    wc_ = wc;
    h.resume();
  });
  struct ibv_sge recv_sge = {};
  recv_sge.addr = reinterpret_cast<uint64_t>(mr_->addr());
  recv_sge.length = mr_->length();
  recv_sge.lkey = mr_->lkey();

  struct ibv_recv_wr recv_wr = {};
  struct ibv_recv_wr *bad_recv_wr = nullptr;
  recv_wr.next = nullptr;
  recv_wr.num_sge = 1;
  recv_wr.wr_id = reinterpret_cast<uint64_t>(callback);
  recv_wr.sg_list = &recv_sge;

  try {
    qp_->post_recv(recv_wr, bad_recv_wr);
  } catch (std::runtime_error &e) {
    delete callback;
    throw;
  }
}

void qp::recv_awaitable::await_resume() {
  check_rc(wc_.status, "failed to recv");
}

qp::recv_awaitable qp::recv(void *buffer, size_t length) {
  return qp::recv_awaitable(this->shared_from_this(), buffer, length);
}

qp::~qp() {
  if (qp_ != nullptr) {
    if (auto rc = ::ibv_destroy_qp(qp_); rc != 0) {
      RDMAPP_LOG_ERROR("failed to destroy qp %p: %s", qp_, strerror(errno));
    } else {
      RDMAPP_LOG_DEBUG("destroyed qp %p", qp_);
    }
  }
}

} // namespace rdmapp