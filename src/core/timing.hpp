/**
 * @file timing.hpp
 * @brief High-precision timing utilities for latency measurement
 * 
 * Provides multiple timing mechanisms:
 * - RDTSC-based timing for minimal overhead
 * - Chrono-based timing for portability
 * - Statistics collection for latency analysis
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <algorithm>
#include <vector>
#include <numeric>
#include <cmath>
#include <array>
#include <atomic>
#include <thread>
#include "types.hpp"

namespace hft {

/**
 * @brief Read Time Stamp Counter (x86-64 specific)
 * 
 * RDTSC provides the lowest-overhead timing available, measuring
 * CPU cycles since reset. Use TSC calibration for conversion to time.
 */
inline std::uint64_t rdtsc() noexcept {
    #if defined(__x86_64__) || defined(_M_X64)
        std::uint32_t lo, hi;
        asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
        return (static_cast<std::uint64_t>(hi) << 32) | lo;
    #elif defined(__aarch64__)
        std::uint64_t val;
        asm volatile("mrs %0, cntvct_el0" : "=r"(val));
        return val;
    #else
        return std::chrono::high_resolution_clock::now().time_since_epoch().count();
    #endif
}

/**
 * @brief RDTSCP - serializing version of RDTSC
 * 
 * Ensures all previous instructions complete before reading TSC.
 * Use for accurate end-point measurements.
 */
inline std::uint64_t rdtscp() noexcept {
    #if defined(__x86_64__) || defined(_M_X64)
        std::uint32_t lo, hi, aux;
        asm volatile("rdtscp" : "=a"(lo), "=d"(hi), "=c"(aux));
        return (static_cast<std::uint64_t>(hi) << 32) | lo;
    #else
        return rdtsc();
    #endif
}

/**
 * @brief Memory fence for timing accuracy
 */
inline void memory_fence() noexcept {
    #if defined(__x86_64__) || defined(_M_X64)
        asm volatile("mfence" ::: "memory");
    #else
        std::atomic_thread_fence(std::memory_order_seq_cst);
    #endif
}

/**
 * @brief TSC frequency calibrator
 * 
 * Calibrates TSC frequency by comparing against system clock.
 */
class TSCCalibrator {
public:
    /**
     * @brief Calibrate TSC frequency
     * @param duration Calibration duration in milliseconds
     * @return Estimated TSC frequency in Hz
     */
    static double calibrate(std::chrono::milliseconds duration = std::chrono::milliseconds(100)) {
        const auto start_time = std::chrono::high_resolution_clock::now();
        const auto start_tsc = rdtsc();
        
        std::this_thread::sleep_for(duration);
        
        const auto end_tsc = rdtscp();
        const auto end_time = std::chrono::high_resolution_clock::now();
        
        const auto elapsed = std::chrono::duration<double>(end_time - start_time).count();
        const auto tsc_delta = end_tsc - start_tsc;
        
        return static_cast<double>(tsc_delta) / elapsed;
    }

    /**
     * @brief Convert TSC ticks to nanoseconds
     */
    static double ticks_to_nanos(std::uint64_t ticks, double frequency) {
        return static_cast<double>(ticks) * 1e9 / frequency;
    }
};

/**
 * @brief Scoped timer for measuring code sections
 */
template<typename DurationT = std::chrono::nanoseconds>
class ScopedTimer {
public:
    explicit ScopedTimer(DurationT& output) noexcept
        : output_(output), start_(Clock::now()) {}

    ~ScopedTimer() {
        output_ = std::chrono::duration_cast<DurationT>(Clock::now() - start_);
    }

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    DurationT& output_;
    TimePoint start_;
};

/**
 * @brief TSC-based scoped timer for minimal overhead
 */
class TSCTimer {
public:
    explicit TSCTimer(std::uint64_t& output) noexcept
        : output_(output), start_(rdtsc()) {}

    ~TSCTimer() {
        output_ = rdtscp() - start_;
    }

    TSCTimer(const TSCTimer&) = delete;
    TSCTimer& operator=(const TSCTimer&) = delete;

private:
    std::uint64_t& output_;
    std::uint64_t start_;
};

/**
 * @brief Latency statistics collector
 * 
 * Collects samples and computes statistics useful for latency analysis.
 */
class LatencyStats {
public:
    explicit LatencyStats(std::size_t reserve_size = 10000) {
        samples_.reserve(reserve_size);
    }

    void add_sample(std::int64_t nanos) {
        samples_.push_back(nanos);
    }

    void add_sample_ns(Duration nanos) {
        samples_.push_back(nanos);
    }

    void clear() {
        samples_.clear();
    }

    [[nodiscard]] std::size_t count() const { return samples_.size(); }
    [[nodiscard]] bool empty() const { return samples_.empty(); }

