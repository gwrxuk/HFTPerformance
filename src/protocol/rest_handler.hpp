/**
 * @file rest_handler.hpp
 * @brief HTTP/REST request handling for order gateway
 * 
 * Implements a minimal HTTP server for:
 * - Order submission
 * - Order cancellation
 * - Status queries
 * 
 * Optimized for low-latency with zero-copy parsing where possible.
 */

#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <functional>
#include <optional>
#include <vector>
#include "core/types.hpp"

namespace hft {

/**
 * @brief HTTP methods
 */
enum class HttpMethod {
    GET,
    POST,
    PUT,
    DELETE,
    OPTIONS,
    HEAD,
    UNKNOWN
};

/**
 * @brief HTTP status codes
 */
enum class HttpStatus : int {
    OK = 200,
    CREATED = 201,
    NO_CONTENT = 204,
    BAD_REQUEST = 400,
    UNAUTHORIZED = 401,
    FORBIDDEN = 403,
    NOT_FOUND = 404,
    METHOD_NOT_ALLOWED = 405,
    CONFLICT = 409,
    TOO_MANY_REQUESTS = 429,
    INTERNAL_ERROR = 500,
    SERVICE_UNAVAILABLE = 503
};

[[nodiscard]] constexpr std::string_view status_text(HttpStatus status) {
    switch (status) {
        case HttpStatus::OK: return "OK";
        case HttpStatus::CREATED: return "Created";
        case HttpStatus::NO_CONTENT: return "No Content";
        case HttpStatus::BAD_REQUEST: return "Bad Request";
        case HttpStatus::UNAUTHORIZED: return "Unauthorized";
        case HttpStatus::FORBIDDEN: return "Forbidden";
        case HttpStatus::NOT_FOUND: return "Not Found";
        case HttpStatus::METHOD_NOT_ALLOWED: return "Method Not Allowed";
        case HttpStatus::CONFLICT: return "Conflict";
        case HttpStatus::TOO_MANY_REQUESTS: return "Too Many Requests";
        case HttpStatus::INTERNAL_ERROR: return "Internal Server Error";
        case HttpStatus::SERVICE_UNAVAILABLE: return "Service Unavailable";
    }
    return "Unknown";
}

/**
 * @brief Parsed HTTP request
 */
struct HttpRequest {
    HttpMethod method = HttpMethod::UNKNOWN;
    std::string_view path;
    std::string_view query_string;
    std::string_view body;
    std::unordered_map<std::string, std::string_view> headers;
    std::unordered_map<std::string, std::string_view> query_params;
    std::unordered_map<std::string, std::string_view> path_params;
    
    [[nodiscard]] std::optional<std::string_view> get_header(std::string_view name) const;
    [[nodiscard]] std::optional<std::string_view> get_query_param(std::string_view name) const;
    [[nodiscard]] std::optional<std::string_view> get_path_param(std::string_view name) const;
};

/**
 * @brief HTTP response builder
 */
class HttpResponse {
public:
    HttpResponse() : status_(HttpStatus::OK) {}
    explicit HttpResponse(HttpStatus status) : status_(status) {}

    HttpResponse& status(HttpStatus s) {
        status_ = s;
        return *this;
    }

    HttpResponse& header(std::string_view name, std::string_view value) {
        headers_.emplace_back(name, value);
        return *this;
    }

    HttpResponse& content_type(std::string_view type) {
        return header("Content-Type", type);
    }

    HttpResponse& json(std::string_view body) {
        body_ = body;
        return content_type("application/json");
    }

    HttpResponse& body(std::string_view b) {
        body_ = b;
        return *this;
    }

    /**
     * @brief Build the HTTP response string
     */
    [[nodiscard]] std::string build() const;

    [[nodiscard]] HttpStatus get_status() const { return status_; }

private:
    HttpStatus status_;
    std::vector<std::pair<std::string_view, std::string_view>> headers_;
    std::string_view body_;
};

/**
 * @brief HTTP request parser (zero-copy)
 */
class HttpParser {
public:
    /**
     * @brief Parse an HTTP request
     * @param data Raw HTTP data
     * @param request Output request structure
     * @return Number of bytes consumed, 0 if incomplete, -1 if invalid
     */
    static int parse(std::string_view data, HttpRequest& request);

    /**
     * @brief Parse method string
     */
    static HttpMethod parse_method(std::string_view method);

    /**
     * @brief Parse query string into key-value pairs
     */
    static void parse_query_string(
        std::string_view query,
        std::unordered_map<std::string, std::string_view>& params
    );

    /**
     * @brief URL decode a string
     */
    static std::string url_decode(std::string_view str);
};

/**
 * @brief Route handler signature
 */
using RouteHandler = std::function<HttpResponse(const HttpRequest&)>;

/**
 * @brief Route definition
 */
struct Route {
    HttpMethod method;
    std::string pattern;
    RouteHandler handler;
    std::vector<std::string> param_names;  // Path parameter names
    
    [[nodiscard]] bool match(HttpMethod m, std::string_view path,
                             std::unordered_map<std::string, std::string_view>& params) const;
};

/**
 * @brief Minimal HTTP router
 */
class HttpRouter {
public:
    /**
     * @brief Register a route
     */
    void add_route(HttpMethod method, std::string_view pattern, RouteHandler handler);

    // Convenience methods
    void get(std::string_view pattern, RouteHandler handler) {
        add_route(HttpMethod::GET, pattern, std::move(handler));
    }
    
    void post(std::string_view pattern, RouteHandler handler) {
        add_route(HttpMethod::POST, pattern, std::move(handler));
    }
    
    void put(std::string_view pattern, RouteHandler handler) {
        add_route(HttpMethod::PUT, pattern, std::move(handler));
    }
    
    void del(std::string_view pattern, RouteHandler handler) {
        add_route(HttpMethod::DELETE, pattern, std::move(handler));
    }

    /**
     * @brief Route a request
     */
    [[nodiscard]] HttpResponse route(HttpRequest& request) const;

private:
    std::vector<Route> routes_;
};

/**
 * @brief JSON response builders for common responses
 */
namespace json_response {
    std::string error(std::string_view message, std::string_view code = "ERROR");
    std::string success(std::string_view message = "OK");
    
    std::string order_accepted(OrderId order_id, std::string_view symbol);
    std::string order_rejected(std::string_view reason);
    std::string order_cancelled(OrderId order_id);
    
    std::string quote(const Quote& quote, std::string_view symbol);
    std::string depth(const std::vector<std::pair<Price, Quantity>>& bids,
                      const std::vector<std::pair<Price, Quantity>>& asks,
                      std::string_view symbol);
}

/**
 * @brief Parse order request from JSON body
 */
struct OrderRequestJson {
    std::string symbol;
    Side side;
    OrderType type;
    double price;
    double quantity;
    std::string client_order_id;
};

std::optional<OrderRequestJson> parse_order_request(std::string_view json);

/**
 * @brief Minimal HTTP server (single-threaded)
 */
class HttpServer {
public:
    explicit HttpServer(std::uint16_t port);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    /**
     * @brief Get router for adding routes
     */
    HttpRouter& router() { return router_; }

    /**
     * @brief Start the server
     */
    bool start();

    /**
     * @brief Stop the server
     */
    void stop();

    /**
     * @brief Process pending connections (non-blocking)
     */
    void poll();

    /**
     * @brief Run the server (blocking)
     */
    void run();

    [[nodiscard]] bool is_running() const { return running_; }

private:
    void accept_connection();
    void handle_connection(int client_fd);

    std::uint16_t port_;
    int server_fd_;
    bool running_;
    HttpRouter router_;
};

} // namespace hft

