/**
 * @file timestamp_buffer.hpp
 * @brief Thread-Local Timestamp Buffer for Contention-Free Recording
 * 
 * Timestamps are recorded into a thread-local buffer to avoid synchronization
 * at record-time. Each thread writes its events (with sequence numbers) to an
 * array. Only after the test, or when a buffer fills up, do we aggregate them.
 * 
 * Design benefits:
 *   - No contention between threads during recording
 *   - No cache line bouncing (each thread has its own buffer)
 *   - No perturbation of other threads' caches
 *   - Logging/aggregation happens after the measured run
 * 
 * Recommendation: Have an extra core available for post-processing, or do
 * aggregation after the measured run to keep measurements clean.
 * 
 * Buffer layout (per thread):
 *   [event1][event2][event3]...[eventN]
 *   
 * Each event contains:
 *   - timestamp (TSC ticks or nanoseconds)
 *   - event_type (user-defined enum)
 *   - sequence_number (for ordering across threads)
 *   - payload (optional 64-bit value)
 */

#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <atomic>
#include <thread>
#include <functional>
#include <algorithm>
#include <mutex>
#include <iostream>
#include <memory>
#include <fstream>
#include <unordered_map>

#include "types.hpp"
#include "timing.hpp"

namespace hft {

/**
 * @brief Timestamp event types
 */
enum class EventType : std::uint8_t {
    TICK_GENERATED = 0,
    TICK_RECEIVED = 1,
    STRATEGY_START = 2,
    STRATEGY_END = 3,
    ORDER_SUBMITTED = 4,
    ORDER_RECEIVED = 5,
    ORDER_MATCHED = 6,
    QUEUE_PUSH = 7,
    QUEUE_POP = 8,
    CUSTOM_1 = 10,
    CUSTOM_2 = 11,
    CUSTOM_3 = 12,
    USER_DEFINED = 255
};

/**
 * @brief Single timestamp event record
 * 
 * Packed to minimize cache usage. 24 bytes per event.
 */
struct alignas(8) TimestampEvent {
    std::int64_t timestamp;         // TSC ticks or nanoseconds
    std::uint64_t sequence;         // Global sequence number for ordering
    std::uint64_t payload;          // User-defined payload (e.g., order ID)
    EventType type;                 // Event type
    std::uint8_t thread_id;         // Thread that recorded this event
    std::uint16_t reserved;         // Padding for alignment
};
static_assert(sizeof(TimestampEvent) == 32, "TimestampEvent should be 32 bytes");

/**
 * @brief Thread-local timestamp buffer
 * 
 * Each thread has its own buffer to avoid contention. The buffer is
 * heap-allocated to avoid TLS size limitations, but pre-allocated
 * at construction time to avoid allocations during recording.
 * 
 * @tparam Capacity Maximum number of events per thread
 */
template<std::size_t Capacity = 100000>
class ThreadLocalTimestampBuffer {
public:
    ThreadLocalTimestampBuffer() : count_(0), thread_id_(0) {
        // Heap allocate to avoid TLS size limitations on ARM64
        events_ = std::make_unique<std::array<TimestampEvent, Capacity>>();
    }
    
    /**
     * @brief Record a timestamp event (lock-free, thread-local)
     * 
     * This is the hot path - must be as fast as possible.
     * No allocations, no locks, no contention.
     * 
     * @param type Event type
     * @param payload Optional payload (e.g., order ID)
     * @return true if recorded, false if buffer full
     */
    [[nodiscard]] bool record(EventType type, std::uint64_t payload = 0) noexcept {
        if (count_ >= Capacity || !events_) {
            return false;  // Buffer full or not allocated
        }
        
        auto& event = (*events_)[count_];
        event.timestamp = rdtscp();  // Use serializing TSC read
        event.sequence = global_sequence_.fetch_add(1, std::memory_order_relaxed);
        event.payload = payload;
        event.type = type;
        event.thread_id = thread_id_;
        event.reserved = 0;
        
        ++count_;
        return true;
    }
    
    /**
     * @brief Record with explicit timestamp (for external timestamps)
     */
    [[nodiscard]] bool record_with_timestamp(
        EventType type, 
        std::int64_t timestamp,
        std::uint64_t payload = 0
    ) noexcept {
        if (count_ >= Capacity || !events_) {
            return false;
        }
        
        auto& event = (*events_)[count_];
        event.timestamp = timestamp;
        event.sequence = global_sequence_.fetch_add(1, std::memory_order_relaxed);
        event.payload = payload;
        event.type = type;
        event.thread_id = thread_id_;
        event.reserved = 0;
        
        ++count_;
        return true;
    }
    
    /**
     * @brief Get recorded events (call after test completes)
     */
    [[nodiscard]] const TimestampEvent* events() const noexcept {
        return events_ ? events_->data() : nullptr;
    }
    
    /**
     * @brief Get number of recorded events
     */
    [[nodiscard]] std::size_t count() const noexcept {
        return count_;
    }
    
