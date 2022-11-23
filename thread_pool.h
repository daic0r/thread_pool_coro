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
#include "task.h"
#include <iostream>
#include <optional>
#include <coroutine>
#include <type_traits>
#include <utility>
#include <cassert>

class thread_pool {
   friend struct fetch_task;

   using task_t = task;
   using queue_t = std::atomic<std::deque<task_t>*>;
   using queues_t = std::vector<queue_t>;

   struct task_queue {
      thread_pool* pool_;
      std::size_t idx_;
      queue_t queue_;

      task_queue(thread_pool* p, std::size_t i) : pool_{p}, idx_{i} {}
   };

   class [[nodiscard]] coro {
   public:
      struct promise_type;
      using handle_t = std::coroutine_handle<promise_type>;
      struct promise_type {
         auto get_return_object() noexcept {
            return coro{ handle_t::from_promise(*this) };
         }

         constexpr std::suspend_never initial_suspend() noexcept { return {}; }
         constexpr std::suspend_always final_suspend() noexcept { return {}; }
         void unhandled_exception() { std::terminate(); }
         constexpr void return_void() noexcept {}
         auto await_transform(task_queue& queue) noexcept {
            struct awaiter {
               task_queue& queue_;

               bool await_ready() const noexcept { 
                  //std::cout << "await_ready on " << idx_ << "\n";
                  return queue_.pool_->m_flag.test();
               }

               bool await_suspend(std::coroutine_handle<> coro) {
                  //std::cout << "await_suspend on " << queue_.idx_ << "\n";
                  queue_.pool_->m_flag.wait(false, std::memory_order_acquire);
                  if (not queue_.pool_->m_bDone.load(std::memory_order_acquire))
                     queue_.pool_->m_flag.clear();
                  return false;
               }

               auto await_resume() noexcept { 
                  //std::cout << "await_resume on " << queue_.idx_ << "\n";
                  std::size_t nCount{};
                  queue_.idx_ %= queue_.pool_->m_nNumQueues;
                  auto task = queue_.pool_->try_pop(queue_.idx_);
                  if (!task) {
                     queue_.idx_ = (queue_.idx_ + 1) % queue_.pool_->m_nNumQueues;
                     while (nCount++ < queue_.pool_->m_nNumQueues && !(task = queue_.pool_->try_pop(queue_.idx_)) )
                        queue_.idx_ = (queue_.idx_ + 1) % queue_.pool_->m_nNumQueues;
                  }
                  return task;
               }
            };
            return awaiter{ queue };
         }
 
      };

   private:
      handle_t m_coro;

   public:
      coro() = default;
      constexpr coro(handle_t coro) : m_coro{ coro } {}
      coro(const coro&) = delete;
      coro& operator=(const coro&) = delete;
      constexpr coro(coro&& rhs) noexcept : m_coro{ std::exchange(rhs.m_coro, nullptr) } {}
      constexpr coro& operator=(coro&& rhs) noexcept { 
         m_coro = std::exchange(rhs.m_coro, nullptr);
         return *this;
      }
      constexpr ~coro() {
         if (m_coro)
            m_coro.destroy();
      }

      void operator()() const noexcept {
         //std::cout << "Resuming\n";
         m_coro.resume();
      }

   };

   std::size_t m_nNumThreads{};
   std::size_t m_nNumQueues{};
   std::vector<std::thread> m_vThreads;
   std::deque<task_queue> m_vTaskQueues;
   std::atomic_flag m_flag = ATOMIC_FLAG_INIT;
   std::atomic<bool> m_bDone{};

public:
   thread_pool(std::size_t numThreads = 0);
   thread_pool(const thread_pool&) = delete;
   thread_pool& operator=(const thread_pool&) = delete;
   thread_pool(thread_pool&&) = delete;
   thread_pool& operator=(thread_pool&&) = delete;
   ~thread_pool();

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

   std::optional<task_t> try_pop(std::size_t nIdx);

private:

   template<typename Task>
   void queue(Task&& task) {
      static std::size_t nIdx{};
      bool bDone{};
      while (!bDone) {
         auto& task_queue = m_vTaskQueues.at(nIdx);
         auto& slot = m_vTaskQueues.at(nIdx).queue_;
         auto pQueue = slot.load(std::memory_order_acquire);
         if (pQueue) {
            if (slot.compare_exchange_strong(pQueue, nullptr, std::memory_order_acq_rel, std::memory_order_relaxed)) {
               pQueue->emplace_front(std::forward<Task>(task));
               slot.store(pQueue, std::memory_order_release);
               m_flag.test_and_set(std::memory_order_release);
               m_flag.notify_all();
               bDone = true;
            }
         }
         nIdx = (nIdx + 1) % m_nNumQueues;
      }
   }

   coro run_tasks(std::size_t nQueueIdx);
};


#endif
