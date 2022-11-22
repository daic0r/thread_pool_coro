#include "thread_pool.h"
#include <atomic>
#include <optional>
#include <iostream>
#include <algorithm>
#include <functional>
#include <cassert>
#include <chrono>

thread_pool::task_queue thread_pool::run_tasks(std::size_t nQueueIdx) {
   std::cout << "Coroutine " << nQueueIdx << " launched\n";
   while (true) {
      auto task = co_await fetch_task{ *this, nQueueIdx };
      if (task) {
         (*task)();
      }
   }
}

thread_pool::thread_pool(std::size_t numThreads) :
   m_nNumThreads{ numThreads == 0 ? std::thread::hardware_concurrency() : numThreads },
   m_nNumQueues{ m_nNumThreads },
   m_vTaskQueues(m_nNumQueues)
{

   m_vThreads.reserve(m_nNumThreads);
   m_vTaskQueues.reserve(m_nNumQueues);
   for (std::size_t i{}; i < m_nNumThreads; ++i) {

      m_vThreads.emplace_back([this, i] () {
         auto& coro = m_vTaskQueues.at(i);
         coro = run_tasks(i);
         coro.queue().store(new std::deque<task_t>{});
         coro();
     });
   }
}

std::optional<thread_pool::task_t> thread_pool::try_pop(std::size_t nIdx) {
   std::optional<task_t> ret;

   auto& slot = m_vTaskQueues.at(nIdx).queue();
   auto pQueue = slot.load(std::memory_order_acquire);
   if (!pQueue) return ret;
   if (pQueue->empty()) return ret;

   if (!slot.compare_exchange_strong(pQueue, nullptr, std::memory_order_acq_rel, std::memory_order_relaxed)) return ret;

   if (!pQueue->empty()) {
      ret = std::move(pQueue->back());
      pQueue->pop_back();
      slot.store(pQueue, std::memory_order_release);
      return ret;
   }
   slot.store(pQueue, std::memory_order_release);
   return ret;
}

thread_pool::~thread_pool() {
   for (auto& t : m_vThreads)
      t.join();

   for (auto& a : m_vTaskQueues) {
      auto pQueue = a.queue().load(std::memory_order_acquire);
      delete pQueue;
   }
}
