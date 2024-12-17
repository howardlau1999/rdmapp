#pragma once

#include <cassert>
#include <coroutine>
#include <exception>
#include <future>
#include <utility>

namespace rdmapp {

template <class T> class value_returner {
public:
  std::promise<T> promise_;
  void return_value(T &&value) { promise_.set_value(std::forward<T>(value)); }
};

template <> class value_returner<void> {
public:
  std::promise<void> promise_;
  void return_void() { promise_.set_value(); }
};

template <class T, class CoroutineHandle>
struct promise_base : public value_returner<T> {
  std::suspend_never initial_suspend() { return {}; }
  auto final_suspend() noexcept {
    struct awaiter {
      std::coroutine_handle<> release_detached_;
      bool await_ready() noexcept { return false; }
      std::coroutine_handle<>
      await_suspend(CoroutineHandle suspended) noexcept {
        if (suspended.promise().continuation_) {
          return suspended.promise().continuation_;
        } else {
          if (release_detached_) {
            release_detached_.destroy();
          }
          return std::noop_coroutine();
        }
      }
      void await_resume() noexcept {}
    };
    return awaiter{release_detached_};
  }

  std::coroutine_handle<> continuation_;
  std::coroutine_handle<> release_detached_;
};

template <class T> struct task {
  struct promise_type
      : public promise_base<T, std::coroutine_handle<promise_type>> {
    task<T> get_return_object() {
      return std::coroutine_handle<promise_type>::from_promise(*this);
    }
    void unhandled_exception() {
      this->promise_.set_exception(std::current_exception());
    }
    promise_type() : future_(this->promise_.get_future()) {}
    std::future<T> &get_future() { return future_; }
    void set_detached_task(std::coroutine_handle<promise_type> h) {
      this->release_detached_ = h;
    }
    std::future<T> future_;
  };

  struct task_awaiter {
    std::coroutine_handle<promise_type> h_;
    task_awaiter(std::coroutine_handle<promise_type> h) : h_(h) {}
    bool await_ready() { return h_.done(); }
    auto await_suspend(std::coroutine_handle<> suspended) {
      h_.promise().continuation_ = suspended;
    }
    auto await_resume() { return h_.promise().future_.get(); }
  };

  using coroutine_handle_type = std::coroutine_handle<promise_type>;

  auto operator co_await() const { return task_awaiter(h_); }

  ~task() {
    if (!detached_) {
      if (!h_.done()) {
        h_.promise().set_detached_task(h_);
        get_future().wait();
      } else {
        h_.destroy();
      }
    }
  }
  task(task &&other)
      : h_(std::exchange(other.h_, nullptr)),
        detached_(std::exchange(other.detached_, true)) {}
  task(coroutine_handle_type h) : h_(h), detached_(false) {}
  coroutine_handle_type h_;
  bool detached_;
  operator coroutine_handle_type() const { return h_; }
  std::future<T> &get_future() const { return h_.promise().get_future(); }
  void detach() {
    assert(!detached_);
    h_.promise().set_detached_task(h_);
    detached_ = true;
  }
};

} // namespace rdmapp
