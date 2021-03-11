cmake_minimum_required(VERSION 2.8.3)
project(decentralized_path_auction)

find_package(catkin)
find_package(Boost)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Wshadow")

catkin_package(
  INCLUDE_DIRS
    include
  LIBRARIES
    ${PROJECT_NAME}
)

add_library(${PROJECT_NAME}
    src/auction.cpp
    src/graph.cpp
    src/path_search.cpp
    src/path_sync.cpp
)
target_include_directories(${PROJECT_NAME} PUBLIC include)

if (CATKIN_ENABLE_TESTING)
  catkin_add_gtest(test_${PROJECT_NAME}
    test/auction_test.cpp
    test/bid_chain_test.cpp
    test/graph_test.cpp
    test/path_sync_test.cpp
    test/single_path_search_test.cpp
    test/multi_path_search_test.cpp
    test/test.cpp
  )
  target_include_directories(test_${PROJECT_NAME} PUBLIC include)
  target_link_libraries(test_${PROJECT_NAME} ${PROJECT_NAME})
endif()
