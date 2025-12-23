/**
 * @file order_gateway_main.cpp
 * @brief Order Gateway Application
 * 
 * Provides order routing and risk management:
 * - Order validation
 * - Rate limiting
 * - Position tracking
 * - Connection to matching engine
 */

#include <iostream>
#include <iomanip>
#include <sstream>
#include <csignal>
#include <atomic>
#include <unordered_map>

#include "matching/matching_engine.hpp"
#include "protocol/rest_handler.hpp"
#include "protocol/fix_message.hpp"
#include "core/cpu_affinity.hpp"
#include "core/timing.hpp"

using namespace hft;

std::atomic<bool> g_running{true};

void signal_handler(int signum) {
    std::cout << "\nReceived signal " << signum << ", shutting down...\n";
    g_running.store(false, std::memory_order_release);
}

/**
 * @brief Simple rate limiter
 */
class RateLimiter {
public:
    explicit RateLimiter(int max_per_second) 
        : max_per_second_(max_per_second)
        , window_start_(now())
        , count_(0)
    {}

    bool check() {
        auto current = now();
        auto elapsed = current - window_start_;
        
        // Reset window every second
        if (elapsed >= 1'000'000'000) {  // 1 second in nanoseconds
            window_start_ = current;
            count_ = 0;
        }
        
        if (count_ >= max_per_second_) {
            return false;
        }
        
        ++count_;
        return true;
    }

private:
    int max_per_second_;
    Timestamp window_start_;
    int count_;
};

/**
 * @brief Position tracker for risk management
 */
class PositionTracker {
public:
    struct Position {
        Quantity net_position = 0;
        Quantity open_buy_orders = 0;
        Quantity open_sell_orders = 0;
        Quantity max_position = 10000;
    };

    bool check_order(const Symbol& symbol, Side side, Quantity quantity) {
        std::string sym(symbol_view(symbol));
        auto& pos = positions_[sym];
        
        Quantity potential_position;
        if (side == Side::BUY) {
            potential_position = pos.net_position + quantity + pos.open_buy_orders;
        } else {
            potential_position = pos.net_position - quantity - pos.open_sell_orders;
        }
        
        return std::abs(potential_position) <= pos.max_position;
    }

    void on_order_accepted(const Symbol& symbol, Side side, Quantity quantity) {
        std::string sym(symbol_view(symbol));
        auto& pos = positions_[sym];
        
        if (side == Side::BUY) {
            pos.open_buy_orders += quantity;
        } else {
            pos.open_sell_orders += quantity;
        }
    }

    void on_fill(const Symbol& symbol, Side side, Quantity quantity) {
        std::string sym(symbol_view(symbol));
        auto& pos = positions_[sym];
        
        if (side == Side::BUY) {
            pos.net_position += quantity;
            pos.open_buy_orders -= quantity;
        } else {
            pos.net_position -= quantity;
            pos.open_sell_orders -= quantity;
        }
    }

    Position get_position(const Symbol& symbol) const {
        std::string sym(symbol_view(symbol));
        auto it = positions_.find(sym);
        if (it != positions_.end()) {
            return it->second;
        }
        return Position{};
    }

private:
    std::unordered_map<std::string, Position> positions_;
};

/**
 * @brief Order Gateway Statistics
 */
