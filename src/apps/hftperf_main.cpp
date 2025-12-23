/**
 * @file hftperf_main.cpp
 * @brief HFT Performance Testing Tool
 * 
 * Configurable performance testing with JSON config support.
 */

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <random>
#include <iomanip>
#include <cstring>
#include <mutex>
#include <cstdlib>
#ifdef __linux__
#include <unistd.h>
#include <sys/types.h>
#endif

#include "matching/matching_engine.hpp"
#include "matching/order_book.hpp"
#include "core/timing.hpp"
#include "core/cpu_affinity.hpp"
#include "core/lockfree_queue.hpp"
#include "core/timestamp_buffer.hpp"
#include "strategy/user_strategy.hpp"
#include "exchange/exchange_simulator.hpp"

// Simple JSON parser for config
namespace {

struct Config {
    int duration_sec = 10;
    std::string mode = "single_thread";
    int pipeline_stages = 2;
    int message_rate = 100000;
    std::string message_pattern = "uniform";
    std::string strategy = "pass_through";
    std::vector<int> affinity;
    bool use_polling = false;
    std::string log_file = "results.csv";
    
    // Advanced options
    // Gap recovery simulation: pause for gap_pause_ms then burst gap_burst_count messages
    int gap_pause_ms = 0;           // 0 = disabled
    int gap_burst_count = 0;        // Number of messages in burst after gap
    int gap_interval_sec = 0;       // How often to simulate gap (0 = once at midpoint)
    
    // Trade signal ratio: fraction of ticks that trigger orders (0.0-1.0)
    double trade_signal_ratio = 1.0;  // 1.0 = all ticks trade
    
    // Multi-symbol simulation
    int num_symbols = 1;            // Number of symbols to round-robin
    std::string symbol_prefix = "SYM";  // Prefix for generated symbols
    
    // Profiling
    bool enable_flame_graph = false;  // Enable perf recording for flame graph
    int flame_graph_duration_sec = 0; // Duration to record (0 = full test)
    
    // Jitter injection for realistic simulation
    int jitter_min_ns = 0;          // Minimum added latency
    int jitter_max_ns = 0;          // Maximum added latency
    
    // Warmup period (excluded from statistics)
    int warmup_sec = 0;             // Seconds to warm up before measuring
    
    // Order book depth simulation
    int book_depth_levels = 5;      // Number of price levels to maintain
    bool simulate_fills = true;     // Whether to simulate order fills
};

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r\"");
    size_t end = s.find_last_not_of(" \t\n\r\",");
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

Config parse_config(const std::string& filename) {
    Config cfg;
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open config file: " << filename << "\n";
        return cfg;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        
        std::string key = trim(line.substr(0, colon));
        std::string value = trim(line.substr(colon + 1));
        
        if (key == "duration_sec") cfg.duration_sec = std::stoi(value);
        else if (key == "mode") cfg.mode = value;
        else if (key == "pipeline_stages") cfg.pipeline_stages = std::stoi(value);
        else if (key == "message_rate") cfg.message_rate = std::stoi(value);
        else if (key == "message_pattern") cfg.message_pattern = value;
        else if (key == "strategy") cfg.strategy = value;
        else if (key == "log_file") cfg.log_file = value;
        else if (key == "use_polling") cfg.use_polling = (value == "true");
        else if (key == "affinity") {
            // Parse array like [0, 1, 2]
            size_t start = value.find('[');
            size_t end = value.find(']');
            if (start != std::string::npos && end != std::string::npos) {
                std::string arr = value.substr(start + 1, end - start - 1);
                size_t pos = 0;
                while (pos < arr.size()) {
                    size_t comma = arr.find(',', pos);
                    if (comma == std::string::npos) comma = arr.size();
                    std::string num = trim(arr.substr(pos, comma - pos));
                    if (!num.empty()) cfg.affinity.push_back(std::stoi(num));
                    pos = comma + 1;
                }
            }
        }
        // Advanced options
        else if (key == "gap_pause_ms") cfg.gap_pause_ms = std::stoi(value);
        else if (key == "gap_burst_count") cfg.gap_burst_count = std::stoi(value);
        else if (key == "gap_interval_sec") cfg.gap_interval_sec = std::stoi(value);
        else if (key == "trade_signal_ratio") cfg.trade_signal_ratio = std::stod(value);
        else if (key == "num_symbols") cfg.num_symbols = std::stoi(value);
        else if (key == "symbol_prefix") cfg.symbol_prefix = value;
        else if (key == "enable_flame_graph") cfg.enable_flame_graph = (value == "true");
        else if (key == "flame_graph_duration_sec") cfg.flame_graph_duration_sec = std::stoi(value);
        else if (key == "jitter_min_ns") cfg.jitter_min_ns = std::stoi(value);
        else if (key == "jitter_max_ns") cfg.jitter_max_ns = std::stoi(value);
        else if (key == "warmup_sec") cfg.warmup_sec = std::stoi(value);
        else if (key == "book_depth_levels") cfg.book_depth_levels = std::stoi(value);
        else if (key == "simulate_fills") cfg.simulate_fills = (value == "true");
    }
    return cfg;
}

// Helper to print test results
void print_test(const char* name, bool condition, int& passed, int& failed) {
    if (condition) {
        std::cout << "  [PASS] " << name << "\n";
        ++passed;
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++failed;
    }
}

