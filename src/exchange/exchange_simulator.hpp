/**
 * @file exchange_simulator.hpp
 * @brief Exchange Simulator for HFT Performance Testing
 * 
 * Simulates an exchange that receives orders and measures tick-to-trade latency.
 * 
 * Key timestamps:
 *   t_gen        - When the tick was generated (from Market Data Generator)
 *   t_order_recv - When the order is received by exchange (this component)
 *   tick-to-trade = t_order_recv - t_gen  (primary metric)
 * 
 * Supports both embedded mode (queue-based) and external mode (socket-based).
 */

#pragma once

#include <atomic>
#include <thread>
#include <vector>
#include <functional>
#include <mutex>
#include <iostream>
#include <algorithm>
#include <numeric>
#include <cmath>

#include "core/types.hpp"
#include "core/timing.hpp"
#include "core/lockfree_queue.hpp"
#include "core/cpu_affinity.hpp"

namespace hft {

/**
 * @brief Order message with generation timestamp for latency tracking
 */
struct ExchangeOrder {
    uint64_t order_id;
    uint64_t tick_sequence;     // Which tick triggered this order
    Timestamp t_gen;            // Tick generation time (from generator)
    Timestamp t_strategy_done;  // When strategy finished processing
    Symbol symbol;
    Side side;
    OrderType type;
    Price price;
    Quantity quantity;
};

/**
 * @brief Order acknowledgment from exchange
 */
struct OrderAck {
    uint64_t order_id;
    Timestamp t_order_recv;     // When exchange received the order
    Timestamp t_ack_sent;       // When acknowledgment was sent
    bool accepted;
    OrderId exchange_order_id;
};

/**
 * @brief Latency statistics for tick-to-trade measurement
 */
struct TickToTradeStats {
    std::vector<int64_t> tick_to_order_latencies;   // t_order_recv - t_gen
    std::vector<int64_t> strategy_latencies;        // t_strategy_done - t_gen
    std::vector<int64_t> order_transit_latencies;   // t_order_recv - t_strategy_done
    
    std::atomic<uint64_t> orders_received{0};
    std::atomic<uint64_t> orders_accepted{0};
    std::atomic<uint64_t> orders_rejected{0};
    
    int64_t min_tick_to_order = INT64_MAX;
    int64_t max_tick_to_order = 0;
    
    void record(const ExchangeOrder& order, Timestamp t_order_recv) {
        int64_t tick_to_order = t_order_recv - order.t_gen;
        int64_t strategy_time = order.t_strategy_done - order.t_gen;
        int64_t order_transit = t_order_recv - order.t_strategy_done;
        
        tick_to_order_latencies.push_back(tick_to_order);
        strategy_latencies.push_back(strategy_time);
        order_transit_latencies.push_back(order_transit);
        
        if (tick_to_order < min_tick_to_order) min_tick_to_order = tick_to_order;
        if (tick_to_order > max_tick_to_order) max_tick_to_order = tick_to_order;
        
        orders_received.fetch_add(1, std::memory_order_relaxed);
    }
    
    void print_report() const {
        if (tick_to_order_latencies.empty()) {
            std::cout << "No orders received.\n";
            return;
        }
        
        // Calculate statistics
        auto sorted_tto = tick_to_order_latencies;
        auto sorted_strat = strategy_latencies;
        auto sorted_transit = order_transit_latencies;
        
        std::sort(sorted_tto.begin(), sorted_tto.end());
        std::sort(sorted_strat.begin(), sorted_strat.end());
        std::sort(sorted_transit.begin(), sorted_transit.end());
        
        auto percentile = [](const std::vector<int64_t>& v, double p) {
            size_t idx = static_cast<size_t>(v.size() * p);
            return v[std::min(idx, v.size() - 1)];
        };
        
        auto average = [](const std::vector<int64_t>& v) {
            if (v.empty()) return 0.0;
            return std::accumulate(v.begin(), v.end(), 0.0) / v.size();
        };
        
        std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
        std::cout << "║              EXCHANGE SIMULATOR STATISTICS                    ║\n";
        std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";
        
        std::cout << "--- Orders ---\n";
        std::cout << "  Received:  " << orders_received.load() << "\n";
        std::cout << "  Accepted:  " << orders_accepted.load() << "\n";
        std::cout << "  Rejected:  " << orders_rejected.load() << "\n\n";
        
        std::cout << "--- Tick-to-Trade Latency (t_order_recv - t_gen) ---\n";
        std::cout << "  This is the PRIMARY METRIC: time from tick generation to order receipt\n";
        std::cout << "  Min:     " << sorted_tto.front() << " ns (" << sorted_tto.front() / 1000.0 << " µs)\n";
        std::cout << "  Max:     " << sorted_tto.back() << " ns (" << sorted_tto.back() / 1000.0 << " µs)\n";
        std::cout << "  Average: " << average(sorted_tto) << " ns\n";
        std::cout << "  Median:  " << percentile(sorted_tto, 0.50) << " ns\n";
        std::cout << "  P90:     " << percentile(sorted_tto, 0.90) << " ns\n";
        std::cout << "  P99:     " << percentile(sorted_tto, 0.99) << " ns\n";
        std::cout << "  P99.9:   " << percentile(sorted_tto, 0.999) << " ns\n\n";
        
        std::cout << "--- Latency Breakdown ---\n";
        std::cout << "  Strategy time (t_strategy_done - t_gen):\n";
        std::cout << "    Median: " << percentile(sorted_strat, 0.50) << " ns\n";
        std::cout << "    P99:    " << percentile(sorted_strat, 0.99) << " ns\n";
        std::cout << "  Order transit (t_order_recv - t_strategy_done):\n";
        std::cout << "    Median: " << percentile(sorted_transit, 0.50) << " ns\n";
        std::cout << "    P99:    " << percentile(sorted_transit, 0.99) << " ns\n";
    }
};

/**
 * @brief Exchange Simulator - receives orders and measures tick-to-trade latency
 * 
 * Can operate in two modes:
 * 1. Embedded mode: Receives orders via lock-free queue
 * 2. External mode: Receives orders via socket (IPC or network)
 */
class ExchangeSimulator {
public:
    using AckCallback = std::function<void(const OrderAck&)>;
    
