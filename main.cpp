#include <iostream>
#include <vector>
#include "thread_pool.h"

thread_pool::task<unsigned long long> add_async(thread_pool& pool) {
   co_await pool;
   unsigned long long res{};
   for (int i = 0; i <1000000000; ++i) {
         res += i; 
   }
   co_return res;
}

int main() {
    thread_pool pool;
	
	std::vector<thread_pool::task<unsigned long long>> vFutures;

    for (auto i = 0; i < 3; ++i) {
      
         vFutures.emplace_back(add_async(pool));
    }
	
	for (auto& fut : vFutures) {
		std::cout << "Result is " << fut.get() << "\n";
	}

    return 0;
}