    /**
     * @brief Clear buffer for reuse
     */
    void clear() noexcept {
        count_ = 0;
    }
    
    /**
     * @brief Check if buffer is full
     */
    [[nodiscard]] bool full() const noexcept {
        return count_ >= Capacity;
    }
    
    /**
     * @brief Get remaining capacity
     */
    [[nodiscard]] std::size_t remaining() const noexcept {
        return Capacity - count_;
    }
    
    /**
     * @brief Set thread ID for this buffer
     */
    void set_thread_id(std::uint8_t id) noexcept {
        thread_id_ = id;
    }
    
    /**
     * @brief Get thread ID
     */
    [[nodiscard]] std::uint8_t thread_id() const noexcept {
        return thread_id_;
    }

private:
    // Heap-allocated to avoid TLS size limitations (32 bytes * 100k = 3.2MB)
    std::unique_ptr<std::array<TimestampEvent, Capacity>> events_;
    std::size_t count_;
    std::uint8_t thread_id_;
    
    // Global sequence counter for ordering events across threads
    static inline std::atomic<std::uint64_t> global_sequence_{0};
};

/**
 * @brief Manager for thread-local timestamp buffers
 * 
 * Provides thread-local storage and aggregation of events from all threads.
 * The logging thread is idle during the test and only wakes up afterwards
 * to collate results.
 * 
 * Key design: Instead of storing pointers to thread-local buffers (which
 * become invalid when threads exit), each thread flushes its events to a
 * central store before exiting, or we use shared_ptr for the buffers.
 */
class TimestampBufferManager {
public:
    static constexpr std::size_t BUFFER_CAPACITY = 100000;
    using Buffer = ThreadLocalTimestampBuffer<BUFFER_CAPACITY>;
    
    /**
     * @brief Get the thread-local buffer for the current thread
     * 
     * This is thread-safe and creates a new buffer if needed.
     * The buffer is managed via shared_ptr to handle thread lifetime.
     */
    static Buffer& get_thread_buffer() {
        thread_local std::shared_ptr<Buffer> buffer_ptr = register_new_buffer();
        return *buffer_ptr;
    }
    
    /**
     * @brief Record an event in the current thread's buffer
     * 
     * Convenience function that gets thread-local buffer and records.
     */
    static bool record(EventType type, std::uint64_t payload = 0) noexcept {
        return get_thread_buffer().record(type, payload);
    }
    
    /**
     * @brief Aggregate all events from all threads (call after test)
     * 
     * This should be called from a dedicated logging thread or after
     * the measured run completes to avoid perturbing measurements.
     * 
     * @param sort_by_sequence If true, sort events by sequence number
     * @return Vector of all events from all threads
     */
    static std::vector<TimestampEvent> aggregate(bool sort_by_sequence = true) {
        std::vector<TimestampEvent> all_events;
        
        {
            std::lock_guard<std::mutex> lock(instance().mutex_);
            
            // Calculate total size
            std::size_t total = 0;
            for (const auto& buffer : instance().buffers_) {
                if (buffer) total += buffer->count();
            }
            all_events.reserve(total);
            
            // Copy events from all buffers
            for (const auto& buffer : instance().buffers_) {
                if (!buffer) continue;
                const auto* events = buffer->events();
                if (!events) continue;
                for (std::size_t i = 0; i < buffer->count(); ++i) {
                    all_events.push_back(events[i]);
                }
            }
        }
        
        // Sort by sequence number for correct ordering
        if (sort_by_sequence) {
            std::sort(all_events.begin(), all_events.end(),
                [](const TimestampEvent& a, const TimestampEvent& b) {
                    return a.sequence < b.sequence;
                });
        }
        
        return all_events;
    }
    
    /**
     * @brief Clear all buffers
     */
    static void clear_all() {
        std::lock_guard<std::mutex> lock(instance().mutex_);
        for (auto& buffer : instance().buffers_) {
            if (buffer) buffer->clear();
        }
    }
    
    /**
     * @brief Get total event count across all threads
     */
    static std::size_t total_count() {
        std::lock_guard<std::mutex> lock(instance().mutex_);
        std::size_t total = 0;
        for (const auto& buffer : instance().buffers_) {
            if (buffer) total += buffer->count();
        }
        return total;
    }
    
    /**
     * @brief Get number of registered threads
     */
    static std::size_t thread_count() {
        std::lock_guard<std::mutex> lock(instance().mutex_);
        return instance().buffers_.size();
    }
    
    /**
     * @brief Print summary statistics
     */
    static void print_summary() {
        std::lock_guard<std::mutex> lock(instance().mutex_);
        
        std::cout << "\n--- Timestamp Buffer Summary ---\n";
        std::cout << "  Threads registered: " << instance().buffers_.size() << "\n";
        
        std::size_t total = 0;
        for (std::size_t i = 0; i < instance().buffers_.size(); ++i) {
            const auto& buffer = instance().buffers_[i];
            if (!buffer) continue;
            std::cout << "  Thread " << i << ": " << buffer->count() << " events";
            if (buffer->full()) {
                std::cout << " (FULL - events may have been dropped!)";
            }
            std::cout << "\n";
            total += buffer->count();
        }
        
        std::cout << "  Total events: " << total << "\n";
    }

private:
    TimestampBufferManager() = default;
    
