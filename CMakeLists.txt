cmake_minimum_required(VERSION 3.20)
project(exchange_engine CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(engine
    src/main.cpp
    src/OrderBook.cpp
)

target_include_directories(engine PRIVATE include)
target_compile_options(engine PRIVATE -Wall -Wextra -Wpedantic -O2)
