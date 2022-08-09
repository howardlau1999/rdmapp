#include "rdmapp/socket/event_loop.h"

#include <asm-generic/errno-base.h>
#include <cassert>
#include <cerrno>
#include <memory>
#include <string>
#include <strings.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <vector>

#include "rdmapp/detail/debug.h"
#include "rdmapp/error.h"

namespace rdmapp {
namespace socket {

static inline std::string events_string(int events) {
  std::vector<std::string> parts;
  if (events & EPOLLIN) {
    parts.emplace_back("EPOLLIN");
  }
  if (events & EPOLLPRI) {
    parts.emplace_back("EPOLLPRI");
  }
  if (events & EPOLLOUT) {
    parts.emplace_back("EPOLLOUT");
  }
  if (events & EPOLLERR) {
    parts.emplace_back("EPOLLERR");
  }
  if (events & EPOLLHUP) {
    parts.emplace_back("EPOLLHUP");
  }
  auto str = std::string();
  bool first = true;
  for (auto &&part : parts) {
    if (!first)
      str += " | ";
    str += part;
    first = false;
  }
  return str;
}

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

std::shared_ptr<event_loop> event_loop::new_loop(size_t max_events) {
  return std::make_shared<event_loop>(max_events);
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
  struct epoll_event event;
  ::bzero(&event, sizeof(event));
  channels_.erase(channel->fd());
  check_errno(::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, channel->fd(), &event),
              "failed to remove fd from epoll");
}

void event_loop::loop() {
  std::vector<struct epoll_event> events(max_events_);
  bool close_triggered = false;
  while (!close_triggered) {
    int nr_events = ::epoll_wait(epoll_fd_, &events[0], max_events_, -1);
    if (nr_events < 0 && errno == EINTR) {
      continue;
    }
    check_errno(nr_events, "failed to epoll wait");
    for (int i = 0; i < nr_events; ++i) {
      auto &event = events[i];
      auto fd = event.data.fd;
      RDMAPP_LOG_DEBUG("fd: %d events: %s", fd,
                       events_string(event.events).c_str());
      if (event.data.fd == close_event_fd_) {
        close_triggered = true;
        continue;
      }
      auto it = channels_.find(fd);
      assert(it != channels_.end());
      auto channel = it->second.lock();
      if (channel) {
        if (event.events & EPOLLIN || event.events & EPOLLERR) {
          channel->readable_callback();
        }
        if (event.events & EPOLLOUT || event.events & EPOLLERR) {
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

} // namespace socket

} // namespace rdmapp