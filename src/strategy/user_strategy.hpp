/**
 * @file user_strategy.hpp
 * @brief User strategy interface for custom trading logic
 * 
 * Implement your trading strategy by overriding the callbacks in this interface.
 * The framework will call these methods during the performance test.
 * 
 * Timestamp Recording API:
 *   Use record_timestamp("label") at any point in your strategy code to measure
 *   time spent in different phases. The framework will aggregate these timestamps
 *   and report latency breakdowns.
 * 
 * Example:
 *   void onTick(const Tick& tick) override {
 *       record_timestamp("tick_received");
 *       
 *       // Your signal calculation
 *       double signal = calculate_signal(tick);
 *       record_timestamp("signal_calculated");
 *       
 *       // Risk checks
 *       if (check_risk(signal)) {
 *           record_timestamp("risk_checked");
 *           submit_order(...);
 *           record_timestamp("order_submitted");
 *       }
 *   }
 */

#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <iostream>
#include "core/types.hpp"
#include "core/timing.hpp"

namespace hft {

/**
 * @brief Timestamp record for strategy profiling
 */
struct TimestampRecord {
    const char* label;
    Timestamp timestamp;
    uint64_t tick_sequence;
};

/**
 * @brief Aggregated timing statistics for a labeled checkpoint
 */
struct TimingStats {
    std::string label;
    uint64_t count = 0;
    int64_t total_ns = 0;
    int64_t min_ns = INT64_MAX;
    int64_t max_ns = 0;
    std::vector<int64_t> samples;  // For percentile calculation
    
    void add_sample(int64_t ns) {
        count++;
        total_ns += ns;
        if (ns < min_ns) min_ns = ns;
        if (ns > max_ns) max_ns = ns;
        samples.push_back(ns);
    }
    
    [[nodiscard]] double average_ns() const {
        return count > 0 ? static_cast<double>(total_ns) / count : 0;
    }
    
    [[nodiscard]] int64_t percentile(double p) {
        if (samples.empty()) return 0;
        std::sort(samples.begin(), samples.end());
        size_t idx = static_cast<size_t>(samples.size() * p);
        return samples[std::min(idx, samples.size() - 1)];
    }
};

/**
 * @brief Market data tick structure
 */
struct Tick {
    Symbol symbol;
    Price bid_price;
    Price ask_price;
    Quantity bid_size;
    Quantity ask_size;
    Price last_price;
    Quantity last_size;
    Timestamp timestamp;
    uint64_t sequence;
};

/**
 * @brief Order request from strategy
 */
struct StrategyOrder {
    Symbol symbol;
    Side side;
    OrderType type;
    Price price;
    Quantity quantity;
    uint64_t client_order_id;
};

/**
 * @brief Order response/fill notification
 */
struct OrderResponse {
    uint64_t client_order_id;
    OrderId exchange_order_id;
    OrderStatus status;
    Price fill_price;
    Quantity fill_quantity;
    Quantity leaves_quantity;
    Timestamp timestamp;
};

/**
 * @brief Base class for user strategies
 * 
 * Override the virtual methods to implement your trading logic.
 * The framework measures latency from onTick() call to order submission.
 * 
 * Use record_timestamp("label") to measure time spent in different phases.
 */
class UserStrategy {
public:
    using OrderCallback = std::function<void(const StrategyOrder&)>;
    using TimestampCallback = std::function<void(const TimestampRecord&)>;
    
    UserStrategy() = default;
    virtual ~UserStrategy() = default;
    
    // Non-copyable
    UserStrategy(const UserStrategy&) = delete;
    UserStrategy& operator=(const UserStrategy&) = delete;
    
    /**
     * @brief Set the callback for submitting orders
     */
    void set_order_callback(OrderCallback callback) {
        order_callback_ = std::move(callback);
    }
    
    /**
     * @brief Set the callback for timestamp recording (used by framework)
     */
    void set_timestamp_callback(TimestampCallback callback) {
        timestamp_callback_ = std::move(callback);
    }
    
    /**
     * @brief Enable/disable timestamp recording
     */
    void set_timestamp_recording(bool enabled) {
        timestamp_recording_enabled_ = enabled;
    }
    
    /**
     * @brief Called when market data tick is received
     * 
     * Implement your strategy logic here. Call submit_order() to send orders.
     * Latency is measured from this call to order submission.
     * 
     * @param tick Market data tick
     */
    virtual void onTick(const Tick& tick) = 0;
    
