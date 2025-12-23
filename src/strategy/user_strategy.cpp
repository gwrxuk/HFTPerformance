/**
 * @file user_strategy.cpp
 * @brief User-defined trading strategy implementation
 * 
 * ═══════════════════════════════════════════════════════════════════════════
 * CUSTOMIZE YOUR STRATEGY HERE
 * ═══════════════════════════════════════════════════════════════════════════
 * 
 * Instructions:
 * 1. Implement your trading logic in the onTick() callback
 * 2. Optionally handle order responses in onOrderResponse()
 * 3. Recompile the project
 * 4. Set "strategy": "user_strategy" in your config file
 * 
 * The framework measures latency from onTick() call to order submission.
 */

#include "user_strategy.hpp"
#include <iostream>
#include <memory>

namespace hft {

/**
 * @brief Your custom trading strategy
 * 
 * Modify this class to implement your trading logic.
 * The framework will measure the latency of your strategy.
 */
class CustomUserStrategy : public UserStrategy {
public:
    CustomUserStrategy() {
        // Initialize your strategy state here
    }
    
    void onInit() override {
        std::cout << "[CustomUserStrategy] Initialized\n";
        // Called once at startup - load parameters, connect to data sources, etc.
    }
    
    void onShutdown() override {
        std::cout << "[CustomUserStrategy] Shutdown - Orders sent: " << orders_sent_ 
                  << ", Fills received: " << fills_received_ << "\n";
        // Called at shutdown - cleanup resources
    }
    
    /**
     * @brief Main strategy callback - called on each market data tick
     * 
     * This is where your trading logic goes. The latency measurement
     * starts when this function is called and ends when submit_order()
     * returns.
     * 
     * @param tick Current market data
     */
    void onTick(const Tick& tick) override {
        // ═══════════════════════════════════════════════════════════════
        // YOUR TRADING LOGIC HERE
        // ═══════════════════════════════════════════════════════════════
        
        // Example: Simple mean reversion strategy
        // Buy when price drops below moving average, sell when above
        
        update_moving_average(tick.last_price);
        
        if (tick.sequence < warmup_ticks_) {
            return;  // Wait for moving average to stabilize
        }
        
        Price mid = (tick.bid_price + tick.ask_price) / 2;
        
        // Calculate signal
        double signal = static_cast<double>(mid - moving_avg_) / moving_avg_;
        
        if (signal < -threshold_ && position_ < max_position_) {
            // Price below average - BUY signal
            StrategyOrder order;
            order.symbol = tick.symbol;
            order.side = Side::BUY;
            order.type = OrderType::LIMIT;
            order.price = tick.ask_price;  // Cross the spread for execution
            order.quantity = order_size_;
            order.client_order_id = next_order_id_++;
            
            submit_order(order);
            ++orders_sent_;
            
        } else if (signal > threshold_ && position_ > -max_position_) {
            // Price above average - SELL signal
            StrategyOrder order;
            order.symbol = tick.symbol;
            order.side = Side::SELL;
            order.type = OrderType::LIMIT;
            order.price = tick.bid_price;  // Cross the spread for execution
            order.quantity = order_size_;
            order.client_order_id = next_order_id_++;
            
            submit_order(order);
            ++orders_sent_;
        }
        
        // ═══════════════════════════════════════════════════════════════
    }
    
    /**
     * @brief Called when order response/fill is received
     * 
     * Use this to update position tracking, PnL calculation, etc.
     */
    void onOrderResponse(const OrderResponse& response) override {
        if (response.status == OrderStatus::FILLED || 
            response.status == OrderStatus::PARTIALLY_FILLED) {
            ++fills_received_;
            
            // Update position (simplified - doesn't track actual side)
            // In production, you'd track this properly
        }
    }
    
    const char* name() const override { 
        return "CustomUserStrategy"; 
    }

private:
    void update_moving_average(Price price) {
        // Exponential moving average
        if (moving_avg_ == 0) {
            moving_avg_ = price;
        } else {
            moving_avg_ = static_cast<Price>(
                alpha_ * price + (1.0 - alpha_) * moving_avg_
            );
        }
    }

    // Strategy parameters - customize these
    static constexpr double alpha_ = 0.01;        // EMA smoothing factor
    static constexpr double threshold_ = 0.001;   // Signal threshold (0.1%)
    static constexpr int max_position_ = 100;     // Max position size
    static constexpr Quantity order_size_ = 10;   // Order quantity
    static constexpr uint64_t warmup_ticks_ = 100; // Warmup period
    
    // Strategy state
    Price moving_avg_ = 0;
    int position_ = 0;
    uint64_t next_order_id_ = 1;
    uint64_t orders_sent_ = 0;
    uint64_t fills_received_ = 0;
};

/**
 * @brief Factory function to create user strategy
 * 
 * This is called by the framework to instantiate your strategy.
 */
std::unique_ptr<UserStrategy> create_user_strategy() {
    return std::make_unique<CustomUserStrategy>();
}

} // namespace hft

