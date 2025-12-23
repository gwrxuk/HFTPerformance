/**
 * @file websocket_handler.cpp
 * @brief WebSocket handler implementation
 */

#include "websocket_handler.hpp"
#include <cstring>
#include <random>
#include <sstream>
#include <iomanip>

#ifdef __linux__
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#endif

namespace hft {

// WebSocket GUID for handshake
static constexpr std::string_view WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

std::size_t WebSocketParser::parse(std::string_view data, WebSocketFrame& frame) {
    if (data.size() < 2) {
        return 0;  // Need more data
    }
    
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(data.data());
    
    // Parse first byte
    frame.fin = (bytes[0] & 0x80) != 0;
    frame.opcode = static_cast<WebSocketOpcode>(bytes[0] & 0x0F);
    
    // Parse second byte
    frame.masked = (bytes[1] & 0x80) != 0;
    std::uint64_t payload_len = bytes[1] & 0x7F;
    
    std::size_t header_len = 2;
    
    // Extended payload length
    if (payload_len == 126) {
        if (data.size() < 4) return 0;
        payload_len = (static_cast<std::uint64_t>(bytes[2]) << 8) | bytes[3];
        header_len = 4;
    } else if (payload_len == 127) {
        if (data.size() < 10) return 0;
        payload_len = 0;
        for (int i = 0; i < 8; ++i) {
            payload_len = (payload_len << 8) | bytes[2 + i];
        }
        header_len = 10;
    }
    
    // Masking key
    std::uint8_t mask[4] = {0, 0, 0, 0};
    if (frame.masked) {
        if (data.size() < header_len + 4) return 0;
        std::memcpy(mask, bytes + header_len, 4);
        header_len += 4;
    }
    
    // Check if we have complete frame
    if (data.size() < header_len + payload_len) {
        return 0;
    }
    
    // Extract payload
    frame.payload.resize(payload_len);
    std::memcpy(frame.payload.data(), bytes + header_len, payload_len);
    
    // Unmask if needed
    if (frame.masked) {
        apply_mask(frame.payload, mask);
    }
    
    return header_len + payload_len;
}

std::vector<std::uint8_t> WebSocketParser::encode(
    WebSocketOpcode opcode,
    std::string_view payload,
    bool mask
) {
    std::vector<std::uint8_t> frame;
    frame.reserve(14 + payload.size());  // Max header + payload
    
    // First byte: FIN + opcode
    frame.push_back(0x80 | static_cast<std::uint8_t>(opcode));
    
    // Second byte: mask flag + payload length
    std::uint8_t mask_bit = mask ? 0x80 : 0x00;
    
    if (payload.size() < 126) {
        frame.push_back(mask_bit | static_cast<std::uint8_t>(payload.size()));
    } else if (payload.size() < 65536) {
        frame.push_back(mask_bit | 126);
        frame.push_back(static_cast<std::uint8_t>(payload.size() >> 8));
        frame.push_back(static_cast<std::uint8_t>(payload.size() & 0xFF));
    } else {
        frame.push_back(mask_bit | 127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<std::uint8_t>(payload.size() >> (8 * i)));
        }
    }
    
    // Masking key (if client)
    std::uint8_t mask_key[4] = {0, 0, 0, 0};
    if (mask) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        for (int i = 0; i < 4; ++i) {
            mask_key[i] = static_cast<std::uint8_t>(dis(gen));
            frame.push_back(mask_key[i]);
        }
    }
    
    // Payload (masked if needed)
    std::size_t start = frame.size();
    frame.insert(frame.end(), payload.begin(), payload.end());
    
    if (mask) {
        for (std::size_t i = 0; i < payload.size(); ++i) {
            frame[start + i] ^= mask_key[i % 4];
        }
    }
    
    return frame;
}

void WebSocketParser::apply_mask(std::vector<std::uint8_t>& data,
                                  const std::uint8_t mask[4]) {
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] ^= mask[i % 4];
    }
}

