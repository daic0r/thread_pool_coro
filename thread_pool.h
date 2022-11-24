#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <condition_variable>
#include <exception>
#include <thread>
#include <future>
#include <mutex>
#include <vector>
#include <deque>
#include <atomic>
#include <iostream>
#include <optional>
#include <coroutine>
#include <type_traits>
#include <utility>

class thread_pool {
public:
   template<typename T>
   class [[nodiscard]] task {
   public:
      struct promise_type;
      using handle_t = std::coroutine_handle<promise_type>;
      struct promise_type {
         std::optional<T> m_value;

         auto get_return_object() noexcept {
            return task<T>{ handle_t::from_promise(*this) };
         }
         void unhandled_exception() { std::terminate(); }
         std::suspend_never initial_suspend() const noexcept { return {}; }
         std::suspend_always final_suspend() const noexcept { return {}; }
         void return_value(T val) noexcept(std::is_nothrow_move_assignable_v<T>) { m_value = std::move(val); }
      };

      /****************** Rule of 6 ****************/
      task(handle_t coro) : m_coro{ coro } {}
      task(const task&) = delete;
      task& operator=(const task&) = delete;
      task(task&& rhs) noexcept : m_coro{ std::exchange(rhs.m_coro), nullptr } {}
      task& operator=(task&& rhs) noexcept { 
         task copy{ std::move(rhs) };
         copy.swap(*this);
         return *this;
      }
      ~task() {
         if (m_coro)
            m_coro.destroy();
      }

      void swap(task& rhs) noexcept {
         using std::swap;
         swap(m_coro, rhs.m_coro);
      }

      friend void swap(task& lhs, task& rhs) noexcept {
         lhs.swap(rhs);
      }
      /*********************************************/

      operator std::coroutine_handle<>() const noexcept {
         return m_coro;
      }

      T get() noexcept(std::is_nothrow_move_constructible_v<T>) {
         auto& promise = m_coro.promise();
         while (!promise.m_value);
         return std::move(promise.m_value.value());
      }

      auto ready() const noexcept {
         return m_coro.promise().m_value.has_value();
      }

   private:
      handle_t m_coro;

   };

private:
   std::size_t m_nNumThreads{};
   std::size_t m_nNumQueues{};
   std::vector<std::thread> m_vThreads;
   std::vector<std::atomic<std::deque<std::coroutine_handle<>>*>> m_vQueues;
   std::atomic<bool> m_bDone{ false };
   std::mutex m_readyMtx;
   std::condition_variable m_ready;
   std::atomic<std::size_t> m_nReady{};

public:
   thread_pool(std::size_t numThreads = 0);
   thread_pool(const thread_pool&) = delete;
   thread_pool& operator=(const thread_pool&) = delete;
   thread_pool(thread_pool&&) = delete;
   thread_pool& operator=(thread_pool&&) = delete;
   ~thread_pool();

   auto operator co_await() {
      struct awaiter {
         thread_pool& m_pool;

         auto await_ready() const noexcept { return false; }
         void await_suspend(std::coroutine_handle<> coro) const noexcept {
            static std::size_t nIdx{};
            bool bDone{};
            while (!bDone) {
               auto& slot = m_pool.m_vQueues.at(nIdx);
               auto pQueue = slot.load(std::memory_order_acquire);
               if (pQueue) {
                  if (slot.compare_exchange_strong(pQueue, nullptr, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                     pQueue->emplace_front(coro);
                     slot.store(pQueue, std::memory_order_release);
                     m_pool.m_nReady.fetch_add(1, std::memory_order_release);
                     m_pool.m_ready.notify_all();
                     bDone = true;
                  }
               }
               nIdx = (nIdx + 1) % m_pool.m_nNumQueues;
            }
         }
         void await_resume() const noexcept {}
      };
      return awaiter{ *this };
   }

/*
   template<typename Callable>
   auto async(Callable&& callable) -> std::future<std::invoke_result_t<Callable>> {
      using return_t = std::invoke_result_t<Callable>;
      auto promise = std::promise<return_t>{};
      auto future = promise.get_future();
      if constexpr (std::is_same_v<void, return_t>)
         queue([p=std::move(promise), f=std::forward<Callable>(callable)]() mutable {
            try {
               f();
            }
            catch (...) {
               p.set_exception(std::current_exception());
               return;
            }
            p.set_value();
         });
      else
         queue([p=std::move(promise), f=std::forward<Callable>(callable)]() mutable {
            try {
               p.set_value(f());
            }
            catch (...) {
               p.set_exception(std::current_exception());
            }
         });
      return future;
   }
   */

   std::optional<std::coroutine_handle<>> try_pop(std::size_t nIdx);
   bool data_ready() const noexcept;

/*
private:
   template<typename Task>
   void queue(Task&& task) {
      static std::size_t nIdx{};
      bool bDone{};
      while (!bDone) {
         auto& slot = m_vQueues.at(nIdx);
         auto pQueue = slot.load(std::memory_order_acquire);
         if (pQueue) {
            if (slot.compare_exchange_strong(pQueue, nullptr, std::memory_order_acq_rel, std::memory_order_relaxed)) {
               pQueue->emplace_front(std::forward<Task>(task));
               slot.store(pQueue, std::memory_order_release);
               m_nReady.fetch_add(1, std::memory_order_release);
               m_ready.notify_all();
               bDone = true;
            }
         }
         nIdx = (nIdx + 1) % m_nNumQueues;
      }
   }
   */
};


#endif
