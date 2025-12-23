/**
 * @file memory_pool.hpp
 * @brief Fixed-size object pool for low-latency memory allocation
 * 
 * This memory pool pre-allocates objects and provides O(1) allocation
 * and deallocation without system calls. Critical for avoiding latency
 * spikes from malloc/new in hot paths.
 * 
 * Key features:
 * - Lock-free allocation for single-threaded use
 * - Optional thread-safe version with spinlock
 * - Cache-aligned allocations
 * - No heap fragmentation
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <memory>
#include <new>
#include <type_traits>
#include <cassert>
#include "types.hpp"
#include "spinlock.hpp"

namespace hft {

/**
 * @brief Fixed-size memory pool with O(1) alloc/free
 * 
 * @tparam T Object type to pool
 * @tparam Capacity Maximum number of objects
 * @tparam Alignment Cache line alignment (default 64 bytes)
 */
template<typename T, std::size_t Capacity, std::size_t Alignment = CACHE_LINE_SIZE>
class MemoryPool {
    static_assert(Capacity > 0, "Capacity must be positive");
    static_assert((Alignment & (Alignment - 1)) == 0, "Alignment must be power of 2");

    // Calculate aligned size
    static constexpr std::size_t ALIGNED_SIZE = 
        ((sizeof(T) + Alignment - 1) / Alignment) * Alignment;

    // Free list node - stored in-place in free blocks
    struct FreeNode {
        FreeNode* next;
    };

public:
    MemoryPool() noexcept {
        // Initialize free list
        free_head_ = reinterpret_cast<FreeNode*>(storage_.data());
        FreeNode* current = free_head_;
        
        for (std::size_t i = 1; i < Capacity; ++i) {
            FreeNode* next = reinterpret_cast<FreeNode*>(
                storage_.data() + i * ALIGNED_SIZE);
            current->next = next;
            current = next;
        }
        current->next = nullptr;
        
        allocated_count_ = 0;
    }

    // Non-copyable, non-movable
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    MemoryPool(MemoryPool&&) = delete;
    MemoryPool& operator=(MemoryPool&&) = delete;

    /**
     * @brief Allocate a raw block of memory
     * @return Pointer to allocated memory, or nullptr if pool is exhausted
     */
    [[nodiscard]] void* allocate() noexcept {
        if (free_head_ == nullptr) {
            return nullptr;
        }
        
        void* block = free_head_;
        free_head_ = free_head_->next;
        ++allocated_count_;
        
        return block;
    }

    /**
     * @brief Deallocate a block of memory
     * @param ptr Pointer to memory block (must have been allocated from this pool)
     */
    void deallocate(void* ptr) noexcept {
        if (ptr == nullptr) return;
        
        assert(owns(ptr) && "Pointer not from this pool");
        
        FreeNode* node = static_cast<FreeNode*>(ptr);
        node->next = free_head_;
        free_head_ = node;
        --allocated_count_;
    }

    /**
     * @brief Allocate and construct an object
     * @param args Constructor arguments
     * @return Pointer to constructed object, or nullptr if pool is exhausted
     */
    template<typename... Args>
    [[nodiscard]] T* create(Args&&... args) {
        void* memory = allocate();
        if (memory == nullptr) {
            return nullptr;
        }
        
        try {
            return new (memory) T(std::forward<Args>(args)...);
        } catch (...) {
            deallocate(memory);
            throw;
        }
    }

    /**
     * @brief Destroy and deallocate an object
     * @param obj Pointer to object (must have been created from this pool)
     */
    void destroy(T* obj) noexcept(std::is_nothrow_destructible_v<T>) {
        if (obj == nullptr) return;
        
        obj->~T();
        deallocate(obj);
    }

    /**
     * @brief Check if pointer belongs to this pool
     */
    [[nodiscard]] bool owns(const void* ptr) const noexcept {
        const auto* byte_ptr = static_cast<const std::uint8_t*>(ptr);
        const auto* start = storage_.data();
        const auto* end = storage_.data() + storage_.size();
        
        if (byte_ptr < start || byte_ptr >= end) {
            return false;
        }
        
        // Check alignment
        const auto offset = static_cast<std::size_t>(byte_ptr - start);
        return (offset % ALIGNED_SIZE) == 0;
    }

