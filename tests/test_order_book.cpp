/**
 * @file test_order_book.cpp
 * @brief Order book unit tests
 */

#include <iostream>
#include "matching/order_book.hpp"

using namespace hft;

#define ASSERT(cond) \
    if (!(cond)) throw std::runtime_error("Assertion failed: " #cond)

void run_order_book_tests() {
    std::cout << "\n=== Order Book Tests ===\n";
    
    // Test 1: Basic order addition
    {
        std::cout << "  Basic order addition... ";
        auto symbol = make_symbol("TEST");
        OrderBook book(symbol);
        
        Order buy(1, Side::BUY, OrderType::LIMIT, to_fixed_price(100.0), 10);
        Order sell(2, Side::SELL, OrderType::LIMIT, to_fixed_price(101.0), 10);
        
        book.add_order(buy);
        book.add_order(sell);
        
        ASSERT(book.order_count() == 2);
        ASSERT(book.best_bid().value() == to_fixed_price(100.0));
        ASSERT(book.best_ask().value() == to_fixed_price(101.0));
        
        std::cout << "PASSED\n";
    }
    
    // Test 2: Price-time priority
    {
        std::cout << "  Price-time priority... ";
        auto symbol = make_symbol("TEST");
        OrderBook book(symbol);
        
        // Add orders at different prices
        book.add_order(Order(1, Side::BUY, OrderType::LIMIT, to_fixed_price(99.0), 10));
        book.add_order(Order(2, Side::BUY, OrderType::LIMIT, to_fixed_price(100.0), 10));
        book.add_order(Order(3, Side::BUY, OrderType::LIMIT, to_fixed_price(100.0), 20));
        
        // Best bid should be highest price
        ASSERT(book.best_bid().value() == to_fixed_price(100.0));
        
        auto depth = book.get_depth(10);
        ASSERT(depth.bids.size() == 2);
        ASSERT(depth.bids[0].price == to_fixed_price(100.0));
        ASSERT(depth.bids[0].quantity == 30);  // 10 + 20
        
        std::cout << "PASSED\n";
    }
    
    // Test 3: Order matching
    {
        std::cout << "  Order matching... ";
        auto symbol = make_symbol("TEST");
        OrderBook book(symbol);
        
        int trades = 0;
        auto callback = [&trades](const ExecutionReport& report) {
            if (report.exec_type == ExecutionType::TRADE) {
                ++trades;
            }
        };
        
        // Resting bid
        book.add_order(Order(1, Side::BUY, OrderType::LIMIT, to_fixed_price(100.0), 10), callback);
        
        // Aggressive sell that crosses
        book.add_order(Order(2, Side::SELL, OrderType::LIMIT, to_fixed_price(99.0), 10), callback);
        
        // Should have matched
        ASSERT(trades == 2);  // One report per side
        ASSERT(book.order_count() == 0);  // Both fully filled
        
        auto stats = book.get_stats();
        ASSERT(stats.trades_matched == 1);
        ASSERT(stats.volume_matched == 10);
        
        std::cout << "PASSED\n";
    }
    
    // Test 4: Partial fills
    {
        std::cout << "  Partial fills... ";
        auto symbol = make_symbol("TEST");
        OrderBook book(symbol);
        
        // Large resting order
        book.add_order(Order(1, Side::BUY, OrderType::LIMIT, to_fixed_price(100.0), 100));
        
        // Small aggressive order
        book.add_order(Order(2, Side::SELL, OrderType::LIMIT, to_fixed_price(99.0), 30));
        
        // Resting order should be partially filled
        ASSERT(book.order_count() == 1);
        
        auto depth = book.get_depth();
        ASSERT(depth.bids.size() == 1);
        ASSERT(depth.bids[0].quantity == 70);  // 100 - 30
        
        std::cout << "PASSED\n";
    }
    
    // Test 5: Order cancellation
    {
        std::cout << "  Order cancellation... ";
        auto symbol = make_symbol("TEST");
        OrderBook book(symbol);
        
        book.add_order(Order(1, Side::BUY, OrderType::LIMIT, to_fixed_price(100.0), 10));
        book.add_order(Order(2, Side::BUY, OrderType::LIMIT, to_fixed_price(99.0), 20));
        
        ASSERT(book.order_count() == 2);
        
        ASSERT(book.cancel_order(1));
        ASSERT(book.order_count() == 1);
        
        ASSERT(!book.cancel_order(1));  // Already cancelled
        ASSERT(!book.cancel_order(999));  // Never existed
        
        std::cout << "PASSED\n";
    }
    
    // Test 6: Quote retrieval
    {
        std::cout << "  Quote retrieval... ";
        auto symbol = make_symbol("TEST");
        OrderBook book(symbol);
        
        ASSERT(!book.get_quote().has_value());  // Empty book
        
        book.add_order(Order(1, Side::BUY, OrderType::LIMIT, to_fixed_price(100.0), 10));
        ASSERT(!book.get_quote().has_value());  // Only bid
        
        book.add_order(Order(2, Side::SELL, OrderType::LIMIT, to_fixed_price(101.0), 20));
        
        auto quote = book.get_quote();
        ASSERT(quote.has_value());
        ASSERT(quote->bid_price == to_fixed_price(100.0));
        ASSERT(quote->ask_price == to_fixed_price(101.0));
        ASSERT(quote->bid_quantity == 10);
        ASSERT(quote->ask_quantity == 20);
        ASSERT(quote->spread() == to_fixed_price(1.0));
        
        std::cout << "PASSED\n";
    }
    
    // Test 7: Multiple price levels
    {
        std::cout << "  Multiple price levels... ";
        auto symbol = make_symbol("TEST");
        OrderBook book(symbol);
        
        // Add bids at multiple levels
        for (int i = 0; i < 10; ++i) {
            book.add_order(Order(i + 1, Side::BUY, OrderType::LIMIT, 
                                to_fixed_price(100.0 - i), 10 * (i + 1)));
        }
        
        auto stats = book.get_stats();
        ASSERT(stats.bid_levels == 10);
        
        auto depth = book.get_depth(5);
        ASSERT(depth.bids.size() == 5);
        ASSERT(depth.bids[0].price == to_fixed_price(100.0));
        ASSERT(depth.bids[4].price == to_fixed_price(96.0));
        
        std::cout << "PASSED\n";
    }
    
    std::cout << "  All order book tests passed!\n";
}

