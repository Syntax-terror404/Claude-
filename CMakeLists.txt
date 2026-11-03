cmake_minimum_required(VERSION 3.14)
project(lobster LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(LOB_SANITIZE "Build with ASan + UBSan" OFF)

add_library(lob src/order_book.cpp)
target_include_directories(lob PUBLIC include)
target_compile_options(lob PUBLIC -Wall -Wextra -Wpedantic)
if(LOB_SANITIZE)
  target_compile_options(lob PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(lob PUBLIC -fsanitize=address,undefined)
endif()

add_executable(lob_tests tests/test_order_book.cpp)
target_link_libraries(lob_tests lob)

add_executable(lob_bench bench/bench.cpp)
target_link_libraries(lob_bench lob)

add_executable(lob_demo examples/demo.cpp)
target_link_libraries(lob_demo lob)

enable_testing()
add_test(NAME order_book COMMAND lob_tests)

add_executable(feed_tests tests/test_feed.cpp)
target_link_libraries(feed_tests lob)
add_test(NAME feed COMMAND feed_tests)

add_executable(pipeline examples/pipeline.cpp)
target_link_libraries(pipeline lob)

add_executable(wire_tests tests/test_wire.cpp)
target_link_libraries(wire_tests lob)
add_test(NAME wire COMMAND wire_tests)
