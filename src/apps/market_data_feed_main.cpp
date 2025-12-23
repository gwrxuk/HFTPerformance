/**
 * @file market_data_feed_main.cpp
 * @brief Market Data Feed Simulator
 * 
 * Generates simulated market data for testing:
 * - Quote updates
 * - Trade events
 * - Order book snapshots
 */

#include <iostream>
#include <iomanip>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <random>

#include "marketdata/market_data_handler.hpp"
#include "core/cpu_affinity.hpp"
#include "core/types.hpp"

using namespace hft;

std::atomic<bool> g_running{true};

void signal_handler(int signum) {
    std::cout << "\nReceived signal " << signum << ", shutting down...\n";
    g_running.store(false, std::memory_order_release);
}

class MarketDataFeedServer {
public:
    struct InstrumentState {
        Symbol symbol;
        double last_price;
        double volatility;
        Quantity bid_size;
        Quantity ask_size;
    };

    MarketDataFeedServer() 
        : gen_(std::random_device{}())
    {}

    void add_instrument(const Symbol& symbol, double initial_price, double volatility) {
        InstrumentState state;
        state.symbol = symbol;
        state.last_price = initial_price;
        state.volatility = volatility;
        state.bid_size = 1000;
        state.ask_size = 1000;
        instruments_.push_back(state);
    }

    void start() {
        running_ = true;
        worker_ = std::thread([this] { run(); });
    }

    void stop() {
        running_ = false;
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    void set_update_callback(UpdateCallback callback) {
        callback_ = std::move(callback);
    }

    std::uint64_t updates_generated() const { return updates_generated_; }

private:
    void run() {
        ThreadConfig config;
        config.priority = ThreadPriority::HIGH;
        config.name = "market-data";
        apply_thread_config(config);

        std::normal_distribution<> price_change(0.0, 1.0);
        std::uniform_int_distribution<> size_change(-100, 100);
        
        const auto interval = std::chrono::microseconds(100);  // 10K updates/sec

        while (running_) {
            auto start = std::chrono::steady_clock::now();
            
            for (auto& inst : instruments_) {
                // Random walk price
                double change = price_change(gen_) * inst.volatility;
                inst.last_price *= (1.0 + change);
                
                // Update sizes
                inst.bid_size = std::max(Quantity(100), 
                                         inst.bid_size + size_change(gen_));
                inst.ask_size = std::max(Quantity(100),
                                         inst.ask_size + size_change(gen_));
                
                // Calculate spread
                double spread = inst.last_price * 0.0001;  // 1 bps spread
                double bid = inst.last_price - spread / 2;
                double ask = inst.last_price + spread / 2;
                
                // Generate update
                if (callback_) {
                    auto update = MarketDataUpdate::make_quote(
                        inst.symbol,
                        to_fixed_price(bid), inst.bid_size,
                        to_fixed_price(ask), inst.ask_size
                    );
                    callback_(update);
                    ++updates_generated_;
                }
                
                // Occasionally generate a trade
                if (gen_() % 10 == 0) {
                    Side side = (gen_() % 2 == 0) ? Side::BUY : Side::SELL;
                    double trade_price = side == Side::BUY ? ask : bid;
                    Quantity trade_size = 10 + (gen_() % 100);
                    
                    if (callback_) {
                        auto update = MarketDataUpdate::make_trade(
                            inst.symbol,
                            to_fixed_price(trade_price),
                            trade_size,
                            side
                        );
                        callback_(update);
                        ++updates_generated_;
                    }
                }
            }
            
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed < interval) {
                std::this_thread::sleep_for(interval - elapsed);
            }
        }
    }

    std::vector<InstrumentState> instruments_;
    std::mt19937 gen_;
    UpdateCallback callback_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> updates_generated_{0};
};

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║              HFT Market Data Feed Simulator v1.0              ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";
    
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    MarketDataFeedServer server;
    
    // Add instruments
    std::cout << "Configuring instruments:\n";
    server.add_instrument(make_symbol("BTC-USD"), 50000.0, 0.0001);
    std::cout << "  + BTC-USD (base: $50,000)\n";
    server.add_instrument(make_symbol("ETH-USD"), 3000.0, 0.00015);
    std::cout << "  + ETH-USD (base: $3,000)\n";
    server.add_instrument(make_symbol("SOL-USD"), 100.0, 0.0002);
    std::cout << "  + SOL-USD (base: $100)\n";
    server.add_instrument(make_symbol("AVAX-USD"), 35.0, 0.0002);
    std::cout << "  + AVAX-USD (base: $35)\n";
    server.add_instrument(make_symbol("MATIC-USD"), 0.90, 0.0003);
    std::cout << "  + MATIC-USD (base: $0.90)\n";
    
    // Set up callback to print some updates
    std::atomic<std::uint64_t> quote_count{0};
    std::atomic<std::uint64_t> trade_count{0};
    
    server.set_update_callback([&](const MarketDataUpdate& update) {
        if (update.type == MarketDataType::QUOTE_UPDATE) {
            ++quote_count;
            // Print occasional quote
            if (quote_count % 10000 == 0) {
                std::cout << "[QUOTE] " << symbol_view(update.symbol)
                          << " Bid: " << to_double_price(update.data.quote.bid_price)
                          << " x " << update.data.quote.bid_quantity
                          << " | Ask: " << to_double_price(update.data.quote.ask_price)
                          << " x " << update.data.quote.ask_quantity << "\n";
            }
        } else if (update.type == MarketDataType::TRADE) {
            ++trade_count;
            // Print all trades
            std::cout << "[TRADE] " << symbol_view(update.symbol)
                      << " " << to_string(update.data.trade.side)
                      << " " << update.data.trade.quantity
                      << " @ " << to_double_price(update.data.trade.price) << "\n";
        }
    });
    
    std::cout << "\nStarting market data feed...\n";
    std::cout << "Press Ctrl+C to stop.\n\n";
    
    server.start();
    
    auto start_time = std::chrono::steady_clock::now();
    
    while (g_running.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        auto seconds = std::chrono::duration<double>(elapsed).count();
        
        std::cout << "\n--- Statistics (after " << std::fixed << std::setprecision(1) 
                  << seconds << " seconds) ---\n";
        std::cout << "  Total updates:  " << server.updates_generated() << "\n";
        std::cout << "  Quotes:         " << quote_count.load() << "\n";
        std::cout << "  Trades:         " << trade_count.load() << "\n";
        std::cout << "  Update rate:    " << std::setprecision(0) 
                  << (server.updates_generated() / seconds) << " updates/sec\n";
    }
    
    server.stop();
    
    std::cout << "\nMarket data feed stopped.\n";
    std::cout << "Total updates generated: " << server.updates_generated() << "\n";
    
    return 0;
}

