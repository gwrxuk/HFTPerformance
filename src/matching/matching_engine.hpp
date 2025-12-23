/**
 * @file matching_engine.hpp
 * @brief Central matching engine managing multiple order books
 * 
 * The matching engine is the core of the trading system:
 * - Manages order books for multiple instruments
 * - Handles order routing and execution
 * - Provides execution callbacks for downstream systems
 * - Maintains statistics and metrics
 */

#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include <mutex>
#include "order_book.hpp"
#include "order.hpp"
#include "core/types.hpp"
#include "core/lockfree_queue.hpp"
#include "core/timing.hpp"

namespace hft {

/**
 * @brief Order request for the matching engine
 */
struct OrderRequest {
    enum class Type : std::uint8_t {
        NEW_ORDER,
        CANCEL_ORDER,
        MODIFY_ORDER
    };

    Type request_type;
    Symbol symbol;
    OrderId order_id;
    Side side;
    OrderType order_type;
    Price price;
    Quantity quantity;
    std::uint64_t client_id;
    Timestamp timestamp;

    static OrderRequest make_new(const Symbol& sym, Side s, OrderType ot, 
                                  Price p, Quantity q, std::uint64_t client = 0) {
        OrderRequest req;
        req.request_type = Type::NEW_ORDER;
        req.symbol = sym;
        req.order_id = 0;  // Will be assigned by engine
        req.side = s;
        req.order_type = ot;
        req.price = p;
        req.quantity = q;
        req.client_id = client;
        req.timestamp = now();
        return req;
    }

    static OrderRequest make_cancel(const Symbol& sym, OrderId id) {
        OrderRequest req;
        req.request_type = Type::CANCEL_ORDER;
        req.symbol = sym;
        req.order_id = id;
        req.timestamp = now();
        return req;
    }
};

/**
 * @brief Matching engine statistics
 */
struct EngineStats {
    std::uint64_t orders_received = 0;
    std::uint64_t orders_matched = 0;
    std::uint64_t orders_cancelled = 0;
    std::uint64_t orders_rejected = 0;
    Quantity total_volume = 0;
    Duration total_latency_ns = 0;
    Duration min_latency_ns = std::numeric_limits<Duration>::max();
    Duration max_latency_ns = 0;
};

/**
 * @brief Central matching engine
 * 
 * Thread-safety: The engine is designed for single-threaded operation
 * in the hot path. Use message queues for multi-threaded access.
 */
class MatchingEngine {
public:
    MatchingEngine() = default;

    // Non-copyable
    MatchingEngine(const MatchingEngine&) = delete;
    MatchingEngine& operator=(const MatchingEngine&) = delete;

    /**
     * @brief Add a new instrument/order book
     */
    bool add_instrument(const Symbol& symbol) {
        std::string sym_str(symbol_view(symbol));
        auto [it, inserted] = books_.try_emplace(
            sym_str, 
            std::make_unique<OrderBook>(symbol)
        );
        return inserted;
    }

    /**
     * @brief Submit a new order
     * 
     * @param symbol Instrument symbol
     * @param side Buy or Sell
     * @param type Order type (limit, market, etc.)
     * @param price Order price (fixed-point)
     * @param quantity Order quantity
     * @param client_id Client identifier
     * @return Order ID if accepted, 0 if rejected
     */
    OrderId submit_order(const Symbol& symbol, Side side, OrderType type,
                         Price price, Quantity quantity, std::uint64_t client_id = 0) {
        const auto start_time = now();
        ++stats_.orders_received;

        // Find order book
        auto* book = get_book(symbol);
        if (!book) {
            ++stats_.orders_rejected;
            return INVALID_ORDER_ID;
        }

        // Generate order ID
        OrderId order_id = id_generator_.next();
        Order order(order_id, side, type, price, quantity, client_id);

        // Add to book (will match immediately if possible)
        bool accepted = book->add_order(order, execution_callback_);
        
        if (!accepted) {
            ++stats_.orders_rejected;
            return INVALID_ORDER_ID;
        }

        // Track latency
        const auto latency = now() - start_time;
        update_latency_stats(latency);

        return order_id;
    }

    /**
     * @brief Cancel an existing order
     */
    bool cancel_order(const Symbol& symbol, OrderId order_id) {
        auto* book = get_book(symbol);
        if (!book) {
            return false;
        }

        bool cancelled = book->cancel_order(order_id, execution_callback_);
        if (cancelled) {
            ++stats_.orders_cancelled;
        }
        return cancelled;
    }

