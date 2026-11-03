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

add_executable(feature_tests tests/test_features.cpp)
target_link_libraries(feature_tests lob)
add_test(NAME features COMMAND feature_tests)

add_executable(record_features examples/record_features.cpp)
target_link_libraries(record_features lob)

add_executable(backtest_tests tests/test_backtest.cpp)
target_link_libraries(backtest_tests lob)
add_test(NAME backtest COMMAND backtest_tests)

add_executable(backtest examples/backtest.cpp)
target_link_libraries(backtest lob)

# BlazingMQ transport (opt-in; BlazingMQ takes about an hour to build from source, so CI skips it).
#   cmake -B build-bmq -DLOB_WITH_BMQ=ON -DBMQ_ROOT=/path/to/blazingmq   (a tree built with bin/build-ubuntu.sh)
# BlazingMQ is built as C++23 with BDE debug settings; targets below mirror those defines for ABI parity.
option(LOB_WITH_BMQ "Build BlazingMQ publisher/consumer" OFF)
if(LOB_WITH_BMQ)
  set(BMQ_ROOT "" CACHE PATH "BlazingMQ source tree, built via bin/build-ubuntu.sh")
  if(NOT BMQ_ROOT)
    message(FATAL_ERROR "set -DBMQ_ROOT=<blazingmq tree>")
  endif()
  file(GLOB BMQ_GROUP_DIRS LIST_DIRECTORIES true ${BMQ_ROOT}/src/groups/bmq/*)
  set(BMQ_LIBS ${BMQ_ROOT}/build/blazingmq/src/groups/bmq/libbmq.a
      ${BMQ_ROOT}/lib64/opt_exc_mt/libntc.a ${BMQ_ROOT}/lib64/opt_exc_mt/libnts.a
      ${BMQ_ROOT}/lib64/libbal.a ${BMQ_ROOT}/lib64/libbdl.a ${BMQ_ROOT}/lib64/libbsl.a
      ${BMQ_ROOT}/lib64/libbbryu.a ${BMQ_ROOT}/lib64/libinteldfp.a ${BMQ_ROOT}/lib64/libpcre2.a)
  foreach(t bmq_publish bmq_consume)
    add_executable(${t} examples/${t}.cpp)
    target_link_libraries(${t} PRIVATE lob)
    set_target_properties(${t} PROPERTIES CXX_STANDARD 23)
    target_include_directories(${t} PRIVATE ${BMQ_GROUP_DIRS})
    target_include_directories(${t} SYSTEM PRIVATE ${BMQ_ROOT}/include)
    target_compile_definitions(${t} PRIVATE BDE_BUILD_TARGET_64 BDE_BUILD_TARGET_DBG
      BSLS_ASSERT_LEVEL_ASSERT_SAFE _POSIX_PTHREAD_SEMANTICS _REENTRANT)
    target_compile_options(${t} PRIVATE -march=westmere -fno-strict-aliasing -pthread)
    target_link_libraries(${t} PRIVATE ${BMQ_LIBS} z ssl crypto dl rt pthread)
  endforeach()
endif()
