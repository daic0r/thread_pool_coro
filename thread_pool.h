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

class thread_pool {
   friend struct fetch_task;

   using task_t = task;
   using queue_t = std::atomic<std::deque<task_t>*>;
   using queues_t = std::vector<queue_t>;

   struct task_queue {
      struct promise_type;
      using handle_t = std::coroutine_handle<promise_type>;
      struct promise_type {
         queue_t queue_;
         std::atomic<bool> bSuspended_{};

         auto get_return_object() noexcept {
            return task_queue{ handle_t::from_promise(*this) };
         }

         constexpr std::suspend_always initial_suspend() noexcept { return {}; }
         constexpr std::suspend_always final_suspend() noexcept { return {}; }
         void unhandled_exception() { std::terminate(); }
         constexpr void return_void() noexcept {}
      };

      handle_t m_coro;

   public:
      task_queue() = default;
      constexpr task_queue(handle_t coro) : m_coro{ coro } {
         //std::cout << "task_queue: constructed at " << this << "\n";
      }
      task_queue(const task_queue&) = delete;
      task_queue& operator=(const task_queue&) = delete;
      constexpr task_queue(task_queue&& rhs) noexcept : m_coro{ std::exchange(rhs.m_coro, nullptr) } {}
      constexpr task_queue& operator=(task_queue&& rhs) noexcept { 
         m_coro = std::exchange(rhs.m_coro, nullptr);
         return *this;
      }
      constexpr ~task_queue() {
         if (m_coro)
            m_coro.destroy();
      }

      constexpr void operator()() const noexcept {
         //std::cout << "Resuming\n";
         m_coro.resume();
      }

      constexpr auto& queue() noexcept {
         return m_coro.promise().queue_;
      }

      constexpr bool suspended() const noexcept {
         return m_coro.promise().bSuspended_.load(std::memory_order_acquire);
      }
   };

   std::size_t m_nNumThreads{};
   std::size_t m_nNumQueues{};
   std::vector<std::thread> m_vThreads;
   std::vector<task_queue> m_vTaskQueues;

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
   struct fetch_task {
      thread_pool& pool_;
      std::size_t idx_;

      constexpr bool await_ready() const noexcept { 
         //std::cout << "await_ready on " << idx_ << "\n";
         return false;
      }

      constexpr void await_suspend(std::coroutine_handle<>) {
         //std::cout << "await_suspend on " << idx_ << "\n";
         pool_.m_vTaskQueues.at(idx_).m_coro.promise().bSuspended_.store(true, std::memory_order_release);
      }

      auto await_resume() noexcept { 
         std::cout << "await_resume on " << idx_ << "\n";

         std::size_t nCount{};
         idx_ %= pool_.m_nNumQueues;
         auto task = pool_.try_pop(idx_);
         if (!task) {
            idx_ = (idx_ + 1) % pool_.m_nNumQueues;
            while (nCount++ < pool_.m_nNumQueues && !(task = pool_.try_pop(idx_)) )
               idx_ = (idx_ + 1) % pool_.m_nNumQueues;
         }
         pool_.m_vTaskQueues.at(idx_).m_coro.promise().bSuspended_.store(false, std::memory_order_release);
         return task;
      }
   };

   template<typename Task>
   void queue(Task&& task) {
      static std::size_t nIdx{};
      bool bDone{};
      while (!bDone) {
         auto& task_queue = m_vTaskQueues.at(nIdx);
         auto& slot = task_queue.queue();
         auto pQueue = slot.load(std::memory_order_acquire);
         if (pQueue) {
            if (slot.compare_exchange_strong(pQueue, nullptr, std::memory_order_acq_rel, std::memory_order_relaxed)) {
               pQueue->emplace_front(std::forward<Task>(task));
               slot.store(pQueue, std::memory_order_release);
               std::size_t i{nIdx};
               while (true) {
                  auto& coro = m_vTaskQueues.at((nIdx+i) % m_nNumQueues);
                  if (coro.suspended()) {
                     coro();
                     break;
                  }
                  ++i;
               }
               bDone = true;
            }
         }
         nIdx = (nIdx + 1) % m_nNumQueues;
      }
   }

   task_queue run_tasks(std::size_t nQueueIdx);
};


#endif
