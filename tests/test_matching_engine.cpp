/**
 * @file test_matching_engine.cpp
 * @brief Matching engine unit tests
 */

#include <iostream>
#include "matching/matching_engine.hpp"

using namespace hft;

#define ASSERT(cond) \
    if (!(cond)) throw std::runtime_error("Assertion failed: " #cond)

void run_matching_engine_tests() {
    std::cout << "\n=== Matching Engine Tests ===\n";
    
    // Test 1: Instrument management
    {
        std::cout << "  Instrument management... ";
        MatchingEngine engine;
        
        auto btc = make_symbol("BTC-USD");
        auto eth = make_symbol("ETH-USD");
        
        ASSERT(engine.add_instrument(btc));
        ASSERT(engine.add_instrument(eth));
        ASSERT(!engine.add_instrument(btc));  // Duplicate
        
        ASSERT(engine.get_book(btc) != nullptr);
        ASSERT(engine.get_book(eth) != nullptr);
        ASSERT(engine.get_book(make_symbol("INVALID")) == nullptr);
        
        auto instruments = engine.instruments();
        ASSERT(instruments.size() == 2);
        
        std::cout << "PASSED\n";
    }
    
    // Test 2: Order submission
    {
        std::cout << "  Order submission... ";
        MatchingEngine engine;
        auto symbol = make_symbol("BTC-USD");
        engine.add_instrument(symbol);
        
        auto order_id = engine.submit_order(
            symbol, Side::BUY, OrderType::LIMIT,
            to_fixed_price(50000.0), 100
        );
        
        ASSERT(order_id != INVALID_ORDER_ID);
        
        auto* book = engine.get_book(symbol);
        ASSERT(book->order_count() == 1);
        ASSERT(book->best_bid().value() == to_fixed_price(50000.0));
        
        std::cout << "PASSED\n";
    }
    
    // Test 3: Order matching
    {
        std::cout << "  Order matching... ";
        MatchingEngine engine;
        auto symbol = make_symbol("BTC-USD");
        engine.add_instrument(symbol);
        
        int fills = 0;
        engine.set_execution_callback([&fills](const ExecutionReport& report) {
            if (report.exec_type == ExecutionType::TRADE) {
                ++fills;
            }
        });
        
        // Submit resting order
        engine.submit_order(symbol, Side::BUY, OrderType::LIMIT,
                           to_fixed_price(50000.0), 100);
        
        // Submit crossing order
        engine.submit_order(symbol, Side::SELL, OrderType::LIMIT,
                           to_fixed_price(49000.0), 100);
        
        ASSERT(fills == 2);  // One per side
        
        auto stats = engine.stats();
        ASSERT(stats.orders_received == 2);
        
        std::cout << "PASSED\n";
    }
    
    // Test 4: Order cancellation
    {
        std::cout << "  Order cancellation... ";
        MatchingEngine engine;
        auto symbol = make_symbol("BTC-USD");
        engine.add_instrument(symbol);
        
        auto order_id = engine.submit_order(
            symbol, Side::BUY, OrderType::LIMIT,
            to_fixed_price(50000.0), 100
        );
        
        ASSERT(engine.cancel_order(symbol, order_id));
        ASSERT(!engine.cancel_order(symbol, order_id));  // Already cancelled
        
        auto* book = engine.get_book(symbol);
        ASSERT(book->order_count() == 0);
        
        std::cout << "PASSED\n";
    }
    
    // Test 5: Quote retrieval
    {
        std::cout << "  Quote retrieval... ";
        MatchingEngine engine;
        auto symbol = make_symbol("BTC-USD");
        engine.add_instrument(symbol);
        
        ASSERT(!engine.get_quote(symbol).has_value());
        
        engine.submit_order(symbol, Side::BUY, OrderType::LIMIT,
                           to_fixed_price(50000.0), 100);
        engine.submit_order(symbol, Side::SELL, OrderType::LIMIT,
                           to_fixed_price(50100.0), 200);
        
        auto quote = engine.get_quote(symbol);
        ASSERT(quote.has_value());
        ASSERT(quote->bid_price == to_fixed_price(50000.0));
        ASSERT(quote->ask_price == to_fixed_price(50100.0));
        
        std::cout << "PASSED\n";
    }
    
    // Test 6: Multiple instruments
    {
        std::cout << "  Multiple instruments... ";
        MatchingEngine engine;
        
        std::vector<Symbol> symbols = {
            make_symbol("BTC-USD"),
            make_symbol("ETH-USD"),
            make_symbol("SOL-USD")
        };
        
        for (const auto& sym : symbols) {
            engine.add_instrument(sym);
            engine.submit_order(sym, Side::BUY, OrderType::LIMIT,
                               to_fixed_price(100.0), 100);
            engine.submit_order(sym, Side::SELL, OrderType::LIMIT,
                               to_fixed_price(101.0), 100);
        }
        
        for (const auto& sym : symbols) {
            auto quote = engine.get_quote(sym);
            ASSERT(quote.has_value());
        }
        
        auto stats = engine.stats();
        ASSERT(stats.orders_received == 6);
        
        std::cout << "PASSED\n";
    }
    
    // Test 7: Statistics tracking
    {
        std::cout << "  Statistics tracking... ";
        MatchingEngine engine;
        auto symbol = make_symbol("BTC-USD");
        engine.add_instrument(symbol);
        
        engine.reset_stats();
        
        // Submit orders that will match
        for (int i = 0; i < 10; ++i) {
            engine.submit_order(symbol, Side::BUY, OrderType::LIMIT,
                               to_fixed_price(100.0), 10);
            engine.submit_order(symbol, Side::SELL, OrderType::LIMIT,
                               to_fixed_price(99.0), 10);
        }
        
        auto stats = engine.stats();
        ASSERT(stats.orders_received == 20);
        
        const auto& latency = engine.latency_stats();
        ASSERT(latency.count() == 20);
        
        std::cout << "PASSED\n";
    }
    
    std::cout << "  All matching engine tests passed!\n";
}