    /**
     * @brief Called when order response is received
     * 
     * @param response Order fill/status update
     */
    virtual void onOrderResponse(const OrderResponse& response) {
        (void)response;  // Default: ignore
    }
    
    /**
     * @brief Called at strategy initialization
     */
    virtual void onInit() {}
    
    /**
     * @brief Called at strategy shutdown
     */
    virtual void onShutdown() {}
    
    /**
     * @brief Get strategy name
     */
    virtual const char* name() const { return "UserStrategy"; }
    
    /**
     * @brief Get timing statistics collected during run
     */
    [[nodiscard]] const std::unordered_map<std::string, TimingStats>& get_timing_stats() const {
        return timing_stats_;
    }
    
    /**
     * @brief Print timing breakdown report
     */
    void print_timing_report() const {
        if (timing_stats_.empty()) {
            return;
        }
        
        std::cout << "\n--- Strategy Timing Breakdown ---\n";
        for (const auto& [label, stats] : timing_stats_) {
            std::cout << "  " << label << ":\n";
            std::cout << "    Count:   " << stats.count << "\n";
            std::cout << "    Average: " << stats.average_ns() << " ns\n";
            std::cout << "    Min:     " << stats.min_ns << " ns\n";
            std::cout << "    Max:     " << stats.max_ns << " ns\n";
        }
    }

    /**
     * @brief Mark the start of tick processing (called by framework)
     */
    void begin_tick_processing(uint64_t tick_sequence) {
        current_tick_sequence_ = tick_sequence;
        tick_start_time_ = now();
        last_timestamp_ = tick_start_time_;
        last_label_ = "tick_start";
        
        if (timestamp_recording_enabled_) {
            record_timestamp_internal("tick_received");
        }
    }
    
    /**
     * @brief Mark the end of tick processing (called by framework)
     */
    void end_tick_processing() {
        if (timestamp_recording_enabled_) {
            record_timestamp_internal("tick_done");
        }
        
        Timestamp end_time = now();
        int64_t total_time = end_time - tick_start_time_;
        timing_stats_["total_tick_processing"].add_sample(total_time);
    }

protected:
    /**
     * @brief Record a timestamp at the current point in strategy execution
     * 
     * Call this at various points in your strategy to measure time spent
     * in different phases. The framework will aggregate these timestamps
     * and report latency breakdowns.
     * 
     * @param label A short descriptive label for this checkpoint
     * 
     * Example labels: "signal_start", "risk_check", "order_built", etc.
     */
    void record_timestamp(const char* label) {
        if (!timestamp_recording_enabled_) return;
        record_timestamp_internal(label);
    }
    
    /**
     * @brief Submit an order from the strategy
     */
    void submit_order(const StrategyOrder& order) {
        if (timestamp_recording_enabled_) {
            record_timestamp_internal("order_submitted");
        }
        
        if (order_callback_) {
            order_callback_(order);
        }
    }

private:
    void record_timestamp_internal(const char* label) {
        Timestamp ts = now();
        
        // Callback to framework for real-time processing
        if (timestamp_callback_) {
            TimestampRecord record{label, ts, current_tick_sequence_};
            timestamp_callback_(record);
        }
        
        // Calculate delta from last timestamp
        if (last_timestamp_ > 0 && last_label_ != nullptr) {
            int64_t delta = ts - last_timestamp_;
            std::string key = std::string(last_label_) + " → " + label;
            timing_stats_[key].add_sample(delta);
        }
        
        last_timestamp_ = ts;
        last_label_ = label;
    }

private:
    OrderCallback order_callback_;
    TimestampCallback timestamp_callback_;
    bool timestamp_recording_enabled_ = false;
    
    // Per-tick state
    uint64_t current_tick_sequence_ = 0;
    Timestamp tick_start_time_ = 0;
    Timestamp last_timestamp_ = 0;
    const char* last_label_ = nullptr;
    
    // Aggregated statistics
    mutable std::unordered_map<std::string, TimingStats> timing_stats_;
};

/**
 * @brief Pass-through strategy (baseline - just echoes ticks as orders)
 */
class PassThroughStrategy : public UserStrategy {
public:
    void onTick(const Tick& tick) override {
        // Simple strategy: alternate buy/sell orders at mid price
        StrategyOrder order;
        order.symbol = tick.symbol;
        order.side = (tick.sequence % 2 == 0) ? Side::BUY : Side::SELL;
        order.type = OrderType::LIMIT;
        order.price = (tick.bid_price + tick.ask_price) / 2;
        order.quantity = 10;
        order.client_order_id = tick.sequence;
        
        submit_order(order);
    }
    
