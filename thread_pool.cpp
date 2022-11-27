#include "thread_pool.h"
#include <functional>

thread_pool::thread_pool(std::size_t numThreads) :
   m_nNumThreads{ numThreads == 0 ? std::thread::hardware_concurrency() : numThreads },
   m_nNumQueues{ m_nNumThreads }, 
   m_vQueues(m_nNumQueues) 
{
   for (auto& a : m_vQueues)
      a.store(new std::deque<std::coroutine_handle<>>{});

   m_vThreads.reserve(m_nNumThreads);
   for (std::size_t i{}; i < m_nNumThreads; ++i) {
      m_vThreads.emplace_back([this, i] () {
         std::size_t nIdx{};
         while (!m_bDone.load(std::memory_order_acquire)) {
            {
               std::unique_lock guard{ m_readyMtx }; 
               m_ready.wait(guard, std::bind(&thread_pool::data_ready, this)); 
               if (m_bDone.load(std::memory_order_acquire))
               {
                  break;
               }
            }
            nIdx = i % m_nNumQueues;
            auto task = try_pop(nIdx);
            if (!task) {
               nIdx = (nIdx + 1) % m_nNumQueues;
               std::size_t nCount{};
               while (data_ready() && nCount++ < m_nNumQueues && !(task = try_pop(nIdx)) )
                  nIdx = (nIdx + 1) % m_nNumQueues;
            }
            // Run the task
            if (task) {
               (*task).resume();
            }
         }
      });
   }
}

std::coroutine_handle<> thread_pool::do_pop_task(std::deque<std::coroutine_handle<>>* pQueue) noexcept {
   auto ret = std::move(pQueue->back());
   pQueue->pop_back();
   m_nReady.fetch_sub(1, std::memory_order_release);
   return ret;
}

void thread_pool::do_emplace_task(std::coroutine_handle<> task, std::deque<std::coroutine_handle<>>* pQueue) {
   pQueue->emplace_front(task);
   m_nReady.fetch_add(1, std::memory_order_release);
}

std::optional<std::coroutine_handle<>> thread_pool::try_pop(std::size_t nIdx) noexcept {
   std::optional<std::coroutine_handle<>> ret;

   auto& slot = m_vQueues.at(nIdx);

   auto pQueue = slot.load(std::memory_order_acquire);
   if (!pQueue) return ret;

   if (!slot.compare_exchange_strong(pQueue, nullptr, std::memory_order_acq_rel, std::memory_order_relaxed)) return ret;

   if (pQueue->empty()) {
      slot.store(pQueue, std::memory_order_release);
      return ret;
   }

   ret = do_pop_task(pQueue);

   slot.store(pQueue, std::memory_order_release);
   return ret;
}

thread_pool::~thread_pool() {
   m_bDone.store(true, std::memory_order_release);
   // If queues < threads more than one 1 thread will be waiting on the same
   // mutex -> notify_all
   m_ready.notify_all();

   for (auto& t : m_vThreads)
      t.join();

   for (auto& a : m_vQueues) {
      auto pQueue = a.load();
      delete pQueue;
   }
}
