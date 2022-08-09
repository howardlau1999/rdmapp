#pragma once

#include "rdmapp/detail/debug.h"

#include <coroutine>
#include <exception>

namespace rdmapp {

template <class CoroutineHandle> struct promise_base {
  std::suspend_never initial_suspend() { return {}; }
  auto final_suspend() noexcept {
    struct awaiter {
      bool await_ready() noexcept { return false; }
      std::coroutine_handle<>
      await_suspend(CoroutineHandle suspended) noexcept {
        if (suspended.promise().continuation_) {
          return suspended.promise().continuation_;
        } else {
          return std::noop_coroutine();
        }
      }
      void await_resume() noexcept {}
    };
    return awaiter{};
  }
  void unhandled_exception() { exception_ = std::current_exception(); }
  std::exception_ptr exception_;
  std::coroutine_handle<> continuation_;
};

template <class T> struct task_awaiter;

template <> struct task_awaiter<void> {
  std::coroutine_handle<> h_;
  std::coroutine_handle<> &continuation_;
  task_awaiter(std::coroutine_handle<> h, std::coroutine_handle<> &continuation)
      : h_(h), continuation_(continuation) {}
  bool await_ready() { return h_.done(); }
  auto await_suspend(std::coroutine_handle<> suspended) {
    continuation_ = suspended;
  }
  void await_resume() {}
};

template <class T> struct task_awaiter {
  std::coroutine_handle<> h_;
  std::coroutine_handle<> &continuation_;
  T &value_;
  task_awaiter(std::coroutine_handle<> h, std::coroutine_handle<> &continuation,
               T &value)
      : h_(h), continuation_(continuation), value_(value) {}
  bool await_ready() { return h_.done(); }
  auto await_suspend(std::coroutine_handle<> suspended) {
    continuation_ = suspended;
  }
  auto await_resume() { return value_; }
};

template <class T> struct task {
  struct promise_type
      : public promise_base<std::coroutine_handle<promise_type>> {
    task<T> get_return_object() {
      return std::coroutine_handle<promise_type>::from_promise(*this);
    }
    void return_value(T &&value) { value_ = value; }
    T value_;
  };
  using coroutine_handle_type = std::coroutine_handle<promise_type>;

  auto operator co_await() const {
    return task_awaiter<T>(h_, h_.promise().continuation_, h_.promise().value_);
  }
  ~task() { h_.destroy(); }
  task(coroutine_handle_type h) : h_(h) {}
  coroutine_handle_type h_;
  operator coroutine_handle_type() const { return h_; }
};

template <> struct task<void> {
  struct promise_type
      : public promise_base<std::coroutine_handle<promise_type>> {
    task<void> get_return_object() {
      return std::coroutine_handle<promise_type>::from_promise(*this);
    }
    void return_void() {}
  };
  using coroutine_handle_type = std::coroutine_handle<promise_type>;

  auto operator co_await() const {
    return task_awaiter<void>(h_, h_.promise().continuation_);
  }
  ~task() { h_.destroy(); }
  task(coroutine_handle_type h) : h_(h) {}
  coroutine_handle_type h_;
  operator coroutine_handle_type() const { return h_; }
};

} // namespace rdmapp