cmake_minimum_required(VERSION 3.20)
project(low-latent-orderbook CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O3")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address -fsanitize=undefined -g")
include_directories(include)
add_executable(low-latent-orderbook 
    src/main.cpp
    src/order_book.cpp
)