    ExchangeSimulator() = default;
    ~ExchangeSimulator() { stop(); }
    
    // Non-copyable
    ExchangeSimulator(const ExchangeSimulator&) = delete;
    ExchangeSimulator& operator=(const ExchangeSimulator&) = delete;
    
    /**
     * @brief Set callback for order acknowledgments
     */
    void set_ack_callback(AckCallback callback) {
        ack_callback_ = std::move(callback);
    }
    
    /**
     * @brief Start the exchange simulator thread
     * @param cpu_core CPU core to pin to (-1 for no pinning)
     * @param use_polling Whether to use busy-wait polling
     */
    void start(int cpu_core = -1, bool use_polling = true) {
        running_ = true;
        use_polling_ = use_polling;
        
        exchange_thread_ = std::thread([this, cpu_core]() {
            #ifdef __linux__
            if (cpu_core >= 0) {
                set_cpu_affinity(cpu_core);
            }
            #endif
            
            run_loop();
        });
    }
    
    /**
     * @brief Stop the exchange simulator
     */
    void stop() {
        running_ = false;
        if (exchange_thread_.joinable()) {
            exchange_thread_.join();
        }
    }
    
    /**
     * @brief Submit an order to the exchange (embedded mode)
     * 
     * This is called by the strategy after processing a tick.
     * The caller should set t_gen and t_strategy_done before calling.
     */
    bool submit_order(const ExchangeOrder& order) {
        return order_queue_.try_push(order);
    }
    
    /**
     * @brief Get the order queue for direct access (advanced use)
     */
    SPSCQueue<ExchangeOrder, 65536>& order_queue() {
        return order_queue_;
    }
    
    /**
     * @brief Get statistics
     */
    const TickToTradeStats& stats() const {
        return stats_;
    }
    
    /**
     * @brief Print statistics report
     */
    void print_stats() const {
        stats_.print_report();
    }
    
    /**
     * @brief Process an order immediately (for single-threaded testing)
     * 
     * Returns the tick-to-trade latency.
     */
    int64_t process_order_sync(const ExchangeOrder& order) {
        Timestamp t_order_recv = now();
        return process_order_internal(order, t_order_recv);
    }

private:
    void run_loop() {
        while (running_.load(std::memory_order_relaxed) || !order_queue_.empty()) {
            auto order = order_queue_.try_pop();
            if (order) {
                // CRITICAL: Record t_order_recv immediately upon dequeue
                Timestamp t_order_recv = now();
                process_order_internal(*order, t_order_recv);
            } else if (use_polling_) {
                // Busy-wait polling for lowest latency
                #if defined(__x86_64__) || defined(_M_X64)
                    __builtin_ia32_pause();
                #elif defined(__aarch64__)
                    asm volatile("yield" ::: "memory");
                #endif
            } else {
                std::this_thread::yield();
            }
        }
    }
    
    int64_t process_order_internal(const ExchangeOrder& order, Timestamp t_order_recv) {
        // Record tick-to-trade latency
        int64_t tick_to_trade = t_order_recv - order.t_gen;
        
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.record(order, t_order_recv);
        }
        
        // Simulate order acceptance (could add rejection logic here)
        bool accepted = true;  // Simplified: always accept
        stats_.orders_accepted.fetch_add(1, std::memory_order_relaxed);
        
        // Send acknowledgment if callback is set
        if (ack_callback_) {
            OrderAck ack;
            ack.order_id = order.order_id;
            ack.t_order_recv = t_order_recv;
            ack.t_ack_sent = now();
            ack.accepted = accepted;
            ack.exchange_order_id = next_exchange_order_id_++;
            
            ack_callback_(ack);
        }
        
        return tick_to_trade;
    }

    SPSCQueue<ExchangeOrder, 65536> order_queue_;
    std::atomic<bool> running_{false};
    bool use_polling_ = true;
    std::thread exchange_thread_;
    
    AckCallback ack_callback_;
    
    mutable std::mutex stats_mutex_;
    TickToTradeStats stats_;
    
    std::atomic<uint64_t> next_exchange_order_id_{1};
};

/**
 * @brief Helper to create ExchangeOrder from strategy output
 */
inline ExchangeOrder make_exchange_order(
    uint64_t order_id,
    uint64_t tick_sequence,
    Timestamp t_gen,
    Timestamp t_strategy_done,
    const Symbol& symbol,
    Side side,
    OrderType type,
    Price price,
    Quantity quantity
) {
    ExchangeOrder order;
    order.order_id = order_id;
    order.tick_sequence = tick_sequence;
    order.t_gen = t_gen;
    order.t_strategy_done = t_strategy_done;
    order.symbol = symbol;
    order.side = side;
    order.type = type;
    order.price = price;
    order.quantity = quantity;
    return order;
}

} // namespace hft

