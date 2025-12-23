/**
 * @file benchmark_main.cpp
 * @brief Performance benchmark suite for HFT system components
 * 
 * Measures latency and throughput of critical system components:
 * - Lock-free queue operations
 * - Memory pool allocation
 * - Order book operations
 * - Matching engine throughput
 */

#include <iostream>
#include <iomanip>
#include <vector>
#include <thread>
#include <chrono>
#include <random>

#include "core/types.hpp"
#include "core/lockfree_queue.hpp"
#include "core/memory_pool.hpp"
#include "core/timing.hpp"
#include "core/cpu_affinity.hpp"
#include "matching/order.hpp"
#include "matching/order_book.hpp"
#include "matching/matching_engine.hpp"

using namespace hft;

// Forward declarations from other benchmark files
void run_latency_benchmarks();
void run_throughput_benchmarks();
void run_orderbook_benchmarks();

/**
 * @brief Print system information
 */
void print_system_info() {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║           HFT Trading System - Performance Benchmark          ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    
    std::cout << "System Information:\n";
    std::cout << "  CPU Cores:      " << get_cpu_count() << "\n";
    std::cout << "  Cache Line:     " << CACHE_LINE_SIZE << " bytes\n";
    std::cout << "  Order Size:     " << sizeof(Order) << " bytes\n";
    std::cout << "  Quote Size:     " << sizeof(Quote) << " bytes\n";
    
    // TSC calibration
    std::cout << "  TSC Frequency:  ";
    auto freq = TSCCalibrator::calibrate(std::chrono::milliseconds(50));
    std::cout << std::fixed << std::setprecision(2) << (freq / 1e9) << " GHz\n";
    
    std::cout << "\n";
}

/**
 * @brief Benchmark lock-free queue
 */
void benchmark_spsc_queue() {
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    std::cout << "SPSC Queue Benchmark\n";
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    
    constexpr std::size_t QUEUE_SIZE = 65536;
    constexpr std::size_t NUM_ITEMS = 10'000'000;
    
    SPSCQueue<std::uint64_t, QUEUE_SIZE> queue;
    LatencyStats push_stats(NUM_ITEMS);
    LatencyStats pop_stats(NUM_ITEMS);
    
    // Producer thread
    std::thread producer([&]() {
        set_cpu_affinity(0);
        
        for (std::size_t i = 0; i < NUM_ITEMS; ++i) {
            auto start = now();
            queue.push(i);
            auto elapsed = now() - start;
            push_stats.add_sample(elapsed);
        }
    });
    
    // Consumer thread
    std::thread consumer([&]() {
        set_cpu_affinity(1);
        
        for (std::size_t i = 0; i < NUM_ITEMS; ++i) {
            auto start = now();
            auto val = queue.pop();
            auto elapsed = now() - start;
            pop_stats.add_sample(elapsed);
            (void)val;
        }
    });
    
    producer.join();
    consumer.join();
    
    std::cout << "\nPush Latency:\n";
    push_stats.print_summary("  ");
    
    std::cout << "\nPop Latency:\n";
    pop_stats.print_summary("  ");
    
    double throughput = static_cast<double>(NUM_ITEMS) / 
                        (push_stats.mean() * NUM_ITEMS / 1e9);
    std::cout << "\nThroughput: " << std::fixed << std::setprecision(2) 
              << (throughput / 1e6) << " million ops/sec\n";
}

/**
 * @brief Benchmark memory pool
 */
