/**
 * @file rest_handler.cpp
 * @brief HTTP/REST handler implementation
 */

#include "rest_handler.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>

#ifdef __linux__
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>
#endif

namespace hft {

std::optional<std::string_view> HttpRequest::get_header(std::string_view name) const {
    std::string lower_name(name);
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
    
    for (const auto& [key, value] : headers) {
        std::string lower_key(key);
        std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(), ::tolower);
        if (lower_key == lower_name) {
            return value;
        }
    }
    return std::nullopt;
}

std::optional<std::string_view> HttpRequest::get_query_param(std::string_view name) const {
    std::string name_str(name);
    auto it = query_params.find(name_str);
    if (it != query_params.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<std::string_view> HttpRequest::get_path_param(std::string_view name) const {
    std::string name_str(name);
    auto it = path_params.find(name_str);
    if (it != path_params.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::string HttpResponse::build() const {
    std::ostringstream ss;
    ss << "HTTP/1.1 " << static_cast<int>(status_) << " " << status_text(status_) << "\r\n";
    
    // Default headers
    ss << "Connection: keep-alive\r\n";
    ss << "Content-Length: " << body_.size() << "\r\n";
    
    // Custom headers
    for (const auto& [name, value] : headers_) {
        ss << name << ": " << value << "\r\n";
    }
    
    ss << "\r\n";
    ss << body_;
    
    return ss.str();
}

int HttpParser::parse(std::string_view data, HttpRequest& request) {
    // Find end of headers
    auto header_end = data.find("\r\n\r\n");
    if (header_end == std::string_view::npos) {
        return 0;  // Incomplete
    }
    
    // Parse request line
    auto line_end = data.find("\r\n");
    std::string_view request_line = data.substr(0, line_end);
    
    // Extract method
    auto method_end = request_line.find(' ');
    if (method_end == std::string_view::npos) {
        return -1;  // Invalid
    }
    request.method = parse_method(request_line.substr(0, method_end));
    
    // Extract path
    auto path_start = method_end + 1;
    auto path_end = request_line.find(' ', path_start);
    if (path_end == std::string_view::npos) {
        return -1;
    }
    
    std::string_view uri = request_line.substr(path_start, path_end - path_start);
    
    // Split path and query string
    auto query_pos = uri.find('?');
    if (query_pos != std::string_view::npos) {
        request.path = uri.substr(0, query_pos);
        request.query_string = uri.substr(query_pos + 1);
        parse_query_string(request.query_string, request.query_params);
    } else {
        request.path = uri;
    }
    
    // Parse headers
    std::size_t pos = line_end + 2;
    while (pos < header_end) {
        auto next_line = data.find("\r\n", pos);
        std::string_view header_line = data.substr(pos, next_line - pos);
        
        auto colon_pos = header_line.find(':');
        if (colon_pos != std::string_view::npos) {
            std::string name(header_line.substr(0, colon_pos));
            std::string_view value = header_line.substr(colon_pos + 1);
            
            // Trim leading whitespace from value
            while (!value.empty() && value[0] == ' ') {
                value.remove_prefix(1);
            }
            
            request.headers[name] = value;
        }
        
        pos = next_line + 2;
    }
    
    // Get body if Content-Length specified
    std::size_t content_length = 0;
    auto cl_it = request.headers.find("Content-Length");
    if (cl_it != request.headers.end()) {
        content_length = std::stoul(std::string(cl_it->second));
    }
    
    std::size_t total_length = header_end + 4 + content_length;
    if (data.size() < total_length) {
        return 0;  // Need more data
    }
    
    if (content_length > 0) {
        request.body = data.substr(header_end + 4, content_length);
    }
    
    return static_cast<int>(total_length);
}

HttpMethod HttpParser::parse_method(std::string_view method) {
    if (method == "GET") return HttpMethod::GET;
    if (method == "POST") return HttpMethod::POST;
    if (method == "PUT") return HttpMethod::PUT;
    if (method == "DELETE") return HttpMethod::DELETE;
    if (method == "OPTIONS") return HttpMethod::OPTIONS;
    if (method == "HEAD") return HttpMethod::HEAD;
    return HttpMethod::UNKNOWN;
}

void HttpParser::parse_query_string(
    std::string_view query,
    std::unordered_map<std::string, std::string_view>& params
) {
    std::size_t pos = 0;
    while (pos < query.size()) {
        auto amp_pos = query.find('&', pos);
        std::string_view pair = query.substr(pos, 
            amp_pos == std::string_view::npos ? std::string_view::npos : amp_pos - pos);
        
        auto eq_pos = pair.find('=');
        if (eq_pos != std::string_view::npos) {
            std::string name(pair.substr(0, eq_pos));
            std::string_view value = pair.substr(eq_pos + 1);
            params[name] = value;
        }
        
        if (amp_pos == std::string_view::npos) break;
        pos = amp_pos + 1;
    }
}

std::string HttpParser::url_decode(std::string_view str) {
    std::string result;
    result.reserve(str.size());
    
    for (std::size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%' && i + 2 < str.size()) {
            char hex[3] = {str[i + 1], str[i + 2], 0};
            result += static_cast<char>(std::strtol(hex, nullptr, 16));
            i += 2;
        } else if (str[i] == '+') {
            result += ' ';
        } else {
            result += str[i];
        }
    }
    
    return result;
}

bool Route::match(HttpMethod m, std::string_view path,
                  std::unordered_map<std::string, std::string_view>& params) const {
    if (m != method) return false;
    
    // Simple pattern matching with path parameters
    // Pattern format: /path/:param/more/:param2
    
    std::size_t pattern_pos = 0;
    std::size_t path_pos = 0;
    std::size_t param_idx = 0;
    
    while (pattern_pos < pattern.size() && path_pos < path.size()) {
        if (pattern[pattern_pos] == ':') {
            // Path parameter
            auto param_end = pattern.find('/', pattern_pos);
            if (param_end == std::string::npos) {
                param_end = pattern.size();
            }
            
            auto value_end = path.find('/', path_pos);
            if (value_end == std::string_view::npos) {
                value_end = path.size();
            }
            
            if (param_idx < param_names.size()) {
                params[param_names[param_idx]] = path.substr(path_pos, value_end - path_pos);
            }
            ++param_idx;
            
            pattern_pos = param_end;
            path_pos = value_end;
        } else if (pattern[pattern_pos] == path[path_pos]) {
            ++pattern_pos;
            ++path_pos;
        } else {
            return false;
        }
    }
    
    return pattern_pos == pattern.size() && path_pos == path.size();
}

void HttpRouter::add_route(HttpMethod method, std::string_view pattern, RouteHandler handler) {
    Route route;
    route.method = method;
    route.pattern = std::string(pattern);
    route.handler = std::move(handler);
    
    // Extract parameter names
    std::size_t pos = 0;
    while ((pos = route.pattern.find(':', pos)) != std::string::npos) {
        auto end = route.pattern.find('/', pos);
        if (end == std::string::npos) {
            end = route.pattern.size();
        }
        route.param_names.push_back(route.pattern.substr(pos + 1, end - pos - 1));
        pos = end;
    }
    
    routes_.push_back(std::move(route));
}

HttpResponse HttpRouter::route(HttpRequest& request) const {
    for (const auto& r : routes_) {
        if (r.match(request.method, request.path, request.path_params)) {
            return r.handler(request);
        }
    }
    
    return HttpResponse(HttpStatus::NOT_FOUND)
        .json(json_response::error("Route not found", "NOT_FOUND"));
}

namespace json_response {

std::string error(std::string_view message, std::string_view code) {
    std::ostringstream ss;
    ss << R"({"error":{"code":")" << code 
       << R"(","message":")" << message << R"("}})";
    return ss.str();
}

