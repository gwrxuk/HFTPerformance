/**
 * @file websocket_handler.hpp
 * @brief WebSocket protocol handler for real-time market data
 * 
 * Implements a lightweight WebSocket client/server for:
 * - Market data streaming
 * - Order submission
 * - Execution reports
 * 
 * Optimized for low-latency with minimal buffering.
 */

#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <memory>
#include <atomic>
#include <thread>
#include <cstdint>

#include "core/types.hpp"
#include "core/lockfree_queue.hpp"

namespace hft {

/**
 * @brief WebSocket frame types
 */
enum class WebSocketOpcode : std::uint8_t {
    CONTINUATION = 0x00,
    TEXT = 0x01,
    BINARY = 0x02,
    CLOSE = 0x08,
    PING = 0x09,
    PONG = 0x0A
};

/**
 * @brief WebSocket connection state
 */
enum class WebSocketState {
    CONNECTING,
    OPEN,
    CLOSING,
    CLOSED
};

/**
 * @brief Parsed WebSocket frame
 */
struct WebSocketFrame {
    WebSocketOpcode opcode;
    bool fin;
    bool masked;
    std::vector<std::uint8_t> payload;
    
    [[nodiscard]] std::string_view payload_string() const {
        return std::string_view(
            reinterpret_cast<const char*>(payload.data()), 
            payload.size()
        );
    }
};

/**
 * @brief WebSocket frame parser
 * 
 * Parses WebSocket frames according to RFC 6455.
 * Zero-copy where possible.
 */
class WebSocketParser {
public:
    /**
     * @brief Parse a WebSocket frame from raw data
     * 
     * @param data Input buffer
     * @param frame Output frame
     * @return Number of bytes consumed, or 0 if incomplete
     */
    std::size_t parse(std::string_view data, WebSocketFrame& frame);

    /**
     * @brief Encode a WebSocket frame
     * 
     * @param opcode Frame type
     * @param payload Frame data
     * @param mask Whether to mask (required for client)
     * @return Encoded frame
     */
    static std::vector<std::uint8_t> encode(
        WebSocketOpcode opcode, 
        std::string_view payload,
        bool mask = false
    );

    /**
     * @brief Generate WebSocket handshake request
     */
    static std::string make_handshake_request(
        std::string_view host,
        std::string_view path,
        std::string_view key
    );

    /**
     * @brief Generate WebSocket handshake response
     */
    static std::string make_handshake_response(std::string_view key);

    /**
     * @brief Compute Sec-WebSocket-Accept header value
     */
    static std::string compute_accept_key(std::string_view client_key);

private:
    static void apply_mask(std::vector<std::uint8_t>& data, 
                           const std::uint8_t mask[4]);
};

/**
 * @brief Message callback types
 */
using WebSocketMessageCallback = std::function<void(const WebSocketFrame&)>;
using WebSocketErrorCallback = std::function<void(const std::string&)>;
using WebSocketStateCallback = std::function<void(WebSocketState)>;

/**
 * @brief WebSocket message for queue
 */
struct WebSocketMessage {
    WebSocketOpcode type;
    std::string data;
    Timestamp timestamp;
    
    static WebSocketMessage text(std::string_view text) {
        return {WebSocketOpcode::TEXT, std::string(text), now()};
    }
    
    static WebSocketMessage binary(const std::vector<std::uint8_t>& data) {
        return {
            WebSocketOpcode::BINARY, 
            std::string(reinterpret_cast<const char*>(data.data()), data.size()),
            now()
        };
    }
};

/**
 * @brief Low-latency WebSocket connection handler
 * 
 * Designed for high-throughput, low-latency messaging.
 * Uses lock-free queues for message passing.
 */
class WebSocketHandler {
public:
    static constexpr std::size_t RECV_BUFFER_SIZE = 65536;
    static constexpr std::size_t SEND_QUEUE_SIZE = 4096;

    WebSocketHandler() : state_(WebSocketState::CLOSED), fd_(-1) {}
    virtual ~WebSocketHandler() { close(); }

    // Non-copyable
    WebSocketHandler(const WebSocketHandler&) = delete;
    WebSocketHandler& operator=(const WebSocketHandler&) = delete;

    /**
     * @brief Connect to a WebSocket server
     */
    bool connect(std::string_view host, std::uint16_t port, std::string_view path);

    /**
     * @brief Accept a WebSocket connection (server mode)
     */
    bool accept(int client_fd, std::string_view request);

    /**
     * @brief Send a text message
     */
    bool send_text(std::string_view message);

    /**
     * @brief Send a binary message
     */
    bool send_binary(const void* data, std::size_t size);

    /**
     * @brief Send a ping
     */
    bool send_ping();

    /**
     * @brief Close the connection
     */
    void close();

    /**
     * @brief Process incoming/outgoing data (call in event loop)
     */
    void poll();

    /**
     * @brief Check if connected
     */
    [[nodiscard]] bool is_connected() const {
        return state_.load(std::memory_order_acquire) == WebSocketState::OPEN;
    }

    /**
     * @brief Get connection state
     */
    [[nodiscard]] WebSocketState state() const {
        return state_.load(std::memory_order_acquire);
    }

    /**
     * @brief Set message callback
     */
    void on_message(WebSocketMessageCallback callback) {
        message_callback_ = std::move(callback);
    }

    /**
     * @brief Set error callback
     */
    void on_error(WebSocketErrorCallback callback) {
        error_callback_ = std::move(callback);
    }

    /**
     * @brief Set state change callback
     */
    void on_state_change(WebSocketStateCallback callback) {
        state_callback_ = std::move(callback);
    }

protected:
    void set_state(WebSocketState new_state);
    bool send_frame(WebSocketOpcode opcode, std::string_view data);
    void handle_frame(const WebSocketFrame& frame);
    void report_error(const std::string& error);

    std::atomic<WebSocketState> state_;
    int fd_;
    
    std::vector<std::uint8_t> recv_buffer_;
    std::vector<std::uint8_t> partial_frame_;
    
    WebSocketParser parser_;
    
    WebSocketMessageCallback message_callback_;
    WebSocketErrorCallback error_callback_;
    WebSocketStateCallback state_callback_;
    
    SPSCQueue<WebSocketMessage, SEND_QUEUE_SIZE> send_queue_;
};

/**
 * @brief JSON message utilities for common exchange formats
 */
namespace ws_json {
    /**
     * @brief Build a subscribe message (common exchange format)
     */
    std::string build_subscribe(
        const std::vector<std::string>& symbols,
        const std::vector<std::string>& channels
    );

    /**
     * @brief Build an order message
     */
    std::string build_order(
        std::string_view symbol,
        std::string_view side,
        std::string_view type,
        double price,
        double quantity
    );

    /**
     * @brief Parse a trade message
     */
    struct TradeUpdate {
        std::string symbol;
        double price;
        double quantity;
        std::string side;
        Timestamp timestamp;
    };

    std::optional<TradeUpdate> parse_trade(std::string_view json);

    /**
     * @brief Parse an order book update
     */
    struct BookUpdate {
        std::string symbol;
        std::vector<std::pair<double, double>> bids;  // price, quantity
        std::vector<std::pair<double, double>> asks;
        Timestamp timestamp;
    };

    std::optional<BookUpdate> parse_book_update(std::string_view json);
}

} // namespace hft