struct GatewayStats {
    std::atomic<std::uint64_t> orders_received{0};
    std::atomic<std::uint64_t> orders_accepted{0};
    std::atomic<std::uint64_t> orders_rejected{0};
    std::atomic<std::uint64_t> rate_limited{0};
    std::atomic<std::uint64_t> risk_rejected{0};
};

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                HFT Order Gateway Server v1.0                  ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";
    
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    // Configure for low latency
    ThreadConfig config;
    config.priority = ThreadPriority::HIGH;
    config.lock_memory = true;
    config.name = "gateway-main";
    apply_thread_config(config);
    
    // Create components
    MatchingEngine engine;
    RateLimiter rate_limiter(1000);  // 1000 orders/sec
    PositionTracker positions;
    GatewayStats stats;
    LatencyStats latency_stats(100000);
    
    // Add instruments
    std::vector<Symbol> instruments = {
        make_symbol("BTC-USD"),
        make_symbol("ETH-USD"),
        make_symbol("SOL-USD")
    };
    
    std::cout << "Initializing instruments:\n";
    for (const auto& sym : instruments) {
        engine.add_instrument(sym);
        std::cout << "  + " << symbol_view(sym) << "\n";
    }
    
    // Set up execution callback
    engine.set_execution_callback([&](const ExecutionReport& report) {
        if (report.exec_type == ExecutionType::TRADE) {
            // Update position on fill
            // positions.on_fill(...);
        }
    });
    
    // Create HTTP server
    constexpr std::uint16_t PORT = 9000;
    HttpServer server(PORT);
    auto& router = server.router();
    
    // Health check
    router.get("/health", [](const HttpRequest&) {
        return HttpResponse(HttpStatus::OK).json(R"({"status":"healthy"})");
    });
    
    // Submit order with validation
    router.post("/api/v1/order", [&](const HttpRequest& req) {
        auto start = now();
        ++stats.orders_received;
        
        // Rate limit check
        if (!rate_limiter.check()) {
            ++stats.rate_limited;
            return HttpResponse(HttpStatus::TOO_MANY_REQUESTS)
                .json(json_response::order_rejected("Rate limit exceeded"));
        }
        
        // Parse order
        auto order_req = parse_order_request(req.body);
        if (!order_req) {
            ++stats.orders_rejected;
            return HttpResponse(HttpStatus::BAD_REQUEST)
                .json(json_response::error("Invalid order request", "INVALID_ORDER"));
        }
        
        Symbol symbol = make_symbol(order_req->symbol);
        
        // Validate instrument
        if (!engine.get_book(symbol)) {
            ++stats.orders_rejected;
            return HttpResponse(HttpStatus::BAD_REQUEST)
                .json(json_response::order_rejected("Unknown symbol"));
        }
        
        // Position/risk check
        if (!positions.check_order(symbol, order_req->side, 
                                   static_cast<Quantity>(order_req->quantity))) {
            ++stats.risk_rejected;
            return HttpResponse(HttpStatus::BAD_REQUEST)
                .json(json_response::order_rejected("Position limit exceeded"));
        }
        
        // Submit to matching engine
        OrderId order_id = engine.submit_order(
            symbol,
            order_req->side,
            order_req->type,
            to_fixed_price(order_req->price),
            static_cast<Quantity>(order_req->quantity)
        );
        
        if (order_id == INVALID_ORDER_ID) {
            ++stats.orders_rejected;
            return HttpResponse(HttpStatus::BAD_REQUEST)
                .json(json_response::order_rejected("Order rejected by engine"));
        }
        
        // Track position
        positions.on_order_accepted(symbol, order_req->side,
                                    static_cast<Quantity>(order_req->quantity));
        ++stats.orders_accepted;
        
        // Track latency
        latency_stats.add_sample(now() - start);
        
        return HttpResponse(HttpStatus::CREATED)
            .json(json_response::order_accepted(order_id, order_req->symbol));
    });
    
    // Get position
    router.get("/api/v1/position/:symbol", [&positions](const HttpRequest& req) {
        auto symbol_param = req.get_path_param("symbol");
        if (!symbol_param) {
            return HttpResponse(HttpStatus::BAD_REQUEST)
                .json(json_response::error("Missing symbol", "INVALID_REQUEST"));
        }
        
        Symbol symbol = make_symbol(*symbol_param);
        auto pos = positions.get_position(symbol);
        
        std::ostringstream ss;
        ss << R"({"symbol":")" << *symbol_param
           << R"(","netPosition":)" << pos.net_position
           << R"(,"openBuyOrders":)" << pos.open_buy_orders
           << R"(,"openSellOrders":)" << pos.open_sell_orders
           << R"(,"maxPosition":)" << pos.max_position << "}";
        
        return HttpResponse(HttpStatus::OK).json(ss.str());
    });
    
    // Get gateway stats
    router.get("/api/v1/stats", [&stats, &latency_stats](const HttpRequest&) {
        auto p = latency_stats.get_percentiles();
        
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2);
        ss << R"({"ordersReceived":)" << stats.orders_received.load()
           << R"(,"ordersAccepted":)" << stats.orders_accepted.load()
           << R"(,"ordersRejected":)" << stats.orders_rejected.load()
           << R"(,"rateLimited":)" << stats.rate_limited.load()
           << R"(,"riskRejected":)" << stats.risk_rejected.load()
           << R"(,"latency":{"p50":)" << p.p50
           << R"(,"p99":)" << p.p99
           << R"(,"p999":)" << p.p999 << "}})";
        
        return HttpResponse(HttpStatus::OK).json(ss.str());
    });
    
    // Start server
    if (!server.start()) {
        std::cerr << "Failed to start HTTP server on port " << PORT << "\n";
        return 1;
    }
    
    std::cout << "\nOrder gateway started on port " << PORT << "\n";
    std::cout << "Rate limit: 1000 orders/sec\n";
    std::cout << "Max position per symbol: 10000\n";
    std::cout << "\nEndpoints:\n";
    std::cout << "  GET  /health\n";
    std::cout << "  POST /api/v1/order\n";
    std::cout << "  GET  /api/v1/position/:symbol\n";
    std::cout << "  GET  /api/v1/stats\n";
    std::cout << "\nPress Ctrl+C to stop...\n\n";
    
    // Main loop
    while (g_running.load(std::memory_order_acquire)) {
        server.poll();
    }
    
    server.stop();
    
    // Print final stats
    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "Final Gateway Statistics:\n";
    std::cout << "  Orders Received:  " << stats.orders_received.load() << "\n";
    std::cout << "  Orders Accepted:  " << stats.orders_accepted.load() << "\n";
    std::cout << "  Orders Rejected:  " << stats.orders_rejected.load() << "\n";
    std::cout << "  Rate Limited:     " << stats.rate_limited.load() << "\n";
    std::cout << "  Risk Rejected:    " << stats.risk_rejected.load() << "\n";
    
    if (latency_stats.count() > 0) {
        auto p = latency_stats.get_percentiles();
        std::cout << "\nLatency Statistics:\n";
        std::cout << "  P50:   " << std::fixed << std::setprecision(2) << p.p50 << " ns\n";
        std::cout << "  P99:   " << p.p99 << " ns\n";
        std::cout << "  P99.9: " << p.p999 << " ns\n";
    }
    
    std::cout << "\nOrder gateway shutdown complete.\n";
    return 0;
}