std::string success(std::string_view message) {
    std::ostringstream ss;
    ss << R"({"success":true,"message":")" << message << R"("})";
    return ss.str();
}

std::string order_accepted(OrderId order_id, std::string_view symbol) {
    std::ostringstream ss;
    ss << R"({"success":true,"orderId":")" << order_id
       << R"(","symbol":")" << symbol << R"("})";
    return ss.str();
}

std::string order_rejected(std::string_view reason) {
    return error(reason, "ORDER_REJECTED");
}

std::string order_cancelled(OrderId order_id) {
    std::ostringstream ss;
    ss << R"({"success":true,"orderId":")" << order_id
       << R"(","status":"CANCELLED"})";
    return ss.str();
}

std::string quote(const Quote& q, std::string_view symbol) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(8);
    ss << R"({"symbol":")" << symbol
       << R"(","bidPrice":)" << to_double_price(q.bid_price)
       << R"(,"askPrice":)" << to_double_price(q.ask_price)
       << R"(,"bidQty":)" << q.bid_quantity
       << R"(,"askQty":)" << q.ask_quantity
       << R"(,"spread":)" << to_double_price(q.spread())
       << R"(,"timestamp":)" << q.timestamp << "}";
    return ss.str();
}

std::string depth(const std::vector<std::pair<Price, Quantity>>& bids,
                  const std::vector<std::pair<Price, Quantity>>& asks,
                  std::string_view symbol) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(8);
    ss << R"({"symbol":")" << symbol << R"(","bids":[)";
    
    for (std::size_t i = 0; i < bids.size(); ++i) {
        if (i > 0) ss << ",";
        ss << "[" << to_double_price(bids[i].first) << "," << bids[i].second << "]";
    }
    
    ss << R"(],"asks":[)";
    
    for (std::size_t i = 0; i < asks.size(); ++i) {
        if (i > 0) ss << ",";
        ss << "[" << to_double_price(asks[i].first) << "," << asks[i].second << "]";
    }
    
    ss << "]}";
    return ss.str();
}

} // namespace json_response

