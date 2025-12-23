/**
 * @file test_memory_pool.cpp
 * @brief Memory pool unit tests
 */

#include <iostream>
#include <vector>
#include "core/memory_pool.hpp"

using namespace hft;

#define ASSERT(cond) \
    if (!(cond)) throw std::runtime_error("Assertion failed: " #cond)

void run_memory_pool_tests() {
    std::cout << "\n=== Memory Pool Tests ===\n";
    
    struct TestObject {
        int x;
        double y;
        char z[32];
        
        TestObject() : x(0), y(0.0), z{} {}
        TestObject(int a, double b) : x(a), y(b), z{} {}
    };
    
    // Test 1: Basic allocation
    {
        std::cout << "  Basic allocation... ";
        MemoryPool<TestObject, 10> pool;
        
        ASSERT(pool.allocated() == 0);
        ASSERT(pool.available() == 10);
        
        auto* obj = pool.create(42, 3.14);
        ASSERT(obj != nullptr);
        ASSERT(obj->x == 42);
        ASSERT(obj->y == 3.14);
        ASSERT(pool.allocated() == 1);
        
        pool.destroy(obj);
        ASSERT(pool.allocated() == 0);
        
        std::cout << "PASSED\n";
    }
    
    // Test 2: Pool exhaustion
    {
        std::cout << "  Pool exhaustion... ";
        MemoryPool<int, 5> pool;
        
        std::vector<int*> ptrs;
        for (int i = 0; i < 5; ++i) {
            auto* ptr = pool.create(i);
            ASSERT(ptr != nullptr);
            ptrs.push_back(ptr);
        }
        
        ASSERT(pool.full());
        ASSERT(pool.create(99) == nullptr);  // Should fail
        
        // Free one and try again
        pool.destroy(ptrs.back());
        ptrs.pop_back();
        
        auto* ptr = pool.create(99);
        ASSERT(ptr != nullptr);
        ASSERT(*ptr == 99);
        
        // Clean up
        for (auto* p : ptrs) {
            pool.destroy(p);
        }
        pool.destroy(ptr);
        
        std::cout << "PASSED\n";
    }
    
    // Test 3: Ownership check
    {
        std::cout << "  Ownership check... ";
        MemoryPool<int, 10> pool;
        
        auto* ptr = pool.create(42);
        ASSERT(pool.owns(ptr));
        
        int stack_var = 0;
        ASSERT(!pool.owns(&stack_var));
        
        int* heap_ptr = new int(0);
        ASSERT(!pool.owns(heap_ptr));
        delete heap_ptr;
        
        pool.destroy(ptr);
        
        std::cout << "PASSED\n";
    }
    
    // Test 4: Reuse after free
    {
        std::cout << "  Reuse after free... ";
        MemoryPool<TestObject, 3> pool;
        
        auto* p1 = pool.create(1, 1.0);
        auto* p2 = pool.create(2, 2.0);
        auto* p3 = pool.create(3, 3.0);
        
        pool.destroy(p2);  // Free middle
        
        auto* p4 = pool.create(4, 4.0);
        ASSERT(p4 == p2);  // Should reuse same slot
        ASSERT(p4->x == 4);
        
        pool.destroy(p1);
        pool.destroy(p3);
        pool.destroy(p4);
        
        std::cout << "PASSED\n";
    }
    
    // Test 5: PooledPtr RAII
    {
        std::cout << "  PooledPtr RAII... ";
        MemoryPool<TestObject, 5> pool;
        
        {
            auto ptr = make_pooled<TestObject>(pool, 42, 3.14);
            ASSERT(ptr);
            ASSERT(ptr->x == 42);
            ASSERT(pool.allocated() == 1);
        }
        
        // Should be automatically freed
        ASSERT(pool.allocated() == 0);
        
        std::cout << "PASSED\n";
    }
    
    std::cout << "  All memory pool tests passed!\n";
}

