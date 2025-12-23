/**
 * @file order_book.hpp
 * @brief Limit Order Book implementation
 * 
 * A high-performance order book using:
 * - Sorted map for price levels (log N access)
 * - Intrusive linked lists for orders at each level (O(1) operations)
 * - Memory pool for order allocation (no heap fragmentation)
 * - Separate bid/ask sides for cache efficiency
 */

#pragma once

#include <map>
#include <unordered_map>
#include <vector>
#include <optional>
#include <functional>
#include "order.hpp"
#include "price_level.hpp"
#include "core/memory_pool.hpp"
#include "core/types.hpp"

namespace hft {

/**
 * @brief Statistics about the order book
 */
struct OrderBookStats {
    std::size_t bid_levels = 0;
    std::size_t ask_levels = 0;
    std::size_t total_orders = 0;
    Quantity total_bid_quantity = 0;
    Quantity total_ask_quantity = 0;
    std::uint64_t trades_matched = 0;
    Quantity volume_matched = 0;
};

/**
 * @brief Limit Order Book for a single instrument
 * 
 * Thread-safety: NOT thread-safe. Use external synchronization or
 * design with single-threaded matching engine.
 */
class OrderBook {
    // Maximum orders per book (for memory pool sizing)
    static constexpr std::size_t MAX_ORDERS = 1'000'000;
    static constexpr std::size_t MAX_PRICE_LEVELS = 10'000;

    // Use red-black tree map for price levels
    // Bids: descending order (highest first)
    // Asks: ascending order (lowest first)
    using BidMap = std::map<Price, PriceLevel, std::greater<Price>>;
    using AskMap = std::map<Price, PriceLevel, std::less<Price>>;

public:
    explicit OrderBook(const Symbol& symbol) 
        : symbol_(symbol)
        , order_pool_()
    {}

    // Non-copyable
    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    /**
     * @brief Add a new limit order to the book
     * 
     * @param order The order to add
     * @param exec_callback Callback for execution reports
     * @return true if order was accepted
     */
    bool add_order(const Order& order, ExecutionCallback exec_callback = nullptr) {
        // Allocate order node from pool
        OrderNode* node = order_pool_.create(order);
        if (!node) {
            // Pool exhausted
            if (exec_callback) {
                Order rejected = order;
                rejected.reject();
                exec_callback(ExecutionReport::make_cancel(rejected));
            }
            return false;
        }

        // Index by order ID
        order_index_[order.order_id] = node;

        // Send NEW execution report
        if (exec_callback) {
            exec_callback(ExecutionReport::make_new(order));
        }

        // Try to match immediately
        if (order.type != OrderType::POST_ONLY) {
            match_order(node, exec_callback);
        }

        // If order still has remaining quantity, add to book
        if (node->order.remaining_quantity() > 0 && node->order.is_active()) {
            add_to_book(node);
        } else if (node->order.remaining_quantity() == 0) {
            // Fully filled, remove from index
            order_index_.erase(order.order_id);
            order_pool_.destroy(node);
        }

        return true;
    }

    /**
     * @brief Cancel an existing order
     * 
     * @param order_id ID of order to cancel
     * @param exec_callback Callback for execution report
     * @return true if order was found and cancelled
     */
    bool cancel_order(OrderId order_id, ExecutionCallback exec_callback = nullptr) {
        auto it = order_index_.find(order_id);
        if (it == order_index_.end()) {
            return false;
        }

        OrderNode* node = it->second;
        
        // Send cancel report
        node->order.cancel();
        if (exec_callback) {
            exec_callback(ExecutionReport::make_cancel(node->order));
        }

        // Remove from book
        remove_from_book(node);
        
        // Remove from index and pool
        order_index_.erase(it);
        order_pool_.destroy(node);

        return true;
    }

    /**
     * @brief Modify an existing order (cancel + replace)
     */
    bool modify_order(OrderId order_id, Price new_price, Quantity new_quantity,
                      ExecutionCallback exec_callback = nullptr) {
        auto it = order_index_.find(order_id);
        if (it == order_index_.end()) {
            return false;
        }

        OrderNode* node = it->second;
        Order& order = node->order;

        // If only reducing quantity at same price, can do in-place
        if (new_price == order.price && new_quantity < order.remaining_quantity()) {
            order.quantity = order.filled_quantity + new_quantity;
            update_level_quantity(order.side, order.price);
            return true;
        }

        // Otherwise, cancel and re-add
        Side side = order.side;
        OrderType type = order.type;
        std::uint64_t client_id = order.client_id;
        
        cancel_order(order_id, nullptr);
        
        Order new_order(order_id, side, type, new_price, new_quantity, client_id);
        return add_order(new_order, exec_callback);
    }