    static TimestampBufferManager& instance() {
        static TimestampBufferManager mgr;
        return mgr;
    }
    
    static std::shared_ptr<Buffer> register_new_buffer() {
        auto buffer = std::make_shared<Buffer>();
        std::lock_guard<std::mutex> lock(instance().mutex_);
        buffer->set_thread_id(static_cast<std::uint8_t>(instance().buffers_.size()));
        instance().buffers_.push_back(buffer);
        return buffer;
    }
    
    std::mutex mutex_;
    std::vector<std::shared_ptr<Buffer>> buffers_;
};

/**
 * @brief Analyze timestamp events and compute latencies
 */
class TimestampAnalyzer {
public:
    struct LatencyPair {
        EventType start_type;
        EventType end_type;
        std::string name;
    };
    
    /**
     * @brief Compute latencies between paired events
     * 
     * @param events Sorted events from TimestampBufferManager::aggregate()
     * @param pairs Event pairs to analyze
     * @param tsc_frequency TSC frequency for conversion to nanoseconds
     */
    static void analyze(
        const std::vector<TimestampEvent>& events,
        const std::vector<LatencyPair>& pairs,
        double tsc_frequency
    ) {
        double ns_per_tick = 1e9 / tsc_frequency;
        
        std::cout << "\n--- Timestamp Event Analysis ---\n";
        std::cout << "  Total events: " << events.size() << "\n";
        std::cout << "  TSC frequency: " << (tsc_frequency / 1e9) << " GHz\n\n";
        
        for (const auto& pair : pairs) {
            std::vector<double> latencies;
            
            // Find matching pairs by payload (e.g., order ID)
            std::unordered_map<std::uint64_t, std::int64_t> start_times;
            
            for (const auto& event : events) {
                if (event.type == pair.start_type) {
                    start_times[event.payload] = event.timestamp;
                } else if (event.type == pair.end_type) {
                    auto it = start_times.find(event.payload);
                    if (it != start_times.end()) {
                        double latency_ns = (event.timestamp - it->second) * ns_per_tick;
                        latencies.push_back(latency_ns);
                        start_times.erase(it);
                    }
                }
            }
            
            if (latencies.empty()) {
                std::cout << pair.name << ": No matching pairs found\n";
                continue;
            }
            
            // Compute statistics
            std::sort(latencies.begin(), latencies.end());
            
            double sum = 0;
            for (double l : latencies) sum += l;
            double avg = sum / latencies.size();
            
            auto percentile = [&](double p) {
                size_t idx = static_cast<size_t>(latencies.size() * p);
                return latencies[std::min(idx, latencies.size() - 1)];
            };
            
            std::cout << pair.name << " (n=" << latencies.size() << "):\n";
            std::cout << "  Min:    " << latencies.front() << " ns\n";
            std::cout << "  Max:    " << latencies.back() << " ns\n";
            std::cout << "  Avg:    " << avg << " ns\n";
            std::cout << "  Median: " << percentile(0.50) << " ns\n";
            std::cout << "  P99:    " << percentile(0.99) << " ns\n\n";
        }
    }
    
    /**
     * @brief Export events to CSV for external analysis
     */
    static void export_csv(
        const std::vector<TimestampEvent>& events,
        const std::string& filename,
        double tsc_frequency
    ) {
        double ns_per_tick = 1e9 / tsc_frequency;
        
        std::ofstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Failed to open " << filename << " for writing\n";
            return;
        }
        
        file << "sequence,timestamp_ns,type,thread_id,payload\n";
        
        for (const auto& event : events) {
            file << event.sequence << ","
                 << static_cast<std::int64_t>(event.timestamp * ns_per_tick) << ","
                 << static_cast<int>(event.type) << ","
                 << static_cast<int>(event.thread_id) << ","
                 << event.payload << "\n";
        }
        
        std::cout << "Exported " << events.size() << " events to " << filename << "\n";
    }
};

/**
 * @brief RAII helper for recording event pairs
 * 
 * Records start event on construction, end event on destruction.
 */
class ScopedTimestampEvent {
public:
    ScopedTimestampEvent(EventType start_type, EventType end_type, std::uint64_t payload = 0)
        : end_type_(end_type), payload_(payload) {
        TimestampBufferManager::record(start_type, payload);
    }
    
    ~ScopedTimestampEvent() {
        TimestampBufferManager::record(end_type_, payload_);
    }
    
    ScopedTimestampEvent(const ScopedTimestampEvent&) = delete;
    ScopedTimestampEvent& operator=(const ScopedTimestampEvent&) = delete;

private:
    EventType end_type_;
    std::uint64_t payload_;
};

} // namespace hft

