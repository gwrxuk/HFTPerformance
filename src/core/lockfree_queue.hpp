/**
 * @file lockfree_queue.hpp
 * @brief Lock-free Single-Producer Single-Consumer (SPSC) Queue
 * 
 * This implementation uses a ring buffer with cache-line padding to prevent
 * false sharing. Optimized for low-latency inter-thread communication.
 * 
 * Key optimizations:
 * - Cache-line aligned head/tail to prevent false sharing
 * - Power-of-2 size for fast modulo operations
 * - Acquire-release memory ordering for minimal synchronization
 * - Local caching of head/tail positions
 */

#pragma once

#include <atomic>
#include <array>
#include <cstddef>
#include <optional>
#include <new>
#include <type_traits>
#include "types.hpp"

namespace hft {

/**
 * @brief Lock-free SPSC queue optimized for low latency
 * 
 * @tparam T Element type (should be trivially copyable for best performance)
 * @tparam Capacity Queue capacity (must be power of 2)
 */
template<typename T, std::size_t Capacity>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, 
                  "Capacity must be a power of 2");
    static_assert(Capacity >= 2, "Capacity must be at least 2");

public:
    SPSCQueue() noexcept : buffer_(), head_(0), tail_(0), cached_head_(0), cached_tail_(0) {
        static_assert(std::is_trivially_copyable_v<T> || std::is_move_constructible_v<T>,
                      "T must be trivially copyable or move constructible");
    }

    // Non-copyable, non-movable
    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;
    SPSCQueue(SPSCQueue&&) = delete;
    SPSCQueue& operator=(SPSCQueue&&) = delete;

    /**
     * @brief Try to push an element (producer only)
     * @param value Element to push
     * @return true if successful, false if queue is full
     */
    template<typename U>
    [[nodiscard]] bool try_push(U&& value) noexcept(std::is_nothrow_constructible_v<T, U&&>) {
        const auto tail = tail_.load(std::memory_order_relaxed);
        const auto next_tail = increment(tail);

        // Check if queue is full using cached head
        if (next_tail == cached_head_) {
            // Refresh cached head
            cached_head_ = head_.load(std::memory_order_acquire);
            if (next_tail == cached_head_) {
                return false;  // Queue is full
            }
        }

        // Construct element in-place
        new (&buffer_[tail]) T(std::forward<U>(value));

        // Publish the element
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    /**
     * @brief Push with busy-wait (producer only)
     * @param value Element to push
     */
    template<typename U>
    void push(U&& value) noexcept(std::is_nothrow_constructible_v<T, U&&>) {
        while (!try_push(std::forward<U>(value))) {
            cpu_pause();
        }
    }

    /**
     * @brief Try to pop an element (consumer only)
     * @return Optional containing the element, or empty if queue is empty
     */
    [[nodiscard]] std::optional<T> try_pop() noexcept(std::is_nothrow_move_constructible_v<T>) {
        const auto head = head_.load(std::memory_order_relaxed);

        // Check if queue is empty using cached tail
        if (head == cached_tail_) {
            // Refresh cached tail
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (head == cached_tail_) {
                return std::nullopt;  // Queue is empty
            }
        }

        // Read element
        T* element = reinterpret_cast<T*>(&buffer_[head]);
        T value = std::move(*element);
        
        // Destroy element if non-trivial
        if constexpr (!std::is_trivially_destructible_v<T>) {
            element->~T();
        }

        // Advance head
        head_.store(increment(head), std::memory_order_release);
        return value;
    }

    /**
     * @brief Pop with busy-wait (consumer only)
     * @return The popped element
     */
    [[nodiscard]] T pop() noexcept(std::is_nothrow_move_constructible_v<T>) {
        std::optional<T> result;
        while (!(result = try_pop())) {
            cpu_pause();
        }
        return std::move(*result);
    }

    /**
     * @brief Peek at the front element without removing (consumer only)
     * @return Pointer to front element, or nullptr if empty
     */
    [[nodiscard]] const T* front() const noexcept {
        const auto head = head_.load(std::memory_order_relaxed);
        const auto tail = tail_.load(std::memory_order_acquire);
        
        if (head == tail) {
            return nullptr;
        }
        
        return reinterpret_cast<const T*>(&buffer_[head]);
    }

    /**
     * @brief Check if queue is empty
     */
    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) == 
               tail_.load(std::memory_order_acquire);
    }

    /**
     * @brief Get current size (approximate, may race with push/pop)
     */
    [[nodiscard]] std::size_t size() const noexcept {
        const auto tail = tail_.load(std::memory_order_acquire);
        const auto head = head_.load(std::memory_order_acquire);
        return (tail - head) & MASK;
    }

    /**
     * @brief Get queue capacity
     */
    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return Capacity - 1;  // One slot is always empty
    }

