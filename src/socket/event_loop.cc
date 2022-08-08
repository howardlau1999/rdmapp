#include "rdmapp/socket/event_loop.h"

#include <cassert>
#include <cerrno>
#include <memory>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <vector>

#include "rdmapp/detail/debug.h"
#include "rdmapp/error.h"

namespace rdmapp {
namespace socket {

event_loop::event_loop(size_t max_events)
    : epoll_fd_(-1), close_event_fd_(-1), max_events_(max_events) {
  epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
  check_errno(epoll_fd_, "failed to create epoll fd");
  close_event_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  check_errno(close_event_fd_, "failed to create close event fd");
  struct epoll_event event;
  event.events = EPOLLIN | EPOLLERR;
  event.data.fd = close_event_fd_;
  check_errno(::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, close_event_fd_, &event),
              "failed to add close event fd to epoll");
}

void event_loop::register_fd(std::shared_ptr<channel> channel,
                             struct epoll_event *event) {
  assert(epoll_fd_ > 0);
  check_errno(::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, channel->fd(), event),
              "failed to add fd to epoll");
  channels_.insert(std::make_pair(channel->fd(), channel));
}

void event_loop::register_read(std::shared_ptr<channel> channel) {
  struct epoll_event event;
  event.data.fd = channel->fd();
  event.events = EPOLLIN | EPOLLPRI;
  register_fd(channel, &event);
}

void event_loop::register_write(std::shared_ptr<channel> channel) {
  struct epoll_event event;
  event.data.fd = channel->fd();
  event.events = EPOLLOUT;
  register_fd(channel, &event);
}

void event_loop::deregister(std::shared_ptr<channel> channel) {
  assert(epoll_fd_ > 0);
  channels_.erase(channel->fd());
  check_errno(::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, channel->fd(), nullptr),
              "failed to remove fd from epoll");
}

void event_loop::loop() {
  std::vector<struct epoll_event> events;
  bool close_triggered = false;
  while (!close_triggered) {
    int nr_events = ::epoll_wait(epoll_fd_, &events[0], max_events_, -1);
    check_errno(nr_events, "failed to epoll wait");
    for (int i = 0; i < nr_events; ++i) {
      auto &event = events[i];
      auto fd = event.data.fd;
      if (event.data.fd == close_event_fd_) {
        close_triggered = true;
        continue;
      }
      auto it = channels_.find(fd);
      assert(it != channels_.end());
      auto channel = it->second.lock();
      if (channel) {
        if (event.events & EPOLLIN) {
          channel->readable_callback();
        } else if (event.events & EPOLLOUT) {
          channel->writable_callback();
        }
      } else {
        channels_.erase(fd);
      }
    }
  }
}

void event_loop::close() {
  int one = 1;
  check_errno(::write(close_event_fd_, &one, sizeof(one)),
              "failed to write event fd");
}

event_loop::~event_loop() {
  if (close_event_fd_ > 0) {
    if (auto rc = ::close(close_event_fd_); rc != 0) {
      RDMAPP_LOG_ERROR("failed to close event fd %d: %s (errno=%d)",
                       close_event_fd_, strerror(errno), errno);
    } else {
      RDMAPP_LOG_DEBUG("closed event fd %d", close_event_fd_);
    }
  }
  if (epoll_fd_ > 0) {
    if (auto rc = ::close(epoll_fd_); rc != 0) {
      RDMAPP_LOG_ERROR("failed to close epoll fd %d: %s (errno=%d)", epoll_fd_,
                       strerror(errno), errno);
    } else {
      RDMAPP_LOG_DEBUG("closed epoll fd %d", epoll_fd_);
    }
  }
}

} // namespace detail

} // namespace rdmapp