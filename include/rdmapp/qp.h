#pragma once

#include <atomic>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string_view>
#include <vector>

#include <infiniband/verbs.h>

#include "rdmapp/cq.h"
#include "rdmapp/detail/noncopyable.h"
#include "rdmapp/detail/serdes.h"
#include "rdmapp/device.h"
#include "rdmapp/pd.h"
#include "rdmapp/srq.h"
#include "rdmapp/task.h"

namespace rdmapp {

struct deserialized_qp {
  struct qp_header {
    static constexpr size_t kSerializedSize =
        sizeof(uint16_t) + 3 * sizeof(uint32_t);
    uint16_t lid;
    uint32_t qp_num;
    uint32_t sq_psn;
    uint32_t user_data_size;
  } header;
  template <class It> static deserialized_qp deserialize(It it) {
    deserialized_qp des_qp;
    detail::deserialize(it, des_qp.header.lid);
    detail::deserialize(it, des_qp.header.qp_num);
    detail::deserialize(it, des_qp.header.sq_psn);
    detail::deserialize(it, des_qp.header.user_data_size);
    return des_qp;
  }
  std::vector<uint8_t> user_data;
};

class qp : public noncopyable, public std::enable_shared_from_this<qp> {
  static std::atomic<uint32_t> next_sq_psn;
  struct ibv_qp *qp_;
  uint32_t sq_psn_;

  std::shared_ptr<pd> pd_;
  std::shared_ptr<cq> recv_cq_;
  std::shared_ptr<cq> send_cq_;
  std::shared_ptr<srq> srq_;
  std::vector<uint8_t> user_data_;
  void create();
  void init();

public:
  class send_awaitable {
    std::shared_ptr<qp> qp_;
    std::shared_ptr<mr> mr_;
    struct ibv_wc wc_;
    void *buffer_;
    size_t length_;
    enum ibv_wr_opcode opcode_;
    void *remote_addr_;
    uint32_t rkey_;

  public:
    send_awaitable(std::shared_ptr<qp> qp, void *buffer, size_t length,
                   enum ibv_wr_opcode opcode);
    send_awaitable(std::shared_ptr<qp> qp, void *buffer, size_t length,
                   enum ibv_wr_opcode opcode, void *remote_addr, uint32_t rkey);
    bool await_ready() const noexcept;
    void await_suspend(std::coroutine_handle<> h);
    void await_resume();
  };

  class recv_awaitable {
    std::shared_ptr<qp> qp_;
    std::unique_ptr<mr> mr_;
    struct ibv_wc wc_;
    void *buffer_;
    size_t length_;
    enum ibv_wr_opcode opcode_;

  public:
    recv_awaitable(std::shared_ptr<qp> qp, void *buffer, size_t length);
    bool await_ready() const noexcept;
    void await_suspend(std::coroutine_handle<> h);
    void await_resume();
  };

  qp(std::string const &hostname, uint16_t port, std::shared_ptr<pd> pd,
     std::shared_ptr<cq> cq, std::shared_ptr<srq> srq = nullptr);
  qp(std::string const &hostname, uint16_t port, std::shared_ptr<pd> pd,
     std::shared_ptr<cq> recv_cq, std::shared_ptr<cq> send_cq,
     std::shared_ptr<srq> srq = nullptr);

  qp(uint16_t remote_device_id, uint32_t remote_qpn, uint32_t remote_psn,
     std::shared_ptr<pd> pd, std::shared_ptr<cq> cq,
     std::shared_ptr<srq> srq = nullptr);
  qp(uint16_t remote_device_id, uint32_t remote_qpn, uint32_t remote_psn,
     std::shared_ptr<pd> pd, std::shared_ptr<cq> recv_cq,
     std::shared_ptr<cq> send_cq, std::shared_ptr<srq> srq = nullptr);

  void post_send(struct ibv_send_wr &send_wr, struct ibv_send_wr *&bad_send_wr);
  void post_recv(struct ibv_recv_wr &recv_wr, struct ibv_recv_wr *&bad_recv_wr);

  send_awaitable send(void *buffer, size_t length);
  send_awaitable write(void *remote_addr, uint32_t rkey, void *buffer,
                       size_t length);
  send_awaitable read(void *remote_addr, uint32_t rkey, void *buffer,
                      size_t length);
  recv_awaitable recv(void *buffer, size_t length);

  std::vector<uint8_t> serialize() const;
  std::vector<uint8_t> &user_data();
  ~qp();

private:
  qp(std::shared_ptr<pd> pd, std::shared_ptr<cq> cq,
     std::shared_ptr<srq> srq = nullptr);
  qp(std::shared_ptr<pd> pd, std::shared_ptr<cq> recv_cq,
     std::shared_ptr<cq> send_cq, std::shared_ptr<srq> srq = nullptr);
  void rtr(uint16_t remote_device_id, uint32_t remote_qpn, uint32_t remote_psn);
  void rts();
};

} // namespace rdmapp