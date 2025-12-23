/**
 * @file fix_parser.cpp
 * @brief FIX protocol parser implementation
 */

#include "fix_message.hpp"
#include <charconv>
#include <cstdio>
#include <optional>
#include <cstring>

namespace hft {

bool FixMessage::parse(std::string_view data) {
    clear();
    raw_data_ = data;
    
    std::size_t pos = 0;
    while (pos < data.size()) {
        // Find tag
        auto eq_pos = data.find('=', pos);
        if (eq_pos == std::string_view::npos) {
            break;
        }
        
        // Parse tag number
        int tag = 0;
        auto tag_str = data.substr(pos, eq_pos - pos);
        auto [ptr, ec] = std::from_chars(tag_str.data(), 
                                          tag_str.data() + tag_str.size(), 
                                          tag);
        if (ec != std::errc()) {
            return false;  // Invalid tag
        }
        
        // Find value end (delimiter)
        auto delim_pos = data.find(FIX_DELIMITER, eq_pos + 1);
        if (delim_pos == std::string_view::npos) {
            // Last field might not have delimiter
            delim_pos = data.size();
        }
        
        // Extract value
        auto value = data.substr(eq_pos + 1, delim_pos - eq_pos - 1);
        fields_[tag] = value;
        
        pos = delim_pos + 1;
    }
    
    return !fields_.empty();
}

std::optional<std::int64_t> FixMessage::get_int(int tag) const {
    auto value = get_field(tag);
    if (!value) {
        return std::nullopt;
    }
    
    std::int64_t result = 0;
    auto [ptr, ec] = std::from_chars(value->data(), 
                                      value->data() + value->size(), 
                                      result);
    if (ec != std::errc()) {
        return std::nullopt;
    }
    return result;
}

std::optional<double> FixMessage::get_double(int tag) const {
    auto value = get_field(tag);
    if (!value) {
        return std::nullopt;
    }
    
    // std::from_chars for double is not widely supported, use strtod
    char buffer[64];
    std::size_t len = std::min(value->size(), sizeof(buffer) - 1);
    std::memcpy(buffer, value->data(), len);
    buffer[len] = '\0';
    
    char* end = nullptr;
    double result = std::strtod(buffer, &end);
    if (end == buffer) {
        return std::nullopt;
    }
    return result;
}

bool FixMessage::verify_checksum() const {
    auto checksum_field = get_field(fix_tag::CheckSum);
    if (!checksum_field) {
        return false;
    }
    
    // Find checksum field position
    auto checksum_pos = raw_data_.rfind("10=");
    if (checksum_pos == std::string_view::npos) {
        return false;
    }
    
    // Calculate checksum of everything before "10="
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i < checksum_pos; ++i) {
        sum += static_cast<std::uint8_t>(raw_data_[i]);
    }
    std::uint8_t expected = sum % 256;
    
    // Parse stated checksum
    int stated = 0;
    auto [ptr, ec] = std::from_chars(checksum_field->data(),
                                      checksum_field->data() + checksum_field->size(),
                                      stated);
    if (ec != std::errc()) {
        return false;
    }
    
    return static_cast<int>(expected) == stated;
}

FixMessageBuilder& FixMessageBuilder::begin(std::string_view msg_type,
                                             std::string_view sender,
                                             std::string_view target,
                                             std::uint64_t seq_num) {
    clear();
    
    // BeginString (will be prepended with body length later)
    append_field(fix_tag::BeginString, "FIX.4.4");
    
    // Placeholder for BodyLength - we'll update this at the end
    body_start_ = buffer_.size();
    append_field(fix_tag::BodyLength, "000");
    
    // Standard header fields
    append_field(fix_tag::MsgType, msg_type);
    append_field(fix_tag::SenderCompID, sender);
    append_field(fix_tag::TargetCompID, target);
    add_field(fix_tag::MsgSeqNum, static_cast<std::int64_t>(seq_num));
    
    // SendingTime
    auto now_time = now();
    char time_buf[32];
    std::snprintf(time_buf, sizeof(time_buf), "%ld", now_time);
    append_field(fix_tag::SendingTime, time_buf);
    
    return *this;
}

FixMessageBuilder& FixMessageBuilder::add_field(int tag, std::int64_t value) {
    char buf[32];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), value);
    if (ec == std::errc()) {
        append_field(tag, std::string_view(buf, ptr - buf));
    }
    return *this;
}

FixMessageBuilder& FixMessageBuilder::add_field(int tag, double value, int precision) {
    char buf[64];
    int len = std::snprintf(buf, sizeof(buf), "%.*f", precision, value);
    if (len > 0) {
        append_field(tag, std::string_view(buf, len));
    }
    return *this;
}