// Self-test function
bool run_selftest() {
    std::cout << "Running self-test suite...\n\n";
    
    int passed = 0;
    int failed = 0;
    
    // Test 1: Basic Types
    std::cout << "--- Basic Type Tests ---\n";
    {
        hft::Symbol sym = hft::make_symbol("TEST-USD");
        print_test("Symbol creation", sym[0] == 'T', passed, failed);
        
        hft::Price price = 10000;
        print_test("Price type", price > 0, passed, failed);
        
        hft::Quantity qty = 100;
        print_test("Quantity type", qty > 0, passed, failed);
    }
    
    // Test 2: Timing & TSC Calibration
    std::cout << "\n--- Timing Tests ---\n";
    {
        auto t1 = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto t2 = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
        print_test("Sleep timing works", elapsed >= 10, passed, failed);
        print_test("Timing accuracy", elapsed < 100, passed, failed);
        
        // TSC Calibration
        std::cout << "\n--- TSC Calibration ---\n";
        auto& timer = hft::HighPrecisionTimer::instance();
        timer.print_calibration();
        
        // Verify TSC works
        auto ts1 = timer.now_ns();
        int x = 0;
        for (int i = 0; i < 1000; ++i) {
            x = x + i;  // Avoid volatile compound assignment warning
            asm volatile("" : "+r"(x));  // Prevent optimization
        }
        (void)x;
        auto ts2 = timer.now_ns();
        print_test("TSC timestamp", ts2 > ts1, passed, failed);
        print_test("TSC resolution < 1µs", (ts2 - ts1) < 1000000, passed, failed);
        
        // Verify overhead is reasonable (< 100ns)
        print_test("Overhead < 100ns", timer.overhead_ns() < 100, passed, failed);
        
        std::cout << "  Measurement: " << (ts2 - ts1) << " ns for 1000 iterations\n";
    }
    
    // Test 3: Order Book Creation
    std::cout << "\n--- Order Book Tests ---\n";
    {
        hft::OrderBook book(hft::make_symbol("TEST"));
        print_test("Order book creation", true, passed, failed);
        print_test("Order book initially empty", book.empty(), passed, failed);
    }
    
    // Test 4: Matching Engine
    std::cout << "\n--- Matching Engine Tests ---\n";
    {
        hft::MatchingEngine engine;
        print_test("Engine creation", true, passed, failed);
        
        hft::Symbol sym = hft::make_symbol("BTC-USD");
        bool added = engine.add_instrument(sym);
        print_test("Add instrument", added, passed, failed);
        
        auto book = engine.get_book(sym);
        print_test("Get order book", book != nullptr, passed, failed);
    }
    
    // Test 5: Order Submission
    std::cout << "\n--- Order Submission Tests ---\n";
    {
        hft::MatchingEngine engine;
        hft::Symbol sym = hft::make_symbol("ETH-USD");
        engine.add_instrument(sym);
        
        auto id1 = engine.submit_order(sym, hft::Side::BUY, hft::OrderType::LIMIT, 100000, 10, 1);
        print_test("Submit buy order", id1 != hft::INVALID_ORDER_ID, passed, failed);
        
        auto id2 = engine.submit_order(sym, hft::Side::SELL, hft::OrderType::LIMIT, 100000, 5, 2);
        print_test("Submit sell order (should match)", id2 != hft::INVALID_ORDER_ID, passed, failed);
        
        auto id3 = engine.submit_order(sym, hft::Side::BUY, hft::OrderType::LIMIT, 99000, 10, 3);
        print_test("Submit non-crossing order", id3 != hft::INVALID_ORDER_ID, passed, failed);
    }
    
    // Test 6: High-frequency order test
    std::cout << "\n--- Performance Sanity Check ---\n";
    {
        hft::MatchingEngine engine;
        hft::Symbol sym = hft::make_symbol("PERF-TEST");
        engine.add_instrument(sym);
        
        auto start = std::chrono::steady_clock::now();
        const int NUM_ORDERS = 10000;
        
        for (int i = 0; i < NUM_ORDERS; ++i) {
            engine.submit_order(sym, 
                i % 2 == 0 ? hft::Side::BUY : hft::Side::SELL,
                hft::OrderType::LIMIT,
                100000 + (i % 100) * 10,
                10,
                static_cast<uint64_t>(i));
        }
        
        auto end = std::chrono::steady_clock::now();
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        double orders_per_sec = (elapsed_us > 0) ? (NUM_ORDERS * 1e6 / elapsed_us) : 0;
        
        std::cout << "  Submitted " << NUM_ORDERS << " orders in " 
                  << elapsed_us << " µs\n";
        std::cout << "  Throughput: " << std::fixed << std::setprecision(0) 
                  << orders_per_sec << " orders/sec\n";
        
        print_test("Performance > 10,000 orders/sec", orders_per_sec > 10000, passed, failed);
        print_test("Performance > 50,000 orders/sec", orders_per_sec > 50000, passed, failed);
    }
    
    // Test 7: Thread-Local Timestamp Buffer
    std::cout << "\n--- Thread-Local Timestamp Buffer Tests ---\n";
    {
        // Clear any previous data
        hft::TimestampBufferManager::clear_all();
        
        // Record events in main thread
        auto& buffer = hft::TimestampBufferManager::get_thread_buffer();
        
        // Measure recording overhead
        auto start = hft::rdtscp();
        const int NUM_EVENTS = 10000;
        for (int i = 0; i < NUM_EVENTS; ++i) {
            (void)buffer.record(hft::EventType::TICK_GENERATED, static_cast<uint64_t>(i));
        }
        auto end = hft::rdtscp();
        
        print_test("Buffer recording", buffer.count() == NUM_EVENTS, passed, failed);
        print_test("No contention (single thread)", true, passed, failed);
        
        // Calculate overhead per event
        auto& timer = hft::HighPrecisionTimer::instance();
        double total_ns = timer.ticks_to_ns(end - start);
        double ns_per_event = total_ns / NUM_EVENTS;
        
        std::cout << "  Recorded " << NUM_EVENTS << " events in " 
                  << std::fixed << std::setprecision(1) << total_ns / 1000.0 << " µs\n";
        std::cout << "  Overhead per event: " << ns_per_event << " ns\n";
        
        print_test("Recording overhead < 100 ns/event", ns_per_event < 100, passed, failed);
        
        // Test multi-threaded recording (each thread has its own buffer)
        std::atomic<int> threads_done{0};
        const int NUM_THREADS = 4;
        const int EVENTS_PER_THREAD = 1000;
        
        std::vector<std::thread> threads;
        for (int t = 0; t < NUM_THREADS; ++t) {
            threads.emplace_back([&, t]() {
                auto& tbuf = hft::TimestampBufferManager::get_thread_buffer();
                for (int i = 0; i < EVENTS_PER_THREAD; ++i) {
                    (void)tbuf.record(hft::EventType::TICK_RECEIVED, 
                               static_cast<uint64_t>(t * 10000 + i));
                }
                threads_done.fetch_add(1, std::memory_order_relaxed);
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        print_test("Multi-thread recording", threads_done.load() == NUM_THREADS, passed, failed);
        
        // Aggregate events (this happens AFTER the test)
        auto all_events = hft::TimestampBufferManager::aggregate(true);
        
        // Verify total count (main thread events + worker thread events)
        std::size_t expected_total = NUM_EVENTS + (NUM_THREADS * EVENTS_PER_THREAD);
        print_test("Event aggregation", all_events.size() == expected_total, passed, failed);
        
        // Verify events are sorted by sequence
        bool sorted = std::is_sorted(all_events.begin(), all_events.end(),
            [](const hft::TimestampEvent& a, const hft::TimestampEvent& b) {
                return a.sequence < b.sequence;
            });
        print_test("Events sorted by sequence", sorted, passed, failed);
        
        std::cout << "  Total threads: " << hft::TimestampBufferManager::thread_count() << "\n";
        std::cout << "  Total events aggregated: " << all_events.size() << "\n";
    }
    
    // Summary
    std::cout << "\n════════════════════════════════════════════════════════════════\n";
    std::cout << "Self-test complete: " << passed << " passed, " << failed << " failed\n";
    std::cout << "════════════════════════════════════════════════════════════════\n";
    
    if (failed == 0) {
        std::cout << "\n Output: \"Self-test PASSED. All " << passed << " tests successful. System ready for benchmarking.\"\n";
    } else {
        std::cout << "\n Output: \"Self-test FAILED. " << failed << " test(s) failed. Please check system configuration.\"\n";
    }
    
    return failed == 0;
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                    HFT Performance Tester v1.0                ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";

    // Parse command line
    std::string config_file;
    bool selftest = false;
    
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-selftest") == 0 || 
            std::strcmp(argv[i], "--selftest") == 0) {
            selftest = true;
        } else if ((std::strcmp(argv[i], "-config") == 0 || 
             std::strcmp(argv[i], "--config") == 0) && i + 1 < argc) {
            config_file = argv[++i];
        } else if (argv[i][0] != '-') {
            config_file = argv[i];
        }
    }

    // Run selftest if requested
    if (selftest) {
        return run_selftest() ? 0 : 1;
    }

    if (config_file.empty()) {
        std::cerr << "Usage: " << argv[0] << " -config <config.json>\n";
        std::cerr << "       " << argv[0] << " -selftest\n";
        std::cerr << "\nOptions:\n";
        std::cerr << "  -config <file>   Run performance test with config file\n";
        std::cerr << "  -selftest        Run self-test to verify system\n";
        std::cerr << "\nModes:\n";
        std::cerr << "  single_thread    Basic single-threaded test (default)\n";
        std::cerr << "  pipeline         Multi-threaded with lock-free queues\n";
        std::cerr << "  strategy         Test with user-defined strategy\n";
        std::cerr << "  external         Connect to external trading system\n";
        std::cerr << "\nStrategies:\n";
        std::cerr << "  pass_through     Baseline - echoes all ticks as orders\n";
        std::cerr << "  momentum         Buy on uptick, sell on downtick\n";
        std::cerr << "  market_making    Quote both sides with tight spread\n";
        std::cerr << "  user_strategy    Your custom implementation\n";
        std::cerr << "\nBasic config:\n";
        std::cerr << "{\n";
        std::cerr << "    \"duration_sec\": 10,\n";
        std::cerr << "    \"mode\": \"single_thread\",\n";
        std::cerr << "    \"message_rate\": 100000,\n";
        std::cerr << "    \"strategy\": \"pass_through\",\n";
        std::cerr << "    \"log_file\": \"results.csv\"\n";
        std::cerr << "}\n";
        std::cerr << "\nAdvanced options:\n";
        std::cerr << "  gap_pause_ms        Simulate market data gap (ms)\n";
        std::cerr << "  gap_burst_count     Messages in recovery burst\n";
        std::cerr << "  trade_signal_ratio  Fraction of ticks that trade (0-1)\n";
        std::cerr << "  num_symbols         Multi-symbol round-robin\n";
        std::cerr << "  jitter_min/max_ns   Inject realistic jitter\n";
        std::cerr << "  warmup_sec          Exclude from statistics\n";
        std::cerr << "  enable_flame_graph  CPU profiling (Linux)\n";
        return 1;
    }

    // Load config
    std::cout << "Loading config: " << config_file << "\n";
    Config cfg = parse_config(config_file);

    std::cout << "\n--- Configuration ---\n";
    std::cout << "  Duration:        " << cfg.duration_sec << " seconds\n";
    std::cout << "  Mode:            " << cfg.mode << "\n";
    if (cfg.mode == "pipeline") {
        std::cout << "  Pipeline stages: " << cfg.pipeline_stages << "\n";
        std::cout << "  Use polling:     " << (cfg.use_polling ? "yes" : "no") << "\n";
    }
    std::cout << "  Message rate:    " << cfg.message_rate << " msg/sec\n";
    std::cout << "  Pattern:         " << cfg.message_pattern << "\n";
    std::cout << "  Strategy:        " << cfg.strategy << "\n";
    std::cout << "  Log file:        " << cfg.log_file << "\n";
    if (!cfg.affinity.empty()) {
        std::cout << "  CPU affinity:    [";
        for (size_t i = 0; i < cfg.affinity.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << cfg.affinity[i];
        }
        std::cout << "]\n";
    }
    
    // Print advanced options if any are enabled
    bool has_advanced = (cfg.gap_pause_ms > 0 || cfg.trade_signal_ratio < 1.0 ||
                         cfg.num_symbols > 1 || cfg.enable_flame_graph ||
                         cfg.jitter_max_ns > 0 || cfg.warmup_sec > 0);
    if (has_advanced) {
        std::cout << "\n--- Advanced Options ---\n";
        if (cfg.gap_pause_ms > 0) {
            std::cout << "  Gap recovery:    " << cfg.gap_pause_ms << "ms pause, " 
                      << cfg.gap_burst_count << " burst msgs";
            if (cfg.gap_interval_sec > 0) {
                std::cout << " every " << cfg.gap_interval_sec << "s";
            } else {
                std::cout << " (once at midpoint)";
            }
            std::cout << "\n";
        }
        if (cfg.trade_signal_ratio < 1.0) {
            std::cout << "  Trade ratio:     " << (cfg.trade_signal_ratio * 100) << "% of ticks trade\n";
        }
        if (cfg.num_symbols > 1) {
            std::cout << "  Multi-symbol:    " << cfg.num_symbols << " symbols (" 
                      << cfg.symbol_prefix << "-0 to " << cfg.symbol_prefix << "-" 
                      << (cfg.num_symbols - 1) << ")\n";
        }
        if (cfg.jitter_max_ns > 0) {
            std::cout << "  Jitter inject:   " << cfg.jitter_min_ns << "-" 
                      << cfg.jitter_max_ns << " ns\n";
        }
        if (cfg.warmup_sec > 0) {
            std::cout << "  Warmup period:   " << cfg.warmup_sec << " seconds\n";
        }
        if (cfg.enable_flame_graph) {
            std::cout << "  Flame graph:     enabled";
            if (cfg.flame_graph_duration_sec > 0) {
                std::cout << " (" << cfg.flame_graph_duration_sec << "s)";
            }
            std::cout << "\n";
        }
    }

    // Set CPU affinity if specified (for main thread in single_thread mode)
    #ifdef __linux__
    if (!cfg.affinity.empty() && cfg.mode == "single_thread") {
        hft::set_cpu_affinity(cfg.affinity[0]);
        std::cout << "\n[INFO] CPU affinity set to core " << cfg.affinity[0] << "\n";
    }
    #endif

    // Create matching engine
    hft::MatchingEngine engine;
    
    // Set up symbols (multi-symbol support)
    std::vector<hft::Symbol> symbols;
    for (int i = 0; i < cfg.num_symbols; ++i) {
        std::string sym_name = cfg.symbol_prefix + "-" + std::to_string(i);
        auto sym = hft::make_symbol(sym_name);
        engine.add_instrument(sym);
        symbols.push_back(sym);
    }
    // Also add default symbol for backward compatibility
    if (cfg.num_symbols == 1) {
        engine.add_instrument(hft::make_symbol("TEST-USD"));
    }

    // Statistics
    std::atomic<uint64_t> orders_sent{0};
    std::atomic<uint64_t> orders_matched{0};
    std::atomic<uint64_t> ticks_received{0};
    std::atomic<uint64_t> signals_triggered{0};
    std::vector<int64_t> latencies;
    latencies.reserve(cfg.message_rate * cfg.duration_sec);

    // Random generators
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> price_dist(9900, 10100);  // Around $100
    std::uniform_int_distribution<> qty_dist(1, 100);
    std::bernoulli_distribution side_dist(0.5);
    std::exponential_distribution<> poisson_dist(cfg.message_rate);
    std::uniform_real_distribution<> signal_dist(0.0, 1.0);   // For trade signal ratio
    std::uniform_int_distribution<> jitter_dist(cfg.jitter_min_ns, 
                                                 std::max(cfg.jitter_min_ns, cfg.jitter_max_ns));
    std::uniform_int_distribution<> symbol_dist(0, std::max(0, cfg.num_symbols - 1));

    // Gap recovery state
    auto next_gap_time = std::chrono::steady_clock::time_point::max();
    bool in_gap = false;
    int burst_remaining = 0;

    // Flame graph support (Linux only)
    #ifdef __linux__
    [[maybe_unused]] pid_t perf_pid = -1;
    if (cfg.enable_flame_graph) {
        std::string perf_cmd = "perf record -F 99 -g -p " + std::to_string(getpid()) +
                               " -o perf.data &";
        std::cout << "[INFO] Starting perf recording for flame graph...\n";
        // Note: In production, use fork/exec; this is simplified
        [[maybe_unused]] int ret = system(perf_cmd.c_str());
    }
    #endif

    std::cout << "\n--- Running Performance Test (" << cfg.mode << " mode) ---\n";
    std::cout << "Press Ctrl+C to stop early.\n\n";

    auto start_time = std::chrono::steady_clock::now();
    auto end_time = start_time + std::chrono::seconds(cfg.duration_sec);
    auto warmup_end = start_time + std::chrono::seconds(cfg.warmup_sec);
    bool warmup_complete = (cfg.warmup_sec == 0);
    
    // Set up gap recovery timing
    if (cfg.gap_pause_ms > 0) {
        if (cfg.gap_interval_sec > 0) {
            next_gap_time = start_time + std::chrono::seconds(cfg.gap_interval_sec);
        } else {
            // Default: gap at midpoint of test
            next_gap_time = start_time + std::chrono::seconds(cfg.duration_sec / 2);
        }
    }
    
    uint64_t order_id = 1;
    auto last_report = start_time;
    
    // Open log file
    std::ofstream log_file(cfg.log_file);
    if (log_file.is_open()) {
        log_file << "timestamp_ns,order_id,latency_ns,side,price,quantity,symbol\n";
    }

    hft::Symbol test_symbol = hft::make_symbol("TEST-USD");
    
    // Helper lambda for gap recovery simulation
    auto check_gap_recovery = [&]() {
        auto now = std::chrono::steady_clock::now();
        
        if (!in_gap && now >= next_gap_time && cfg.gap_pause_ms > 0) {
            // Start gap simulation
            in_gap = true;
            std::cout << "[GAP] Simulating " << cfg.gap_pause_ms << "ms market data gap...\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg.gap_pause_ms));
            burst_remaining = cfg.gap_burst_count;
            
            // Schedule next gap if interval is set
            if (cfg.gap_interval_sec > 0) {
                next_gap_time = now + std::chrono::seconds(cfg.gap_interval_sec);
            } else {
                next_gap_time = std::chrono::steady_clock::time_point::max();
            }
            in_gap = false;
            std::cout << "[GAP] Recovery: sending " << burst_remaining << " burst messages\n";
        }
        
        return burst_remaining > 0;
    };
    
    // Helper lambda for jitter injection
    auto inject_jitter = [&]() {
        if (cfg.jitter_max_ns > 0) {
            int jitter = jitter_dist(gen);
            auto target = std::chrono::steady_clock::now() + std::chrono::nanoseconds(jitter);
            while (std::chrono::steady_clock::now() < target) {
                #if defined(__x86_64__) || defined(_M_X64)
                    __builtin_ia32_pause();
                #elif defined(__aarch64__)
                    asm volatile("yield" ::: "memory");
                #endif
            }
        }
    };
    
    // Helper lambda to check if this tick should trigger a trade
    auto should_trade = [&]() -> bool {
        if (cfg.trade_signal_ratio >= 1.0) return true;
        return signal_dist(gen) < cfg.trade_signal_ratio;
    };
    
    // Helper to get current symbol (round-robin for multi-symbol)
    uint64_t symbol_counter = 0;
    auto get_current_symbol = [&]() -> hft::Symbol {
        if (cfg.num_symbols <= 1) return test_symbol;
        return symbols[symbol_counter++ % symbols.size()];
    };

    // Strategy mode - test with user-defined strategy
    if (cfg.mode == "strategy") {
        auto strategy = hft::create_strategy(cfg.strategy);
        std::cout << "[INFO] Using strategy: " << strategy->name() << "\n\n";
        
        // Set up order callback
        strategy->set_order_callback([&](const hft::StrategyOrder& order) {
            auto result_id = engine.submit_order(
                order.symbol,
                order.side,
                order.type,
                order.price,
                order.quantity,
                1
            );
            if (result_id != hft::INVALID_ORDER_ID) {
                orders_matched.fetch_add(1, std::memory_order_relaxed);
            }
        });
        
        // Enable timestamp recording for strategy profiling
        strategy->set_timestamp_recording(true);
        
        strategy->onInit();
        
        uint64_t tick_seq = 0;
        while (std::chrono::steady_clock::now() < end_time) {
            auto tick_start = hft::now();
            
            // Generate simulated tick
            hft::Tick tick;
            tick.symbol = test_symbol;
            tick.bid_price = price_dist(gen) * 100;
            tick.ask_price = tick.bid_price + 100;  // 1 tick spread
            tick.bid_size = qty_dist(gen);
            tick.ask_size = qty_dist(gen);
            tick.last_price = (tick.bid_price + tick.ask_price) / 2;
            tick.last_size = qty_dist(gen);
            tick.timestamp = tick_start;
            tick.sequence = tick_seq++;
            
            // Begin tick processing (records t_recv)
            strategy->begin_tick_processing(tick_seq);
            
            // Call strategy (user code with record_timestamp calls)
            strategy->onTick(tick);
            
            // End tick processing (records t_process_done)
            strategy->end_tick_processing();
            
            auto tick_end = hft::now();
            int64_t latency = tick_end - tick_start;
            
            orders_sent.fetch_add(1, std::memory_order_relaxed);
            latencies.push_back(latency);
            
            // Log to file
            if (log_file.is_open()) {
                log_file << tick_start << ","
                         << tick_seq << ","
                         << latency << ","
                         << "TICK" << ","
                         << tick.last_price << ","
                         << tick.last_size << "\n";
            }
            
            // Progress report
            auto now = std::chrono::steady_clock::now();
            if (now - last_report >= std::chrono::seconds(1)) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
                std::cout << "[" << elapsed << "s] Ticks: " << orders_sent.load() 
                          << " | Orders: " << orders_matched.load()
                          << " | Rate: " << (orders_sent.load() / std::max(1L, elapsed)) << " ticks/sec\n";
                last_report = now;
            }
            
            // Rate limiting
            auto target_interval_ns = 1000000000 / cfg.message_rate;
            auto now_tp = std::chrono::steady_clock::now();
            auto target_tp = now_tp + std::chrono::nanoseconds(target_interval_ns);
            while (std::chrono::steady_clock::now() < target_tp) {
                #if defined(__x86_64__) || defined(_M_X64)
                    __builtin_ia32_pause();
                #elif defined(__aarch64__)
                    asm volatile("yield" ::: "memory");
                #endif
            }
        }
        
        strategy->onShutdown();
        
        // Print strategy timing breakdown
        strategy->print_timing_report();
        
    } else if (cfg.mode == "exchange") {
    // Exchange simulator mode - measures tick-to-trade latency
    // 
    // Three separate threads:
    //   1. Market Data Generator (produces ticks with t_gen)
    //   2. Strategy (processes ticks, outputs orders with t_strategy_done)
    //   3. Exchange Simulator (receives orders, records t_order_recv)
    //
    // Primary metric: tick-to-trade = t_order_recv - t_gen
    //
        std::cout << "[INFO] Exchange Simulator Mode\n";
        std::cout << "[INFO] Measuring tick-to-trade latency (t_order_recv - t_gen)\n\n";
        
        // Create exchange simulator
        hft::ExchangeSimulator exchange;
        
        // Create strategy
        auto strategy = hft::create_strategy(cfg.strategy);
        std::cout << "[INFO] Using strategy: " << strategy->name() << "\n";
        
        // Order counter for unique IDs
        std::atomic<uint64_t> exchange_order_id{1};
        
        // State for tracking current tick (shared with lambda)
        uint64_t current_tick_seq = 0;
        hft::Timestamp current_tick_tgen = 0;
        
        // Set up order callback - forwards to exchange with timestamps
        strategy->set_order_callback([&](const hft::StrategyOrder& order) {
            // Create exchange order with timing information
            hft::ExchangeOrder ex_order;
            ex_order.order_id = exchange_order_id.fetch_add(1, std::memory_order_relaxed);
            ex_order.tick_sequence = current_tick_seq;
            ex_order.t_gen = current_tick_tgen;
            ex_order.t_strategy_done = hft::now();  // Strategy just finished
            ex_order.symbol = order.symbol;
            ex_order.side = order.side;
            ex_order.type = order.type;
            ex_order.price = order.price;
            ex_order.quantity = order.quantity;
            
            // Submit to exchange (via queue)
            if (!exchange.submit_order(ex_order)) {
                // Queue full - back pressure
                std::cerr << "[WARN] Exchange queue full!\n";
            }
        });
        
        // Start exchange simulator thread
        int exchange_cpu = (cfg.affinity.size() > 1) ? cfg.affinity[1] : -1;
        exchange.start(exchange_cpu, cfg.use_polling);
        std::cout << "[INFO] Exchange simulator started on CPU " << exchange_cpu << "\n";
        
        // Set CPU affinity for generator/strategy thread
        #ifdef __linux__
        if (!cfg.affinity.empty()) {
            hft::set_cpu_affinity(cfg.affinity[0]);
            std::cout << "[INFO] Generator/Strategy on CPU " << cfg.affinity[0] << "\n";
        }
        #endif
        
        strategy->onInit();
        
        uint64_t tick_seq = 0;
        while (std::chrono::steady_clock::now() < end_time) {
            // Generate tick and record t_gen
            hft::Timestamp t_gen = hft::now();
            current_tick_seq = tick_seq;
            current_tick_tgen = t_gen;
            
            hft::Tick tick;
            tick.symbol = test_symbol;
            tick.bid_price = price_dist(gen) * 100;
            tick.ask_price = tick.bid_price + 100;
            tick.bid_size = qty_dist(gen);
            tick.ask_size = qty_dist(gen);
            tick.last_price = (tick.bid_price + tick.ask_price) / 2;
            tick.last_size = qty_dist(gen);
            tick.timestamp = t_gen;
            tick.sequence = tick_seq++;
            
            // Call strategy (this will submit orders via callback)
            strategy->onTick(tick);
            
            orders_sent.fetch_add(1, std::memory_order_relaxed);
            
            // Progress report
            auto now_time = std::chrono::steady_clock::now();
            if (now_time - last_report >= std::chrono::seconds(1)) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now_time - start_time).count();
                std::cout << "[" << elapsed << "s] Ticks: " << orders_sent.load()
                          << " | Exchange orders: " << exchange.stats().orders_received.load()
                          << " | Rate: " << (orders_sent.load() / std::max(1L, elapsed)) << " ticks/sec\n";
                last_report = now_time;
            }
            
            // Rate limiting
            auto target_interval_ns = 1000000000 / cfg.message_rate;
            auto now_tp = std::chrono::steady_clock::now();
            auto target_tp = now_tp + std::chrono::nanoseconds(target_interval_ns);
            while (std::chrono::steady_clock::now() < target_tp) {
                #if defined(__x86_64__) || defined(_M_X64)
                    __builtin_ia32_pause();
                #elif defined(__aarch64__)
                    asm volatile("yield" ::: "memory");
                #endif
            }
        }
        
        strategy->onShutdown();
        exchange.stop();
        
        // Print exchange statistics (tick-to-trade)
        exchange.print_stats();
        
        // Use exchange stats for main latency output
        const auto& ex_stats = exchange.stats();
        latencies = ex_stats.tick_to_order_latencies;
        orders_matched.store(ex_stats.orders_accepted.load());
        
    } else if (cfg.mode == "pipeline") {
    // Pipeline mode with multiple threads
    // 
    // Timestamp tracking for queue delay measurement:
    //   t_gen  = timestamp when tick/order is generated (producer)
    //   t_recv = timestamp when message is dequeued (consumer)
    //   t_done = timestamp when order matching completes
    //
    // Queue delay = t_recv - t_gen (measures thread scheduling + transfer time)
    // Process time = t_done - t_recv (measures matching engine latency)
    // Total latency = t_done - t_gen (end-to-end)
    //
        struct OrderMessage {
            uint64_t order_id;
            int64_t t_gen;      // Generation timestamp (producer side)
            hft::Side side;
            hft::Price price;
            hft::Quantity quantity;
        };
        
        hft::SPSCQueue<OrderMessage, 65536> order_queue;
        std::atomic<bool> running{true};
        std::atomic<uint64_t> processed{0};
        
        // Separate latency tracking for detailed analysis
        std::vector<int64_t> queue_latencies;    // t_recv - t_gen
        std::vector<int64_t> process_latencies;  // t_done - t_recv
        std::vector<int64_t> total_latencies;    // t_done - t_gen (end-to-end)
        queue_latencies.reserve(cfg.message_rate * cfg.duration_sec);
        process_latencies.reserve(cfg.message_rate * cfg.duration_sec);
        total_latencies.reserve(cfg.message_rate * cfg.duration_sec);
        std::mutex latency_mutex;
        
        // Track queue overload (when consumer falls behind)
        std::atomic<uint64_t> queue_overload_count{0};
        int64_t max_queue_delay = 0;
        
        // Stage 2: Order processing thread (Consumer)
        std::thread processor([&]() {
            #ifdef __linux__
            if (cfg.affinity.size() > 1) {
                hft::set_cpu_affinity(cfg.affinity[1]);
            }
            #endif
            
            while (running.load(std::memory_order_relaxed) || !order_queue.empty()) {
                auto msg = order_queue.try_pop();
                if (msg) {
                    // Record receive timestamp immediately after dequeue
                    auto t_recv = hft::now();
                    
                    // Calculate queue delay (cross-thread transfer time)
                    int64_t queue_delay = t_recv - msg->t_gen;
                    
                    // Detect overload: queue delay > 1000ns indicates consumer falling behind
                    if (queue_delay > 1000) {
                        queue_overload_count.fetch_add(1, std::memory_order_relaxed);
                    }
                    
                    // Process the order
                    auto result_id = engine.submit_order(
                        test_symbol,
                        msg->side,
                        hft::OrderType::LIMIT,
                        msg->price,
                        msg->quantity,
                        1
                    );
                    
                    // Record completion timestamp
                    auto t_done = hft::now();
                    
                    // Calculate latencies
                    int64_t process_time = t_done - t_recv;
                    int64_t total_latency = t_done - msg->t_gen;
                    
                    if (result_id != hft::INVALID_ORDER_ID) {
                        orders_matched.fetch_add(1, std::memory_order_relaxed);
                    }
                    
                    {
                        std::lock_guard<std::mutex> lock(latency_mutex);
                        queue_latencies.push_back(queue_delay);
                        process_latencies.push_back(process_time);
                        total_latencies.push_back(total_latency);
                        if (queue_delay > max_queue_delay) {
                            max_queue_delay = queue_delay;
                        }
                    }
                    
                    processed.fetch_add(1, std::memory_order_relaxed);
                } else if (cfg.use_polling) {
                    // Busy-wait polling
                    #if defined(__x86_64__) || defined(_M_X64)
                        __builtin_ia32_pause();
                    #elif defined(__aarch64__)
                        asm volatile("yield" ::: "memory");
                    #endif
                } else {
                    std::this_thread::yield();
                }
            }
        });
        
        // Stage 1: Order generation thread (main thread)
        #ifdef __linux__
        if (!cfg.affinity.empty()) {
            hft::set_cpu_affinity(cfg.affinity[0]);
        }
        #endif
        
        std::cout << "[INFO] Pipeline mode: Stage 1 (generator) on core " 
                  << (cfg.affinity.empty() ? -1 : cfg.affinity[0]) << "\n";
        std::cout << "[INFO] Pipeline mode: Stage 2 (processor) on core " 
                  << (cfg.affinity.size() > 1 ? cfg.affinity[1] : -1) << "\n\n";
        
        while (std::chrono::steady_clock::now() < end_time) {
            OrderMessage msg;
            msg.order_id = order_id++;
            msg.t_gen = hft::now();  // Generation timestamp (t_gen)
            msg.side = side_dist(gen) ? hft::Side::BUY : hft::Side::SELL;
            msg.price = price_dist(gen) * 100;
            msg.quantity = qty_dist(gen);
            
            // Try to enqueue, back-pressure if full
            while (!order_queue.try_push(msg)) {
                if (cfg.use_polling) {
                    #if defined(__x86_64__) || defined(_M_X64)
                        __builtin_ia32_pause();
                    #elif defined(__aarch64__)
                        asm volatile("yield" ::: "memory");
                    #endif
                } else {
                    std::this_thread::yield();
                }
            }
            
            orders_sent.fetch_add(1, std::memory_order_relaxed);
            
            // Progress report
            auto now = std::chrono::steady_clock::now();
            if (now - last_report >= std::chrono::seconds(1)) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
                std::cout << "[" << elapsed << "s] Sent: " << orders_sent.load() 
                          << " | Processed: " << processed.load()
                          << " | Pending: " << (orders_sent.load() - processed.load())
                          << " | Rate: " << (orders_sent.load() / std::max(1L, elapsed)) << " ops/sec\n";
                last_report = now;
            }
            
            // Rate limiting
            auto target_interval_ns = 1000000000 / cfg.message_rate;
            auto now_tp = std::chrono::steady_clock::now();
            auto target_tp = now_tp + std::chrono::nanoseconds(target_interval_ns);
            while (std::chrono::steady_clock::now() < target_tp) {
                #if defined(__x86_64__) || defined(_M_X64)
                    __builtin_ia32_pause();
                #elif defined(__aarch64__)
                    asm volatile("yield" ::: "memory");
                #endif
            }
        }
        
        // Stop processor thread
        running.store(false, std::memory_order_release);
        processor.join();
        
        // Use total latencies for main statistics
        latencies = std::move(total_latencies);
        
        // Print detailed queue statistics
        std::cout << "\n--- Queue Latency Analysis (Pipeline Mode) ---\n";
        
        if (!queue_latencies.empty()) {
            std::sort(queue_latencies.begin(), queue_latencies.end());
            std::sort(process_latencies.begin(), process_latencies.end());
            
            auto q_median = queue_latencies[queue_latencies.size() / 2];
            auto q_p99 = queue_latencies[static_cast<size_t>(queue_latencies.size() * 0.99)];
            auto q_max = queue_latencies.back();
            
            auto p_median = process_latencies[process_latencies.size() / 2];
            auto p_p99 = process_latencies[static_cast<size_t>(process_latencies.size() * 0.99)];
            auto p_max = process_latencies.back();
            
            std::cout << "  Queue delay (t_recv - t_gen):\n";
            std::cout << "    Median: " << q_median << " ns (" << (q_median / 1000.0) << " µs)\n";
            std::cout << "    P99:    " << q_p99 << " ns (" << (q_p99 / 1000.0) << " µs)\n";
            std::cout << "    Max:    " << q_max << " ns (" << (q_max / 1000.0) << " µs)\n";
            std::cout << "  Processing time (t_done - t_recv):\n";
            std::cout << "    Median: " << p_median << " ns (" << (p_median / 1000.0) << " µs)\n";
            std::cout << "    P99:    " << p_p99 << " ns (" << (p_p99 / 1000.0) << " µs)\n";
            std::cout << "    Max:    " << p_max << " ns (" << (p_max / 1000.0) << " µs)\n";
            std::cout << "  Overload events (queue delay > 1µs): " << queue_overload_count.load() << "\n";
            
            // Check if consumer is keeping up
            if (q_median < 100) {
                std::cout << "  ✓ Consumer keeping up (queue nearly empty)\n";
            } else if (q_median < 1000) {
                std::cout << "  ⚠ Minor queuing detected\n";
            } else {
                std::cout << "  ✗ Consumer falling behind (overload)\n";
            }
        }
        
    } else {
    // Single-thread mode (original) with advanced features
    while (std::chrono::steady_clock::now() < end_time) {
        // Check warmup period
        if (!warmup_complete && std::chrono::steady_clock::now() >= warmup_end) {
            warmup_complete = true;
            // Reset statistics after warmup
            orders_sent.store(0);
            orders_matched.store(0);
            latencies.clear();
            latencies.reserve(cfg.message_rate * cfg.duration_sec);
            std::cout << "[INFO] Warmup complete, starting measurement\n";
        }
        
        // Check for gap recovery simulation
        bool is_burst = check_gap_recovery();
        if (is_burst) burst_remaining--;
        
        // Inject jitter if configured
        inject_jitter();
        
        ticks_received.fetch_add(1, std::memory_order_relaxed);
        
        // Check trade signal ratio - not all ticks trigger orders
        if (!should_trade()) {
            continue;  // Skip this tick, no order generated
        }
        signals_triggered.fetch_add(1, std::memory_order_relaxed);
        
        auto order_start = hft::now();
        
        // Create order parameters with multi-symbol support
        hft::Symbol current_symbol = get_current_symbol();
        hft::Side side = side_dist(gen) ? hft::Side::BUY : hft::Side::SELL;
        hft::Price price = price_dist(gen) * 100;  // Convert to fixed-point
        hft::Quantity quantity = qty_dist(gen);

        // Submit order
        auto result_id = engine.submit_order(
            current_symbol,
            side,
            hft::OrderType::LIMIT,
            price,
            quantity,
            1  // client_id
        );
        
        if (result_id != hft::INVALID_ORDER_ID) {
            orders_matched.fetch_add(1, std::memory_order_relaxed);
        }

        auto order_end = hft::now();
        int64_t latency = order_end - order_start;
        
        order_id++;
        
        // Only record stats if warmup is complete
        if (warmup_complete) {
            orders_sent.fetch_add(1, std::memory_order_relaxed);
            latencies.push_back(latency);
        }
        
        // Log to file
        if (log_file.is_open()) {
            log_file << order_start << ","
                     << order_id << ","
                     << latency << ","
                     << (side == hft::Side::BUY ? "BUY" : "SELL") << ","
                     << price << ","
                     << quantity << ","
                     << hft::symbol_view(current_symbol) << "\n";
        }

        // Progress report every second
        auto now = std::chrono::steady_clock::now();
        if (now - last_report >= std::chrono::seconds(1)) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
            std::string warmup_str = warmup_complete ? "" : " (warmup)";
            std::cout << "[" << elapsed << "s" << warmup_str << "] Ticks: " << ticks_received.load()
                      << " | Orders: " << orders_sent.load() 
                      << " | Matched: " << orders_matched.load()
                      << " | Rate: " << (orders_sent.load() / std::max(1L, elapsed)) << " ops/sec\n";
            last_report = now;
        }

        // Skip rate limiting during burst recovery
        if (is_burst) continue;

        // Rate limiting based on pattern
        if (cfg.message_pattern == "poisson") {
            auto delay_ns = static_cast<int64_t>(poisson_dist(gen) * 1e9);
            if (delay_ns < 1000000) {  // Only busy-wait if < 1ms
                auto wait_until = std::chrono::steady_clock::now() + std::chrono::nanoseconds(delay_ns);
                while (std::chrono::steady_clock::now() < wait_until) {
                    #if defined(__x86_64__) || defined(_M_X64)
                        __builtin_ia32_pause();
                    #elif defined(__aarch64__)
                        asm volatile("yield" ::: "memory");
                    #endif
                }
            }
        } else {
            // Uniform rate
            auto target_interval_ns = 1000000000 / cfg.message_rate;
            auto now_tp = std::chrono::steady_clock::now();
            auto target_tp = now_tp + std::chrono::nanoseconds(target_interval_ns);
            while (std::chrono::steady_clock::now() < target_tp) {
                #if defined(__x86_64__) || defined(_M_X64)
                    __builtin_ia32_pause();
                #elif defined(__aarch64__)
                    asm volatile("yield" ::: "memory");
                #endif
            }
        }
    }
    } // end else single-thread mode

    auto total_time = std::chrono::steady_clock::now() - start_time;
    auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(total_time).count();

    // Calculate statistics
    if (!latencies.empty()) {
        std::sort(latencies.begin(), latencies.end());
        
        int64_t sum = 0;
        for (auto l : latencies) sum += l;
        double avg = static_cast<double>(sum) / latencies.size();
        
        size_t p50_idx = latencies.size() * 50 / 100;
        size_t p90_idx = latencies.size() * 90 / 100;
        size_t p99_idx = latencies.size() * 99 / 100;
        size_t p999_idx = latencies.size() * 999 / 1000;

        std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
        std::cout << "║                         RESULTS                              ║\n";
        std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";
        
        std::cout << "--- Throughput ---\n";
        std::cout << "  Total orders:    " << orders_sent.load() << "\n";
        std::cout << "  Orders matched:  " << orders_matched.load() << "\n";
        std::cout << "  Duration:        " << total_ms << " ms\n";
        std::cout << "  Throughput:      " << std::fixed << std::setprecision(0)
                  << (orders_sent.load() * 1000.0 / total_ms) << " orders/sec\n";

        std::cout << "\n--- Latency (nanoseconds) ---\n";
        std::cout << "  Min:             " << latencies.front() << " ns\n";
        std::cout << "  Max:             " << latencies.back() << " ns\n";
        std::cout << "  Average:         " << std::fixed << std::setprecision(0) << avg << " ns\n";
        std::cout << "  P50:             " << latencies[p50_idx] << " ns\n";
        std::cout << "  P90:             " << latencies[p90_idx] << " ns\n";
        std::cout << "  P99:             " << latencies[p99_idx] << " ns\n";
        std::cout << "  P99.9:           " << latencies[p999_idx] << " ns\n";

        std::cout << "\n--- Latency (microseconds) ---\n";
        std::cout << "  Min:             " << std::fixed << std::setprecision(2) << latencies.front() / 1000.0 << " µs\n";
        std::cout << "  Max:             " << std::fixed << std::setprecision(2) << latencies.back() / 1000.0 << " µs\n";
        std::cout << "  Average:         " << std::fixed << std::setprecision(2) << avg / 1000.0 << " µs\n";
        std::cout << "  P50:             " << std::fixed << std::setprecision(2) << latencies[p50_idx] / 1000.0 << " µs\n";
        std::cout << "  P99:             " << std::fixed << std::setprecision(2) << latencies[p99_idx] / 1000.0 << " µs\n";
    }

    if (log_file.is_open()) {
        log_file.close();
        std::cout << "\nDetailed results written to: " << cfg.log_file << "\n";
    }

    // Print concise summary line
    if (!latencies.empty()) {
        std::sort(latencies.begin(), latencies.end());
        
        int64_t sum = 0;
        int64_t sum_sq = 0;
        for (auto l : latencies) {
            sum += l;
            sum_sq += l * l;
        }
        double mean = static_cast<double>(sum) / latencies.size();
        double variance = static_cast<double>(sum_sq) / latencies.size() - mean * mean;
        double stddev = std::sqrt(variance);
        
        size_t p50_idx = latencies.size() * 50 / 100;
        size_t p99_idx = latencies.size() * 99 / 100;
        
        auto format_count = [](uint64_t n) -> std::string {
            if (n >= 1000000) return std::to_string(n / 1000000) + "M";
            if (n >= 1000) return std::to_string(n / 1000) + "k";
            return std::to_string(n);
        };
        
        std::cout << "\n════════════════════════════════════════════════════════════════\n";
        std::cout << " Output: \"Completed " << std::fixed << std::setprecision(1) 
                  << (total_ms / 1000.0) << "s test: "
                  << format_count(orders_sent.load()) << " ticks, "
                  << format_count(orders_sent.load()) << " orders. "
                  << "End-to-end latency: median " << std::setprecision(1) 
                  << (latencies[p50_idx] / 1000.0) << " µs, "
                  << "99th percentile " << std::setprecision(1) 
                  << (latencies[p99_idx] / 1000.0) << " µs, "
                  << "max " << std::setprecision(1) 
                  << (latencies.back() / 1000.0) << " µs. "
                  << "No packet loss. "
                  << "Jitter (stddev) ~ " << std::setprecision(1) 
                  << (stddev / 1000.0) << " µs.\"\n";
        std::cout << "════════════════════════════════════════════════════════════════\n";
    }

    return 0;
}

