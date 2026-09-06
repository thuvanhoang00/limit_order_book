cmake_minimum_required(VERSION 3.20)

project(order_book
    VERSION 0.1.0
    DESCRIPTION "C++20 low-latency order book practice project"
    LANGUAGES CXX)

option(ORDER_BOOK_BUILD_TESTS "Build unit tests" ON)
option(ORDER_BOOK_BUILD_BENCHMARKS "Build Google Benchmark targets" ON)
option(ORDER_BOOK_BUILD_TOOLS "Build replay and event-generator tools" ON)
option(ORDER_BOOK_FETCH_DEPS "Fetch missing test dependencies with FetchContent" ON)
option(ORDER_BOOK_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ORDER_BOOK_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(ORDER_BOOK_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)

add_library(order_book STATIC
    src/order_book.cpp
    src/event_io.cpp)

add_library(order_book::order_book ALIAS order_book)

target_compile_features(order_book PUBLIC cxx_std_20)
target_include_directories(order_book
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

target_set_project_warnings(order_book)
target_enable_sanitizers(order_book)

if(ORDER_BOOK_BUILD_TOOLS)
    add_executable(generate_events tools/generate_events.cpp)
    target_link_libraries(generate_events PRIVATE order_book::order_book)
    target_set_project_warnings(generate_events)
    target_enable_sanitizers(generate_events)

    add_executable(replay_order_book tools/replay.cpp)
    target_link_libraries(replay_order_book PRIVATE order_book::order_book)
    target_set_project_warnings(replay_order_book)
    target_enable_sanitizers(replay_order_book)
endif()

include(FetchContent)

if(ORDER_BOOK_BUILD_TESTS)
    include(CTest)
    enable_testing()

    find_package(GTest CONFIG QUIET)
    if(NOT GTest_FOUND AND ORDER_BOOK_FETCH_DEPS)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.15.2
            GIT_SHALLOW TRUE)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()

    if(TARGET GTest::gtest_main)
        add_executable(order_book_tests
            tests/test_order_book.cpp
            tests/test_order_registry.cpp
            tests/test_event_io.cpp)
        target_link_libraries(order_book_tests
            PRIVATE
                order_book::order_book
                GTest::gtest_main)
        target_set_project_warnings(order_book_tests)
        target_enable_sanitizers(order_book_tests)

        include(GoogleTest)
        gtest_discover_tests(order_book_tests)

        add_custom_target(check
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            DEPENDS order_book_tests
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            USES_TERMINAL)
    else()
        message(WARNING "GoogleTest was not found; tests are disabled")
    endif()
endif()

if(ORDER_BOOK_BUILD_BENCHMARKS)
    find_package(benchmark CONFIG QUIET)
    if(NOT benchmark_FOUND AND ORDER_BOOK_FETCH_DEPS)
        FetchContent_Declare(
            googlebenchmark
            GIT_REPOSITORY https://github.com/google/benchmark.git
            GIT_TAG v1.9.1
            GIT_SHALLOW TRUE)
        set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
        set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googlebenchmark)
    endif()

    if(TARGET benchmark::benchmark_main)
        add_executable(order_book_benchmarks
            benchmarks/benchmark_order_book.cpp)
        target_link_libraries(order_book_benchmarks
            PRIVATE
                order_book::order_book
                benchmark::benchmark_main)
        target_set_project_warnings(order_book_benchmarks)
        target_enable_sanitizers(order_book_benchmarks)
    else()
        message(WARNING "Google Benchmark was not found; benchmarks are disabled")
    endif()
endif()
