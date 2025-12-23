/**
 * @file test_main.cpp
 * @brief Main test runner
 */

#include <iostream>

// Simple test framework
#define TEST(name) void test_##name()
#define RUN_TEST(name) \
    do { \
        std::cout << "Running " << #name << "... "; \
        try { \
            test_##name(); \
            std::cout << "PASSED\n"; \
            ++passed; \
        } catch (const std::exception& e) { \
            std::cout << "FAILED: " << e.what() << "\n"; \
            ++failed; \
        } \
    } while(0)

#define ASSERT(cond) \
    if (!(cond)) throw std::runtime_error("Assertion failed: " #cond)

#define ASSERT_EQ(a, b) \
    if ((a) != (b)) throw std::runtime_error("Assertion failed: " #a " == " #b)

// Forward declarations
void run_lockfree_queue_tests();
void run_memory_pool_tests();
void run_order_book_tests();
void run_matching_engine_tests();
void run_fix_parser_tests();

int main() {
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                    HFT Trading System Tests                   ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";
    
    try {
        run_lockfree_queue_tests();
        run_memory_pool_tests();
        run_order_book_tests();
        run_matching_engine_tests();
        run_fix_parser_tests();
    } catch (const std::exception& e) {
        std::cerr << "Test suite error: " << e.what() << "\n";
        return 1;
    }
    
    std::cout << "\n══════════════════════════════════════════════════════════════\n";
    std::cout << "All tests completed successfully!\n";
    
    return 0;
}