    const char* name() const override { return "PassThrough"; }
};

/**
 * @brief Example momentum strategy with timestamp recording
 * 
 * Demonstrates how to use record_timestamp() to profile strategy code.
 */
class MomentumStrategy : public UserStrategy {
public:
    void onTick(const Tick& tick) override {
        // Record signal calculation start
        record_timestamp("signal_start");
        
        // Calculate momentum signal
        Price delta = 0;
        if (last_price_ > 0) {
            delta = tick.last_price - last_price_;
        }
        last_price_ = tick.last_price;
        
        record_timestamp("signal_calculated");
        
        // Skip if no signal
        if (delta == 0) return;
        
        // Risk check
        record_timestamp("risk_check_start");
        bool can_buy = (delta > 0 && position_ < max_position_);
        bool can_sell = (delta < 0 && position_ > -max_position_);
        record_timestamp("risk_check_done");
        
        // Build and submit order if signal is valid
        if (can_buy) {
            record_timestamp("order_build_start");
            StrategyOrder order;
            order.symbol = tick.symbol;
            order.side = Side::BUY;
            order.type = OrderType::LIMIT;
            order.price = tick.ask_price;
            order.quantity = 10;
            order.client_order_id = tick.sequence;
            record_timestamp("order_built");
            submit_order(order);  // This also records "order_submitted"
        } else if (can_sell) {
            record_timestamp("order_build_start");
            StrategyOrder order;
            order.symbol = tick.symbol;
            order.side = Side::SELL;
            order.type = OrderType::LIMIT;
            order.price = tick.bid_price;
            order.quantity = 10;
            order.client_order_id = tick.sequence;
            record_timestamp("order_built");
            submit_order(order);  // This also records "order_submitted"
        }
    }
    
    void onOrderResponse(const OrderResponse& response) override {
        if (response.status == OrderStatus::FILLED) {
            // Update position (simplified)
            position_ += (response.fill_quantity > 0) ? 1 : -1;
        }
    }
    
    const char* name() const override { return "Momentum"; }

private:
    Price last_price_ = 0;
    int position_ = 0;
    int max_position_ = 100;
};

/**
 * @brief Example market making strategy
 */
class MarketMakingStrategy : public UserStrategy {
public:
    void onTick(const Tick& tick) override {
        Price mid = (tick.bid_price + tick.ask_price) / 2;
        Price spread = tick.ask_price - tick.bid_price;
        
        // Quote both sides with tighter spread
        Price my_spread = spread / 2;
        if (my_spread < min_spread_) my_spread = min_spread_;
        
        // Buy order
        StrategyOrder buy_order;
        buy_order.symbol = tick.symbol;
        buy_order.side = Side::BUY;
        buy_order.type = OrderType::LIMIT;
        buy_order.price = mid - my_spread / 2;
        buy_order.quantity = quote_size_;
        buy_order.client_order_id = tick.sequence * 2;
        submit_order(buy_order);
        
        // Sell order
        StrategyOrder sell_order;
        sell_order.symbol = tick.symbol;
        sell_order.side = Side::SELL;
        sell_order.type = OrderType::LIMIT;
        sell_order.price = mid + my_spread / 2;
        sell_order.quantity = quote_size_;
        sell_order.client_order_id = tick.sequence * 2 + 1;
        submit_order(sell_order);
    }
    
    const char* name() const override { return "MarketMaking"; }

private:
    Price min_spread_ = 100;  // Minimum spread in price units
    Quantity quote_size_ = 10;
};

/**
 * @brief Factory function to create strategies by name
 */
inline std::unique_ptr<UserStrategy> create_strategy(const std::string& name) {
    if (name == "pass_through" || name == "PassThrough") {
        return std::make_unique<PassThroughStrategy>();
    } else if (name == "momentum" || name == "Momentum") {
        return std::make_unique<MomentumStrategy>();
    } else if (name == "market_making" || name == "MarketMaking") {
        return std::make_unique<MarketMakingStrategy>();
    }
    // Default to pass-through
    return std::make_unique<PassThroughStrategy>();
}

} // namespace hft

