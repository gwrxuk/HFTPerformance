/**
 * @file matching_engine_main.cpp
 * @brief Matching Engine Server Application
 * 
 * A standalone matching engine server that:
 * - Accepts orders via REST API
 * - Maintains order books for multiple instruments
 * - Publishes execution reports
 */

#include <iostream>
#include <csignal>
#include <atomic>

#include "matching/matching_engine.hpp"
#include "protocol/rest_handler.hpp"
#include "core/cpu_affinity.hpp"

using namespace hft;

// Global flag for graceful shutdown
std::atomic<bool> g_running{true};

void signal_handler(int signum) {
    std::cout << "\nReceived signal " << signum << ", shutting down...\n";
    g_running.store(false, std::memory_order_release);
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║              HFT Matching Engine Server v1.0                  ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";
    
    // Set up signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    // Configure thread for low latency
    ThreadConfig config;
    config.priority = ThreadPriority::HIGH;
    config.lock_memory = true;
    config.name = "matching-main";
    apply_thread_config(config);
    
    // Create matching engine
    MatchingEngine engine;
    
    // Add default instruments
    std::vector<Symbol> instruments = {
        make_symbol("BTC-USD"),
        make_symbol("ETH-USD"),
        make_symbol("SOL-USD"),
        make_symbol("AVAX-USD"),
        make_symbol("MATIC-USD")
    };
    
    std::cout << "Initializing instruments:\n";
    for (const auto& sym : instruments) {
        engine.add_instrument(sym);
        std::cout << "  + " << symbol_view(sym) << "\n";
    }
    
    // Set up execution callback
    engine.set_execution_callback([](const ExecutionReport& report) {
        // In production, this would publish to message queue
        if (report.exec_type == ExecutionType::TRADE) {
            std::cout << "[TRADE] OrderID=" << report.order_id
                      << " Price=" << to_double_price(report.execution_price)
                      << " Qty=" << report.execution_quantity << "\n";
        }
    });
    
    // Create HTTP server
    constexpr std::uint16_t PORT = 8080;
    HttpServer server(PORT);
    
    // Set up REST routes
    auto& router = server.router();
    
    // Health check
    router.get("/health", [](const HttpRequest&) {
        return HttpResponse(HttpStatus::OK).json(R"({"status":"healthy"})");
    });
    
    // Get order book depth
    router.get("/api/v1/depth/:symbol", [&engine](const HttpRequest& req) {
        auto symbol_param = req.get_path_param("symbol");
        if (!symbol_param) {
            return HttpResponse(HttpStatus::BAD_REQUEST)
                .json(json_response::error("Missing symbol", "INVALID_REQUEST"));
        }
        
        Symbol symbol = make_symbol(*symbol_param);
        auto* book = engine.get_book(symbol);
        if (!book) {
            return HttpResponse(HttpStatus::NOT_FOUND)
                .json(json_response::error("Symbol not found", "SYMBOL_NOT_FOUND"));
        }
        
        auto depth = book->get_depth(20);
        
        std::vector<std::pair<Price, Quantity>> bids, asks;
        for (const auto& level : depth.bids) {
            bids.emplace_back(level.price, level.quantity);
        }
        for (const auto& level : depth.asks) {
            asks.emplace_back(level.price, level.quantity);
        }
        
        return HttpResponse(HttpStatus::OK)
            .json(json_response::depth(bids, asks, *symbol_param));
    });
    
    // Get quote
    router.get("/api/v1/quote/:symbol", [&engine](const HttpRequest& req) {
        auto symbol_param = req.get_path_param("symbol");
        if (!symbol_param) {
            return HttpResponse(HttpStatus::BAD_REQUEST)
                .json(json_response::error("Missing symbol", "INVALID_REQUEST"));
        }
        
        Symbol symbol = make_symbol(*symbol_param);
        auto quote = engine.get_quote(symbol);
        if (!quote) {
            return HttpResponse(HttpStatus::NOT_FOUND)
                .json(json_response::error("No quote available", "NO_QUOTE"));
        }
        
        return HttpResponse(HttpStatus::OK)
            .json(json_response::quote(*quote, *symbol_param));
    });
    
    // Submit order
    router.post("/api/v1/order", [&engine](const HttpRequest& req) {
        auto order_req = parse_order_request(req.body);
        if (!order_req) {
            return HttpResponse(HttpStatus::BAD_REQUEST)
                .json(json_response::error("Invalid order request", "INVALID_ORDER"));
        }
        
        Symbol symbol = make_symbol(order_req->symbol);
        OrderId order_id = engine.submit_order(
            symbol,
            order_req->side,
            order_req->type,
            to_fixed_price(order_req->price),
            static_cast<Quantity>(order_req->quantity)
        );
        
        if (order_id == INVALID_ORDER_ID) {
            return HttpResponse(HttpStatus::BAD_REQUEST)
                .json(json_response::order_rejected("Order rejected"));
        }
        
        return HttpResponse(HttpStatus::CREATED)
            .json(json_response::order_accepted(order_id, order_req->symbol));
    });
    
    // Cancel order
    router.del("/api/v1/order/:symbol/:orderId", [&engine](const HttpRequest& req) {
        auto symbol_param = req.get_path_param("symbol");
        auto order_id_param = req.get_path_param("orderId");
        
        if (!symbol_param || !order_id_param) {
            return HttpResponse(HttpStatus::BAD_REQUEST)
                .json(json_response::error("Missing parameters", "INVALID_REQUEST"));
        }
        
        Symbol symbol = make_symbol(*symbol_param);
        OrderId order_id = std::stoull(std::string(*order_id_param));
        
        if (engine.cancel_order(symbol, order_id)) {
            return HttpResponse(HttpStatus::OK)
                .json(json_response::order_cancelled(order_id));
        } else {
            return HttpResponse(HttpStatus::NOT_FOUND)
                .json(json_response::error("Order not found", "ORDER_NOT_FOUND"));
        }
    });
    
    // Get engine stats
    router.get("/api/v1/stats", [&engine](const HttpRequest&) {
        const auto& stats = engine.stats();
        std::ostringstream ss;
        ss << R"({"ordersReceived":)" << stats.orders_received
           << R"(,"ordersMatched":)" << stats.orders_matched
           << R"(,"ordersCancelled":)" << stats.orders_cancelled
           << R"(,"ordersRejected":)" << stats.orders_rejected
           << R"(,"totalVolume":)" << stats.total_volume
           << "}";
        return HttpResponse(HttpStatus::OK).json(ss.str());
    });
    
    // Start server
    if (!server.start()) {
        std::cerr << "Failed to start HTTP server on port " << PORT << "\n";
        return 1;
    }
    
    std::cout << "\nMatching engine started on port " << PORT << "\n";
    std::cout << "Endpoints:\n";
    std::cout << "  GET  /health\n";
    std::cout << "  GET  /api/v1/depth/:symbol\n";
    std::cout << "  GET  /api/v1/quote/:symbol\n";
    std::cout << "  POST /api/v1/order\n";
    std::cout << "  DEL  /api/v1/order/:symbol/:orderId\n";
    std::cout << "  GET  /api/v1/stats\n";
    std::cout << "\nPress Ctrl+C to stop...\n\n";
    
    // Main loop
    while (g_running.load(std::memory_order_acquire)) {
        server.poll();
    }
    
    server.stop();
    
    // Print final stats
    const auto& stats = engine.stats();
    std::cout << "\nFinal Statistics:\n";
    std::cout << "  Orders Received:  " << stats.orders_received << "\n";
    std::cout << "  Orders Matched:   " << stats.orders_matched << "\n";
    std::cout << "  Orders Cancelled: " << stats.orders_cancelled << "\n";
    std::cout << "  Total Volume:     " << stats.total_volume << "\n";
    
    std::cout << "\nMatching engine shutdown complete.\n";
    return 0;
}

