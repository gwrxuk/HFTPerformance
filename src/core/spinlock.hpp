/**
 * @file spinlock.hpp
 * @brief High-performance spinlock implementations
 * 
 * Spinlocks are preferred over mutexes in HFT when:
 * - Critical sections are very short
 * - Contention is low
 * - Thread cannot afford to be descheduled
 * 
 * Implementations:
 * - Spinlock: Basic test-and-set spinlock
 * - TicketSpinlock: Fair spinlock with FIFO ordering
 * - ReaderWriterSpinlock: Multiple readers, single writer
 */

#pragma once

#include <atomic>
#include <thread>
#include "types.hpp"

namespace hft {

/**
 * @brief Basic spinlock using test-and-test-and-set
 * 
 * Uses exponential backoff to reduce bus traffic under contention.
 */
class Spinlock {
public:
    Spinlock() noexcept : locked_(false) {}

    // Non-copyable
    Spinlock(const Spinlock&) = delete;
    Spinlock& operator=(const Spinlock&) = delete;

    void lock() noexcept {
        // Fast path: try to acquire immediately
        if (!locked_.exchange(true, std::memory_order_acquire)) {
            return;
        }
        
        // Slow path: spin with backoff
        lock_slow();
    }

    [[nodiscard]] bool try_lock() noexcept {
        // First check without writing (cache-friendly)
        if (locked_.load(std::memory_order_relaxed)) {
            return false;
        }
        return !locked_.exchange(true, std::memory_order_acquire);
    }

    void unlock() noexcept {
        locked_.store(false, std::memory_order_release);
    }

private:
    void lock_slow() noexcept {
        std::size_t spin_count = 0;
        constexpr std::size_t MAX_SPIN = 1000;
        
        while (true) {
            // Test before test-and-set (reduces bus traffic)
            if (!locked_.load(std::memory_order_relaxed)) {
                if (!locked_.exchange(true, std::memory_order_acquire)) {
                    return;
                }
            }
            
            // Exponential backoff
            if (spin_count < MAX_SPIN) {
                const std::size_t pause_count = static_cast<std::size_t>(1) << (spin_count / 100);
                for (std::size_t i = 0; i < pause_count; ++i) {
                    cpu_pause();
                }
                ++spin_count;
            } else {
                // Yield after too much spinning
                std::this_thread::yield();
            }
        }
    }

    static void cpu_pause() noexcept {
        #if defined(__x86_64__) || defined(_M_X64)
            __builtin_ia32_pause();
        #elif defined(__aarch64__)
            asm volatile("yield" ::: "memory");
        #endif
    }

    alignas(CACHE_LINE_SIZE) std::atomic<bool> locked_;
};

/**
 * @brief Fair ticket spinlock with FIFO ordering
 * 
 * Guarantees fairness by serving threads in order of arrival.
 * Slightly higher overhead than basic spinlock but prevents starvation.
 */
class TicketSpinlock {
public:
    TicketSpinlock() noexcept : next_ticket_(0), now_serving_(0) {}

    // Non-copyable
    TicketSpinlock(const TicketSpinlock&) = delete;
    TicketSpinlock& operator=(const TicketSpinlock&) = delete;

    void lock() noexcept {
        const auto my_ticket = next_ticket_.fetch_add(1, std::memory_order_relaxed);
        
        while (now_serving_.load(std::memory_order_acquire) != my_ticket) {
            cpu_pause();
        }
    }

    [[nodiscard]] bool try_lock() noexcept {
        auto current = now_serving_.load(std::memory_order_relaxed);
        auto next = next_ticket_.load(std::memory_order_relaxed);
        
        if (current != next) {
            return false;
        }
        
        return next_ticket_.compare_exchange_strong(
            next, next + 1, std::memory_order_acquire);
    }

    void unlock() noexcept {
        now_serving_.fetch_add(1, std::memory_order_release);
    }

private:
    static void cpu_pause() noexcept {
        #if defined(__x86_64__) || defined(_M_X64)
            __builtin_ia32_pause();
        #elif defined(__aarch64__)
            asm volatile("yield" ::: "memory");
        #endif
    }

