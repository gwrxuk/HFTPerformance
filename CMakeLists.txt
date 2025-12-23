cmake_minimum_required(VERSION 3.20)
project(HFTTradingSystem 
    VERSION 1.0.0 
    LANGUAGES CXX
    DESCRIPTION "High-Frequency Trading System - Low Latency C++ Implementation"
)

# ============================================================================
# C++ Standard and Compiler Configuration
# ============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ============================================================================
# Build Type Configuration
# ============================================================================
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

# ============================================================================
# Compiler Flags for Low-Latency Performance
# ============================================================================
set(COMMON_FLAGS "-Wall -Wextra -Wpedantic -Werror")
set(PERFORMANCE_FLAGS "-march=native -mtune=native -ffast-math")
set(LTO_FLAGS "-flto")

# Debug flags with sanitizers
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${COMMON_FLAGS} -g3 -O0 -fsanitize=address,undefined")
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -fsanitize=address,undefined")

# Release flags optimized for low latency
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${COMMON_FLAGS} ${PERFORMANCE_FLAGS} ${LTO_FLAGS} -O3 -DNDEBUG")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} ${LTO_FLAGS}")

# Profile-guided optimization ready
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} ${COMMON_FLAGS} ${PERFORMANCE_FLAGS} -O3 -g")

# ============================================================================
# Find Required Packages
# ============================================================================
find_package(Threads REQUIRED)
find_package(Boost 1.74 REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)

# Optional packages
find_package(nlohmann_json 3.10 QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found, will use header-only version")
endif()

# ============================================================================
# Core Library - Low Latency Utilities
# ============================================================================
add_library(hft_core STATIC
    src/core/lockfree_queue.hpp
    src/core/memory_pool.hpp
    src/core/timing.hpp
    src/core/cpu_affinity.hpp
    src/core/spinlock.hpp
    src/core/types.hpp
    src/core/utils.cpp
)

target_include_directories(hft_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(hft_core PUBLIC
    Threads::Threads
)

set_target_properties(hft_core PROPERTIES
    LINKER_LANGUAGE CXX
)

# ============================================================================
# Protocol Library - FIX, WebSocket, REST
# ============================================================================
add_library(hft_protocol STATIC
    src/protocol/fix_parser.cpp
    src/protocol/fix_message.cpp
    src/protocol/websocket_handler.cpp
    src/protocol/rest_handler.cpp
)

target_include_directories(hft_protocol PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(hft_protocol PUBLIC
    hft_core
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
)

# ============================================================================
# Matching Engine Library
# ============================================================================
add_library(hft_matching STATIC
    src/matching/order.cpp
    src/matching/order_book.cpp
    src/matching/matching_engine.cpp
    src/matching/price_level.cpp
)

target_include_directories(hft_matching PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(hft_matching PUBLIC
    hft_core
)

# ============================================================================
# Market Data Library
# ============================================================================
add_library(hft_marketdata STATIC
    src/marketdata/market_data_handler.cpp
    src/marketdata/book_builder.cpp
    src/marketdata/feed_simulator.cpp
)

target_include_directories(hft_marketdata PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(hft_marketdata PUBLIC
    hft_core
    hft_protocol
)

# ============================================================================
# Executables
# ============================================================================

# Matching Engine Server
add_executable(matching_engine
    src/apps/matching_engine_main.cpp
)
target_link_libraries(matching_engine PRIVATE
    hft_matching
    hft_protocol
)

# Market Data Feed
add_executable(market_data_feed
    src/apps/market_data_feed_main.cpp
)
target_link_libraries(market_data_feed PRIVATE
    hft_marketdata
)

# Order Gateway
add_executable(order_gateway
    src/apps/order_gateway_main.cpp
)
target_link_libraries(order_gateway PRIVATE
    hft_matching
    hft_protocol
)

# ============================================================================
# Benchmark Suite
# ============================================================================
add_executable(benchmark_suite
    src/benchmark/benchmark_main.cpp
    src/benchmark/latency_benchmark.cpp
    src/benchmark/throughput_benchmark.cpp
    src/benchmark/orderbook_benchmark.cpp
)

target_link_libraries(benchmark_suite PRIVATE
    hft_core
    hft_matching
    hft_marketdata
)

# ============================================================================
# Tests
# ============================================================================
enable_testing()

add_executable(unit_tests
    tests/test_main.cpp
    tests/test_lockfree_queue.cpp
    tests/test_memory_pool.cpp
    tests/test_order_book.cpp
    tests/test_matching_engine.cpp
    tests/test_fix_parser.cpp
)

target_link_libraries(unit_tests PRIVATE
    hft_core
    hft_matching
    hft_protocol
)

add_test(NAME UnitTests COMMAND unit_tests)

# ============================================================================
# Installation
# ============================================================================
install(TARGETS 
    matching_engine 
    market_data_feed 
    order_gateway 
    benchmark_suite
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/ 
    DESTINATION include
    FILES_MATCHING PATTERN "*.hpp"
)

# ============================================================================
# Documentation (optional)
# ============================================================================
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
    set(DOXYGEN_GENERATE_HTML YES)
    set(DOXYGEN_GENERATE_MAN NO)
    doxygen_add_docs(docs
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        COMMENT "Generating API documentation"
    )
endif()

# ============================================================================
# Summary
# ============================================================================
message(STATUS "")
message(STATUS "=== HFT Trading System Configuration ===")
message(STATUS "Version:        ${PROJECT_VERSION}")
message(STATUS "Build type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard:   ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler:       ${CMAKE_CXX_COMPILER}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "=========================================")
message(STATUS "")

