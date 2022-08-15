#pragma once

#include <cassert>
#include <coroutine>
#include <exception>
#include <functional>
#include <future>

#include "rdmapp/detail/debug.h"
#include "rdmapp/detail/noncopyable.h"

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
          return std::noop_coroutine();
        }
      }
      void await_resume() noexcept {
        if (release_detached_) {
          release_detached_.destroy();
        }
      }
    };
    return awaiter{release_detached_};
  }

  std::coroutine_handle<> continuation_;
  std::coroutine_handle<> release_detached_;
};

template <class T> struct task_awaiter {
  std::coroutine_handle<> h_;
  std::coroutine_handle<> &continuation_;
  std::shared_future<T> value_;
  task_awaiter(std::coroutine_handle<> h, std::coroutine_handle<> &continuation,
               std::shared_future<T> value)
      : h_(h), continuation_(continuation), value_(value) {}
  bool await_ready() { return h_.done(); }
  auto await_suspend(std::coroutine_handle<> suspended) {
    continuation_ = suspended;
  }
  auto await_resume() { return value_.get(); }
};

template <class T> struct task : public noncopyable {
  struct promise_type
      : public promise_base<T, std::coroutine_handle<promise_type>> {
    task<T> get_return_object() {
      return std::coroutine_handle<promise_type>::from_promise(*this);
    }
    void unhandled_exception() {
      this->promise_.set_exception(std::current_exception());
    }
    promise_type() : future_(this->promise_.get_future().share()) {}
    std::shared_future<T> get_future() { return future_; }
    void set_detached_task(std::coroutine_handle<promise_type> h) {
      this->release_detached_ = h;
    }
    std::shared_future<T> future_;
  };
  using coroutine_handle_type = std::coroutine_handle<promise_type>;

  auto operator co_await() const {
    return task_awaiter<T>(h_, h_.promise().continuation_,
                           h_.promise().get_future());
  }
  ~task() {
    if (!detached_) {
      RDMAPP_LOG_TRACE("waiting coroutine %p", h_.address());
      get_future().wait();
      h_.destroy();
      RDMAPP_LOG_TRACE("destroyed coroutine %p", h_.address());
    }
  }
  task(task &&) = default;
  task(coroutine_handle_type h) : h_(h) {}
  coroutine_handle_type h_;
  bool detached_;
  operator coroutine_handle_type() const { return h_; }
  std::shared_future<T> get_future() const { return h_.promise().get_future(); }
  void detach() {
    assert(!detached_);
    h_.promise().set_detached_task(h_);
    detached_ = true;
  }
};

} // namespace rdmapp