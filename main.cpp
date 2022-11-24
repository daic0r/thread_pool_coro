#include <iostream>
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

    for (auto i = 0; i < 3; ++i) {
      
         auto nSum = add_async(pool);
         std::cout << "Result is " << nSum.get() << "\n";
    }

    return 0;
}