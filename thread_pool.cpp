#include "thread_pool.h"
#include <atomic>
#include <optional>
#include <iostream>
#include <algorithm>
#include <functional>
#include <cassert>
#include <chrono>

thread_pool::coro thread_pool::run_tasks(std::size_t nQueueIdx) {
   std::cout << "Coroutine " << nQueueIdx << " launched\n";
   while (not m_bDone.load(std::memory_order_acquire)) {
      auto task = co_await m_vTaskQueues.at(nQueueIdx);
      std::cout << "Wakeup\n";
      if (task) {
         std::cout << "Running task...\n";
         (*task)();
      }
      std::cout << "Done with task...\n";
   }
   std::cout << "Coroutine " << nQueueIdx << " finished\n";
   co_return;
}

thread_pool::thread_pool(std::size_t numThreads) :
   m_nNumThreads{ numThreads == 0 ? std::thread::hardware_concurrency() : numThreads },
   m_nNumQueues{ m_nNumThreads }
{

   m_vThreads.reserve(m_nNumThreads);
   //m_vTaskQueues.reserve(m_nNumQueues);
   for (std::size_t i{}; i < m_nNumQueues; ++i) {
      m_vTaskQueues.emplace_back(this, i);
      m_vTaskQueues.back().queue_.store(new std::deque<task_t>{}, std::memory_order_release);
   }
   for (std::size_t i{}; i < m_nNumThreads; ++i) {
      auto bindable = std::bind(&thread_pool::run_tasks, this, std::placeholders::_1);
      m_vThreads.emplace_back(bindable, i);
   }
}

std::optional<thread_pool::task_t> thread_pool::try_pop(std::size_t nIdx) {
   std::optional<task_t> ret;

   auto& slot = m_vTaskQueues.at(nIdx).queue_;
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
   m_bDone.store(true, std::memory_order_release);
   m_flag.test_and_set(std::memory_order_release);
   m_flag.notify_all();

   for (auto& t : m_vThreads)
      t.join();

   for (auto& a : m_vTaskQueues) {
      auto pQueue = a.queue_.load(std::memory_order_acquire);
      delete pQueue;
   }
}
