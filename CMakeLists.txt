cmake_minimum_required(VERSION 2.8.3)
project(decentralized_path_auction)

find_package(catkin REQUIRED)
find_package(Boost)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
add_compile_options(-Wall -Wextra -Wpedantic -Wshadow)

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

#############
## Install ##
#############

install(TARGETS
  ${PROJECT_NAME}
  ARCHIVE DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  LIBRARY DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  RUNTIME DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
)

catkin_install_python(PROGRAMS
   scripts/plot.py
   DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
)

install(DIRECTORY include/${PROJECT_NAME}/
  DESTINATION ${CATKIN_PACKAGE_INCLUDE_DESTINATION}
  FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h"
)
