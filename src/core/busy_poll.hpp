/**
 * @file busy_poll.hpp
 * @brief High-Performance Busy Polling Utilities for HFT
 * 
 * Inter-thread communication is carefully designed to avoid mutexes.
 * We use busy polling on the consumer side for very short periods and
 * yield periodically to avoid 100% CPU burn in less demanding tests.
 * 
 * In high-throughput tests, the busy-poll is beneficial as it cuts down
 * the latency by avoiding context switch. Using Linux's sched_yield() or
 * nanosleep in the polling loop adds a lot of latency variance.
 * 
 * By default we prefer to dedicate the core and spin with backoff.
 * This aligns with the HFT philosophy: you spin a core waiting for work
 * because the cost is justified by latency gains.
 * 
 * Performance characteristics:
 *   - Pure spin:     ~50ns wake latency, 100% CPU
 *   - Spin + pause:  ~100ns wake latency, ~95% CPU (recommended)
 *   - Spin + yield:  ~1-10µs wake latency, ~50% CPU
 *   - nanosleep:     ~50-100µs wake latency, ~10% CPU
 */

#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <cstdint>
#include <thread>
#include <algorithm>

namespace hft {

/**
 * @brief CPU pause instruction - gives hint to processor that we're spinning
 * 
 * On x86: PAUSE instruction reduces power and avoids memory order violations
 * On ARM: YIELD instruction hints to processor
 */
inline void cpu_pause() noexcept {
    #if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
        __builtin_ia32_pause();
    #elif defined(__aarch64__) || defined(__arm__)
        asm volatile("yield" ::: "memory");
    #else
        // Fallback: compiler barrier
        asm volatile("" ::: "memory");
    #endif
}

/**
 * @brief Polling mode configuration
 */
enum class PollMode {
    AGGRESSIVE,    // Pure spin, no yield (lowest latency, 100% CPU)
    BALANCED,      // Spin with PAUSE, occasional yield (recommended)
    RELAXED,       // More frequent yields (lower CPU, higher latency)
    ADAPTIVE       // Adjusts based on observed wait times
};

/**
 * @brief Busy poll with exponential backoff
 * 
 * This is the core polling primitive. It spins waiting for a condition,
 * using exponential backoff to balance latency vs CPU usage.
 * 
 * Key insight: We avoid sched_yield() and nanosleep() because they add
 * significant latency variance (10µs-100µs). Instead, we use:
 *   1. PAUSE instruction (reduces power, ~10 cycles)
 *   2. Exponential backoff (reduces bus traffic)
 *   3. Only yield after exhausting spin budget
 * 
 * @tparam Predicate Callable returning bool (true = done waiting)
 * @param pred Condition to wait for
 * @param mode Polling mode
 * @param max_spin Maximum spin iterations before yielding
 * @return Number of iterations spent waiting
 */
template<typename Predicate>
[[nodiscard]] inline std::size_t busy_poll(
    Predicate pred,
    PollMode mode = PollMode::BALANCED,
    std::size_t max_spin = 10000
) noexcept {
    std::size_t iterations = 0;
    std::size_t backoff = 1;
    
    while (!pred()) {
        ++iterations;
        
        switch (mode) {
            case PollMode::AGGRESSIVE:
                // Pure spin - just check condition
                // Compiler barrier to prevent optimization
                asm volatile("" ::: "memory");
                break;
                
            case PollMode::BALANCED:
                // Spin with PAUSE instruction and exponential backoff
                for (std::size_t i = 0; i < backoff; ++i) {
                    cpu_pause();
                }
                // Exponential backoff: 1, 2, 4, 8, ... up to 64
                if (backoff < 64) {
                    backoff *= 2;
                }
                // Reset backoff periodically to stay responsive
                if (iterations % 1000 == 0) {
                    backoff = 1;
                }
                break;
                
            case PollMode::RELAXED:
                // More PAUSE instructions, yield more frequently
                for (std::size_t i = 0; i < 32; ++i) {
                    cpu_pause();
                }
                if (iterations % 100 == 0) {
                    // Note: We do NOT use std::this_thread::yield() here
                    // because it calls sched_yield() which has high variance.
                    // Instead, we just do more PAUSE instructions.
                    for (std::size_t i = 0; i < 256; ++i) {
                        cpu_pause();
                    }
                }
                break;
                
            case PollMode::ADAPTIVE:
                // Start aggressive, become more relaxed over time
                if (iterations < 100) {
                    cpu_pause();
                } else if (iterations < 1000) {
                    for (std::size_t i = 0; i < backoff; ++i) {
                        cpu_pause();
                    }
                    if (backoff < 32) backoff *= 2;
                } else {
                    for (std::size_t i = 0; i < 64; ++i) {
                        cpu_pause();
                    }
                }
                break;
        }
        
        // Only yield if we've been spinning too long (safety valve)
        // This prevents complete CPU starvation in edge cases
        if (iterations >= max_spin) {
            // IMPORTANT: Even in HFT, we eventually yield to prevent
            // system-wide starvation. But this should be rare.
            std::this_thread::yield();
            iterations = 0;  // Reset counter after yield
            backoff = 1;
        }
    }
    
    return iterations;
}

/**
 * @brief Busy poll with timeout
 * 
 * @tparam Predicate Callable returning bool
 * @param pred Condition to wait for
 * @param timeout Maximum time to wait
 * @param mode Polling mode
 * @return true if condition was met, false if timeout
 */
template<typename Predicate, typename Duration>
[[nodiscard]] inline bool busy_poll_for(
    Predicate pred,
    Duration timeout,
    PollMode mode = PollMode::BALANCED
) noexcept {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    
    while (!pred()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        
        // Use balanced polling during timed wait
        cpu_pause();
    }
    
    return true;
}

/**
 * @brief High-performance consumer loop pattern
 * 
 * This implements the recommended pattern for a dedicated consumer thread:
 * - Spin waiting for work with minimal latency
 * - Process work immediately when available
 * - Avoid context switches at all costs
 * 
 * Usage:
 *   consumer_loop(
 *       [&]{ return !running; },                    // Stop condition
 *       [&]{ return queue.try_pop(); },             // Check for work
 *       [](auto& item) { process(item); },          // Process work
 *       PollMode::BALANCED
 *   );
 */
template<typename StopPred, typename TryGetWork, typename ProcessWork>
inline void consumer_loop(
    StopPred should_stop,
    TryGetWork try_get_work,
    ProcessWork process,
    PollMode mode = PollMode::BALANCED
) {
    std::size_t empty_polls = 0;
    std::size_t backoff = 1;
    
    while (!should_stop()) {
        auto work = try_get_work();
        
        if (work) {
            // Work available - process immediately
            process(*work);
            empty_polls = 0;
            backoff = 1;
        } else {
            // No work - spin with backoff
            ++empty_polls;
            
            switch (mode) {
                case PollMode::AGGRESSIVE:
                    // Just check again immediately
                    asm volatile("" ::: "memory");
                    break;
                    
                case PollMode::BALANCED:
                    for (std::size_t i = 0; i < backoff; ++i) {
                        cpu_pause();
                    }
                    if (backoff < 64 && empty_polls > 10) {
                        backoff *= 2;
                    }
                    break;
                    
                case PollMode::RELAXED:
                case PollMode::ADAPTIVE:
                    for (std::size_t i = 0; i < std::min(backoff * 4, size_t{256}); ++i) {
                        cpu_pause();
                    }
                    if (backoff < 64) {
                        backoff *= 2;
                    }
                    break;
            }
            
            // Safety valve: yield after extended spinning
            if (empty_polls > 100000) {
                std::this_thread::yield();
                empty_polls = 0;
                backoff = 1;
            }
        }
    }
}

/**
 * @brief Spin-wait for a specific duration (busy wait)
 * 
 * This is used for precise timing when you can't afford the
 * variance of nanosleep or other sleep primitives.
 * 
 * Warning: Burns CPU. Only use when latency precision is critical.
 */
template<typename Duration>
inline void spin_wait(Duration duration) noexcept {
    auto target = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < target) {
        cpu_pause();
    }
}

