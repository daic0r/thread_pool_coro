#include <iostream>
#include <vector>
#include <algorithm>
#include "thread_pool.h"
#include <string_view>
#include <future>
#include <chrono>

class ScopedTimeMeasurement {
    std::chrono::time_point<std::chrono::high_resolution_clock> tp_;
    std::chrono::nanoseconds dur_;
    bool bStopped_{};

public:
    ScopedTimeMeasurement(std::string_view strName) : tp_{ std::chrono::high_resolution_clock::now() } {
        std::cout << "*** Measuring " << strName << " ***\n\n";
    }
    void stop() {
        dur_ = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - tp_);
    }
    operator std::chrono::nanoseconds() const noexcept {
        return dur_;
    }
    ~ScopedTimeMeasurement() {
        if (not bStopped_)
            stop();
        std::cout << "This took " << dur_.count() << "ns\n";
    }
};

thread_pool::task<unsigned long long> add_async(thread_pool& pool) {
   co_await pool;
   //throw std::runtime_error("error");
   unsigned long long res{};
   for (int i = 0; i <100000000; ++i) {
         res += i; 
   }
   co_return res;
}

int main() {
    thread_pool pool;
	
    static constexpr auto ITERATIONS = 300;

    std::chrono::nanoseconds durationCoro;
    {
        std::vector<thread_pool::task<unsigned long long>> vFutures;
        vFutures.reserve(ITERATIONS);
        ScopedTimeMeasurement m{"execution on thread pool with coroutines"};
        for (auto i = 0; i < ITERATIONS; ++i) {
        
            vFutures.emplace_back(add_async(pool));
        }
        
        for (auto& fut : vFutures) {
            (void) fut.get();
            /*
            try {
                std::cout << "Result is " << fut.get() << "\n";
            }
            catch (...) {
                std::cout << "Horrible exception\n";
            }
            */
        }
        m.stop();
        durationCoro = m;
    }
    std::chrono::nanoseconds durationAsync;
    {
        std::vector<std::future<unsigned long long>> vFutures;
        vFutures.reserve(ITERATIONS);
        ScopedTimeMeasurement m{"execution on thread pool with async"};
        for (auto i = 0; i < ITERATIONS; ++i) {
        
            vFutures.emplace_back(std::async(std::launch::async, []() {
                unsigned long long res{};
                for (int i = 0; i <100000000; ++i) {
                        res += i; 
                }
                return res;
            }));
        }
        
        for (auto& fut : vFutures) {
            (void) fut.get();
            /*
            try {
                std::cout << "Result is " << fut.get() << "\n";
            }
            catch (...) {
                std::cout << "Horrible exception\n";
            }
            */
        }
        m.stop();
        durationAsync = m;
    }

    const auto fPercentage = (static_cast<float>(durationAsync.count()) / durationCoro.count()) * 100.0f;
    std::cout << "std::async variant took " << fPercentage << "% of the time of the coroutine thread pool.\n";

    return 0;
}