/**
 * @file market_data_handler.hpp
 * @brief Market data feed handler
 * 
 * Handles incoming market data from various sources:
 * - Exchange WebSocket feeds
 * - FIX market data connections
 * - Internal matching engine events
 */

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include "core/types.hpp"
#include "core/lockfree_queue.hpp"
#include "protocol/websocket_handler.hpp"

namespace hft {

/**
 * @brief Market data update types
 */
enum class MarketDataType : std::uint8_t {
    QUOTE_UPDATE,
    TRADE,
    BOOK_SNAPSHOT,
    BOOK_UPDATE
};

/**
 * @brief Market data update message
 */
struct MarketDataUpdate {
    MarketDataType type;
    Symbol symbol;
    Timestamp timestamp;
    
    union {
        struct {
            Price bid_price;
            Price ask_price;
            Quantity bid_quantity;
            Quantity ask_quantity;
        } quote;
        
        struct {
            Price price;
            Quantity quantity;
            Side side;
        } trade;
    } data;
    
    static MarketDataUpdate make_quote(const Symbol& sym, 
                                        Price bid_px, Quantity bid_qty,
                                        Price ask_px, Quantity ask_qty) {
        MarketDataUpdate update;
        update.type = MarketDataType::QUOTE_UPDATE;
        update.symbol = sym;
        update.timestamp = now();
        update.data.quote.bid_price = bid_px;
        update.data.quote.ask_price = ask_px;
        update.data.quote.bid_quantity = bid_qty;
        update.data.quote.ask_quantity = ask_qty;
        return update;
    }
    
    static MarketDataUpdate make_trade(const Symbol& sym,
                                        Price price, Quantity qty, Side side) {
        MarketDataUpdate update;
        update.type = MarketDataType::TRADE;
        update.symbol = sym;
        update.timestamp = now();
        update.data.trade.price = price;
        update.data.trade.quantity = qty;
        update.data.trade.side = side;
        return update;
    }
};

/**
 * @brief Callback types for market data
 */
using QuoteCallback = std::function<void(const Symbol&, const Quote&)>;
using MarketTradeCallback = std::function<void(const Symbol&, const Trade&)>;
using UpdateCallback = std::function<void(const MarketDataUpdate&)>;

/**
 * @brief Market data handler for processing incoming data
 */
class MarketDataHandler {
    static constexpr std::size_t UPDATE_QUEUE_SIZE = 65536;

public:
    MarketDataHandler() = default;

    /**
     * @brief Subscribe to a symbol
     */
    void subscribe(const Symbol& symbol);

    /**
     * @brief Unsubscribe from a symbol
     */
    void unsubscribe(const Symbol& symbol);

    /**
     * @brief Process an incoming update
     */
    void on_update(const MarketDataUpdate& update);

    /**
     * @brief Set quote callback
     */
    void set_quote_callback(QuoteCallback callback) {
        quote_callback_ = std::move(callback);
    }

    /**
     * @brief Set trade callback
     */
    void set_trade_callback(MarketTradeCallback callback) {
        trade_callback_ = std::move(callback);
    }

    /**
     * @brief Get latest quote for a symbol
     */
    [[nodiscard]] std::optional<Quote> get_quote(const Symbol& symbol) const;

    /**
     * @brief Get subscription count
     */
    [[nodiscard]] std::size_t subscription_count() const {
        return subscriptions_.size();
    }

private:
    std::unordered_map<std::string, bool> subscriptions_;
    std::unordered_map<std::string, Quote> latest_quotes_;
    QuoteCallback quote_callback_;
    MarketTradeCallback trade_callback_;
};

/**
 * @brief WebSocket market data feed client
 */
class WebSocketFeedClient {
public:
    WebSocketFeedClient(std::string_view host, std::uint16_t port);

    /**
     * @brief Connect to the feed
     */
    bool connect();

    /**
     * @brief Disconnect from the feed
     */
    void disconnect();

    /**
     * @brief Subscribe to symbols
     */
    void subscribe(const std::vector<Symbol>& symbols);

    /**
     * @brief Set update callback
     */
    void set_callback(UpdateCallback callback) {
        callback_ = std::move(callback);
    }

    /**
     * @brief Process incoming data (call in event loop)
     */
    void poll();

    [[nodiscard]] bool is_connected() const {
        return handler_.is_connected();
    }

private:
    void on_message(const WebSocketFrame& frame);

    std::string host_;
    std::uint16_t port_;
    WebSocketHandler handler_;
    UpdateCallback callback_;
};

} // namespace hft