void FixMessageBuilder::append_field(int tag, std::string_view value) {
    char tag_buf[16];
    auto [ptr, ec] = std::to_chars(tag_buf, tag_buf + sizeof(tag_buf), tag);
    if (ec == std::errc()) {
        buffer_.append(tag_buf, ptr - tag_buf);
        buffer_ += '=';
        buffer_.append(value);
        buffer_ += FIX_DELIMITER;
    }
}

std::uint8_t FixMessageBuilder::calculate_checksum(std::string_view data) {
    std::uint32_t sum = 0;
    for (char c : data) {
        sum += static_cast<std::uint8_t>(c);
    }
    return static_cast<std::uint8_t>(sum % 256);
}

std::string FixMessageBuilder::build() {
    // Calculate body length (from after BodyLength field to before CheckSum)
    std::size_t body_length = buffer_.size() - body_start_ - 6;  // -6 for "9=000|"
    
    // Update body length field
    char len_buf[8];
    std::snprintf(len_buf, sizeof(len_buf), "%03zu", body_length);
    
    // Find and replace body length placeholder
    auto body_len_pos = buffer_.find("9=000");
    if (body_len_pos != std::string::npos) {
        buffer_.replace(body_len_pos + 2, 3, len_buf);
    }
    
    // Calculate checksum
    auto checksum = calculate_checksum(buffer_);
    
    // Append checksum
    char cs_buf[16];
    std::snprintf(cs_buf, sizeof(cs_buf), "10=%03d%c", checksum, FIX_DELIMITER);
    buffer_ += cs_buf;
    
    return buffer_;
}

std::string order_to_fix(const Order& order,
                          std::string_view sender,
                          std::string_view target,
                          std::uint64_t seq_num) {
    FixMessageBuilder builder;
    
    builder.begin(fix_msgtype::NewOrderSingle, sender, target, seq_num)
           .add_field(fix_tag::ClOrdID, static_cast<std::int64_t>(order.order_id))
           .add_field(fix_tag::Symbol, symbol_view(order.client_id > 0 ? 
                      Symbol{} : Symbol{}))  // Would need actual symbol
           .add_field(fix_tag::Side, side_to_fix(order.side))
           .add_field(fix_tag::OrderQty, order.quantity)
           .add_field(fix_tag::OrdType, order_type_to_fix(order.type))
           .add_field(fix_tag::Price, to_double_price(order.price));
    
    return builder.build();
}

std::optional<ExecutionReport> fix_to_execution_report(const FixMessage& msg) {
    if (msg.msg_type() != fix_msgtype::ExecutionReport) {
        return std::nullopt;
    }
    
    auto oid = msg.get_int(fix_tag::OrderID);
    if (!oid) {
        return std::nullopt;
    }
    
    ExecutionReport result;
    result.order_id = static_cast<OrderId>(*oid);
    result.exec_type = ExecutionType::NEW;
    result.order_status = OrderStatus::NEW;
    result.execution_price = 0;
    result.execution_quantity = 0;
    result.leaves_quantity = 0;
    result.cumulative_quantity = 0;
    result.side = Side::BUY;
    result.contra_order_id = 0;
    result.client_id = 0;
    result.timestamp = now();
    
    // Parse execution type
    auto exec_type = msg.get_field(fix_tag::ExecType);
    if (exec_type && !exec_type->empty()) {
        char c = (*exec_type)[0];
        if (c == '0') result.exec_type = ExecutionType::NEW;
        else if (c == 'F') result.exec_type = ExecutionType::TRADE;
        else if (c == '4') result.exec_type = ExecutionType::CANCELLED;
        else if (c == '8') result.exec_type = ExecutionType::REJECTED;
    }
    
    // Parse prices and quantities
    auto px = msg.get_double(fix_tag::LastPx);
    if (px) result.execution_price = to_fixed_price(*px);
    
    auto qty = msg.get_int(fix_tag::LastQty);
    if (qty) result.execution_quantity = *qty;
    
    auto leaves = msg.get_int(fix_tag::LeavesQty);
    if (leaves) result.leaves_quantity = *leaves;
    
    auto cum = msg.get_int(fix_tag::CumQty);
    if (cum) result.cumulative_quantity = *cum;
    
    // Parse side
    auto side_field = msg.get_field(fix_tag::Side);
    if (side_field && !side_field->empty()) {
        auto s = fix_to_side((*side_field)[0]);
        if (s) result.side = *s;
    }
    
    return result;
}

} // namespace hft

