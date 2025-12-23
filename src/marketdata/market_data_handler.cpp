/**
 * @file market_data_handler.cpp
 * @brief Market data handler implementation
 */

#include "market_data_handler.hpp"

namespace hft {

void MarketDataHandler::subscribe(const Symbol& symbol) {
    subscriptions_[std::string(symbol_view(symbol))] = true;
}

void MarketDataHandler::unsubscribe(const Symbol& symbol) {
    subscriptions_.erase(std::string(symbol_view(symbol)));
}

void MarketDataHandler::on_update(const MarketDataUpdate& update) {
    std::string sym_str(symbol_view(update.symbol));
    
    // Check if subscribed
    auto it = subscriptions_.find(sym_str);
    if (it == subscriptions_.end() || !it->second) {
        return;
    }
    
    switch (update.type) {
        case MarketDataType::QUOTE_UPDATE: {
            Quote quote;
            quote.bid_price = update.data.quote.bid_price;
            quote.ask_price = update.data.quote.ask_price;
            quote.bid_quantity = update.data.quote.bid_quantity;
            quote.ask_quantity = update.data.quote.ask_quantity;
            quote.timestamp = update.timestamp;
            
            latest_quotes_[sym_str] = quote;
            
            if (quote_callback_) {
                quote_callback_(update.symbol, quote);
            }
            break;
        }
        
        case MarketDataType::TRADE: {
            if (trade_callback_) {
                Trade trade;
                trade.price = update.data.trade.price;
                trade.quantity = update.data.trade.quantity;
                trade.aggressor_side = update.data.trade.side;
                trade.timestamp = update.timestamp;
                trade_callback_(update.symbol, trade);
            }
            break;
        }
        
        default:
            break;
    }
}

std::optional<Quote> MarketDataHandler::get_quote(const Symbol& symbol) const {
    auto it = latest_quotes_.find(std::string(symbol_view(symbol)));
    if (it != latest_quotes_.end()) {
        return it->second;
    }
    return std::nullopt;
}

// WebSocket Feed Client
WebSocketFeedClient::WebSocketFeedClient(std::string_view host, std::uint16_t port)
    : host_(host), port_(port) {}

bool WebSocketFeedClient::connect() {
    handler_.on_message([this](const WebSocketFrame& frame) {
        on_message(frame);
    });
    
    return handler_.connect(host_, port_, "/ws/market");
}

void WebSocketFeedClient::disconnect() {
    handler_.close();
}

void WebSocketFeedClient::subscribe(const std::vector<Symbol>& symbols) {
    std::vector<std::string> sym_strings;
    for (const auto& sym : symbols) {
        sym_strings.push_back(std::string(symbol_view(sym)));
    }
    
    std::string msg = ws_json::build_subscribe(sym_strings, {"quote", "trade"});
    handler_.send_text(msg);
}

void WebSocketFeedClient::poll() {
    handler_.poll();
}

void WebSocketFeedClient::on_message(const WebSocketFrame& frame) {
    if (!callback_) return;
    
    // Parse message and convert to MarketDataUpdate
    auto trade = ws_json::parse_trade(frame.payload_string());
    if (trade) {
        MarketDataUpdate update = MarketDataUpdate::make_trade(
            make_symbol(trade->symbol),
            to_fixed_price(trade->price),
            static_cast<Quantity>(trade->quantity),
            trade->side == "BUY" ? Side::BUY : Side::SELL
        );
        callback_(update);
    }
}

} // namespace hft

