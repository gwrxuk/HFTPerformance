/**
 * @file orderbook_benchmark.cpp
 * @brief Order book specific benchmarks
 */

#include <iostream>
#include <iomanip>
#include <random>
#include "core/types.hpp"
#include "core/timing.hpp"
#include "matching/order_book.hpp"

using namespace hft;

void run_orderbook_benchmarks() {
    std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    std::cout << "Order Book Detailed Benchmarks\n";
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    
    std::random_device rd;
    std::mt19937 gen(rd());
    
    // Test with varying book depths
    {
        std::cout << "\nOrder Book Latency vs Depth:\n";
        std::cout << "  Depth    | Add (ns) | Cancel (ns) | Match (ns)\n";
        std::cout << "  ---------+----------+-------------+-----------\n";
        
        for (int depth : {100, 1000, 10000, 50000}) {
            auto symbol = make_symbol("TEST");
            OrderBook book(symbol);
            OrderIdGenerator id_gen;
            
            Price base_price = to_fixed_price(1000.0);
            std::vector<OrderId> order_ids;
            
            // Populate book
            for (int i = 0; i < depth; ++i) {
                Order buy(id_gen.next(), Side::BUY, OrderType::LIMIT,
                         base_price - i * 100, 100);
                Order sell(id_gen.next(), Side::SELL, OrderType::LIMIT,
                          base_price + i * 100, 100);
                book.add_order(buy);
                book.add_order(sell);
                order_ids.push_back(buy.order_id);
                order_ids.push_back(sell.order_id);
            }
            
            // Measure add latency
            LatencyStats add_stats(1000);
            for (int i = 0; i < 1000; ++i) {
                Order order(id_gen.next(), Side::BUY, OrderType::LIMIT,
                           base_price - (depth + i) * 100, 100);
                auto start = now();
                book.add_order(order);
                auto elapsed = now() - start;
                add_stats.add_sample(elapsed);
                order_ids.push_back(order.order_id);
            }
            
            // Shuffle for random cancellation
            std::shuffle(order_ids.begin(), order_ids.end(), gen);
            
            // Measure cancel latency
            LatencyStats cancel_stats(1000);
            for (int i = 0; i < 1000 && !order_ids.empty(); ++i) {
                auto order_id = order_ids.back();
                order_ids.pop_back();
                auto start = now();
                book.cancel_order(order_id);
                auto elapsed = now() - start;
                cancel_stats.add_sample(elapsed);
            }
            
            // Repopulate for matching test
            book.clear();
            for (int i = 0; i < depth; ++i) {
                Order order(id_gen.next(), Side::BUY, OrderType::LIMIT,
                           base_price, 100);
                book.add_order(order);
            }
            
            // Measure match latency
            LatencyStats match_stats(1000);
            for (int i = 0; i < 1000; ++i) {
                Order order(id_gen.next(), Side::SELL, OrderType::LIMIT,
                           base_price, 10);
                auto start = now();
                book.add_order(order);
                auto elapsed = now() - start;
                match_stats.add_sample(elapsed);
            }
            
            std::cout << "  " << std::setw(7) << depth << " | "
                      << std::setw(8) << std::fixed << std::setprecision(0) 
                      << add_stats.mean() << " | "
                      << std::setw(11) << cancel_stats.mean() << " | "
                      << std::setw(9) << match_stats.mean() << "\n";
        }
    }
    
    // Test different price distributions
    {
        std::cout << "\nOrder Book Latency vs Price Distribution:\n";
        
        auto symbol = make_symbol("TEST");
        
        // Uniform distribution
        {
            OrderBook book(symbol);
            OrderIdGenerator id_gen;
            std::uniform_int_distribution<> price_dist(1, 10000);
            
            LatencyStats stats(10000);
            for (int i = 0; i < 10000; ++i) {
                Price price = to_fixed_price(price_dist(gen));
                Side side = (i % 2 == 0) ? Side::BUY : Side::SELL;
                Order order(id_gen.next(), side, OrderType::LIMIT, price, 100);
                auto start = now();
                book.add_order(order);
                auto elapsed = now() - start;
                stats.add_sample(elapsed);
            }
            std::cout << "  Uniform prices:     mean=" << std::fixed << std::setprecision(2)
                      << stats.mean() << "ns, p99=" << stats.percentile(99) << "ns\n";
        }
        
        // Normal distribution (clustered around mid)
        {
            OrderBook book(symbol);
            OrderIdGenerator id_gen;
            std::normal_distribution<> price_dist(5000, 100);
            
            LatencyStats stats(10000);
            for (int i = 0; i < 10000; ++i) {
                Price price = to_fixed_price(std::max(1.0, price_dist(gen)));
                Side side = (i % 2 == 0) ? Side::BUY : Side::SELL;
                Order order(id_gen.next(), side, OrderType::LIMIT, price, 100);
                auto start = now();
                book.add_order(order);
                auto elapsed = now() - start;
                stats.add_sample(elapsed);
            }
            std::cout << "  Normal prices:      mean=" << std::fixed << std::setprecision(2)
                      << stats.mean() << "ns, p99=" << stats.percentile(99) << "ns\n";
        }
        
        // Bimodal (simulating bid/ask clustering)
        {
            OrderBook book(symbol);
            OrderIdGenerator id_gen;
            std::normal_distribution<> bid_dist(4900, 50);
            std::normal_distribution<> ask_dist(5100, 50);
            
            LatencyStats stats(10000);
            for (int i = 0; i < 10000; ++i) {
                Side side = (i % 2 == 0) ? Side::BUY : Side::SELL;
                double price = side == Side::BUY ? bid_dist(gen) : ask_dist(gen);
                Order order(id_gen.next(), side, OrderType::LIMIT, 
                           to_fixed_price(std::max(1.0, price)), 100);
                auto start = now();
                book.add_order(order);
                auto elapsed = now() - start;
                stats.add_sample(elapsed);
            }
            std::cout << "  Bimodal prices:     mean=" << std::fixed << std::setprecision(2)
                      << stats.mean() << "ns, p99=" << stats.percentile(99) << "ns\n";
        }
    }
    
    // Burst testing
    {
        std::cout << "\nBurst Order Processing:\n";
        
        auto symbol = make_symbol("TEST");
        OrderBook book(symbol);
        OrderIdGenerator id_gen;
        
        Price base_price = to_fixed_price(1000.0);
        
        // Pre-populate
        for (int i = 0; i < 1000; ++i) {
            Order order(id_gen.next(), Side::BUY, OrderType::LIMIT,
                       base_price - i * 10, 1000);
            book.add_order(order);
        }
        
        // Burst of aggressive orders
        constexpr int BURST_SIZE = 10000;
        auto start = std::chrono::steady_clock::now();
        
        for (int i = 0; i < BURST_SIZE; ++i) {
            Order order(id_gen.next(), Side::SELL, OrderType::LIMIT,
                       base_price - 10000, 1);
            book.add_order(order);
        }
        
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration<double, std::nano>(end - start).count();
        
        auto stats = book.get_stats();
        std::cout << "  Burst size:     " << BURST_SIZE << " orders\n";
        std::cout << "  Total time:     " << std::fixed << std::setprecision(2)
                  << (duration / 1e6) << " ms\n";
        std::cout << "  Avg latency:    " << (duration / BURST_SIZE) << " ns/order\n";
        std::cout << "  Trades matched: " << stats.trades_matched << "\n";
        std::cout << "  Volume matched: " << stats.volume_matched << "\n";
    }
}

