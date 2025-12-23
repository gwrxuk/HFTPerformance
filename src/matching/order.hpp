/**
 * @file order.hpp
 * @brief Order structures for the matching engine
 * 
 * Orders are designed to be cache-efficient with minimal memory footprint.
 * The layout is optimized to fit within a single cache line where possible.
 */

#pragma once

#include <cstdint>
#include <atomic>
#include <functional>
#include "core/types.hpp"

namespace hft {

/**
 * @brief Order structure optimized for cache efficiency
 * 
 * Size: 64 bytes (one cache line)
 * - Frequently accessed fields grouped together
 * - Aligned for optimal cache performance
 */
struct alignas(CACHE_LINE_SIZE) Order {
    // Primary key
    OrderId order_id;          // 8 bytes
    
    // Matching fields (hot path)
    Price price;               // 8 bytes
    Quantity quantity;         // 8 bytes - remaining quantity
    Quantity filled_quantity;  // 8 bytes
    
    // Order attributes
    Side side;                 // 1 byte
    OrderType type;            // 1 byte
    OrderStatus status;        // 1 byte
    std::uint8_t flags;        // 1 byte (reserved)
    
    // Timing
    Timestamp entry_time;      // 8 bytes - when order entered the book
    Timestamp update_time;     // 8 bytes - last modification time
    
    // Client info (less frequently accessed)
    std::uint64_t client_id;   // 8 bytes
    std::uint32_t sequence_num;// 4 bytes
    
    // Padding to cache line
    std::uint8_t padding[4];
    
    // ========================================================================
    // Constructors
    // ========================================================================
    
    Order() noexcept = default;
    
    Order(OrderId id, Side s, OrderType t, Price p, Quantity q, 
          std::uint64_t client = 0) noexcept
        : order_id(id)
        , price(p)
        , quantity(q)
        , filled_quantity(0)
        , side(s)
        , type(t)
        , status(OrderStatus::NEW)
        , flags(0)
        , entry_time(now())
        , update_time(entry_time)
        , client_id(client)
        , sequence_num(0)
        , padding{}
    {}

    // ========================================================================
    // Accessors
    // ========================================================================
    
    [[nodiscard]] Quantity remaining_quantity() const noexcept {
        return quantity - filled_quantity;
    }
    
    [[nodiscard]] bool is_filled() const noexcept {
        return filled_quantity >= quantity;
    }
    
    [[nodiscard]] bool is_active() const noexcept {
        return status == OrderStatus::NEW || 
               status == OrderStatus::PARTIALLY_FILLED;
    }
    
    [[nodiscard]] bool is_buy() const noexcept {
        return side == Side::BUY;
    }
    
    [[nodiscard]] bool is_sell() const noexcept {
        return side == Side::SELL;
    }
    
    // ========================================================================
    // Mutators
    // ========================================================================
    
    void fill(Quantity qty) noexcept {
        filled_quantity += qty;
        update_time = now();
        
        if (filled_quantity >= quantity) {
            status = OrderStatus::FILLED;
        } else {
            status = OrderStatus::PARTIALLY_FILLED;
        }
    }
    
    void cancel() noexcept {
        status = OrderStatus::CANCELLED;
        update_time = now();
    }
    
    void reject() noexcept {
        status = OrderStatus::REJECTED;
        update_time = now();
    }
};

// Note: Order size may vary by platform (64-128 bytes)
// On ARM64, alignas(64) may result in 128 byte alignment
static_assert(sizeof(Order) <= 2 * CACHE_LINE_SIZE, 
              "Order should fit in at most two cache lines");

/**
 * @brief Order comparison for price-time priority
 * 
 * Buy orders: higher price first, then earlier time
 * Sell orders: lower price first, then earlier time
 */
struct OrderPriorityCompare {
    bool operator()(const Order* lhs, const Order* rhs) const noexcept {
        if (lhs->price != rhs->price) {
            // For buy orders, higher price has priority
            // For sell orders, lower price has priority
            if (lhs->side == Side::BUY) {
                return lhs->price < rhs->price;  // Max heap semantics
            } else {
                return lhs->price > rhs->price;  // Min heap semantics
            }
        }
        // Same price: earlier order has priority
        return lhs->entry_time > rhs->entry_time;  // Earlier time first
    }
};

/**
 * @brief Intrusive list node for order book
 * 
 * Used for O(1) order cancellation within a price level.
 */
struct OrderNode {
    Order order;
    OrderNode* prev = nullptr;
    OrderNode* next = nullptr;
    
    OrderNode() = default;
    explicit OrderNode(const Order& o) : order(o) {}
};

/**
 * @brief Order ID generator (thread-safe)
 */
class OrderIdGenerator {
public:
    OrderIdGenerator(std::uint64_t start = 1) : next_id_(start) {}
    
    [[nodiscard]] OrderId next() noexcept {
        return next_id_.fetch_add(1, std::memory_order_relaxed);
    }
    
    [[nodiscard]] OrderId current() const noexcept {
        return next_id_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<OrderId> next_id_;
};

/**
 * @brief Execution report for trade callbacks
 */
struct alignas(CACHE_LINE_SIZE) ExecutionReport {
    OrderId order_id;
    OrderId contra_order_id;  // Counterparty order
    Price execution_price;
    Quantity execution_quantity;
    Side side;
    ExecutionType exec_type;
    OrderStatus order_status;
    Timestamp timestamp;
    std::uint64_t client_id;
    Quantity leaves_quantity;   // Remaining quantity
    Quantity cumulative_quantity;
    
    ExecutionReport() = default;
    
    static ExecutionReport make_trade(const Order& order, const Order& contra,
                                      Price price, Quantity qty) {
        ExecutionReport report;
        report.order_id = order.order_id;
        report.contra_order_id = contra.order_id;
        report.execution_price = price;
        report.execution_quantity = qty;
        report.side = order.side;
        report.exec_type = ExecutionType::TRADE;
        report.order_status = order.status;
        report.timestamp = now();
        report.client_id = order.client_id;
        report.leaves_quantity = order.remaining_quantity() - qty;
        report.cumulative_quantity = order.filled_quantity + qty;
        return report;
    }
    
    static ExecutionReport make_new(const Order& order) {
        ExecutionReport report;
        report.order_id = order.order_id;
        report.contra_order_id = 0;
        report.execution_price = order.price;
        report.execution_quantity = 0;
        report.side = order.side;
        report.exec_type = ExecutionType::NEW;
        report.order_status = OrderStatus::NEW;
        report.timestamp = now();
        report.client_id = order.client_id;
        report.leaves_quantity = order.quantity;
        report.cumulative_quantity = 0;
        return report;
    }
    
    static ExecutionReport make_cancel(const Order& order) {
        ExecutionReport report;
        report.order_id = order.order_id;
        report.contra_order_id = 0;
        report.execution_price = order.price;
        report.execution_quantity = 0;
        report.side = order.side;
        report.exec_type = ExecutionType::CANCELLED;
        report.order_status = OrderStatus::CANCELLED;
        report.timestamp = now();
        report.client_id = order.client_id;
        report.leaves_quantity = 0;
        report.cumulative_quantity = order.filled_quantity;
        return report;
    }
};

// Callback types
using ExecutionCallback = std::function<void(const ExecutionReport&)>;
using TradeCallback = std::function<void(const Trade&)>;

} // namespace hft

