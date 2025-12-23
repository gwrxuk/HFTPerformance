/**
 * @file socket_utils.hpp
 * @brief High-Performance Socket Utilities for HFT
 * 
 * When use_polling=true, we utilize busy-polling on socket file descriptors.
 * This is a Linux feature using setsockopt(..., SO_BUSY_POLL, ...) that allows
 * a thread to poll a socket buffer for a short time before the kernel schedules
 * a context switch.
 * 
 * Benefits:
 *   - Reduces kernel wake-up latency by 2-10 microseconds
 *   - More deterministic latency (lower jitter)
 *   - Trades CPU cycles for latency
 * 
 * Requirements:
 *   - Linux kernel 3.11+ for SO_BUSY_POLL
 *   - Root or CAP_NET_ADMIN for some settings
 *   - Dedicated CPU core for best results
 * 
 * System-wide settings (requires root):
 *   echo 50 > /proc/sys/net/core/busy_read
 *   echo 50 > /proc/sys/net/core/busy_poll
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <iostream>

#ifdef __linux__
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

// SO_BUSY_POLL may not be defined on older systems
#ifndef SO_BUSY_POLL
#define SO_BUSY_POLL 46
#endif

#ifndef SO_BUSY_POLL_BUDGET
#define SO_BUSY_POLL_BUDGET 70
#endif

#endif

namespace hft {

/**
 * @brief Socket optimization flags
 */
struct SocketOptions {
    bool busy_poll = false;          // Enable SO_BUSY_POLL
    int busy_poll_usec = 50;         // Microseconds to busy poll (default 50µs)
    bool tcp_nodelay = true;         // Disable Nagle's algorithm
    bool tcp_quickack = true;        // Send ACKs immediately
    bool so_reuseaddr = true;        // Allow address reuse
    bool so_reuseport = false;       // Allow port reuse (load balancing)
    int recv_buffer_size = 0;        // 0 = use default
    int send_buffer_size = 0;        // 0 = use default
    bool non_blocking = false;       // Non-blocking I/O
    int tcp_defer_accept = 0;        // Defer accept until data arrives
    bool so_keepalive = false;       // Enable keepalive
    bool so_timestamp = false;       // Hardware timestamps
};

/**
 * @brief Result of socket configuration
 */
struct SocketConfigResult {
    bool success = true;
    int busy_poll_status = 0;        // 0 = not attempted, 1 = success, -1 = failed
    int tcp_nodelay_status = 0;
    int tcp_quickack_status = 0;
    std::string error_message;
};

#ifdef __linux__

/**
 * @brief Enable SO_BUSY_POLL on a socket for low-latency polling
 * 
 * This allows the kernel to busy-poll the socket's receive queue for
 * a specified number of microseconds before blocking. This can reduce
 * latency by avoiding the cost of sleeping and waking up.
 * 
 * @param sockfd Socket file descriptor
 * @param usec Microseconds to busy poll (recommended: 50-100)
 * @return true if successful
 * 
 * Note: Also consider setting system-wide parameters:
 *   /proc/sys/net/core/busy_read = 50
 *   /proc/sys/net/core/busy_poll = 50
 */
inline bool enable_socket_busy_poll(int sockfd, int usec = 50) {
    int val = usec;
    if (setsockopt(sockfd, SOL_SOCKET, SO_BUSY_POLL, &val, sizeof(val)) < 0) {
        return false;
    }
    return true;
}

/**
 * @brief Disable Nagle's algorithm for lowest latency
 * 
 * TCP_NODELAY ensures that small packets are sent immediately without
 * waiting to coalesce with other data. Essential for HFT.
 */
inline bool enable_tcp_nodelay(int sockfd) {
    int val = 1;
    return setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)) == 0;
}

/**
 * @brief Enable TCP_QUICKACK for immediate acknowledgments
 * 
 * This disables delayed ACKs, reducing round-trip time for
 * request-response patterns.
 */
inline bool enable_tcp_quickack(int sockfd) {
    #ifdef TCP_QUICKACK
    int val = 1;
    return setsockopt(sockfd, IPPROTO_TCP, TCP_QUICKACK, &val, sizeof(val)) == 0;
    #else
    (void)sockfd;
    return false;
    #endif
}

/**
 * @brief Set socket buffer sizes
 * 
 * Larger buffers can help with burst traffic but add latency.
 * For HFT, smaller buffers are often preferred.
 */
inline bool set_socket_buffers(int sockfd, int recv_size, int send_size) {
    bool success = true;
    
    if (recv_size > 0) {
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &recv_size, sizeof(recv_size)) < 0) {
            success = false;
        }
    }
    
    if (send_size > 0) {
        if (setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &send_size, sizeof(send_size)) < 0) {
            success = false;
        }
    }
    
    return success;
}

/**
 * @brief Set socket to non-blocking mode
 */
inline bool set_non_blocking(int sockfd) {
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) >= 0;
}

/**
 * @brief Enable SO_REUSEADDR
 */