std::string WebSocketParser::make_handshake_request(
    std::string_view host,
    std::string_view path,
    std::string_view key
) {
    std::ostringstream ss;
    ss << "GET " << path << " HTTP/1.1\r\n"
       << "Host: " << host << "\r\n"
       << "Upgrade: websocket\r\n"
       << "Connection: Upgrade\r\n"
       << "Sec-WebSocket-Key: " << key << "\r\n"
       << "Sec-WebSocket-Version: 13\r\n"
       << "\r\n";
    return ss.str();
}

std::string WebSocketParser::make_handshake_response(std::string_view key) {
    std::string accept = compute_accept_key(key);
    
    std::ostringstream ss;
    ss << "HTTP/1.1 101 Switching Protocols\r\n"
       << "Upgrade: websocket\r\n"
       << "Connection: Upgrade\r\n"
       << "Sec-WebSocket-Accept: " << accept << "\r\n"
       << "\r\n";
    return ss.str();
}

std::string WebSocketParser::compute_accept_key(std::string_view client_key) {
    // Simplified: would need proper SHA-1 + Base64
    // For demo, return a placeholder
    std::string combined(client_key);
    combined += WS_GUID;
    
    // TODO: Implement proper SHA-1 hash and Base64 encoding
    // This is a placeholder - in production, use OpenSSL or similar
    return "dGhlIHNhbXBsZSBub25jZQ==";  // Placeholder
}

bool WebSocketHandler::connect(std::string_view host, 
                                std::uint16_t port, 
                                std::string_view path) {
#ifdef __linux__
    // Resolve host
    struct addrinfo hints{}, *result;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    std::string host_str(host);
    std::string port_str = std::to_string(port);
    
    if (getaddrinfo(host_str.c_str(), port_str.c_str(), &hints, &result) != 0) {
        report_error("Failed to resolve host");
        return false;
    }
    
    // Create socket
    fd_ = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (fd_ < 0) {
        freeaddrinfo(result);
        report_error("Failed to create socket");
        return false;
    }
    
    // Set TCP_NODELAY for low latency
    int flag = 1;
    setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    
    // Connect
    set_state(WebSocketState::CONNECTING);
    if (::connect(fd_, result->ai_addr, result->ai_addrlen) < 0) {
        freeaddrinfo(result);
        ::close(fd_);
        fd_ = -1;
        set_state(WebSocketState::CLOSED);
        report_error("Failed to connect");
        return false;
    }
    
    freeaddrinfo(result);
    
    // Send handshake
    std::string key = "dGhlIHNhbXBsZSBub25jZQ==";  // Would generate random
    std::string handshake = WebSocketParser::make_handshake_request(host, path, key);
    
    if (::send(fd_, handshake.data(), handshake.size(), 0) < 0) {
        ::close(fd_);
        fd_ = -1;
        set_state(WebSocketState::CLOSED);
        report_error("Failed to send handshake");
        return false;
    }
    
    // Wait for response (simplified)
    recv_buffer_.resize(RECV_BUFFER_SIZE);
    ssize_t n = ::recv(fd_, recv_buffer_.data(), recv_buffer_.size(), 0);
    if (n <= 0) {
        ::close(fd_);
        fd_ = -1;
        set_state(WebSocketState::CLOSED);
        report_error("Failed to receive handshake response");
        return false;
    }
    
    // Verify response contains "101 Switching Protocols"
    std::string_view response(reinterpret_cast<char*>(recv_buffer_.data()), n);
    if (response.find("101") == std::string_view::npos) {
        ::close(fd_);
        fd_ = -1;
        set_state(WebSocketState::CLOSED);
        report_error("Invalid handshake response");
        return false;
    }
    
    // Set non-blocking
    int flags = fcntl(fd_, F_GETFL, 0);
    fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
    
    set_state(WebSocketState::OPEN);
    return true;
#else
    (void)host;
    (void)port;
    (void)path;
    report_error("WebSocket not supported on this platform");
    return false;
#endif
}

