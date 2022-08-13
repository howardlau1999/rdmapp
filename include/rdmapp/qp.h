#pragma once

#include <atomic>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include <infiniband/verbs.h>

#include "rdmapp/cq.h"
#include "rdmapp/detail/noncopyable.h"
#include "rdmapp/detail/serdes.h"
#include "rdmapp/device.h"
#include "rdmapp/pd.h"
#include "rdmapp/socket/tcp_connection.h"
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
    std::shared_ptr<mr<tags::mr::local>> local_mr_;
    mr<tags::mr::remote> remote_mr_;
    uint64_t compare_add_;
    uint64_t swap_;
    uint32_t imm_;
    struct ibv_wc wc_;
    const enum ibv_wr_opcode opcode_;

  public:
    send_awaitable(std::shared_ptr<qp> qp, void *buffer, size_t length,
                   enum ibv_wr_opcode opcode);
    send_awaitable(std::shared_ptr<qp> qp, void *buffer, size_t length,
                   enum ibv_wr_opcode opcode,
                   mr<tags::mr::remote> const &remote_mr);
    send_awaitable(std::shared_ptr<qp> qp, void *buffer, size_t length,
                   enum ibv_wr_opcode opcode,
                   mr<tags::mr::remote> const &remote_mr, uint32_t imm);
    send_awaitable(std::shared_ptr<qp> qp, void *buffer, size_t length,
                   enum ibv_wr_opcode opcode,
                   mr<tags::mr::remote> const &remote_mr, uint64_t add);
    send_awaitable(std::shared_ptr<qp> qp, void *buffer, size_t length,
                   enum ibv_wr_opcode opcode,
                   mr<tags::mr::remote> const &remote_mr, uint64_t compare,
                   uint64_t swap);
    send_awaitable(std::shared_ptr<qp> qp,
                   std::shared_ptr<mr<tags::mr::local>> local_mr,
                   enum ibv_wr_opcode opcode);
    send_awaitable(std::shared_ptr<qp> qp,
                   std::shared_ptr<mr<tags::mr::local>> local_mr,
                   enum ibv_wr_opcode opcode,
                   mr<tags::mr::remote> const &remote_mr);
    send_awaitable(std::shared_ptr<qp> qp,
                   std::shared_ptr<mr<tags::mr::local>> local_mr,
                   enum ibv_wr_opcode opcode,
                   mr<tags::mr::remote> const &remote_mr, uint32_t imm);
    send_awaitable(std::shared_ptr<qp> qp,
                   std::shared_ptr<mr<tags::mr::local>> local_mr,
                   enum ibv_wr_opcode opcode,
                   mr<tags::mr::remote> const &remote_mr, uint64_t add);
    send_awaitable(std::shared_ptr<qp> qp,
                   std::shared_ptr<mr<tags::mr::local>> local_mr,
                   enum ibv_wr_opcode opcode,
                   mr<tags::mr::remote> const &remote_mr, uint64_t compare,
                   uint64_t swap);
    bool await_ready() const noexcept;
    void await_suspend(std::coroutine_handle<> h);
    void await_resume();
    bool is_rdma();
    bool is_atomic();
  };

  class recv_awaitable {
    std::shared_ptr<qp> qp_;
    std::shared_ptr<mr<tags::mr::local>> local_mr_;
    struct ibv_wc wc_;
    enum ibv_wr_opcode opcode_;

  public:
    recv_awaitable(std::shared_ptr<qp> qp,
                   std::shared_ptr<mr<tags::mr::local>> local_mr);
    recv_awaitable(std::shared_ptr<qp> qp, void *buffer, size_t length);
    bool await_ready() const noexcept;
    void await_suspend(std::coroutine_handle<> h);
    std::optional<uint32_t> await_resume();
  };
  static task<std::shared_ptr<qp>>
  from_tcp_connection(socket::tcp_connection &connection,
                      std::shared_ptr<pd> pd, std::shared_ptr<cq> recv_cq,
                      std::shared_ptr<cq> send_cq,
                      std::shared_ptr<srq> srq = nullptr);
  static task<std::shared_ptr<qp>>
  from_tcp_connection(socket::tcp_connection &connection,
                      std::shared_ptr<pd> pd, std::shared_ptr<cq> cq,
                      std::shared_ptr<srq> srq = nullptr);
  qp(uint16_t remote_device_id, uint32_t remote_qpn, uint32_t remote_psn,
     std::shared_ptr<pd> pd, std::shared_ptr<cq> cq,
     std::shared_ptr<srq> srq = nullptr);
  qp(uint16_t remote_device_id, uint32_t remote_qpn, uint32_t remote_psn,
     std::shared_ptr<pd> pd, std::shared_ptr<cq> recv_cq,
     std::shared_ptr<cq> send_cq, std::shared_ptr<srq> srq = nullptr);
  qp(std::shared_ptr<pd> pd, std::shared_ptr<cq> cq,
     std::shared_ptr<srq> srq = nullptr);
  qp(std::shared_ptr<pd> pd, std::shared_ptr<cq> recv_cq,
     std::shared_ptr<cq> send_cq, std::shared_ptr<srq> srq = nullptr);

  void post_send(struct ibv_send_wr &send_wr, struct ibv_send_wr *&bad_send_wr);
  void post_recv(struct ibv_recv_wr &recv_wr, struct ibv_recv_wr *&bad_recv_wr);

  /**
   * @brief This method sends local buffer to remote. The address will be
   * registered as a memory region first and then deregistered upon completion.
   *
   * @param buffer Pointer to local buffer. It should be valid until completion.
   * @param length The length of the local buffer.
   * @return send_awaitable
   */
  [[nodiscard]] send_awaitable send(void *buffer, size_t length);

  /**
   * @brief This method writes local buffer to a remote memory region. The local
   * buffer will be registered as a memory region first and then deregistered
   * upon completion.
   *
   * @param remote_mr Remote memory region handle.
   * @param buffer Pointer to local buffer. It should be valid until completion.
   * @param length The length of the local buffer.
   * @return send_awaitable
   */
  [[nodiscard]] send_awaitable write(mr<tags::mr::remote> const &remote_mr,
                                     void *buffer, size_t length);

  /**
   * @brief This method writes local buffer to a remote memory region with an
   * immediate value. The local buffer will be registered as a memory region
   * first and then deregistered upon completion.
   *
   * @param remote_mr Remote memory region handle.
   * @param buffer Pointer to local buffer. It should be valid until completion.
   * @param length The length of the local buffer.
   * @param imm The immediate value.
   * @return send_awaitable
   */
  [[nodiscard]] send_awaitable
  write_with_imm(mr<tags::mr::remote> const &remote_mr, void *buffer,
                 size_t length, uint32_t imm);

  /**
   * @brief This method reads to local buffer from a remote memory region. The
   * local buffer will be registered as a memory region first and then
   * deregistered upon completion.
   *
   * @param remote_mr Remote memory region handle.
   * @param buffer Pointer to local buffer. It should be valid until completion.
   * @param length The length of the local buffer.
   * @return send_awaitable
   */
  [[nodiscard]] send_awaitable read(mr<tags::mr::remote> const &remote_mr,
                                    void *buffer, size_t length);

  /**
   * @brief This method performs an atomic fetch-and-add operation on the
   * given remote memory region. The local buffer will be registered as a memory
   * region first and then deregistered upon completion.
   *
   * @param remote_mr Remote memory region handle.
   * @param buffer Pointer to local buffer. It should be valid until completion.
   * @param length The length of the local buffer.
   * @param add The delta.
   * @return send_awaitable
   */
  [[nodiscard]] send_awaitable
  fetch_and_add(mr<tags::mr::remote> const &remote_mr, void *buffer,
                size_t length, uint64_t add);

  /**
   * @brief This method performs an atomic compare-and-swap operation on the
   * given remote memory region. The local buffer will be registered as a memory
   * region first and then deregistered upon completion.
   *
   * @param remote_mr Remote memory region handle.
   * @param buffer Pointer to local buffer. It should be valid until completion.
   * @param length The length of the local buffer.
   * @param compare The expected old value.
   * @param swap The desired new value.
   * @return send_awaitable
   */
  [[nodiscard]] send_awaitable
  compare_and_swap(mr<tags::mr::remote> const &remote_mr, void *buffer,
                   size_t length, uint64_t compare, uint64_t swap);

  /**
   * @brief This method posts a recv request on the queue pair. The buffer will
   * be filled with data received. The local buffer will be registered as a
   * memory region first and then deregistered upon completion.
   * @param buffer Pointer to local buffer. It should be valid until completion.
   * @param length The length of the local buffer.
   * @return recv_awaitable
   */
  [[nodiscard]] recv_awaitable recv(void *buffer, size_t length);

  [[nodiscard]] send_awaitable
  send(std::shared_ptr<mr<tags::mr::local>> local_mr);
  [[nodiscard]] send_awaitable
  write(mr<tags::mr::remote> const &remote_mr,
        std::shared_ptr<mr<tags::mr::local>> local_mr);
  [[nodiscard]] send_awaitable
  write_with_imm(mr<tags::mr::remote> const &remote_mr,
                 std::shared_ptr<mr<tags::mr::local>> local_mr, uint32_t imm);
  [[nodiscard]] send_awaitable
  read(mr<tags::mr::remote> const &remote_mr,
       std::shared_ptr<mr<tags::mr::local>> local_mr);
  [[nodiscard]] send_awaitable
  fetch_and_add(mr<tags::mr::remote> const &remote_mr,
                std::shared_ptr<mr<tags::mr::local>> local_mr, uint64_t add);
  [[nodiscard]] send_awaitable
  compare_and_swap(mr<tags::mr::remote> const &remote_mr,
                   std::shared_ptr<mr<tags::mr::local>> local_mr,
                   uint64_t compare, uint64_t swap);
  [[nodiscard]] recv_awaitable
  recv(std::shared_ptr<mr<tags::mr::local>> local_mr);

  static task<deserialized_qp> recv_qp(socket::tcp_connection &connection);
  task<void> send_qp(socket::tcp_connection &connection);
  std::vector<uint8_t> serialize() const;
  std::vector<uint8_t> &user_data();
  std::shared_ptr<pd> pd_ptr() const;
  ~qp();

private:
  void rtr(uint16_t remote_device_id, uint32_t remote_qpn, uint32_t remote_psn);
  void rts();
};

} // namespace rdmapp