    /**
     * @brief Modify an existing order
     */
    bool modify_order(const Symbol& symbol, OrderId order_id, 
                      Price new_price, Quantity new_quantity) {
        auto* book = get_book(symbol);
        if (!book) {
            return false;
        }

        return book->modify_order(order_id, new_price, new_quantity, execution_callback_);
    }

    /**
     * @brief Process an order request (batch interface)
     */
    OrderId process_request(const OrderRequest& request) {
        switch (request.request_type) {
            case OrderRequest::Type::NEW_ORDER:
                return submit_order(request.symbol, request.side, request.order_type,
                                   request.price, request.quantity, request.client_id);
            case OrderRequest::Type::CANCEL_ORDER:
                return cancel_order(request.symbol, request.order_id) ? 
                       request.order_id : INVALID_ORDER_ID;
            case OrderRequest::Type::MODIFY_ORDER:
                return modify_order(request.symbol, request.order_id, 
                                   request.price, request.quantity) ?
                       request.order_id : INVALID_ORDER_ID;
        }
        return INVALID_ORDER_ID;
    }

    /**
     * @brief Get order book for a symbol
     */
    [[nodiscard]] OrderBook* get_book(const Symbol& symbol) {
        auto it = books_.find(std::string(symbol_view(symbol)));
        return it != books_.end() ? it->second.get() : nullptr;
    }

    [[nodiscard]] const OrderBook* get_book(const Symbol& symbol) const {
        auto it = books_.find(std::string(symbol_view(symbol)));
        return it != books_.end() ? it->second.get() : nullptr;
    }

    /**
     * @brief Get best quote for a symbol
     */
    [[nodiscard]] std::optional<Quote> get_quote(const Symbol& symbol) const {
        const auto* book = get_book(symbol);
        return book ? book->get_quote() : std::nullopt;
    }

    /**
     * @brief Set execution callback
     */
    void set_execution_callback(ExecutionCallback callback) {
        execution_callback_ = std::move(callback);
    }

    /**
     * @brief Get engine statistics
     */
    [[nodiscard]] const EngineStats& stats() const noexcept {
        return stats_;
    }

    /**
     * @brief Get latency statistics
     */
    [[nodiscard]] const LatencyStats& latency_stats() const noexcept {
        return latency_stats_;
    }

    /**
     * @brief Get all instrument symbols
     */
    [[nodiscard]] std::vector<std::string> instruments() const {
        std::vector<std::string> result;
        result.reserve(books_.size());
        for (const auto& [symbol, _] : books_) {
            result.push_back(symbol);
        }
        return result;
    }

    /**
     * @brief Clear all order books
     */
    void clear() {
        for (auto& [_, book] : books_) {
            book->clear();
        }
    }

    /**
     * @brief Reset statistics
     */
    void reset_stats() {
        stats_ = EngineStats{};
        latency_stats_.clear();
    }

private:
    void update_latency_stats(Duration latency) {
        stats_.total_latency_ns += latency;
        stats_.min_latency_ns = std::min(stats_.min_latency_ns, latency);
        stats_.max_latency_ns = std::max(stats_.max_latency_ns, latency);
        latency_stats_.add_sample_ns(latency);
    }

    std::unordered_map<std::string, std::unique_ptr<OrderBook>> books_;
    OrderIdGenerator id_generator_;
    ExecutionCallback execution_callback_;
    EngineStats stats_;
    LatencyStats latency_stats_;
};

/**
 * @brief Thread-safe wrapper for matching engine
 * 
 * Uses a lock-free queue for order submission and processes
 * orders in a dedicated thread.
 */
class AsyncMatchingEngine {
    static constexpr std::size_t QUEUE_SIZE = 65536;

public:
    AsyncMatchingEngine() : running_(false) {}

    ~AsyncMatchingEngine() {
        stop();
    }

    void start() {
        running_.store(true, std::memory_order_release);
        worker_ = std::thread([this] { run(); });
    }

    void stop() {
        running_.store(false, std::memory_order_release);
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    /**
     * @brief Submit order asynchronously
     */
    bool submit(const OrderRequest& request) {
        return request_queue_.try_push(request);
    }

    MatchingEngine& engine() { return engine_; }
    const MatchingEngine& engine() const { return engine_; }

private:
    void run() {
        while (running_.load(std::memory_order_acquire)) {
            auto request = request_queue_.try_pop();
            if (request) {
                engine_.process_request(*request);
            } else {
                // Busy wait with pause
                #if defined(__x86_64__)
                    __builtin_ia32_pause();
                #endif
            }
        }
    }

    MatchingEngine engine_;
    SPSCQueue<OrderRequest, QUEUE_SIZE> request_queue_;
    std::thread worker_;
    std::atomic<bool> running_;
};

} // namespace hft

