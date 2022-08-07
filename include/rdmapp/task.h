#pragma once

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

template <class CoroutineHandle> struct task_awaiter {
  CoroutineHandle h_;
  task_awaiter(CoroutineHandle h) : h_(h) {}
  auto await_suspend(CoroutineHandle suspended) {
    h_.promise().continuation_ = suspended;
    return h_;
  }
};

template <class T> struct task {
  struct promise_type
      : public promise_base<std::coroutine_handle<promise_type>> {
    task<T> get_return_object() { return std::coroutine_handle<promise_type>::from_promise(*this); }
    void return_value(T &&value) { value_ = value; }
    T value_;
  };
  using coroutine_handle_type = std::coroutine_handle<promise_type>;

  auto operator co_await() const { return task_awaiter{h_}; }
  ~task() { h_.destroy(); }
  task(coroutine_handle_type h) : h_(h) {}
  coroutine_handle_type h_;
  operator coroutine_handle_type() const { return h_; }
};

template <> struct task<void> {
  struct promise_type
      : public promise_base<std::coroutine_handle<promise_type>> {
    task<void> get_return_object() { return std::coroutine_handle<promise_type>::from_promise(*this); }
    void return_void() {}
  };
  using coroutine_handle_type = std::coroutine_handle<promise_type>;

  auto operator co_await() const { return task_awaiter{h_}; }
  ~task() { h_.destroy(); }
  task(coroutine_handle_type h) : h_(h) {}
  coroutine_handle_type h_;
  operator coroutine_handle_type() const { return h_; }
};

} // namespace rdmapp