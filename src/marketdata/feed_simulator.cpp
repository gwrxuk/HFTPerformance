/**
 * @file feed_simulator.cpp
 * @brief Market data feed simulator for testing
 */

#include "market_data_handler.hpp"
#include <random>
#include <chrono>
#include <thread>

namespace hft {

/**
 * @brief Simulated market data feed for testing
 */
class FeedSimulator {
public:
    struct Config {
        double base_price;
        double volatility;
        double tick_size;
        int updates_per_second;
        int spread_ticks;
        
        Config() : base_price(100.0), volatility(0.001), tick_size(0.01),
                   updates_per_second(1000), spread_ticks(1) {}
    };

    explicit FeedSimulator(const Config& config = Config())
        : config_(config)
        , gen_(std::random_device{}())
        , price_dist_(0.0, config.volatility)
        , running_(false)
    {}

    void start(UpdateCallback callback) {
        callback_ = std::move(callback);
        running_ = true;
        worker_ = std::thread([this] { run(); });
    }

    void stop() {
        running_ = false;
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    void add_symbol(const Symbol& symbol) {
        symbols_.push_back(symbol);
        prices_[std::string(symbol_view(symbol))] = config_.base_price;
    }

private:
    void run() {
        auto interval = std::chrono::microseconds(1'000'000 / config_.updates_per_second);
        
        while (running_) {
            auto start = std::chrono::steady_clock::now();
            
            for (const auto& symbol : symbols_) {
                generate_update(symbol);
            }
            
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed < interval) {
                std::this_thread::sleep_for(interval - elapsed);
            }
        }
    }

    void generate_update(const Symbol& symbol) {
        if (!callback_) return;
        
        std::string sym_str(symbol_view(symbol));
        auto& price = prices_[sym_str];
        
        // Random walk
        price *= (1.0 + price_dist_(gen_));
        
        // Snap to tick
        price = std::round(price / config_.tick_size) * config_.tick_size;
        
        // Generate quote
        double spread = config_.tick_size * config_.spread_ticks;
        double bid = price - spread / 2;
        double ask = price + spread / 2;
        
        auto update = MarketDataUpdate::make_quote(
            symbol,
            to_fixed_price(bid), 100 + (gen_() % 1000),
            to_fixed_price(ask), 100 + (gen_() % 1000)
        );
        
        callback_(update);
    }

    Config config_;
    std::vector<Symbol> symbols_;
    std::unordered_map<std::string, double> prices_;
    std::mt19937 gen_;
    std::normal_distribution<> price_dist_;
    UpdateCallback callback_;
    std::thread worker_;
    std::atomic<bool> running_;
};

} // namespace hft

