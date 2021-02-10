cmake_minimum_required(VERSION 3.0.2)
project(decentralized_path_auction)

find_package(Boost) 

# C++ setup
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Wshadow -Og -g")

add_library(decentralized_path_auction
    src/auction.cpp
    src/graph.cpp
    src/path_search.cpp
    src/path_sync.cpp
)
target_include_directories(decentralized_path_auction PUBLIC include)

# tests
find_package(GTest) 
add_subdirectory("/usr/src/gtest" ${CMAKE_BINARY_DIR}/gtest)

add_executable(test_app
    test/test.cpp
)

target_link_libraries(test_app gtest decentralized_path_auction)
target_include_directories(test_app PUBLIC include)