    /**
     * @brief Get an order by ID
     */
    [[nodiscard]] const Order* get_order(OrderId order_id) const {
        auto it = order_index_.find(order_id);
        return it != order_index_.end() ? &it->second->order : nullptr;
    }

    /**
     * @brief Get best bid price
     */
    [[nodiscard]] std::optional<Price> best_bid() const {
        if (bids_.empty()) return std::nullopt;
        return bids_.begin()->first;
    }

    /**
     * @brief Get best ask price
     */
    [[nodiscard]] std::optional<Price> best_ask() const {
        if (asks_.empty()) return std::nullopt;
        return asks_.begin()->first;
    }

    /**
     * @brief Get best bid/ask quote
     */
    [[nodiscard]] std::optional<Quote> get_quote() const {
        if (bids_.empty() || asks_.empty()) return std::nullopt;
        
        Quote quote;
        quote.bid_price = bids_.begin()->first;
        quote.ask_price = asks_.begin()->first;
        quote.bid_quantity = bids_.begin()->second.total_quantity();
        quote.ask_quantity = asks_.begin()->second.total_quantity();
        quote.timestamp = now();
        return quote;
    }

    /**
     * @brief Get order book depth (top N levels)
     */
    struct Level {
        Price price;
        Quantity quantity;
        std::size_t order_count;
    };

    struct Depth {
        std::vector<Level> bids;
        std::vector<Level> asks;
    };

    [[nodiscard]] Depth get_depth(std::size_t levels = 10) const {
        Depth depth;
        depth.bids.reserve(levels);
        depth.asks.reserve(levels);

        std::size_t count = 0;
        for (const auto& [price, level] : bids_) {
            if (count++ >= levels) break;
            depth.bids.push_back({price, level.total_quantity(), level.order_count()});
        }

        count = 0;
        for (const auto& [price, level] : asks_) {
            if (count++ >= levels) break;
            depth.asks.push_back({price, level.total_quantity(), level.order_count()});
        }

        return depth;
    }

    /**
     * @brief Get spread
     */
    [[nodiscard]] std::optional<Price> spread() const {
        auto bid = best_bid();
        auto ask = best_ask();
        if (!bid || !ask) return std::nullopt;
        return *ask - *bid;
    }

    /**
     * @brief Get mid price
     */
    [[nodiscard]] std::optional<Price> mid_price() const {
        auto bid = best_bid();
        auto ask = best_ask();
        if (!bid || !ask) return std::nullopt;
        return (*bid + *ask) / 2;
    }

    /**
     * @brief Get book statistics
     */
    [[nodiscard]] OrderBookStats get_stats() const {
        OrderBookStats stats;
        stats.bid_levels = bids_.size();
        stats.ask_levels = asks_.size();
        stats.total_orders = order_index_.size();
        stats.trades_matched = trades_matched_;
        stats.volume_matched = volume_matched_;

        for (const auto& [_, level] : bids_) {
            stats.total_bid_quantity += level.total_quantity();
        }
        for (const auto& [_, level] : asks_) {
            stats.total_ask_quantity += level.total_quantity();
        }

        return stats;
    }

    /**
     * @brief Clear all orders from the book
     */
    void clear() {
        // Clean up all orders
        for (auto& [_, node] : order_index_) {
            order_pool_.destroy(node);
        }
        order_index_.clear();
        bids_.clear();
        asks_.clear();
    }