bool WebSocketHandler::accept(int client_fd, std::string_view request) {
    fd_ = client_fd;
    
    // Find Sec-WebSocket-Key
    auto key_pos = request.find("Sec-WebSocket-Key:");
    if (key_pos == std::string_view::npos) {
        report_error("Missing WebSocket key");
        return false;
    }
    
    auto key_start = request.find_first_not_of(" \t", key_pos + 18);
    auto key_end = request.find("\r\n", key_start);
    std::string_view key = request.substr(key_start, key_end - key_start);
    
    // Send response
    std::string response = WebSocketParser::make_handshake_response(key);
    
#ifdef __linux__
    if (::send(fd_, response.data(), response.size(), 0) < 0) {
        report_error("Failed to send handshake response");
        return false;
    }
    
    // Set non-blocking
    int flags = fcntl(fd_, F_GETFL, 0);
    fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
#endif
    
    set_state(WebSocketState::OPEN);
    return true;
}

bool WebSocketHandler::send_text(std::string_view message) {
    return send_frame(WebSocketOpcode::TEXT, message);
}

bool WebSocketHandler::send_binary(const void* data, std::size_t size) {
    return send_frame(WebSocketOpcode::BINARY, 
                      std::string_view(static_cast<const char*>(data), size));
}

bool WebSocketHandler::send_ping() {
    return send_frame(WebSocketOpcode::PING, "");
}

bool WebSocketHandler::send_frame(WebSocketOpcode opcode, std::string_view data) {
    if (!is_connected()) {
        return false;
    }
    
    auto frame = WebSocketParser::encode(opcode, data, true);  // Client masks
    
#ifdef __linux__
    ssize_t sent = ::send(fd_, frame.data(), frame.size(), MSG_NOSIGNAL);
    return sent == static_cast<ssize_t>(frame.size());
#else
    (void)frame;
    return false;
#endif
}

void WebSocketHandler::close() {
    if (state_.load(std::memory_order_acquire) == WebSocketState::CLOSED) {
        return;
    }
    
    set_state(WebSocketState::CLOSING);
    
    // Send close frame
    send_frame(WebSocketOpcode::CLOSE, "");
    
#ifdef __linux__
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
#endif
    
    set_state(WebSocketState::CLOSED);
}

void WebSocketHandler::poll() {
    if (!is_connected()) {
        return;
    }
    
#ifdef __linux__
    // Receive data
    recv_buffer_.resize(RECV_BUFFER_SIZE);
    ssize_t n = ::recv(fd_, recv_buffer_.data(), recv_buffer_.size(), MSG_DONTWAIT);
    
    if (n > 0) {
        // Append to partial buffer
        partial_frame_.insert(partial_frame_.end(), 
                              recv_buffer_.begin(), 
                              recv_buffer_.begin() + n);
        
        // Try to parse frames
        WebSocketFrame frame;
        std::string_view data(reinterpret_cast<char*>(partial_frame_.data()),
                              partial_frame_.size());
        
        while (!data.empty()) {
            std::size_t consumed = parser_.parse(data, frame);
            if (consumed == 0) {
                break;  // Need more data
            }
            
            handle_frame(frame);
            data.remove_prefix(consumed);
        }
        
        // Keep unconsumed data
        if (data.size() < partial_frame_.size()) {
            partial_frame_.erase(partial_frame_.begin(),
                                 partial_frame_.begin() + (partial_frame_.size() - data.size()));
        }
    } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        close();
    }
    
    // Send queued messages
    while (auto msg = send_queue_.try_pop()) {
        send_frame(msg->type, msg->data);
    }
#endif
}

void WebSocketHandler::set_state(WebSocketState new_state) {
    state_.store(new_state, std::memory_order_release);
    if (state_callback_) {
        state_callback_(new_state);
    }
}