    /**
     * @brief Get number of allocated objects
     */
    [[nodiscard]] std::size_t allocated() const noexcept {
        return allocated_count_;
    }

    /**
     * @brief Get number of available slots
     */
    [[nodiscard]] std::size_t available() const noexcept {
        return Capacity - allocated_count_;
    }

    /**
     * @brief Get pool capacity
     */
    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return Capacity;
    }

    /**
     * @brief Check if pool is empty
     */
    [[nodiscard]] bool empty() const noexcept {
        return allocated_count_ == 0;
    }

    /**
     * @brief Check if pool is full
     */
    [[nodiscard]] bool full() const noexcept {
        return allocated_count_ == Capacity;
    }

private:
    alignas(Alignment) std::array<std::uint8_t, Capacity * ALIGNED_SIZE> storage_;
    FreeNode* free_head_;
    std::size_t allocated_count_;
};

/**
 * @brief Thread-safe memory pool with spinlock protection
 * 
 * Use this when multiple threads need to allocate from the same pool.
 * For best performance, prefer per-thread pools when possible.
 */
template<typename T, std::size_t Capacity, std::size_t Alignment = CACHE_LINE_SIZE>
class ThreadSafeMemoryPool {
public:
    [[nodiscard]] void* allocate() noexcept {
        std::lock_guard<Spinlock> lock(spinlock_);
        return pool_.allocate();
    }

    void deallocate(void* ptr) noexcept {
        std::lock_guard<Spinlock> lock(spinlock_);
        pool_.deallocate(ptr);
    }

    template<typename... Args>
    [[nodiscard]] T* create(Args&&... args) {
        void* memory = allocate();
        if (memory == nullptr) {
            return nullptr;
        }
        
        try {
            return new (memory) T(std::forward<Args>(args)...);
        } catch (...) {
            deallocate(memory);
            throw;
        }
    }

    void destroy(T* obj) noexcept(std::is_nothrow_destructible_v<T>) {
        if (obj == nullptr) return;
        
        obj->~T();
        deallocate(obj);
    }

    [[nodiscard]] std::size_t allocated() const noexcept {
        std::lock_guard<Spinlock> lock(spinlock_);
        return pool_.allocated();
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return Capacity;
    }

private:
    MemoryPool<T, Capacity, Alignment> pool_;
    mutable Spinlock spinlock_;
};

/**
 * @brief RAII wrapper for pooled objects
 * 
 * Automatically returns object to pool when destroyed.
 */
template<typename T, typename Pool>
class PooledPtr {
public:
    PooledPtr() noexcept : pool_(nullptr), ptr_(nullptr) {}
    
    PooledPtr(Pool& pool, T* ptr) noexcept : pool_(&pool), ptr_(ptr) {}
    
    ~PooledPtr() {
        if (ptr_ && pool_) {
            pool_->destroy(ptr_);
        }
    }

    // Move-only
    PooledPtr(const PooledPtr&) = delete;
    PooledPtr& operator=(const PooledPtr&) = delete;
    
    PooledPtr(PooledPtr&& other) noexcept 
        : pool_(other.pool_), ptr_(other.ptr_) {
        other.pool_ = nullptr;
        other.ptr_ = nullptr;
    }
    
    PooledPtr& operator=(PooledPtr&& other) noexcept {
        if (this != &other) {
            if (ptr_ && pool_) {
                pool_->destroy(ptr_);
            }
            pool_ = other.pool_;
            ptr_ = other.ptr_;
            other.pool_ = nullptr;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    T* get() const noexcept { return ptr_; }
    T* operator->() const noexcept { return ptr_; }
    T& operator*() const noexcept { return *ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    T* release() noexcept {
        T* tmp = ptr_;
        ptr_ = nullptr;
        pool_ = nullptr;
        return tmp;
    }

private:
    Pool* pool_;
    T* ptr_;
};

/**
 * @brief Create a pooled pointer
 */
template<typename T, typename Pool, typename... Args>
[[nodiscard]] PooledPtr<T, Pool> make_pooled(Pool& pool, Args&&... args) {
    T* ptr = pool.template create<Args...>(std::forward<Args>(args)...);
    return PooledPtr<T, Pool>(pool, ptr);
}

} // namespace hft

