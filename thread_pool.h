#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <condition_variable>
#include <exception>
#include <thread>
#include <mutex>
#include <vector>
#include <deque>
#include <atomic>
#include <optional>
#include <coroutine>
#include <type_traits>
#include <utility>
#include <variant>

class thread_pool {
public:
   template<typename T>
   class [[nodiscard]] task {
   public:
      struct promise_type;
      using handle_t = std::coroutine_handle<promise_type>;
      struct promise_type {
         std::variant<std::monostate, T, std::exception_ptr> m_value;

         auto get_return_object() noexcept {
            return task<T>{ handle_t::from_promise(*this) };
         }
         void unhandled_exception() {
            m_value = std::current_exception();
         }
         std::suspend_never initial_suspend() const noexcept { return {}; }
         std::suspend_always final_suspend() const noexcept { return {}; }
         void return_value(T val) noexcept(std::is_nothrow_move_assignable_v<T>) { m_value = std::move(val); }
      };

      /****************** Rule of 6 ****************/
      task(handle_t coro) : m_coro{ coro } {}
      task(const task&) = delete;
      task& operator=(const task&) = delete;
      task(task&& rhs) noexcept : m_coro{ std::exchange(rhs.m_coro, nullptr) } {}
      task& operator=(task&& rhs) noexcept { 
         task copy{ std::move(rhs) };
         copy.swap(*this);
         return *this;
      }
      ~task() {
         if (m_coro && !holds_exception())
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

      T get() {
         auto& promise = m_coro.promise();
         while (!ready());
         if (holds_exception()) {
            std::rethrow_exception(std::get<std::exception_ptr>(promise.m_value));
         }
         return std::move(std::get<T>(promise.m_value));
      }

      auto ready() const noexcept {
         return m_coro.promise().m_value.index() != 0;
      }

      auto holds_exception() const noexcept {
         return std::holds_alternative<std::exception_ptr>(m_coro.promise().m_value);
      }

      auto holds_value() const noexcept {
         return std::holds_alternative<T>(m_coro.promise().m_value);
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

   std::optional<std::coroutine_handle<>> try_pop(std::size_t nIdx) noexcept;
   bool data_ready() const noexcept;

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

};


#endif
