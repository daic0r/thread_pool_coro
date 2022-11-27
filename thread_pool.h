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
   friend struct awaiter;

public:
   template<typename T>
   class [[nodiscard]] task {
   public:
      struct promise_type;
      using handle_t = std::coroutine_handle<promise_type>;
      struct promise_type {
         std::variant<std::monostate, T, std::exception_ptr> m_value;

         constexpr auto get_return_object() noexcept {
            return task<T>{ handle_t::from_promise(*this) };
         }
         constexpr void unhandled_exception() {
            m_value = std::current_exception();
         }
         constexpr std::suspend_never initial_suspend() const noexcept { return {}; }
         constexpr std::suspend_always final_suspend() const noexcept { return {}; }
         constexpr void return_value(T val) noexcept(std::is_nothrow_move_assignable_v<T>) { m_value = std::move(val); }
      };

      /****************** Rule of 6 ****************/
      constexpr task(handle_t coro) : m_coro{ coro } {}
      task(const task&) = delete;
      task& operator=(const task&) = delete;
      constexpr task(task&& rhs) noexcept : m_coro{ std::exchange(rhs.m_coro, nullptr) } {}
      constexpr task& operator=(task&& rhs) noexcept { 
         task copy{ std::move(rhs) };
         copy.swap(*this);
         return *this;
      }
      constexpr ~task() {
         if (m_coro && !holds_exception())
            m_coro.destroy();
      }

      constexpr void swap(task& rhs) noexcept {
         using std::swap;
         swap(m_coro, rhs.m_coro);
      }

      friend constexpr void swap(task& lhs, task& rhs) noexcept {
         lhs.swap(rhs);
      }
      /*********************************************/

      constexpr operator std::coroutine_handle<>() const noexcept {
         return m_coro;
      }

      constexpr T get() {
         auto& promise = m_coro.promise();
         while (!ready());
         if (holds_exception()) {
            std::rethrow_exception(std::get<std::exception_ptr>(promise.m_value));
         }
         return std::move(std::get<T>(promise.m_value));
      }

      constexpr auto ready() const noexcept {
         return m_coro.promise().m_value.index() != 0;
      }

      constexpr auto holds_exception() const noexcept {
         return std::holds_alternative<std::exception_ptr>(m_coro.promise().m_value);
      }

      constexpr auto holds_value() const noexcept {
         return std::holds_alternative<T>(m_coro.promise().m_value);
      }

   private:
      handle_t m_coro;
   };

private:
   std::coroutine_handle<> do_pop_task(std::deque<std::coroutine_handle<>>*) noexcept;
   void do_emplace_task(std::coroutine_handle<>, std::deque<std::coroutine_handle<>>*);
   std::optional<std::coroutine_handle<>> try_pop(std::size_t nIdx) noexcept;
   constexpr bool data_ready() const noexcept {
      return m_nReady.load(std::memory_order_acquire) > 0 || m_bDone.load(std::memory_order_acquire);
   }

public:
   thread_pool(std::size_t numThreads = 0);
   thread_pool(const thread_pool&) = delete;
   thread_pool& operator=(const thread_pool&) = delete;
   thread_pool(thread_pool&&) = delete;
   thread_pool& operator=(thread_pool&&) = delete;
   ~thread_pool();

   constexpr auto operator co_await() noexcept {
      struct awaiter {
         thread_pool& m_pool;

         constexpr auto await_ready() const noexcept { return false; }
         void await_suspend(std::coroutine_handle<> coro) const noexcept {
            static std::size_t nIdx{};
            bool bDone{};
            while (!bDone) {
               auto& slot = m_pool.m_vQueues.at(nIdx);
               auto pQueue = slot.load(std::memory_order_acquire);
               if (pQueue) {
                  if (slot.compare_exchange_strong(pQueue, nullptr, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                     m_pool.do_emplace_task(coro, pQueue);
                     slot.store(pQueue, std::memory_order_release);
                     m_pool.m_ready.notify_all();
                     bDone = true;
                  }
               }
               nIdx = (nIdx + 1) % m_pool.m_nNumQueues;
            }
         }
         constexpr void await_resume() const noexcept {}
      };
      return awaiter{ *this };
   }

private:
   std::size_t m_nNumThreads{};
   std::size_t m_nNumQueues{};
   std::vector<std::thread> m_vThreads;
   std::vector<std::atomic<std::deque<std::coroutine_handle<>>*>> m_vQueues;
   std::atomic<bool> m_bDone{ false };
   std::mutex m_readyMtx;
   std::condition_variable m_ready;
   std::atomic<std::size_t> m_nReady{};
};


#endif
