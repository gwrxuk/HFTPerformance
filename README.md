# High-Frequency Trading System

A performance-critical trading system implementation demonstrating expertise in low-latency C++ development, systems programming, and financial infrastructure.

## 🚀 Overview

This project implements a complete high-frequency trading (HFT) system stack including:

- **Matching Engine**: Price-time priority order matching with sub-microsecond latency
- **Order Book**: Lock-free, cache-optimized limit order book
- **Market Data Handler**: Real-time market data processing with FIX and WebSocket support
- **Order Gateway**: REST API with rate limiting and risk management
- **Performance Benchmarks**: Comprehensive latency and throughput measurement tools

## 🏗️ Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        Order Gateway                             │
│                    (Rate Limiting, Risk)                         │
└─────────────────────────────┬───────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                      Matching Engine                             │
│              (Multi-instrument, Price-Time Priority)             │
├─────────────────┬─────────────────┬─────────────────────────────┤
│   BTC-USD Book  │   ETH-USD Book  │        ... more books       │
└─────────────────┴─────────────────┴─────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    Market Data Publisher                         │
│              (WebSocket, FIX, Execution Reports)                 │
└─────────────────────────────────────────────────────────────────┘
```

## ⚡ Performance Highlights

| Component | Metric | Performance |
|-----------|--------|-------------|
| Order Book Add | Latency | < 500 ns |
| Order Book Cancel | Latency | < 200 ns |
| Order Match | Latency | < 1 μs |
| SPSC Queue | Throughput | > 50M ops/sec |
| Memory Pool | Allocation | < 50 ns |

## 🔧 Key Technical Features

### Low-Latency Optimizations

- **Lock-free Data Structures**: SPSC/MPSC queues with cache-line padding
- **Custom Memory Pool**: O(1) allocation without heap fragmentation
- **Cache-Aware Design**: 64-byte aligned structures for cache efficiency
- **CPU Affinity**: Thread pinning for deterministic performance
- **RDTSC Timing**: Nanosecond-precision measurement with minimal overhead

### Modern C++20 Features

- Concepts and constraints
- `std::span` for zero-copy views
- `constexpr` compile-time computation
- Structured bindings
- `std::optional` and `std::string_view`

### Protocol Support

- **FIX 4.4**: Message parsing and generation
- **WebSocket**: Real-time market data streaming
- **REST API**: Order submission and management

## 📁 Project Structure

```
.
├── CMakeLists.txt          # Build configuration
├── Dockerfile              # Development container
├── docker-compose.yml      # Multi-service orchestration
├── src/
│   ├── core/               # Low-latency utilities
│   │   ├── lockfree_queue.hpp
│   │   ├── memory_pool.hpp
│   │   ├── spinlock.hpp
│   │   ├── timing.hpp
│   │   ├── cpu_affinity.hpp
│   │   └── types.hpp
│   ├── matching/           # Matching engine
│   │   ├── order.hpp
│   │   ├── price_level.hpp
│   │   ├── order_book.hpp
│   │   └── matching_engine.hpp
│   ├── protocol/           # Exchange protocols
│   │   ├── fix_message.hpp
│   │   ├── websocket_handler.hpp
│   │   └── rest_handler.hpp
│   ├── marketdata/         # Market data handling
│   │   └── market_data_handler.hpp
│   ├── benchmark/          # Performance tests
│   │   └── benchmark_main.cpp
│   └── apps/               # Applications
│       ├── matching_engine_main.cpp
│       ├── market_data_feed_main.cpp
│       └── order_gateway_main.cpp
└── tests/                  # Unit tests
```

## 🐳 Docker Setup

### Quick Start

```bash
# Build and run the development container
docker-compose up -d hft-dev

# Enter the container
docker exec -it hft-trading-system bash

# Build the project
mkdir -p build && cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
ninja
```

### Run Components

```bash
# Start matching engine
docker-compose up matching-engine

# Start market data feed
docker-compose up market-data

# Run benchmarks
docker-compose --profile benchmark up benchmarks
```

### Run Individual Services

```bash
# Matching Engine Server (port 8080)
./build/bin/matching_engine

# Market Data Feed Simulator (port 9090)
./build/bin/market_data_feed

