/**
 * @file throughput_benchmark.cpp
 * @brief Throughput benchmarks for system components
 */

#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <atomic>
#include "core/types.hpp"
#include "core/lockfree_queue.hpp"
#include "core/cpu_affinity.hpp"
#include "matching/order.hpp"

using namespace hft;

void run_throughput_benchmarks() {
    std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    std::cout << "Throughput Benchmarks\n";
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    
    // SPSC Queue throughput with different core configurations
    {
        std::cout << "\nSPSC Queue Throughput (varying core configurations):\n";
        
        constexpr std::size_t QUEUE_SIZE = 65536;
        constexpr std::size_t NUM_ITEMS = 10'000'000;
        
        auto run_test = [](int producer_core, int consumer_core) {
            SPSCQueue<std::uint64_t, QUEUE_SIZE> queue;
            std::atomic<bool> done{false};
            std::uint64_t received = 0;
            
            auto start = std::chrono::steady_clock::now();
            
            std::thread producer([&]() {
                if (producer_core >= 0) set_cpu_affinity(producer_core);
                for (std::size_t i = 0; i < NUM_ITEMS; ++i) {
                    queue.push(i);
                }
                done.store(true, std::memory_order_release);
            });
            
            std::thread consumer([&]() {
                if (consumer_core >= 0) set_cpu_affinity(consumer_core);
                while (received < NUM_ITEMS) {
                    if (auto val = queue.try_pop()) {
                        ++received;
                    }
                }
            });
            
            producer.join();
            consumer.join();
            
            auto end = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration<double>(end - start).count();
            
            return NUM_ITEMS / duration;
        };
        
        // Same core (no pinning)
        double throughput_no_pin = run_test(-1, -1);
        std::cout << "  No pinning:        " << std::fixed << std::setprecision(2) 
                  << (throughput_no_pin / 1e6) << " M ops/sec\n";
        
        // Adjacent cores
        if (get_cpu_count() >= 2) {
            double throughput_adj = run_test(0, 1);
            std::cout << "  Adjacent cores:    " << (throughput_adj / 1e6) << " M ops/sec\n";
        }
        
        // Distant cores (if available)
        if (get_cpu_count() >= 4) {
            double throughput_dist = run_test(0, get_cpu_count() / 2);
            std::cout << "  Distant cores:     " << (throughput_dist / 1e6) << " M ops/sec\n";
        }
    }
    
    // Order processing throughput
    {
        std::cout << "\nOrder Processing Throughput:\n";
        
        constexpr std::size_t NUM_ORDERS = 1'000'000;
        std::vector<Order> orders;
        orders.reserve(NUM_ORDERS);
        
        // Generate orders
        for (std::size_t i = 0; i < NUM_ORDERS; ++i) {
            orders.emplace_back(
                i,
                i % 2 == 0 ? Side::BUY : Side::SELL,
                OrderType::LIMIT,
                to_fixed_price(100.0 + (i % 100)),
                100 + (i % 1000)
            );
        }
        
        // Measure various operations
        auto start = std::chrono::steady_clock::now();
        
        std::uint64_t checksum = 0;
        for (const auto& order : orders) {
            checksum += order.price;
            checksum += order.quantity;
            checksum += static_cast<int>(order.side);
        }
        
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration<double>(end - start).count();
        
        std::cout << "  Order iteration:   " << std::fixed << std::setprecision(2)
                  << (NUM_ORDERS / duration / 1e6) << " M orders/sec\n";
        std::cout << "  Checksum:          " << checksum << " (to prevent optimization)\n";
    }
    
    // Memory bandwidth
    {
        std::cout << "\nMemory Bandwidth:\n";
        
        constexpr std::size_t SIZE = 256 * 1024 * 1024;  // 256MB
        std::vector<std::uint64_t> buffer(SIZE / sizeof(std::uint64_t));
        
        // Write bandwidth
        auto start = std::chrono::steady_clock::now();
        for (auto& val : buffer) {
            val = 0xDEADBEEFDEADBEEF;
        }
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration<double>(end - start).count();
        double write_bw = SIZE / duration / (1024 * 1024 * 1024);
        
        // Read bandwidth
        start = std::chrono::steady_clock::now();
        std::uint64_t sum = 0;
        for (const auto& val : buffer) {
            sum += val;
        }
        end = std::chrono::steady_clock::now();
        duration = std::chrono::duration<double>(end - start).count();
        double read_bw = SIZE / duration / (1024 * 1024 * 1024);
        
        std::cout << "  Write: " << std::fixed << std::setprecision(2) << write_bw << " GB/sec\n";
        std::cout << "  Read:  " << read_bw << " GB/sec (sum=" << sum << ")\n";
    }
}