    alignas(CACHE_LINE_SIZE) std::atomic<std::uint64_t> next_ticket_;
    alignas(CACHE_LINE_SIZE) std::atomic<std::uint64_t> now_serving_;
};

/**
 * @brief Reader-writer spinlock
 * 
 * Allows multiple concurrent readers or single exclusive writer.
 * Uses a single atomic counter with special encoding.
 */
class RWSpinlock {
    static constexpr std::uint32_t WRITER_BIT = 1U << 31;
    static constexpr std::uint32_t READER_MASK = ~WRITER_BIT;

public:
    RWSpinlock() noexcept : state_(0) {}

    // Non-copyable
    RWSpinlock(const RWSpinlock&) = delete;
    RWSpinlock& operator=(const RWSpinlock&) = delete;

    void lock_shared() noexcept {
        while (true) {
            // Wait while writer is active
            std::uint32_t state = state_.load(std::memory_order_relaxed);
            while (state & WRITER_BIT) {
                cpu_pause();
                state = state_.load(std::memory_order_relaxed);
            }
            
            // Try to increment reader count
            if (state_.compare_exchange_weak(
                    state, state + 1, 
                    std::memory_order_acquire, 
                    std::memory_order_relaxed)) {
                return;
            }
        }
    }

    [[nodiscard]] bool try_lock_shared() noexcept {
        std::uint32_t state = state_.load(std::memory_order_relaxed);
        if (state & WRITER_BIT) {
            return false;
        }
        return state_.compare_exchange_strong(
            state, state + 1, std::memory_order_acquire);
    }

    void unlock_shared() noexcept {
        state_.fetch_sub(1, std::memory_order_release);
    }

    void lock() noexcept {
        // Acquire writer bit
        while (true) {
            std::uint32_t state = state_.load(std::memory_order_relaxed);
            if (state & WRITER_BIT) {
                cpu_pause();
                continue;
            }
            
            if (state_.compare_exchange_weak(
                    state, state | WRITER_BIT,
                    std::memory_order_acquire,
                    std::memory_order_relaxed)) {
                break;
            }
        }
        
        // Wait for readers to drain
        while ((state_.load(std::memory_order_relaxed) & READER_MASK) != 0) {
            cpu_pause();
        }
    }

    [[nodiscard]] bool try_lock() noexcept {
        std::uint32_t expected = 0;
        return state_.compare_exchange_strong(
            expected, WRITER_BIT, std::memory_order_acquire);
    }

    void unlock() noexcept {
        state_.fetch_and(~WRITER_BIT, std::memory_order_release);
    }

private:
    static void cpu_pause() noexcept {
        #if defined(__x86_64__) || defined(_M_X64)
            __builtin_ia32_pause();
        #elif defined(__aarch64__)
            asm volatile("yield" ::: "memory");
        #endif
    }

    alignas(CACHE_LINE_SIZE) std::atomic<std::uint32_t> state_;
};

/**
 * @brief RAII lock guards
 */
template<typename Lock>
class SharedLockGuard {
public:
    explicit SharedLockGuard(Lock& lock) noexcept : lock_(lock) {
        lock_.lock_shared();
    }
    
    ~SharedLockGuard() {
        lock_.unlock_shared();
    }

    SharedLockGuard(const SharedLockGuard&) = delete;
    SharedLockGuard& operator=(const SharedLockGuard&) = delete;

private:
    Lock& lock_;
};

/**
 * @brief Scoped spinlock with try semantics
 */
template<typename Lock>
class TryLockGuard {
public:
    explicit TryLockGuard(Lock& lock) noexcept 
        : lock_(&lock), locked_(lock.try_lock()) {}
    
    ~TryLockGuard() {
        if (locked_) {
            lock_->unlock();
        }
    }

    TryLockGuard(const TryLockGuard&) = delete;
    TryLockGuard& operator=(const TryLockGuard&) = delete;

    [[nodiscard]] bool owns_lock() const noexcept { return locked_; }
    explicit operator bool() const noexcept { return locked_; }

private:
    Lock* lock_;
    bool locked_;
};

} // namespace hft

