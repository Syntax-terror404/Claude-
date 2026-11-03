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

add_executable(bbg_translate_tests tests/test_bbg_translate.cpp)
target_link_libraries(bbg_translate_tests lob)
add_test(NAME bbg_translate COMMAND bbg_translate_tests)

# Live Bloomberg source: needs the BLPAPI C++ SDK from Bloomberg (not on public package indexes).
#   cmake -B build -DLOB_WITH_BLPAPI=ON -DBLPAPI_ROOT=/path/to/blpapi_cpp
option(LOB_WITH_BLPAPI "Build the BLPAPI market-data source" OFF)
if(LOB_WITH_BLPAPI)
  find_path(BLPAPI_INCLUDE blpapi_session.h HINTS ${BLPAPI_ROOT}/include REQUIRED)
  find_library(BLPAPI_LIB NAMES blpapi3_64 blpapi3_32 blpapi HINTS ${BLPAPI_ROOT}/Linux ${BLPAPI_ROOT}/lib REQUIRED)
  add_library(lob_blpapi src/blpapi_source.cpp)
  target_include_directories(lob_blpapi PUBLIC src ${BLPAPI_INCLUDE})
  target_link_libraries(lob_blpapi PUBLIC lob ${BLPAPI_LIB})
endif()