inline bool enable_reuse_addr(int sockfd) {
    int val = 1;
    return setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) == 0;
}

/**
 * @brief Enable SO_REUSEPORT for load balancing
 */
inline bool enable_reuse_port(int sockfd) {
    #ifdef SO_REUSEPORT
    int val = 1;
    return setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &val, sizeof(val)) == 0;
    #else
    (void)sockfd;
    return false;
    #endif
}

/**
 * @brief Enable hardware timestamps on socket
 * 
 * This allows receiving hardware timestamps for incoming packets,
 * useful for precise latency measurement.
 */
inline bool enable_timestamps(int sockfd) {
    #ifdef SO_TIMESTAMPNS
    int val = 1;
    return setsockopt(sockfd, SOL_SOCKET, SO_TIMESTAMPNS, &val, sizeof(val)) == 0;
    #else
    (void)sockfd;
    return false;
    #endif
}

/**
 * @brief Apply all socket optimizations for HFT
 * 
 * @param sockfd Socket file descriptor
 * @param opts Configuration options
 * @return Result indicating success/failure of each option
 */
inline SocketConfigResult configure_socket_for_hft(int sockfd, const SocketOptions& opts) {
    SocketConfigResult result;
    
    // SO_BUSY_POLL - the key optimization for low latency
    if (opts.busy_poll) {
        if (enable_socket_busy_poll(sockfd, opts.busy_poll_usec)) {
            result.busy_poll_status = 1;
        } else {
            result.busy_poll_status = -1;
            result.error_message += "SO_BUSY_POLL failed (may need root or kernel support); ";
        }
    }
    
    // TCP_NODELAY - disable Nagle's algorithm
    if (opts.tcp_nodelay) {
        if (enable_tcp_nodelay(sockfd)) {
            result.tcp_nodelay_status = 1;
        } else {
            result.tcp_nodelay_status = -1;
            result.error_message += "TCP_NODELAY failed; ";
        }
    }
    
    // TCP_QUICKACK - immediate ACKs
    if (opts.tcp_quickack) {
        if (enable_tcp_quickack(sockfd)) {
            result.tcp_quickack_status = 1;
        } else {
            result.tcp_quickack_status = -1;
            // Not critical, may not be available
        }
    }
    
    // SO_REUSEADDR
    if (opts.so_reuseaddr) {
        enable_reuse_addr(sockfd);
    }
    
    // SO_REUSEPORT
    if (opts.so_reuseport) {
        enable_reuse_port(sockfd);
    }
    
    // Buffer sizes
    if (opts.recv_buffer_size > 0 || opts.send_buffer_size > 0) {
        set_socket_buffers(sockfd, opts.recv_buffer_size, opts.send_buffer_size);
    }
    
    // Non-blocking
    if (opts.non_blocking) {
        if (!set_non_blocking(sockfd)) {
            result.error_message += "Non-blocking failed; ";
            result.success = false;
        }
    }
    
    // Timestamps
    if (opts.so_timestamp) {
        enable_timestamps(sockfd);
    }
    
    // TCP_DEFER_ACCEPT
    if (opts.tcp_defer_accept > 0) {
        #ifdef TCP_DEFER_ACCEPT
        setsockopt(sockfd, IPPROTO_TCP, TCP_DEFER_ACCEPT, 
                   &opts.tcp_defer_accept, sizeof(opts.tcp_defer_accept));
        #endif
    }
    
    return result;
}

/**
 * @brief Print socket configuration status
 */
inline void print_socket_config(const SocketConfigResult& result) {
    std::cout << "Socket configuration:\n";
    std::cout << "  SO_BUSY_POLL:  " << (result.busy_poll_status == 1 ? "✓ enabled" : 
                                         result.busy_poll_status == -1 ? "✗ failed" : "- skipped") << "\n";
    std::cout << "  TCP_NODELAY:   " << (result.tcp_nodelay_status == 1 ? "✓ enabled" : 
                                         result.tcp_nodelay_status == -1 ? "✗ failed" : "- skipped") << "\n";
    std::cout << "  TCP_QUICKACK:  " << (result.tcp_quickack_status == 1 ? "✓ enabled" : 
                                         result.tcp_quickack_status == -1 ? "✗ failed" : "- skipped") << "\n";
    if (!result.error_message.empty()) {
        std::cout << "  Warnings: " << result.error_message << "\n";
    }
}

/**
 * @brief Check if SO_BUSY_POLL is supported on this system
 */
inline bool is_busy_poll_supported() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) return false;
    
    int val = 50;
    bool supported = (setsockopt(sockfd, SOL_SOCKET, SO_BUSY_POLL, &val, sizeof(val)) == 0);
    close(sockfd);
    
    return supported;
}

/**
 * @brief Get recommended system tuning commands for HFT
 */