std::optional<OrderRequestJson> parse_order_request(std::string_view json) {
    OrderRequestJson req;
    
    // Simple JSON parsing - in production use nlohmann/json
    auto find_string = [&json](std::string_view key) -> std::optional<std::string> {
        auto pos = json.find(key);
        if (pos == std::string_view::npos) return std::nullopt;
        auto start = json.find('"', pos + key.size() + 2) + 1;
        auto end = json.find('"', start);
        return std::string(json.substr(start, end - start));
    };
    
    auto find_number = [&json](std::string_view key) -> std::optional<double> {
        auto pos = json.find(key);
        if (pos == std::string_view::npos) return std::nullopt;
        auto start = json.find(':', pos) + 1;
        while (start < json.size() && (json[start] == ' ' || json[start] == '"')) {
            ++start;
        }
        return std::stod(std::string(json.substr(start, 20)));
    };
    
    auto symbol = find_string("symbol");
    if (!symbol) return std::nullopt;
    req.symbol = *symbol;
    
    auto side = find_string("side");
    if (!side) return std::nullopt;
    req.side = (*side == "BUY" || *side == "buy") ? Side::BUY : Side::SELL;
    
    auto type = find_string("type");
    if (type) {
        if (*type == "LIMIT" || *type == "limit") {
            req.type = OrderType::LIMIT;
        } else if (*type == "MARKET" || *type == "market") {
            req.type = OrderType::MARKET;
        } else {
            req.type = OrderType::LIMIT;
        }
    } else {
        req.type = OrderType::LIMIT;
    }
    
    auto price = find_number("price");
    req.price = price.value_or(0.0);
    
    auto quantity = find_number("quantity");
    if (!quantity) return std::nullopt;
    req.quantity = *quantity;
    
    auto client_id = find_string("clientOrderId");
    if (client_id) {
        req.client_order_id = *client_id;
    }
    
    return req;
}

// HTTP Server implementation
HttpServer::HttpServer(std::uint16_t port) 
    : port_(port), server_fd_(-1), running_(false) {}

HttpServer::~HttpServer() {
    stop();
}

bool HttpServer::start() {
#ifdef __linux__
    // Create socket
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        return false;
    }
    
    // Set socket options
    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(server_fd_, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
    
    // Bind
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);
    
    if (bind(server_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }
    
    // Listen
    if (listen(server_fd_, 128) < 0) {
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }
    
    // Set non-blocking
    int flags = fcntl(server_fd_, F_GETFL, 0);
    fcntl(server_fd_, F_SETFL, flags | O_NONBLOCK);
    
    running_ = true;
    return true;
#else
    return false;
#endif
}

void HttpServer::stop() {
    running_ = false;
#ifdef __linux__
    if (server_fd_ >= 0) {
        close(server_fd_);
        server_fd_ = -1;
    }
#endif
}

void HttpServer::poll() {
    if (!running_) return;
    accept_connection();
}

void HttpServer::run() {
    while (running_) {
        poll();
    }
}

void HttpServer::accept_connection() {
#ifdef __linux__
    struct sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    
    int client_fd = accept(server_fd_, 
                           reinterpret_cast<struct sockaddr*>(&client_addr), 
                           &client_len);
    
    if (client_fd >= 0) {
        // Set TCP_NODELAY
        int opt = 1;
        setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
        
        handle_connection(client_fd);
        close(client_fd);
    }
#endif
}

void HttpServer::handle_connection(int client_fd) {
#ifdef __linux__
    char buffer[8192];
    ssize_t n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    
    if (n <= 0) return;
    buffer[n] = '\0';
    
    HttpRequest request;
    int parsed = HttpParser::parse(std::string_view(buffer, n), request);
    
    if (parsed <= 0) {
        // Invalid or incomplete request
        HttpResponse response(HttpStatus::BAD_REQUEST);
        response.json(json_response::error("Invalid request", "BAD_REQUEST"));
        std::string resp_str = response.build();
        send(client_fd, resp_str.data(), resp_str.size(), MSG_NOSIGNAL);
        return;
    }
    
    // Route and handle
    HttpResponse response = router_.route(request);
    std::string resp_str = response.build();
    send(client_fd, resp_str.data(), resp_str.size(), MSG_NOSIGNAL);
#else
    (void)client_fd;
#endif
}

} // namespace hft