    [[nodiscard]] const Symbol& symbol() const noexcept { return symbol_; }
    [[nodiscard]] std::size_t order_count() const noexcept { return order_index_.size(); }
    [[nodiscard]] bool empty() const noexcept { return order_index_.empty(); }

private:
    /**
     * @brief Match a buy order against asks
     */
    void match_buy_order(OrderNode* aggressor, ExecutionCallback exec_callback) {
        while (!asks_.empty() && aggressor->order.remaining_quantity() > 0) {
            auto it = asks_.begin();
            Price best_price = it->first;
            PriceLevel& level = it->second;
            
            // Check if prices cross
            if (aggressor->order.price < best_price) break;
            
            // Match against orders at this level
            while (!level.empty() && aggressor->order.remaining_quantity() > 0) {
                OrderNode* passive = level.front();
                
                Quantity fill_qty = std::min(
                    aggressor->order.remaining_quantity(),
                    passive->order.remaining_quantity()
                );
                
                Price exec_price = passive->order.price;
                
                aggressor->order.fill(fill_qty);
                passive->order.fill(fill_qty);
                level.update_quantity(passive, fill_qty);
                
                if (exec_callback) {
                    exec_callback(ExecutionReport::make_trade(
                        aggressor->order, passive->order, exec_price, fill_qty));
                    exec_callback(ExecutionReport::make_trade(
                        passive->order, aggressor->order, exec_price, fill_qty));
                }
                
                ++trades_matched_;
                volume_matched_ += fill_qty;
                
                if (passive->order.is_filled()) {
                    level.pop_front();
                    order_index_.erase(passive->order.order_id);
                    order_pool_.destroy(passive);
                }
            }
            
            if (level.empty()) {
                asks_.erase(it);
            }
        }
    }

    /**
     * @brief Match a sell order against bids
     */
    void match_sell_order(OrderNode* aggressor, ExecutionCallback exec_callback) {
        while (!bids_.empty() && aggressor->order.remaining_quantity() > 0) {
            auto it = bids_.begin();
            Price best_price = it->first;
            PriceLevel& level = it->second;
            
            // Check if prices cross
            if (aggressor->order.price > best_price) break;
            
            // Match against orders at this level
            while (!level.empty() && aggressor->order.remaining_quantity() > 0) {
                OrderNode* passive = level.front();
                
                Quantity fill_qty = std::min(
                    aggressor->order.remaining_quantity(),
                    passive->order.remaining_quantity()
                );
                
                Price exec_price = passive->order.price;
                
                aggressor->order.fill(fill_qty);
                passive->order.fill(fill_qty);
                level.update_quantity(passive, fill_qty);
                
                if (exec_callback) {
                    exec_callback(ExecutionReport::make_trade(
                        aggressor->order, passive->order, exec_price, fill_qty));
                    exec_callback(ExecutionReport::make_trade(
                        passive->order, aggressor->order, exec_price, fill_qty));
                }
                
                ++trades_matched_;
                volume_matched_ += fill_qty;
                
                if (passive->order.is_filled()) {
                    level.pop_front();
                    order_index_.erase(passive->order.order_id);
                    order_pool_.destroy(passive);
                }
            }
            
            if (level.empty()) {
                bids_.erase(it);
            }
        }
    }

    /**
     * @brief Match an incoming order against the book
     */
    void match_order(OrderNode* aggressor, ExecutionCallback exec_callback) {
        if (aggressor->order.side == Side::BUY) {
            match_buy_order(aggressor, exec_callback);
        } else {
            match_sell_order(aggressor, exec_callback);
        }
    }

    /**
     * @brief Add order to the appropriate side of the book
     */
    void add_to_book(OrderNode* node) {
        if (node->order.side == Side::BUY) {
            auto [it, inserted] = bids_.try_emplace(node->order.price, node->order.price);
            it->second.add_order(node);
        } else {
            auto [it, inserted] = asks_.try_emplace(node->order.price, node->order.price);
            it->second.add_order(node);
        }
    }

    /**
     * @brief Remove order from the book
     */
    void remove_from_book(OrderNode* node) {
        if (node->order.side == Side::BUY) {
            auto it = bids_.find(node->order.price);
            if (it != bids_.end()) {
                it->second.remove_order(node);
                if (it->second.empty()) {
                    bids_.erase(it);
                }
            }
        } else {
            auto it = asks_.find(node->order.price);
            if (it != asks_.end()) {
                it->second.remove_order(node);
                if (it->second.empty()) {
                    asks_.erase(it);
                }
            }
        }
    }

    /**
     * @brief Update quantity tracking at a price level
     */
    void update_level_quantity(Side side, Price price) {
        // Quantity is tracked in PriceLevel, this triggers recalculation if needed
        (void)side;
        (void)price;
    }

    Symbol symbol_;
    BidMap bids_;
    AskMap asks_;
    std::unordered_map<OrderId, OrderNode*> order_index_;
    MemoryPool<OrderNode, MAX_ORDERS> order_pool_;
    
    // Statistics
    std::uint64_t trades_matched_ = 0;
    Quantity volume_matched_ = 0;
};

} // namespace hft