inline void print_system_tuning_recommendations() {
    std::cout << "\n=== Recommended System Tuning for HFT ===\n\n";
    
    std::cout << "1. Enable busy polling system-wide (requires root):\n";
    std::cout << "   echo 50 > /proc/sys/net/core/busy_read\n";
    std::cout << "   echo 50 > /proc/sys/net/core/busy_poll\n\n";
    
    std::cout << "2. Reduce network buffer bloat:\n";
    std::cout << "   sysctl -w net.core.rmem_default=262144\n";
    std::cout << "   sysctl -w net.core.wmem_default=262144\n\n";
    
    std::cout << "3. Disable TCP delayed ACKs:\n";
    std::cout << "   sysctl -w net.ipv4.tcp_low_latency=1\n\n";
    
    std::cout << "4. Increase socket backlog:\n";
    std::cout << "   sysctl -w net.core.somaxconn=65535\n\n";
    
    std::cout << "5. Pin IRQs to specific CPUs:\n";
    std::cout << "   echo CPU_MASK > /proc/irq/IRQ_NUM/smp_affinity\n\n";
    
    std::cout << "6. Disable CPU frequency scaling:\n";
    std::cout << "   cpupower frequency-set -g performance\n\n";
}

#else // Non-Linux systems

inline bool enable_socket_busy_poll(int, int) { return false; }
inline bool enable_tcp_nodelay(int) { return false; }
inline bool enable_tcp_quickack(int) { return false; }
inline bool set_socket_buffers(int, int, int) { return false; }
inline bool set_non_blocking(int) { return false; }
inline bool enable_reuse_addr(int) { return false; }
inline bool enable_reuse_port(int) { return false; }
inline bool enable_timestamps(int) { return false; }
inline bool is_busy_poll_supported() { return false; }

inline SocketConfigResult configure_socket_for_hft(int, const SocketOptions&) {
    SocketConfigResult result;
    result.success = false;
    result.error_message = "Socket optimizations only available on Linux";
    return result;
}

inline void print_socket_config(const SocketConfigResult&) {}
inline void print_system_tuning_recommendations() {
    std::cout << "Socket optimizations only available on Linux.\n";
}

#endif // __linux__

/**
 * @brief Low-latency UDP socket wrapper
 */
class LowLatencyUDPSocket {
public:
    LowLatencyUDPSocket() : sockfd_(-1) {}
    
    ~LowLatencyUDPSocket() {
        close_socket();
    }
    
    /**
     * @brief Create and configure a UDP socket for HFT
     */
    bool create(bool enable_busy_poll = true) {
        #ifdef __linux__
        sockfd_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd_ < 0) {
            return false;
        }
        
        SocketOptions opts;
        opts.busy_poll = enable_busy_poll;
        opts.busy_poll_usec = 50;
        opts.non_blocking = true;
        opts.so_reuseaddr = true;
        
        config_result_ = configure_socket_for_hft(sockfd_, opts);
        return config_result_.success;
        #else
        return false;
        #endif
    }
    
    /**
     * @brief Bind to a local address
     */
    bool bind(const char* ip, uint16_t port) {
        #ifdef __linux__
        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (ip) {
            inet_pton(AF_INET, ip, &addr.sin_addr);
        } else {
            addr.sin_addr.s_addr = INADDR_ANY;
        }
        
        return ::bind(sockfd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0;
        #else
        (void)ip; (void)port;
        return false;
        #endif
    }
    
    /**
     * @brief Join a multicast group
     */
    bool join_multicast(const char* group_ip, const char* local_ip = nullptr) {
        #ifdef __linux__
        struct ip_mreq mreq;
        inet_pton(AF_INET, group_ip, &mreq.imr_multiaddr);
        if (local_ip) {
            inet_pton(AF_INET, local_ip, &mreq.imr_interface);
        } else {
            mreq.imr_interface.s_addr = INADDR_ANY;
        }
        
        return setsockopt(sockfd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, 
                          &mreq, sizeof(mreq)) == 0;
        #else
        (void)group_ip; (void)local_ip;
        return false;
        #endif
    }
    
    /**
     * @brief Receive data with busy polling
     */
    ssize_t recv(void* buf, size_t len) {
        #ifdef __linux__
        return ::recv(sockfd_, buf, len, 0);
        #else
        (void)buf; (void)len;
        return -1;
        #endif
    }
    
    /**
     * @brief Send data
     */
    ssize_t sendto(const void* buf, size_t len, const char* dest_ip, uint16_t dest_port) {
        #ifdef __linux__
        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(dest_port);
        inet_pton(AF_INET, dest_ip, &addr.sin_addr);
        
        return ::sendto(sockfd_, buf, len, 0, 
                        reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
        #else
        (void)buf; (void)len; (void)dest_ip; (void)dest_port;
        return -1;
        #endif
    }
    
    int fd() const { return sockfd_; }
    const SocketConfigResult& config_result() const { return config_result_; }
    
    void close_socket() {
        #ifdef __linux__
        if (sockfd_ >= 0) {
            ::close(sockfd_);
            sockfd_ = -1;
        }
        #endif
    }

private:
    int sockfd_;
    SocketConfigResult config_result_;
};

} // namespace hft