    [[nodiscard]] double min() const {
        if (samples_.empty()) return 0.0;
        return static_cast<double>(*std::min_element(samples_.begin(), samples_.end()));
    }

    [[nodiscard]] double max() const {
        if (samples_.empty()) return 0.0;
        return static_cast<double>(*std::max_element(samples_.begin(), samples_.end()));
    }

    [[nodiscard]] double mean() const {
        if (samples_.empty()) return 0.0;
        const auto sum = std::accumulate(samples_.begin(), samples_.end(), 0LL);
        return static_cast<double>(sum) / static_cast<double>(samples_.size());
    }

    [[nodiscard]] double median() const {
        return percentile(50.0);
    }

    [[nodiscard]] double percentile(double p) const {
        if (samples_.empty()) return 0.0;
        
        std::vector<std::int64_t> sorted = samples_;
        std::sort(sorted.begin(), sorted.end());
        
        const double rank = (p / 100.0) * static_cast<double>(sorted.size() - 1);
        const auto lower = static_cast<std::size_t>(rank);
        const auto upper = std::min(lower + 1, sorted.size() - 1);
        const double frac = rank - static_cast<double>(lower);
        
        return static_cast<double>(sorted[lower]) * (1.0 - frac) + 
               static_cast<double>(sorted[upper]) * frac;
    }

    [[nodiscard]] double stddev() const {
        if (samples_.size() < 2) return 0.0;
        
        const double avg = mean();
        double sum_sq = 0.0;
        for (const auto& sample : samples_) {
            const double diff = static_cast<double>(sample) - avg;
            sum_sq += diff * diff;
        }
        return std::sqrt(sum_sq / static_cast<double>(samples_.size() - 1));
    }

    /**
     * @brief Get common percentiles (p50, p90, p95, p99, p99.9)
     */
    struct Percentiles {
        double p50, p90, p95, p99, p999;
    };

    [[nodiscard]] Percentiles get_percentiles() const {
        return {
            percentile(50.0),
            percentile(90.0),
            percentile(95.0),
            percentile(99.0),
            percentile(99.9)
        };
    }

    /**
     * @brief Print statistics summary
     */
    void print_summary(const char* label = "Latency") const {
        const auto p = get_percentiles();
        std::printf("%s Statistics (n=%zu):\n", label, samples_.size());
        std::printf("  Min:    %.2f ns\n", min());
        std::printf("  Max:    %.2f ns\n", max());
        std::printf("  Mean:   %.2f ns\n", mean());
        std::printf("  StdDev: %.2f ns\n", stddev());
        std::printf("  P50:    %.2f ns\n", p.p50);
        std::printf("  P90:    %.2f ns\n", p.p90);
        std::printf("  P95:    %.2f ns\n", p.p95);
        std::printf("  P99:    %.2f ns\n", p.p99);
        std::printf("  P99.9:  %.2f ns\n", p.p999);
    }

private:
    std::vector<std::int64_t> samples_;
};

/**
 * @brief Histogram for latency distribution
 * 
 * Fixed-bucket histogram optimized for typical latency ranges.
 */
template<std::size_t BucketCount = 100>
class LatencyHistogram {
public:
    explicit LatencyHistogram(std::int64_t bucket_width_ns = 100)
        : bucket_width_(bucket_width_ns), count_(0) {
        buckets_.fill(0);
    }

    void record(std::int64_t nanos) {
        const auto bucket = std::min(
            static_cast<std::size_t>(nanos / bucket_width_),
            BucketCount - 1
        );
        ++buckets_[bucket];
        ++count_;
    }

    void reset() {
        buckets_.fill(0);
        count_ = 0;
    }

    [[nodiscard]] std::size_t total_count() const { return count_; }
    [[nodiscard]] std::int64_t bucket_width() const { return bucket_width_; }

    [[nodiscard]] std::size_t bucket_count(std::size_t bucket) const {
        return bucket < BucketCount ? buckets_[bucket] : 0;
    }

    void print_histogram() const {
        const auto max_count = *std::max_element(buckets_.begin(), buckets_.end());
        constexpr std::size_t BAR_WIDTH = 50;
        
        std::printf("Latency Histogram (bucket=%ldns, total=%zu):\n", 
                    bucket_width_, count_);
        
        for (std::size_t i = 0; i < BucketCount; ++i) {
            if (buckets_[i] == 0) continue;
            
            const auto bar_len = (buckets_[i] * BAR_WIDTH) / max_count;
            std::printf("%6ldns: ", static_cast<long>(i * bucket_width_));
            for (std::size_t j = 0; j < bar_len; ++j) {
                std::printf("█");
            }
            std::printf(" %zu\n", buckets_[i]);
        }
    }

private:
    std::int64_t bucket_width_;
    std::array<std::size_t, BucketCount> buckets_;
    std::size_t count_;
};

} // namespace hft