void benchmark_memory_pool() {
    std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    std::cout << "Memory Pool Benchmark\n";
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    
    constexpr std::size_t POOL_SIZE = 100'000;
    constexpr std::size_t NUM_ITERATIONS = 1'000'000;
    
    MemoryPool<Order, POOL_SIZE> pool;
    LatencyStats alloc_stats(NUM_ITERATIONS);
    LatencyStats dealloc_stats(NUM_ITERATIONS);
    
    std::vector<Order*> orders;
    orders.reserve(POOL_SIZE);
    
    // Allocation benchmark
    for (std::size_t i = 0; i < POOL_SIZE; ++i) {
        auto start = now();
        auto* order = pool.create(i, Side::BUY, OrderType::LIMIT, 
                                   to_fixed_price(100.0), 100);
        auto elapsed = now() - start;
        alloc_stats.add_sample(elapsed);
        orders.push_back(order);
    }
    
    // Deallocation benchmark
    for (auto* order : orders) {
        auto start = now();
        pool.destroy(order);
        auto elapsed = now() - start;
        dealloc_stats.add_sample(elapsed);
    }
    
    std::cout << "\nAllocation Latency:\n";
    alloc_stats.print_summary("  ");
    
    std::cout << "\nDeallocation Latency:\n";
    dealloc_stats.print_summary("  ");
    
    // Compare with standard new/delete
    LatencyStats new_stats(NUM_ITERATIONS);
    LatencyStats delete_stats(NUM_ITERATIONS);
    orders.clear();
    
    for (std::size_t i = 0; i < 10000; ++i) {
        auto start = now();
        auto* order = new Order(i, Side::BUY, OrderType::LIMIT,
                                to_fixed_price(100.0), 100);
        auto elapsed = now() - start;
        new_stats.add_sample(elapsed);
        orders.push_back(order);
    }
    
    for (auto* order : orders) {
        auto start = now();
        delete order;
        auto elapsed = now() - start;
        delete_stats.add_sample(elapsed);
    }
    
    std::cout << "\nnew/delete comparison (10K samples):\n";
    std::cout << "  new:    mean=" << std::fixed << std::setprecision(2) 
              << new_stats.mean() << "ns\n";
    std::cout << "  delete: mean=" << std::fixed << std::setprecision(2) 
              << delete_stats.mean() << "ns\n";
    std::cout << "  Speedup: " << std::setprecision(1) 
              << (new_stats.mean() / alloc_stats.mean()) << "x (alloc), "
              << (delete_stats.mean() / dealloc_stats.mean()) << "x (dealloc)\n";
}

/**
 * @brief Benchmark order book operations
 */
void benchmark_order_book() {
    std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    std::cout << "Order Book Benchmark\n";
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    
    constexpr std::size_t NUM_ORDERS = 100'000;
    
    auto symbol = make_symbol("BTC-USD");
    OrderBook book(symbol);
    OrderIdGenerator id_gen;
    
    LatencyStats add_stats(NUM_ORDERS);
    LatencyStats match_stats(NUM_ORDERS);
    LatencyStats cancel_stats(NUM_ORDERS);
    
    std::vector<OrderId> order_ids;
    order_ids.reserve(NUM_ORDERS);
    
    // Add orders (alternating sides, no matching)
    Price base_price = to_fixed_price(50000.0);
    for (std::size_t i = 0; i < NUM_ORDERS; ++i) {
        Side side = (i % 2 == 0) ? Side::BUY : Side::SELL;
        Price price = base_price + (side == Side::BUY ? 
                                    -static_cast<Price>(i) : 
                                    static_cast<Price>(i));
        
        Order order(id_gen.next(), side, OrderType::LIMIT, price, 100);
        
        auto start = now();
        book.add_order(order);
        auto elapsed = now() - start;
        add_stats.add_sample(elapsed);
        order_ids.push_back(order.order_id);
    }
    
    std::cout << "\nAdd Order Latency (no matching):\n";
    add_stats.print_summary("  ");
    
    auto stats = book.get_stats();
    std::cout << "\nBook Statistics:\n";
    std::cout << "  Bid Levels: " << stats.bid_levels << "\n";
    std::cout << "  Ask Levels: " << stats.ask_levels << "\n";
    std::cout << "  Total Orders: " << stats.total_orders << "\n";
    
    // Cancel orders
    for (auto order_id : order_ids) {
        auto start = now();
        book.cancel_order(order_id);
        auto elapsed = now() - start;
        cancel_stats.add_sample(elapsed);
    }
    
    std::cout << "\nCancel Order Latency:\n";
    cancel_stats.print_summary("  ");
    
    // Matching benchmark
    book.clear();
    std::cout << "\nMatching Benchmark (crossing orders):\n";
    
    for (std::size_t i = 0; i < NUM_ORDERS / 2; ++i) {
        // Add resting order
        Order resting(id_gen.next(), Side::BUY, OrderType::LIMIT, 
                     base_price, 100);
        book.add_order(resting);
        
        // Add crossing order
        Order aggressor(id_gen.next(), Side::SELL, OrderType::LIMIT,
                       base_price - 1, 100);
        
        auto start = now();
        book.add_order(aggressor);
        auto elapsed = now() - start;
        match_stats.add_sample(elapsed);
    }
    
    match_stats.print_summary("  ");
    
    stats = book.get_stats();
    std::cout << "  Trades Matched: " << stats.trades_matched << "\n";
    std::cout << "  Volume Matched: " << stats.volume_matched << "\n";
}

