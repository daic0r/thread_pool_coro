#include "thread_pool.h"
#include <atomic>
#include <optional>
#include <iostream>
#include <algorithm>
#include <functional>
#include <cassert>

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
            nIdx = i % m_nNumQueues;
            std::size_t nCount{};
            std::optional<std::coroutine_handle<>> task;
            {
               std::unique_lock guard{ m_readyMtx }; 
               m_ready.wait(guard, std::bind(&thread_pool::data_ready, this)); 
               if (m_bDone.load(std::memory_order_acquire))
               {
                  break;
               }
            }
            task = try_pop(nIdx);
            if (!task) {
               nIdx = (nIdx + 1) % m_nNumQueues;
               while (data_ready() && nCount++ < m_nNumQueues && !(task = try_pop(nIdx)) )
                  nIdx = (nIdx + 1) % m_nNumQueues;
            }
            // Check if loop above finished because of valid situation
            if (task) {
               m_nReady.fetch_sub(1, std::memory_order_release);
               (*task).resume();
            }
         }
      });
   }
}

std::optional<std::coroutine_handle<>> thread_pool::try_pop(std::size_t nIdx) {
   std::optional<std::coroutine_handle<>> ret;

   auto& slot = m_vQueues.at(nIdx);
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

bool thread_pool::data_ready() const noexcept {
   return m_nReady.load(std::memory_order_acquire) > 0 || m_bDone.load(std::memory_order_acquire);
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
