#pragma once

#include <cassert>
#include <coroutine>
#include <exception>
#include <functional>
#include <future>

#include "rdmapp/detail/debug.h"
#include "rdmapp/detail/noncopyable.h"

namespace rdmapp {

template <class CoroutineHandle> struct promise_base {
  std::suspend_never initial_suspend() { return {}; }
  auto final_suspend() noexcept {
    struct awaiter {
      std::function<void()> release_detached_;
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
          release_detached_();
        }
      }
    };
    return awaiter{release_detached_};
  }

  std::exception_ptr exception_;
  std::coroutine_handle<> continuation_;
  std::function<void()> release_detached_;
};

template <class T> struct task_awaiter;

template <> struct task_awaiter<void> {
  std::coroutine_handle<> h_;
  std::coroutine_handle<> &continuation_;
  std::exception_ptr &exception_;
  task_awaiter(std::coroutine_handle<> h, std::coroutine_handle<> &continuation,
               std::exception_ptr &exception)
      : h_(h), continuation_(continuation), exception_(exception) {}
  bool await_ready() { return h_.done(); }
  auto await_suspend(std::coroutine_handle<> suspended) {
    continuation_ = suspended;
  }
  void await_resume() {
    if (exception_) {
      std::rethrow_exception(exception_);
    }
  }
};

template <class T> struct task_awaiter {
  std::coroutine_handle<> h_;
  std::coroutine_handle<> &continuation_;
  std::exception_ptr &exception_;
  std::shared_future<T> value_;
  task_awaiter(std::coroutine_handle<> h, std::coroutine_handle<> &continuation,
               std::exception_ptr &exception, std::shared_future<T> value)
      : h_(h), continuation_(continuation), exception_(exception),
        value_(value) {}
  bool await_ready() { return h_.done(); }
  auto await_suspend(std::coroutine_handle<> suspended) {
    continuation_ = suspended;
  }
  auto await_resume() {
    if (exception_) {
      std::rethrow_exception(exception_);
    }
    return value_.get();
  }
};

template <class T> struct task : public noncopyable {
  struct promise_type
      : public promise_base<std::coroutine_handle<promise_type>> {
    task<T> get_return_object() {
      return std::coroutine_handle<promise_type>::from_promise(*this);
    }
    void unhandled_exception() {
      this->exception_ = std::current_exception();
      promise_.set_exception(this->exception_);
    }
    promise_type() : future_(promise_.get_future().share()) {}
    void return_value(T &&value) { promise_.set_value(value); }
    std::shared_future<T> get_future() { return future_; }
    void set_detached_task(task<T> *detached) {
      this->release_detached_ = [detached]() { delete detached; };
    }
    std::promise<T> promise_;
    std::shared_future<T> future_;
  };
  using coroutine_handle_type = std::coroutine_handle<promise_type>;

  auto operator co_await() const {
    return task_awaiter<T>(h_, h_.promise().continuation_,
                           h_.promise().exception_, h_.promise().get_future());
  }
  ~task() {
    if (!detached_) {
      RDMAPP_LOG_TRACE("waiting coroutine %p", h_.address());
      get_future().wait();
      RDMAPP_LOG_TRACE("destroying coroutine %p", h_.address());
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
  std::exception_ptr get_exception() const { return h_.promise().exception_; }
  void detach() {
    assert(!detached_);
    auto detached_task = new task<T>(std::move(*this));
    h_.promise().set_detached_task(detached_task);
    detached_ = true;
  }
};

template <> struct task<void> : public noncopyable {
  struct promise_type
      : public promise_base<std::coroutine_handle<promise_type>> {
    promise_type() : future_(promise_.get_future().share()) {}
    task<void> get_return_object() {
      return std::coroutine_handle<promise_type>::from_promise(*this);
    }
    void unhandled_exception() {
      this->exception_ = std::current_exception();
      promise_.set_exception(this->exception_);
    }
    void return_void() { promise_.set_value(); }
    std::shared_future<void> get_future() { return future_; }
    void set_detached_task(task<void> *detached) {
      release_detached_ = [detached]() { delete detached; };
    }
    std::promise<void> promise_;
    std::shared_future<void> future_;
  };
  using coroutine_handle_type = std::coroutine_handle<promise_type>;

  auto operator co_await() const {
    return task_awaiter<void>(h_, h_.promise().continuation_,
                              h_.promise().exception_);
  }
  ~task() {
    if (!detached_) {
      RDMAPP_LOG_TRACE("waiting coroutine %p", h_.address());
      get_future().wait();
      RDMAPP_LOG_TRACE("destroying coroutine %p", h_.address());
      h_.destroy();
      RDMAPP_LOG_TRACE("destroyed coroutine %p", h_.address());
    }
  }
  task(task &&) = default;
  task(coroutine_handle_type h) : h_(h), detached_(false) {}
  coroutine_handle_type h_;
  bool detached_;
  operator coroutine_handle_type() const { return h_; }
  std::shared_future<void> get_future() const {
    return h_.promise().get_future();
  }
  std::exception_ptr get_exception() const { return h_.promise().exception_; }
  void detach() {
    assert(!detached_);
    auto detached_task = new task<void>(std::move(*this));
    h_.promise().set_detached_task(detached_task);
    detached_ = true;
  }
};

} // namespace rdmapp