/**
 * @brief Rate limiter using busy wait
 * 
 * Maintains a steady rate by spinning until the next scheduled time.
 * More precise than sleep-based rate limiters but uses more CPU.
 */
class BusyRateLimiter {
public:
    explicit BusyRateLimiter(std::size_t ops_per_second) noexcept
        : interval_ns_(1'000'000'000 / ops_per_second)
        , next_time_(std::chrono::steady_clock::now()) {}
    
    /**
     * @brief Wait until next scheduled slot
     */
    void wait() noexcept {
        // Spin until target time
        while (std::chrono::steady_clock::now() < next_time_) {
            cpu_pause();
        }
        
        // Schedule next
        next_time_ += std::chrono::nanoseconds(interval_ns_);
        
        // Catch up if we fell behind
        auto now = std::chrono::steady_clock::now();
        if (next_time_ < now) {
            next_time_ = now;
        }
    }
    
    /**
     * @brief Check if we can proceed without waiting
     */
    [[nodiscard]] bool try_acquire() noexcept {
        auto now = std::chrono::steady_clock::now();
        if (now >= next_time_) {
            next_time_ = now + std::chrono::nanoseconds(interval_ns_);
            return true;
        }
        return false;
    }

private:
    std::size_t interval_ns_;
    std::chrono::steady_clock::time_point next_time_;
};

} // namespace hft

