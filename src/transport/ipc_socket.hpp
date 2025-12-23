/**
 * @file ipc_socket.hpp
 * @brief Unix domain socket transport for order routing
 * 
 * Low-latency IPC for external mode - connects your trading system
 * to the exchange simulator.
 */

#pragma once

#include <string>
#include <cstdint>
#include <cstring>
#include <functional>
#include <atomic>
#include <thread>
#include <queue>
#include <mutex>

#ifdef __linux__
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#endif

#include "core/types.hpp"
#include "strategy/user_strategy.hpp"

namespace hft {

/**
 * @brief Wire format for order messages over IPC
 */
struct alignas(64) OrderPacket {
    uint64_t client_order_id;
    int64_t timestamp;
    char symbol[16];
    int64_t price;
    int64_t quantity;
    uint8_t side;       // 0=BUY, 1=SELL
    uint8_t order_type; // 0=MARKET, 1=LIMIT
    uint8_t action;     // 0=NEW, 1=CANCEL, 2=MODIFY
    uint8_t padding[5];
    uint32_t checksum;
};

/**
 * @brief Wire format for order response messages
 */
struct alignas(64) OrderResponsePacket {
    uint64_t client_order_id;
    uint64_t exchange_order_id;
    int64_t timestamp;
    int64_t fill_price;
    int64_t fill_quantity;
    int64_t leaves_quantity;
    uint8_t status;     // 0=NEW, 1=PARTIAL, 2=FILLED, 3=CANCELLED, 4=REJECTED
    uint8_t padding[7];
    uint32_t checksum;
};

/**
 * @brief IPC Socket Server (Exchange Simulator side)
 */
class IPCSocketServer {
public:
    using OrderCallback = std::function<void(const OrderPacket&, int client_fd)>;
    
    explicit IPCSocketServer(const std::string& socket_path)
        : socket_path_(socket_path), server_fd_(-1), running_(false) {}
    
    ~IPCSocketServer() {
        stop();
        close_socket();
    }
    
    bool init() {
        #ifdef __linux__
        // Remove existing socket file
        unlink(socket_path_.c_str());
        
        server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (server_fd_ < 0) return false;
        
        sockaddr_un addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);
        
        if (bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            close_socket();
            return false;
        }
        
        if (listen(server_fd_, 5) < 0) {
            close_socket();
            return false;
        }
        
        // Set non-blocking
        int flags = fcntl(server_fd_, F_GETFL, 0);
        fcntl(server_fd_, F_SETFL, flags | O_NONBLOCK);
        
        return true;
        #else
        return false;
        #endif
    }
    
    void start(OrderCallback callback) {
        running_ = true;
        server_thread_ = std::thread([this, callback]() {
            run_loop(callback);
        });
    }
    
    void stop() {
        running_ = false;
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }
    
    bool send_response(int client_fd, const OrderResponsePacket& response) {
        #ifdef __linux__
        ssize_t sent = send(client_fd, &response, sizeof(response), MSG_NOSIGNAL);
        return sent == sizeof(response);
        #else
        (void)client_fd;
        (void)response;
        return false;
        #endif
    }

private:
    void run_loop(OrderCallback callback) {
        #ifdef __linux__
        std::vector<pollfd> fds;
        fds.push_back({server_fd_, POLLIN, 0});
        
        while (running_) {
            int ret = poll(fds.data(), fds.size(), 10);  // 10ms timeout
            if (ret <= 0) continue;
            
            // Check for new connections
            if (fds[0].revents & POLLIN) {
                int client_fd = accept(server_fd_, nullptr, nullptr);
                if (client_fd >= 0) {
                    int flags = fcntl(client_fd, F_GETFL, 0);
                    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
                    fds.push_back({client_fd, POLLIN, 0});
                }
            }
            
            // Check client sockets
            for (size_t i = 1; i < fds.size(); ) {
                if (fds[i].revents & POLLIN) {
                    OrderPacket packet;
                    ssize_t received = recv(fds[i].fd, &packet, sizeof(packet), 0);
                    if (received == sizeof(packet)) {
                        callback(packet, fds[i].fd);
                    } else if (received <= 0) {
                        // Client disconnected
                        close(fds[i].fd);
                        fds.erase(fds.begin() + i);
                        continue;
                    }
                }
                ++i;
            }
        }
        
        // Close all client connections
        for (size_t i = 1; i < fds.size(); ++i) {
            close(fds[i].fd);
        }
        #else
        (void)callback;
        #endif
    }
    
    void close_socket() {
        #ifdef __linux__
        if (server_fd_ >= 0) {
            close(server_fd_);
            server_fd_ = -1;
        }
        unlink(socket_path_.c_str());
        #endif
    }

    std::string socket_path_;
    int server_fd_;
    std::atomic<bool> running_;
    std::thread server_thread_;
};

/**
 * @brief IPC Socket Client (Trading System side)
 */
class IPCSocketClient {
public:
    using ResponseCallback = std::function<void(const OrderResponsePacket&)>;
    
    explicit IPCSocketClient(const std::string& socket_path)
        : socket_path_(socket_path), socket_fd_(-1), running_(false) {}
    
    ~IPCSocketClient() {
        stop();
        close_socket();
    }
    
    bool connect() {
        #ifdef __linux__
        socket_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (socket_fd_ < 0) return false;
        
        sockaddr_un addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);
        
        if (::connect(socket_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            close_socket();
            return false;
        }
        
        return true;
        #else
        return false;
        #endif
    }
    
    bool send_order(const OrderPacket& order) {
        #ifdef __linux__
        if (socket_fd_ < 0) return false;
        ssize_t sent = send(socket_fd_, &order, sizeof(order), MSG_NOSIGNAL);
        return sent == sizeof(order);
        #else
        (void)order;
        return false;
        #endif
    }
    
    void start_receiver(ResponseCallback callback) {
        running_ = true;
        recv_thread_ = std::thread([this, callback]() {
            #ifdef __linux__
            OrderResponsePacket response;
            while (running_) {
                ssize_t received = recv(socket_fd_, &response, sizeof(response), 0);
                if (received == sizeof(response)) {
                    callback(response);
                } else if (received <= 0) {
                    break;  // Disconnected
                }
            }
            #else
            (void)callback;
            #endif
        });
    }
    
    void stop() {
        running_ = false;
        close_socket();  // This will unblock recv()
        if (recv_thread_.joinable()) {
            recv_thread_.join();
        }
    }
    
    // Blocking receive (for synchronous use)
    bool receive_response(OrderResponsePacket& response, int timeout_ms = 1000) {
        #ifdef __linux__
        if (socket_fd_ < 0) return false;
        
        pollfd pfd = {socket_fd_, POLLIN, 0};
        int ret = poll(&pfd, 1, timeout_ms);
        if (ret <= 0) return false;
        
        ssize_t received = recv(socket_fd_, &response, sizeof(response), 0);
        return received == sizeof(response);
        #else
        (void)response;
        (void)timeout_ms;
        return false;
        #endif
    }

private:
    void close_socket() {
        #ifdef __linux__
        if (socket_fd_ >= 0) {
            shutdown(socket_fd_, SHUT_RDWR);
            close(socket_fd_);
            socket_fd_ = -1;
        }
        #endif
    }

    std::string socket_path_;
    int socket_fd_;
    std::atomic<bool> running_;
    std::thread recv_thread_;
};

} // namespace hft

