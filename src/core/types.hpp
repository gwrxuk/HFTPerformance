/**
 * @file types.hpp
 * @brief Core type definitions for the HFT Trading System
 * 
 * This header defines fundamental types used throughout the system,
 * optimized for cache efficiency and minimal overhead.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <array>
#include <chrono>
#include <string_view>
#include <limits>

namespace hft {

// ============================================================================
// Time Types - Nanosecond precision for HFT
// ============================================================================

using Timestamp = std::int64_t;  // Nanoseconds since epoch
using Duration = std::int64_t;   // Duration in nanoseconds

// High-resolution clock for timing
using Clock = std::chrono::high_resolution_clock;
using TimePoint = Clock::time_point;

[[nodiscard]] inline Timestamp now() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now().time_since_epoch()
    ).count();
}

// ============================================================================
// Trading Types
// ============================================================================

using OrderId = std::uint64_t;
using Price = std::int64_t;      // Fixed-point: actual_price * 1e8
using Quantity = std::int64_t;
using Symbol = std::array<char, 16>;  // Padded for cache alignment

constexpr OrderId INVALID_ORDER_ID = 0;
constexpr Price INVALID_PRICE = std::numeric_limits<Price>::min();

// Price conversion utilities (8 decimal places precision)
constexpr std::int64_t PRICE_MULTIPLIER = 100'000'000LL;

[[nodiscard]] constexpr Price to_fixed_price(double price) noexcept {
    return static_cast<Price>(price * PRICE_MULTIPLIER);
}

[[nodiscard]] constexpr double to_double_price(Price price) noexcept {
    return static_cast<double>(price) / PRICE_MULTIPLIER;
}

// ============================================================================
// Order Side
// ============================================================================

enum class Side : std::uint8_t {
    BUY = 0,
    SELL = 1
};

[[nodiscard]] constexpr std::string_view to_string(Side side) noexcept {
    return side == Side::BUY ? "BUY" : "SELL";
}

[[nodiscard]] constexpr Side opposite(Side side) noexcept {
    return side == Side::BUY ? Side::SELL : Side::BUY;
}

// ============================================================================
// Order Types
// ============================================================================

enum class OrderType : std::uint8_t {
    LIMIT = 0,
    MARKET = 1,
    STOP_LIMIT = 2,
    IMMEDIATE_OR_CANCEL = 3,
    FILL_OR_KILL = 4,
    POST_ONLY = 5
};

[[nodiscard]] constexpr std::string_view to_string(OrderType type) noexcept {
    switch (type) {
        case OrderType::LIMIT: return "LIMIT";
        case OrderType::MARKET: return "MARKET";
        case OrderType::STOP_LIMIT: return "STOP_LIMIT";
        case OrderType::IMMEDIATE_OR_CANCEL: return "IOC";
        case OrderType::FILL_OR_KILL: return "FOK";
        case OrderType::POST_ONLY: return "POST_ONLY";
    }
    return "UNKNOWN";
}

// ============================================================================
// Order Status
// ============================================================================

enum class OrderStatus : std::uint8_t {
    NEW = 0,
    PARTIALLY_FILLED = 1,
    FILLED = 2,
    CANCELLED = 3,
    REJECTED = 4,
    EXPIRED = 5
};

[[nodiscard]] constexpr std::string_view to_string(OrderStatus status) noexcept {
    switch (status) {
        case OrderStatus::NEW: return "NEW";
        case OrderStatus::PARTIALLY_FILLED: return "PARTIALLY_FILLED";
        case OrderStatus::FILLED: return "FILLED";
        case OrderStatus::CANCELLED: return "CANCELLED";
        case OrderStatus::REJECTED: return "REJECTED";
        case OrderStatus::EXPIRED: return "EXPIRED";
    }
    return "UNKNOWN";
}

// ============================================================================
// Execution Report Types
// ============================================================================

enum class ExecutionType : std::uint8_t {
    NEW = 0,
    TRADE = 1,
    CANCELLED = 2,
    REPLACED = 3,
    REJECTED = 4
};

// ============================================================================
// Market Data Types
// ============================================================================

struct alignas(64) Quote {
    Price bid_price;
    Price ask_price;
    Quantity bid_quantity;
    Quantity ask_quantity;
    Timestamp timestamp;
    
    [[nodiscard]] Price spread() const noexcept {
        return ask_price - bid_price;
    }
    
    [[nodiscard]] Price mid_price() const noexcept {
        return (bid_price + ask_price) / 2;
    }
};

struct alignas(64) Trade {
    OrderId maker_order_id;
    OrderId taker_order_id;
    Price price;
    Quantity quantity;
    Side aggressor_side;
    Timestamp timestamp;
};

// ============================================================================
// Symbol Utilities
// ============================================================================

inline Symbol make_symbol(std::string_view str) noexcept {
    Symbol sym{};
    const auto len = std::min(str.size(), sym.size() - 1);
    std::memcpy(sym.data(), str.data(), len);
    return sym;
}

inline std::string_view symbol_view(const Symbol& sym) noexcept {
    return std::string_view(sym.data());
}

// ============================================================================
// Cache Line Alignment Helpers
// ============================================================================

constexpr std::size_t CACHE_LINE_SIZE = 64;

template<typename T>
struct alignas(CACHE_LINE_SIZE) CacheAligned {
    T value;
    
    CacheAligned() = default;
    explicit CacheAligned(T v) : value(std::move(v)) {}
    
    operator T&() noexcept { return value; }
    operator const T&() const noexcept { return value; }
};

// Padding to prevent false sharing
template<std::size_t Size>
using Padding = std::array<char, Size>;

} // namespace hft