# Order Gateway (port 9000)
./build/bin/order_gateway

# Benchmark Suite
./build/bin/benchmark_suite
```

## 🔌 API Reference

### Matching Engine REST API

```bash
# Health check
curl http://localhost:8080/health

# Get order book depth
curl http://localhost:8080/api/v1/depth/BTC-USD

# Get quote
curl http://localhost:8080/api/v1/quote/BTC-USD

# Submit order
curl -X POST http://localhost:8080/api/v1/order \
  -H "Content-Type: application/json" \
  -d '{"symbol":"BTC-USD","side":"BUY","type":"LIMIT","price":50000.0,"quantity":1.0}'

# Cancel order
curl -X DELETE http://localhost:8080/api/v1/order/BTC-USD/12345

# Get stats
curl http://localhost:8080/api/v1/stats
```

### Order Gateway REST API

```bash
# Submit order with risk checks
curl -X POST http://localhost:9000/api/v1/order \
  -H "Content-Type: application/json" \
  -d '{"symbol":"BTC-USD","side":"BUY","type":"LIMIT","price":50000.0,"quantity":1.0}'

# Get position
curl http://localhost:9000/api/v1/position/BTC-USD

# Get gateway stats (includes latency percentiles)
curl http://localhost:9000/api/v1/stats
```

## 📊 Benchmark Results

Run the benchmark suite to measure system performance:

```bash
./build/bin/benchmark_suite
```

Example output:

```
╔══════════════════════════════════════════════════════════════╗
║           HFT Trading System - Performance Benchmark          ║
╚══════════════════════════════════════════════════════════════╝

System Information:
  CPU Cores:      16
  Cache Line:     64 bytes
  Order Size:     64 bytes
  TSC Frequency:  3.20 GHz

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
SPSC Queue Benchmark
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  Push Latency:  mean=15.2ns, p99=45ns
  Pop Latency:   mean=12.8ns, p99=38ns
  Throughput:    52.3M ops/sec

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Order Book Benchmark
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  Add Order:     mean=425ns, p99=1.2μs
  Cancel Order:  mean=180ns, p99=450ns
  Match Order:   mean=890ns, p99=2.1μs
```

## 🧪 Running Tests

```bash
cd build
./bin/unit_tests
```

## 🔒 Performance Tuning

### Linux Kernel Parameters

```bash
# Disable CPU frequency scaling
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor

# Increase network buffer sizes
sudo sysctl -w net.core.rmem_max=16777216
sudo sysctl -w net.core.wmem_max=16777216

# Disable swap
sudo swapoff -a

# Set real-time scheduling limits
echo '* soft rtprio 99' | sudo tee -a /etc/security/limits.conf
echo '* hard rtprio 99' | sudo tee -a /etc/security/limits.conf
```

### CPU Isolation

```bash
# Add to kernel parameters (GRUB_CMDLINE_LINUX)
isolcpus=2,3 nohz_full=2,3 rcu_nocbs=2,3
```

## 📚 Technical Deep Dive

### Lock-Free SPSC Queue

The SPSC queue implementation uses:
- Power-of-2 sizing for branchless modulo
- Cache-line separated head/tail indices
- Local caching of remote indices to reduce cache coherence traffic
- Acquire-release memory ordering for minimal synchronization

### Memory Pool

The custom memory pool provides:
- O(1) allocation and deallocation
- Zero heap fragmentation
- Cache-aligned blocks
- Thread-local pools for lock-free operation

### Order Book

The order book uses:
- `std::map` for price levels (O(log N) access)
- Intrusive linked lists at each level (O(1) order ops)
- Separate bid/ask structures for cache locality

## 🛠️ Development

### Prerequisites

- C++20 compatible compiler (GCC 12+, Clang 14+)
- CMake 3.20+
- Boost 1.74+
- OpenSSL
- Linux (for full feature support)

### Building without Docker

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

## 📄 License

MIT License - See LICENSE file for details.

## 🙏 Acknowledgments

Inspired by real-world trading systems and low-latency programming techniques from:
- "Trading and Exchanges" by Larry Harris
- "Market Microstructure in Practice" by Lehalle and Laruelle
- Various open-source trading system implementations

