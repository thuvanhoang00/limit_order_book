cmake_minimum_required(VERSION 3.20)

project(order_book
    VERSION 0.1.0
    DESCRIPTION "C++20 low-latency order book practice project"
    LANGUAGES CXX)

option(ORDER_BOOK_BUILD_TESTS "Build unit tests" ON)
option(ORDER_BOOK_BUILD_BENCHMARKS "Build Google Benchmark targets" ON)
option(ORDER_BOOK_BUILD_TOOLS "Build replay and event-generator tools" ON)
option(ORDER_BOOK_BUILD_NETWORKING "Build Linux TCP and UDP multicast event transports" ON)
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
    src/core/order_book.cpp
    src/core/logger.cpp
    src/io/event_io.cpp)

add_library(order_book::order_book ALIAS order_book)

find_package(Threads REQUIRED)
target_link_libraries(order_book PUBLIC Threads::Threads)
target_compile_features(order_book PUBLIC cxx_std_20)
target_include_directories(order_book
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

target_set_project_warnings(order_book)
target_enable_sanitizers(order_book)

if(ORDER_BOOK_BUILD_NETWORKING)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "Networking demos support Linux; set ORDER_BOOK_BUILD_NETWORKING=OFF")
    endif()
    add_library(order_book_net STATIC
        src/net/socket.cpp src/net/tcp.cpp src/net/udp.cpp src/net/protocol.cpp)
    add_library(order_book::net ALIAS order_book_net)
    target_link_libraries(order_book_net PUBLIC order_book::order_book)
    target_set_project_warnings(order_book_net)
    target_enable_sanitizers(order_book_net)
    if(ORDER_BOOK_BUILD_TOOLS)
        foreach(tool IN ITEMS tcp_event_server tcp_event_client udp_event_publisher udp_event_subscriber)
            add_executable(${tool} tools/${tool}.cpp)
            target_link_libraries(${tool} PRIVATE order_book::net)
            target_set_project_warnings(${tool})
            target_enable_sanitizers(${tool})
        endforeach()
    endif()
endif()

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

# Actual pre-pool implementation, renamed for side-by-side verification.
if(ORDER_BOOK_BUILD_TESTS OR ORDER_BOOK_BUILD_BENCHMARKS)
    add_library(order_book_heap_baseline STATIC benchmarks/support/heap_order_book.cpp)
    target_link_libraries(order_book_heap_baseline PUBLIC order_book::order_book)
    target_include_directories(order_book_heap_baseline PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks)
    target_set_project_warnings(order_book_heap_baseline)
    target_enable_sanitizers(order_book_heap_baseline)
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
            tests/core/test_order_book.cpp
            tests/core/test_lfqueue.cpp
            tests/core/test_logger.cpp
            tests/test_order_registry.cpp
            tests/io/test_event_io.cpp
            tests/memory/test_memory_pool.cpp
            tests/memory/test_byte_pool.cpp
            tests/memory/test_fixed_slot_pool.cpp
            tests/memory/test_allocator_adaptor.cpp
            tests/memory/test_slot_allocator.cpp)
        target_link_libraries(order_book_tests
            PRIVATE
                order_book::order_book
                GTest::gtest_main)
        target_include_directories(order_book_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks)
        target_set_project_warnings(order_book_tests)
        target_enable_sanitizers(order_book_tests)

        if(ORDER_BOOK_BUILD_NETWORKING)
            target_sources(order_book_tests PRIVATE tests/net/test_tcp.cpp tests/net/test_udp.cpp)
            target_link_libraries(order_book_tests PRIVATE order_book::net)
        endif()

        include(GoogleTest)
        gtest_discover_tests(order_book_tests)

        if(ORDER_BOOK_BUILD_NETWORKING AND ORDER_BOOK_BUILD_TOOLS)
            find_package(Python3 COMPONENTS Interpreter QUIET)
            if(Python3_Interpreter_FOUND)
                add_test(NAME TcpCli.IntegratedBook
                    COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tests/net/test_tcp_cli.py
                        --bin-dir $<TARGET_FILE_DIR:tcp_event_server>)
                set_tests_properties(TcpCli.IntegratedBook PROPERTIES TIMEOUT 60)
                add_test(NAME UdpCli.MulticastBook
                    COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tests/net/test_udp_cli.py
                        --bin-dir $<TARGET_FILE_DIR:udp_event_subscriber>)
                set_tests_properties(UdpCli.MulticastBook PROPERTIES TIMEOUT 60)
            endif()
        endif()

        # Keep global operator-new interception isolated from the ordinary test binary.
        add_executable(order_book_allocation_tests
            tests/core/test_order_book_pool.cpp
            benchmarks/support/allocation_probe.cpp)
        target_link_libraries(order_book_allocation_tests PRIVATE
            order_book_heap_baseline GTest::gtest_main)
        target_set_project_warnings(order_book_allocation_tests)
        target_enable_sanitizers(order_book_allocation_tests)
        gtest_discover_tests(order_book_allocation_tests)

        add_custom_target(check
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            DEPENDS order_book_tests order_book_allocation_tests
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            USES_TERMINAL)
        if(ORDER_BOOK_BUILD_NETWORKING AND ORDER_BOOK_BUILD_TOOLS AND Python3_Interpreter_FOUND)
            add_dependencies(check tcp_event_server tcp_event_client replay_order_book
                udp_event_publisher udp_event_subscriber)
        endif()
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
        add_executable(order_book_allocator_benchmarks
            benchmarks/benchmark_book_allocators.cpp
            benchmarks/support/allocation_probe.cpp)
        target_link_libraries(order_book_allocator_benchmarks PRIVATE
            order_book_heap_baseline benchmark::benchmark_main)
        target_set_project_warnings(order_book_allocator_benchmarks)
        target_enable_sanitizers(order_book_allocator_benchmarks)

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
