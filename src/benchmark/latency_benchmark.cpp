/**
 * @file latency_benchmark.cpp
 * @brief Detailed latency benchmarks
 */

#include <iostream>
#include <iomanip>
#include <random>
#include <vector>
#include "core/types.hpp"
#include "core/timing.hpp"
#include "core/cpu_affinity.hpp"

using namespace hft;

void run_latency_benchmarks() {
    std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    std::cout << "Low-Level Latency Benchmarks\n";
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    
    constexpr int ITERATIONS = 1'000'000;
    
    // RDTSC overhead
    {
        LatencyStats stats(ITERATIONS);
        for (int i = 0; i < ITERATIONS; ++i) {
            auto start = rdtsc();
            auto end = rdtscp();
            stats.add_sample(static_cast<std::int64_t>(end - start));
        }
        std::cout << "\nRDTSC overhead (cycles):\n";
        std::cout << "  Min:  " << stats.min() << "\n";
        std::cout << "  Mean: " << std::fixed << std::setprecision(2) << stats.mean() << "\n";
        std::cout << "  Max:  " << stats.max() << "\n";
    }
    
    // now() overhead
    {
        LatencyStats stats(ITERATIONS);
        for (int i = 0; i < ITERATIONS; ++i) {
            auto start = now();
            auto end = now();
            stats.add_sample(end - start);
        }
        std::cout << "\nnow() overhead (ns):\n";
        std::cout << "  Min:  " << stats.min() << "\n";
        std::cout << "  Mean: " << std::fixed << std::setprecision(2) << stats.mean() << "\n";
        std::cout << "  Max:  " << stats.max() << "\n";
    }
    
    // Memory access latency
    {
        constexpr std::size_t SIZE = 64 * 1024 * 1024;  // 64MB
        std::vector<char> buffer(SIZE);
        
        // Sequential access
        LatencyStats seq_stats(1000);
        for (int i = 0; i < 1000; ++i) {
            auto start = now();
            volatile char c = buffer[i * 64];  // Cache line stride
            auto end = now();
            (void)c;
            seq_stats.add_sample(end - start);
        }
        
        // Random access
        LatencyStats rand_stats(1000);
        std::vector<std::size_t> indices(1000);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<std::size_t> dist(0, SIZE - 1);
        for (auto& idx : indices) {
            idx = dist(gen);
        }
        
        for (const auto idx : indices) {
            auto start = now();
            volatile char c = buffer[idx];
            auto end = now();
            (void)c;
            rand_stats.add_sample(end - start);
        }
        
        std::cout << "\nMemory access latency (ns):\n";
        std::cout << "  Sequential: mean=" << std::fixed << std::setprecision(2) 
                  << seq_stats.mean() << "\n";
        std::cout << "  Random:     mean=" << std::fixed << std::setprecision(2)
                  << rand_stats.mean() << "\n";
    }
    
    // Function call overhead
    {
        auto dummy_func = [](int x) __attribute__((noinline)) {
            return x + 1;
        };
        
        LatencyStats stats(ITERATIONS);
        for (int i = 0; i < ITERATIONS; ++i) {
            auto start = now();
            volatile int result = dummy_func(i);
            auto end = now();
            (void)result;
            stats.add_sample(end - start);
        }
        
        std::cout << "\nFunction call overhead (ns):\n";
        std::cout << "  Mean: " << std::fixed << std::setprecision(2) << stats.mean() << "\n";
    }
    
    // Atomic operation overhead
    {
        std::atomic<int> counter{0};
        
        LatencyStats load_stats(ITERATIONS);
        for (int i = 0; i < ITERATIONS; ++i) {
            auto start = now();
            volatile int val = counter.load(std::memory_order_acquire);
            auto end = now();
            (void)val;
            load_stats.add_sample(end - start);
        }
        
        LatencyStats store_stats(ITERATIONS);
        for (int i = 0; i < ITERATIONS; ++i) {
            auto start = now();
            counter.store(i, std::memory_order_release);
            auto end = now();
            store_stats.add_sample(end - start);
        }
        
        LatencyStats cas_stats(ITERATIONS);
        for (int i = 0; i < ITERATIONS; ++i) {
            int expected = i;
            auto start = now();
            counter.compare_exchange_strong(expected, i + 1, std::memory_order_acq_rel);
            auto end = now();
            cas_stats.add_sample(end - start);
        }
        
        std::cout << "\nAtomic operation overhead (ns):\n";
        std::cout << "  Load:  mean=" << std::fixed << std::setprecision(2) 
                  << load_stats.mean() << "\n";
        std::cout << "  Store: mean=" << std::fixed << std::setprecision(2)
                  << store_stats.mean() << "\n";
        std::cout << "  CAS:   mean=" << std::fixed << std::setprecision(2)
                  << cas_stats.mean() << "\n";
    }
}

