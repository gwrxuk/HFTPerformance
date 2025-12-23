/**
 * @file fix_message.hpp
 * @brief FIX protocol message structures
 * 
 * Implements a simplified FIX (Financial Information eXchange) protocol
 * message format optimized for low-latency parsing.
 * 
 * FIX format: Tag=Value|Tag=Value|...
 * Where | is the SOH delimiter (ASCII 0x01)
 */

#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <optional>
#include <cstdint>
#include "core/types.hpp"
#include "matching/order.hpp"

namespace hft {

// Standard FIX delimiter
constexpr char FIX_DELIMITER = '\x01';  // SOH

// Common FIX tags
namespace fix_tag {
    constexpr int BeginString = 8;
    constexpr int BodyLength = 9;
    constexpr int MsgType = 35;
    constexpr int SenderCompID = 49;
    constexpr int TargetCompID = 56;
    constexpr int MsgSeqNum = 34;
    constexpr int SendingTime = 52;
    constexpr int CheckSum = 10;
    
    // Order-related
    constexpr int ClOrdID = 11;
    constexpr int OrderID = 37;
    constexpr int Symbol = 55;
    constexpr int Side = 54;
    constexpr int OrderQty = 38;
    constexpr int OrdType = 40;
    constexpr int Price = 44;
    constexpr int TimeInForce = 59;
    constexpr int ExecType = 150;
    constexpr int OrdStatus = 39;
    constexpr int LeavesQty = 151;
    constexpr int CumQty = 14;
    constexpr int AvgPx = 6;
    constexpr int LastQty = 32;
    constexpr int LastPx = 31;
    
    // Market data
    constexpr int MDReqID = 262;
    constexpr int SubscriptionRequestType = 263;
    constexpr int MarketDepth = 264;
    constexpr int MDUpdateType = 265;
    constexpr int NoMDEntries = 268;
    constexpr int MDEntryType = 269;
    constexpr int MDEntryPx = 270;
    constexpr int MDEntrySize = 271;
}

// FIX message types
namespace fix_msgtype {
    constexpr std::string_view Heartbeat = "0";
    constexpr std::string_view TestRequest = "1";
    constexpr std::string_view Logon = "A";
    constexpr std::string_view Logout = "5";
    constexpr std::string_view NewOrderSingle = "D";
    constexpr std::string_view OrderCancelRequest = "F";
    constexpr std::string_view OrderCancelReplaceRequest = "G";
    constexpr std::string_view ExecutionReport = "8";
    constexpr std::string_view OrderCancelReject = "9";
    constexpr std::string_view MarketDataRequest = "V";
    constexpr std::string_view MarketDataSnapshot = "W";
    constexpr std::string_view MarketDataIncRefresh = "X";
}

/**
 * @brief FIX field (tag-value pair)
 */
struct FixField {
    int tag;
    std::string_view value;
    
    FixField() : tag(0), value() {}
    FixField(int t, std::string_view v) : tag(t), value(v) {}
};

/**
 * @brief FIX message container
 * 
 * Provides efficient access to FIX fields with minimal copying.
 * Uses string_view for zero-copy field access.
 */
class FixMessage {
public:
    FixMessage() = default;

    /**
     * @brief Parse a FIX message from raw bytes
     */
    bool parse(std::string_view data);

    /**
     * @brief Get field value by tag
     */
    [[nodiscard]] std::optional<std::string_view> get_field(int tag) const {
        auto it = fields_.find(tag);
        if (it != fields_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    /**
     * @brief Get field as integer
     */
    [[nodiscard]] std::optional<std::int64_t> get_int(int tag) const;

    /**
     * @brief Get field as double
     */
    [[nodiscard]] std::optional<double> get_double(int tag) const;

    /**
     * @brief Get message type
     */
    [[nodiscard]] std::string_view msg_type() const {
        auto mt = get_field(fix_tag::MsgType);
        return mt.value_or("");
    }

    /**
     * @brief Check if message has a field
     */
    [[nodiscard]] bool has_field(int tag) const {
        return fields_.find(tag) != fields_.end();
    }

    /**
     * @brief Get all fields
     */
    [[nodiscard]] const std::unordered_map<int, std::string_view>& fields() const {
        return fields_;
    }

    /**
     * @brief Get raw message data
     */
    [[nodiscard]] std::string_view raw() const { return raw_data_; }

    /**
     * @brief Clear the message
     */
    void clear() {
        fields_.clear();
        raw_data_ = {};
    }

    /**
     * @brief Verify checksum
     */
    [[nodiscard]] bool verify_checksum() const;

private:
    std::unordered_map<int, std::string_view> fields_;
    std::string_view raw_data_;
};

/**
 * @brief FIX message builder for constructing outbound messages
 */
class FixMessageBuilder {
public:
    FixMessageBuilder() {
        buffer_.reserve(256);
    }

    /**
     * @brief Start a new message
     */
    FixMessageBuilder& begin(std::string_view msg_type,
                              std::string_view sender,
                              std::string_view target,
                              std::uint64_t seq_num);

    /**
     * @brief Add a string field
     */
    FixMessageBuilder& add_field(int tag, std::string_view value) {
        append_field(tag, value);
        return *this;
    }

    /**
     * @brief Add an integer field
     */
    FixMessageBuilder& add_field(int tag, std::int64_t value);

    /**
     * @brief Add a double field
     */
    FixMessageBuilder& add_field(int tag, double value, int precision = 8);

    /**
     * @brief Add a char field
     */
    FixMessageBuilder& add_field(int tag, char value) {
        char buf[2] = {value, '\0'};
        append_field(tag, std::string_view(buf, 1));
        return *this;
    }

    /**
     * @brief Finalize and get the message
     */
    std::string build();

    /**
     * @brief Get current buffer (without finalization)
     */
    [[nodiscard]] const std::string& buffer() const { return buffer_; }

    /**
     * @brief Clear the builder
     */
    void clear() {
        buffer_.clear();
        body_start_ = 0;
    }

private:
    void append_field(int tag, std::string_view value);
    [[nodiscard]] static std::uint8_t calculate_checksum(std::string_view data);

    std::string buffer_;
    std::size_t body_start_ = 0;
};

/**
 * @brief Convert HFT order to FIX NewOrderSingle message
 */
std::string order_to_fix(const Order& order, 
                          std::string_view sender,
                          std::string_view target,
                          std::uint64_t seq_num);

/**
 * @brief Convert FIX ExecutionReport to HFT ExecutionReport
 */
std::optional<ExecutionReport> fix_to_execution_report(const FixMessage& msg);

/**
 * @brief Convert HFT side to FIX side character
 */
inline char side_to_fix(Side side) {
    return side == Side::BUY ? '1' : '2';
}

/**
 * @brief Convert FIX side to HFT side
 */
inline std::optional<Side> fix_to_side(char c) {
    switch (c) {
        case '1': return Side::BUY;
        case '2': return Side::SELL;
        default: return std::nullopt;
    }
}

/**
 * @brief Convert HFT order type to FIX order type
 */
inline char order_type_to_fix(OrderType type) {
    switch (type) {
        case OrderType::MARKET: return '1';
        case OrderType::LIMIT: return '2';
        case OrderType::STOP_LIMIT: return '4';
        default: return '2';  // Default to limit
    }
}

} // namespace hft