void WebSocketHandler::handle_frame(const WebSocketFrame& frame) {
    switch (frame.opcode) {
        case WebSocketOpcode::TEXT:
        case WebSocketOpcode::BINARY:
            if (message_callback_) {
                message_callback_(frame);
            }
            break;
            
        case WebSocketOpcode::CLOSE:
            close();
            break;
            
        case WebSocketOpcode::PING:
            // Respond with pong
            send_frame(WebSocketOpcode::PONG, frame.payload_string());
            break;
            
        case WebSocketOpcode::PONG:
            // Ignore
            break;
            
        default:
            break;
    }
}

void WebSocketHandler::report_error(const std::string& error) {
    if (error_callback_) {
        error_callback_(error);
    }
}

// JSON utilities
namespace ws_json {

std::string build_subscribe(
    const std::vector<std::string>& symbols,
    const std::vector<std::string>& channels
) {
    std::ostringstream ss;
    ss << R"({"type":"subscribe","symbols":[)";
    
    for (std::size_t i = 0; i < symbols.size(); ++i) {
        if (i > 0) ss << ",";
        ss << "\"" << symbols[i] << "\"";
    }
    
    ss << R"(],"channels":[)";
    
    for (std::size_t i = 0; i < channels.size(); ++i) {
        if (i > 0) ss << ",";
        ss << "\"" << channels[i] << "\"";
    }
    
    ss << "]}";
    return ss.str();
}

std::string build_order(
    std::string_view symbol,
    std::string_view side,
    std::string_view type,
    double price,
    double quantity
) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(8);
    ss << R"({"type":"order","symbol":")" << symbol
       << R"(","side":")" << side
       << R"(","orderType":")" << type
       << R"(","price":)" << price
       << R"(,"quantity":)" << quantity
       << "}";
    return ss.str();
}

std::optional<TradeUpdate> parse_trade(std::string_view json) {
    // Simplified JSON parsing - in production use a proper JSON library
    TradeUpdate trade;
    trade.timestamp = now();
    
    // Find symbol
    auto sym_pos = json.find("\"symbol\"");
    if (sym_pos == std::string_view::npos) return std::nullopt;
    
    auto sym_start = json.find("\"", sym_pos + 8) + 1;
    auto sym_end = json.find("\"", sym_start);
    trade.symbol = std::string(json.substr(sym_start, sym_end - sym_start));
    
    // Find price
    auto price_pos = json.find("\"price\"");
    if (price_pos != std::string_view::npos) {
        auto price_start = json.find(":", price_pos) + 1;
        while (price_start < json.size() && 
               (json[price_start] == ' ' || json[price_start] == '"')) {
            ++price_start;
        }
        trade.price = std::stod(std::string(json.substr(price_start, 20)));
    }
    
    // Find quantity
    auto qty_pos = json.find("\"quantity\"");
    if (qty_pos == std::string_view::npos) {
        qty_pos = json.find("\"size\"");
    }
    if (qty_pos != std::string_view::npos) {
        auto qty_start = json.find(":", qty_pos) + 1;
        while (qty_start < json.size() && 
               (json[qty_start] == ' ' || json[qty_start] == '"')) {
            ++qty_start;
        }
        trade.quantity = std::stod(std::string(json.substr(qty_start, 20)));
    }
    
    // Find side
    auto side_pos = json.find("\"side\"");
    if (side_pos != std::string_view::npos) {
        auto side_start = json.find("\"", side_pos + 6) + 1;
        auto side_end = json.find("\"", side_start);
        trade.side = std::string(json.substr(side_start, side_end - side_start));
    }
    
    return trade;
}

std::optional<BookUpdate> parse_book_update(std::string_view json) {
    // Simplified - in production use nlohmann/json or similar
    BookUpdate update;
    update.timestamp = now();
    
    // Find symbol
    auto sym_pos = json.find("\"symbol\"");
    if (sym_pos == std::string_view::npos) return std::nullopt;
    
    auto sym_start = json.find("\"", sym_pos + 8) + 1;
    auto sym_end = json.find("\"", sym_start);
    update.symbol = std::string(json.substr(sym_start, sym_end - sym_start));
    
    // Would parse bids/asks arrays here
    // ...
    
    return update;
}

} // namespace ws_json

} // namespace hft

