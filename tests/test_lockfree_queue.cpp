/**
 * @file test_lockfree_queue.cpp
 * @brief Lock-free queue unit tests
 */

#include <iostream>
#include <thread>
#include <vector>
#include "core/lockfree_queue.hpp"

using namespace hft;

#define ASSERT(cond) \
    if (!(cond)) throw std::runtime_error("Assertion failed: " #cond)

void run_lockfree_queue_tests() {
    std::cout << "\n=== Lock-Free Queue Tests ===\n";
    
    // Test 1: Basic push/pop
    {
        std::cout << "  Basic push/pop... ";
        SPSCQueue<int, 16> queue;
        
        ASSERT(queue.empty());
        ASSERT(queue.try_push(42));
        ASSERT(!queue.empty());
        
        auto val = queue.try_pop();
        ASSERT(val.has_value());
        ASSERT(*val == 42);
        ASSERT(queue.empty());
        
        std::cout << "PASSED\n";
    }
    
    // Test 2: Queue capacity
    {
        std::cout << "  Queue capacity... ";
        SPSCQueue<int, 4> queue;  // Capacity is 3 (N-1)
        
        ASSERT(queue.try_push(1));
        ASSERT(queue.try_push(2));
        ASSERT(queue.try_push(3));
        ASSERT(!queue.try_push(4));  // Should fail - full
        
        ASSERT(queue.size() == 3);
        
        (void)queue.try_pop();
        ASSERT(queue.try_push(4));  // Should succeed now
        
        std::cout << "PASSED\n";
    }
    
    // Test 3: FIFO ordering
    {
        std::cout << "  FIFO ordering... ";
        SPSCQueue<int, 128> queue;
        
        for (int i = 0; i < 100; ++i) {
            (void)queue.try_push(i);
        }
        
        for (int i = 0; i < 100; ++i) {
            auto val = queue.try_pop();
            ASSERT(val.has_value());
            ASSERT(*val == i);
        }
        
        std::cout << "PASSED\n";
    }
    
    // Test 4: Multi-threaded producer/consumer
    {
        std::cout << "  Multi-threaded... ";
        SPSCQueue<std::uint64_t, 1024> queue;
        constexpr std::size_t N = 100000;
        
        std::thread producer([&]() {
            for (std::uint64_t i = 0; i < N; ++i) {
                queue.push(i);
            }
        });
        
        std::uint64_t expected = 0;
        std::thread consumer([&]() {
            for (std::size_t i = 0; i < N; ++i) {
                auto val = queue.pop();
                ASSERT(val == expected);
                ++expected;
            }
        });
        
        producer.join();
        consumer.join();
        
        ASSERT(expected == N);
        ASSERT(queue.empty());
        
        std::cout << "PASSED\n";
    }
    
    // Test 5: Complex types
    {
        std::cout << "  Complex types... ";
        struct TestStruct {
            int a;
            double b;
            std::string c;
        };
        
        SPSCQueue<TestStruct, 16> queue;
        
        (void)queue.try_push(TestStruct{1, 2.5, "hello"});
        (void)queue.try_push(TestStruct{2, 3.5, "world"});
        
        auto val1 = queue.try_pop();
        ASSERT(val1.has_value());
        ASSERT(val1->a == 1);
        ASSERT(val1->c == "hello");
        
        auto val2 = queue.try_pop();
        ASSERT(val2.has_value());
        ASSERT(val2->a == 2);
        ASSERT(val2->c == "world");
        
        std::cout << "PASSED\n";
    }
    
    std::cout << "  All lock-free queue tests passed!\n";
}

