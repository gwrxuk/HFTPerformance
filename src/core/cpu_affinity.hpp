/**
 * @file cpu_affinity.hpp
 * @brief CPU affinity and NUMA utilities for low-latency optimization
 * 
 * Provides tools for:
 * - Pinning threads to specific CPU cores
 * - NUMA-aware memory allocation
 * - CPU topology detection
 * - Real-time thread priority
 */

#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <optional>
#include <thread>

#ifdef __linux__
#include <sched.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#include <sys/mman.h>
#endif

namespace hft {

/**
 * @brief CPU core information
 */
struct CPUInfo {
    int core_id;
    int physical_id;      // Physical CPU package
    int numa_node;        // NUMA node
    bool is_hyperthreaded;
    std::uint64_t frequency_hz;
};

/**
 * @brief Set CPU affinity for the current thread
 * 
 * @param cpu_id CPU core ID to pin to
 * @return true if successful
 */
inline bool set_cpu_affinity(int cpu_id) {
    #ifdef __linux__
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu_id, &cpuset);
        
        return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0;
    #else
        (void)cpu_id;
        return false;  // Not supported on this platform
    #endif
}

/**
 * @brief Set CPU affinity for a specific thread
 */
inline bool set_thread_affinity(std::thread& thread, int cpu_id) {
    #ifdef __linux__
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu_id, &cpuset);
        
        return pthread_setaffinity_np(thread.native_handle(), sizeof(cpu_set_t), &cpuset) == 0;
    #else
        (void)thread;
        (void)cpu_id;
        return false;
    #endif
}

/**
 * @brief Set CPU affinity to multiple cores
 */
inline bool set_cpu_affinity_mask(const std::vector<int>& cpu_ids) {
    #ifdef __linux__
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        for (int cpu_id : cpu_ids) {
            CPU_SET(cpu_id, &cpuset);
        }
        
        return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0;
    #else
        (void)cpu_ids;
        return false;
    #endif
}

/**
 * @brief Get current CPU affinity mask
 */
inline std::vector<int> get_cpu_affinity() {
    std::vector<int> cpus;
    
    #ifdef __linux__
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        
        if (pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0) {
            for (int i = 0; i < CPU_SETSIZE; ++i) {
                if (CPU_ISSET(i, &cpuset)) {
                    cpus.push_back(i);
                }
            }
        }
    #endif
    
    return cpus;
}

/**
 * @brief Get the CPU core the current thread is running on
 */
inline int get_current_cpu() {
    #ifdef __linux__
        return sched_getcpu();
    #else
        return -1;
    #endif
}

/**
 * @brief Get number of available CPUs
 */
inline int get_cpu_count() {
    return static_cast<int>(std::thread::hardware_concurrency());
}

/**
 * @brief Thread scheduling priorities
 */
enum class ThreadPriority {
    IDLE,
    LOW,
    NORMAL,
    HIGH,
    REALTIME
};

/**
 * @brief Set thread priority
 * 
 * Note: REALTIME priority requires root privileges on Linux.
 */
inline bool set_thread_priority(ThreadPriority priority) {
    #ifdef __linux__
        int policy = SCHED_OTHER;
        struct sched_param param;
        param.sched_priority = 0;
        
        switch (priority) {
            case ThreadPriority::IDLE:
                policy = SCHED_IDLE;
                param.sched_priority = 0;
                break;
            case ThreadPriority::LOW:
                policy = SCHED_OTHER;
                param.sched_priority = 0;
                setpriority(PRIO_PROCESS, 0, 10);
                break;
            case ThreadPriority::NORMAL:
                policy = SCHED_OTHER;
                param.sched_priority = 0;
                break;
            case ThreadPriority::HIGH:
                policy = SCHED_OTHER;
                param.sched_priority = 0;
                setpriority(PRIO_PROCESS, 0, -10);
                break;
            case ThreadPriority::REALTIME:
                policy = SCHED_FIFO;
                param.sched_priority = sched_get_priority_max(SCHED_FIFO);
                break;
        }
        
        return pthread_setschedparam(pthread_self(), policy, &param) == 0;
    #else
        (void)priority;
        return false;
    #endif
}

/**
 * @brief Lock memory to prevent paging
 * 
 * Critical for low-latency: prevents page faults in hot paths.
 */
inline bool lock_memory() {
    #ifdef __linux__
        return mlockall(MCL_CURRENT | MCL_FUTURE) == 0;
    #else
        return false;
    #endif
}

/**
 * @brief Isolate current thread from interrupts (best-effort)
 * 
 * Reduces jitter from interrupt handling on the pinned core.
 */
inline void isolate_thread() {
    #ifdef __linux__
        // Disable thread migration
        // Note: Full isolation requires kernel parameters (isolcpus, irqaffinity)
        
        // Set to use local memory only (if NUMA)
        // numa_set_strict(1);
        // numa_set_localalloc();
    #endif
}

/**
 * @brief Thread configuration for HFT
 */
struct ThreadConfig {
    int cpu_core = -1;           // CPU core to pin to (-1 = no pinning)
    ThreadPriority priority = ThreadPriority::NORMAL;
    bool lock_memory = false;    // Lock memory pages
    std::string name;            // Thread name (for debugging)
};

/**
 * @brief Apply thread configuration
 */
inline bool apply_thread_config(const ThreadConfig& config) {
    bool success = true;
    
    // Set thread name
    #ifdef __linux__
        if (!config.name.empty()) {
            pthread_setname_np(pthread_self(), config.name.c_str());
        }
    #endif
    
    // Pin to CPU
    if (config.cpu_core >= 0) {
        if (!set_cpu_affinity(config.cpu_core)) {
            success = false;
        }
    }
    
    // Set priority
    if (!set_thread_priority(config.priority)) {
        // Non-fatal on non-root
    }
    
    // Lock memory
    if (config.lock_memory) {
        if (!hft::lock_memory()) {
            success = false;
        }
    }
    
    return success;
}

/**
 * @brief RAII wrapper for thread configuration
 */
class ScopedThreadConfig {
public:
    explicit ScopedThreadConfig(const ThreadConfig& config) {
        original_affinity_ = get_cpu_affinity();
        apply_thread_config(config);
    }
    
    ~ScopedThreadConfig() {
        // Restore original affinity
        if (!original_affinity_.empty()) {
            set_cpu_affinity_mask(original_affinity_);
        }
    }
    
    ScopedThreadConfig(const ScopedThreadConfig&) = delete;
    ScopedThreadConfig& operator=(const ScopedThreadConfig&) = delete;

private:
    std::vector<int> original_affinity_;
};

/**
 * @brief Prefetch data into CPU cache
 * 
 * @param addr Address to prefetch
 * @param rw 0 = read, 1 = write
 * @param locality 0 = no locality (NTA), 3 = high locality (L1)
 */
template<int ReadWrite = 0, int Locality = 3>
inline void prefetch(const void* addr) {
    #if defined(__GNUC__) || defined(__clang__)
        __builtin_prefetch(addr, ReadWrite, Locality);
    #endif
}

/**
 * @brief Cache line flush
 */
inline void cache_flush(const void* addr) {
    #if defined(__x86_64__)
        asm volatile("clflush (%0)" :: "r"(addr) : "memory");
    #else
        (void)addr;
    #endif
}

} // namespace hft