private:
    static constexpr std::size_t MASK = Capacity - 1;

    [[nodiscard]] static constexpr std::size_t increment(std::size_t index) noexcept {
        return (index + 1) & MASK;
    }

    static void cpu_pause() noexcept {
        #if defined(__x86_64__) || defined(_M_X64)
            __builtin_ia32_pause();
        #elif defined(__aarch64__)
            asm volatile("yield" ::: "memory");
        #else
            std::this_thread::yield();
        #endif
    }

    // Aligned storage for elements
    struct alignas(alignof(T)) Storage {
        unsigned char data[sizeof(T)];
    };
    
    std::array<Storage, Capacity> buffer_;

    // Cache-line separated indices to prevent false sharing
    // Order matters for initialization list
    alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> head_;
    alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> tail_;
    alignas(CACHE_LINE_SIZE) std::size_t cached_head_;  // Producer's cached head
    alignas(CACHE_LINE_SIZE) std::size_t cached_tail_;  // Consumer's cached tail
};

/**
 * @brief Lock-free Multi-Producer Single-Consumer (MPSC) Queue
 * 
 * Uses a linked-list approach with atomic operations for thread-safe
 * insertion from multiple producers.
 */
template<typename T>
class MPSCQueue {
    struct Node {
        T data;
        std::atomic<Node*> next;
        
        template<typename U>
        explicit Node(U&& value) : data(std::forward<U>(value)), next(nullptr) {}
    };

public:
    MPSCQueue() : head_(new Node{}), tail_(head_.load()) {
        head_.load()->next.store(nullptr, std::memory_order_relaxed);
    }

    ~MPSCQueue() {
        // Clean up remaining nodes
        Node* node = head_.load(std::memory_order_relaxed);
        while (node) {
            Node* next = node->next.load(std::memory_order_relaxed);
            delete node;
            node = next;
        }
    }

    // Non-copyable
    MPSCQueue(const MPSCQueue&) = delete;
    MPSCQueue& operator=(const MPSCQueue&) = delete;

    /**
     * @brief Push an element (thread-safe for multiple producers)
     */
    template<typename U>
    void push(U&& value) {
        Node* node = new Node(std::forward<U>(value));
        Node* prev = tail_.exchange(node, std::memory_order_acq_rel);
        prev->next.store(node, std::memory_order_release);
    }

    /**
     * @brief Try to pop an element (single consumer only)
     */
    [[nodiscard]] std::optional<T> try_pop() {
        Node* head = head_.load(std::memory_order_relaxed);
        Node* next = head->next.load(std::memory_order_acquire);
        
        if (next == nullptr) {
            return std::nullopt;
        }
        
        T value = std::move(next->data);
        head_.store(next, std::memory_order_release);
        delete head;
        
        return value;
    }

    [[nodiscard]] bool empty() const noexcept {
        Node* head = head_.load(std::memory_order_acquire);
        return head->next.load(std::memory_order_acquire) == nullptr;
    }

private:
    alignas(CACHE_LINE_SIZE) std::atomic<Node*> head_;
    alignas(CACHE_LINE_SIZE) std::atomic<Node*> tail_;
};

} // namespace hft

