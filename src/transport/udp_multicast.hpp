/**
 * @file udp_multicast.hpp
 * @brief UDP multicast transport for market data
 * 
 * Low-latency UDP multicast sender/receiver for external mode testing.
 */

#pragma once

#include <string>
#include <cstdint>
#include <cstring>
#include <functional>
#include <atomic>
#include <thread>

#ifdef __linux__
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#endif

#include "core/types.hpp"
#include "strategy/user_strategy.hpp"

namespace hft {

/**
 * @brief Wire format for market data packets
 */
struct alignas(64) MarketDataPacket {
    uint64_t sequence;
    int64_t timestamp;
    char symbol[16];
    int64_t bid_price;
    int64_t ask_price;
    int64_t bid_size;
    int64_t ask_size;
    int64_t last_price;
    int64_t last_size;
    uint32_t flags;
    uint32_t checksum;
};

static_assert(sizeof(MarketDataPacket) <= 128, "Packet too large");

/**
 * @brief UDP Multicast sender for market data
 */
class UDPMulticastSender {
public:
    UDPMulticastSender(const std::string& multicast_ip, uint16_t port)
        : multicast_ip_(multicast_ip), port_(port), socket_fd_(-1) {}
    
    ~UDPMulticastSender() {
        close_socket();
    }
    
    bool init() {
        #ifdef __linux__
        socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (socket_fd_ < 0) return false;
        
        // Set multicast TTL
        int ttl = 1;
        setsockopt(socket_fd_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
        
        // Set destination address
        std::memset(&dest_addr_, 0, sizeof(dest_addr_));
        dest_addr_.sin_family = AF_INET;
        dest_addr_.sin_port = htons(port_);
        inet_pton(AF_INET, multicast_ip_.c_str(), &dest_addr_.sin_addr);
        
        return true;
        #else
        return false;
        #endif
    }
    
    bool send(const MarketDataPacket& packet) {
        #ifdef __linux__
        if (socket_fd_ < 0) return false;
        
        ssize_t sent = sendto(socket_fd_, &packet, sizeof(packet), 0,
                              reinterpret_cast<sockaddr*>(&dest_addr_), 
                              sizeof(dest_addr_));
        return sent == sizeof(packet);
        #else
        (void)packet;
        return false;
        #endif
    }
    
    void close_socket() {
        #ifdef __linux__
        if (socket_fd_ >= 0) {
            close(socket_fd_);
            socket_fd_ = -1;
        }
        #endif
    }

private:
    std::string multicast_ip_;
    uint16_t port_;
    int socket_fd_;
    #ifdef __linux__
    sockaddr_in dest_addr_;
    #endif
};

/**
 * @brief UDP Multicast receiver for market data
 */
class UDPMulticastReceiver {
public:
    using PacketCallback = std::function<void(const MarketDataPacket&)>;
    
    UDPMulticastReceiver(const std::string& multicast_ip, uint16_t port,
                         const std::string& interface_ip = "0.0.0.0")
        : multicast_ip_(multicast_ip), port_(port), 
          interface_ip_(interface_ip), socket_fd_(-1), running_(false) {}
    
    ~UDPMulticastReceiver() {
        stop();
        close_socket();
    }
    
    bool init() {
        #ifdef __linux__
        socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (socket_fd_ < 0) return false;
        
        // Allow multiple sockets to bind to same port
        int reuse = 1;
        setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        
        // Bind to port
        sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        addr.sin_addr.s_addr = INADDR_ANY;
        
        if (bind(socket_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            close_socket();
            return false;
        }
        
        // Join multicast group
        ip_mreq mreq;
        inet_pton(AF_INET, multicast_ip_.c_str(), &mreq.imr_multiaddr);
        inet_pton(AF_INET, interface_ip_.c_str(), &mreq.imr_interface);
        
        if (setsockopt(socket_fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, 
                       &mreq, sizeof(mreq)) < 0) {
            close_socket();
            return false;
        }
        
        // Set non-blocking
        int flags = fcntl(socket_fd_, F_GETFL, 0);
        fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK);
        
        return true;
        #else
        return false;
        #endif
    }
    
    void start(PacketCallback callback) {
        running_ = true;
        recv_thread_ = std::thread([this, callback]() {
            run_loop(callback);
        });
    }
    
    void stop() {
        running_ = false;
        if (recv_thread_.joinable()) {
            recv_thread_.join();
        }
    }
    
    // Poll-based receive (for use in busy-wait loop)
    bool try_receive(MarketDataPacket& packet) {
        #ifdef __linux__
        if (socket_fd_ < 0) return false;
        
        ssize_t received = recv(socket_fd_, &packet, sizeof(packet), MSG_DONTWAIT);
        return received == sizeof(packet);
        #else
        (void)packet;
        return false;
        #endif
    }

private:
    void run_loop(PacketCallback callback) {
        #ifdef __linux__
        MarketDataPacket packet;
        while (running_) {
            ssize_t received = recv(socket_fd_, &packet, sizeof(packet), 0);
            if (received == sizeof(packet)) {
                callback(packet);
            }
        }
        #else
        (void)callback;
        #endif
    }
    
    void close_socket() {
        #ifdef __linux__
        if (socket_fd_ >= 0) {
            close(socket_fd_);
            socket_fd_ = -1;
        }
        #endif
    }

    std::string multicast_ip_;
    uint16_t port_;
    std::string interface_ip_;
    int socket_fd_;
    std::atomic<bool> running_;
    std::thread recv_thread_;
};

} // namespace hft