/**
 * @brief Benchmark matching engine
 */
void benchmark_matching_engine() {
    std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    std::cout << "Matching Engine Benchmark\n";
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    
    constexpr std::size_t NUM_ORDERS = 100'000;
    
    MatchingEngine engine;
    
    // Add instruments
    std::vector<Symbol> symbols = {
        make_symbol("BTC-USD"),
        make_symbol("ETH-USD"),
        make_symbol("SOL-USD")
    };
    
    for (const auto& sym : symbols) {
        engine.add_instrument(sym);
    }
    
    // Pre-populate with resting orders
    for (const auto& sym : symbols) {
        Price base = to_fixed_price(1000.0);
        for (int i = 0; i < 1000; ++i) {
            engine.submit_order(sym, Side::BUY, OrderType::LIMIT, 
                              base - i * 100, 100);
            engine.submit_order(sym, Side::SELL, OrderType::LIMIT,
                              base + i * 100, 100);
        }
    }
    
    engine.reset_stats();
    
    // Benchmark crossing orders
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> sym_dist(0, symbols.size() - 1);
    std::uniform_int_distribution<> side_dist(0, 1);
    
    auto start_time = std::chrono::steady_clock::now();
    
    for (std::size_t i = 0; i < NUM_ORDERS; ++i) {
        const auto& sym = symbols[sym_dist(gen)];
        Side side = side_dist(gen) == 0 ? Side::BUY : Side::SELL;
        
        // Market-crossing price
        Price price = to_fixed_price(side == Side::BUY ? 2000.0 : 1.0);
        
        engine.submit_order(sym, side, OrderType::LIMIT, price, 10);
    }
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration<double>(end_time - start_time).count();
    
    const auto& stats = engine.stats();
    const auto& latency = engine.latency_stats();
    
    std::cout << "\nEngine Statistics:\n";
    std::cout << "  Orders Processed: " << stats.orders_received << "\n";
    std::cout << "  Time:             " << std::fixed << std::setprecision(3) 
              << duration << " sec\n";
    std::cout << "  Throughput:       " << std::setprecision(0)
              << (stats.orders_received / duration) << " orders/sec\n";
    
    std::cout << "\nLatency Statistics:\n";
    auto percentiles = latency.get_percentiles();
    std::cout << "  Min:   " << std::setprecision(2) << latency.min() << " ns\n";
    std::cout << "  P50:   " << percentiles.p50 << " ns\n";
    std::cout << "  P99:   " << percentiles.p99 << " ns\n";
    std::cout << "  P99.9: " << percentiles.p999 << " ns\n";
    std::cout << "  Max:   " << latency.max() << " ns\n";
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    print_system_info();
    
    try {
        benchmark_spsc_queue();
        benchmark_memory_pool();
        benchmark_order_book();
        benchmark_matching_engine();
        
        // Run additional benchmarks from other files
        run_latency_benchmarks();
        run_throughput_benchmarks();
        run_orderbook_benchmarks();
        
    } catch (const std::exception& e) {
        std::cerr << "Benchmark failed: " << e.what() << "\n";
        return 1;
    }
    
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                    Benchmark Complete                         ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n";
    
    return 0;
}

