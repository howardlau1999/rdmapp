#pragma once

#include <coroutine>
#include <exception>

namespace rdmapp {
template <class T> struct task {
  struct promise_type {
    task<T> get_return_object() {
      return task<T>(std::coroutine_handle<promise_type>::from_promise(*this));
    }
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void unhandled_exception() { exception_ = std::current_exception(); }
    void return_value(T &&value) { value_ = value; }
    T value_;
    std::exception_ptr exception_;
  };
  task(std::coroutine_handle<promise_type> h) : h_(h) {}
  std::coroutine_handle<promise_type> h_;
  operator std::coroutine_handle<promise_type>() const { return h_; }
};

template <> struct task<void> {
  struct promise_type {
    task<void> get_return_object() {
      return task<void>(
          std::coroutine_handle<promise_type>::from_promise(*this));
    }
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void unhandled_exception() { exception_ = std::current_exception(); }
    void return_void() {}
    std::exception_ptr exception_;
  };
  task(std::coroutine_handle<promise_type> h) : h_(h) {}
  std::coroutine_handle<promise_type> h_;
  operator std::coroutine_handle<promise_type>() const { return h_; }
};

} // namespace rdmapp