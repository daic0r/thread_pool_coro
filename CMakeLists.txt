cmake_minimum_required(VERSION 3.24)

project (thread_pool_coro)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED 20)

add_executable(${PROJECT_NAME} thread_pool.cpp main.cpp)