/**
 * @file price_level.hpp
 * @brief Price level for order book - manages orders at a single price
 * 
 * Uses an intrusive doubly-linked list for O(1) order operations:
 * - Add order to back: O(1)
 * - Remove order by pointer: O(1)
 * - Get front order: O(1)
 */

#pragma once

#include <cstddef>
#include "order.hpp"
#include "core/memory_pool.hpp"

namespace hft {

/**
 * @brief Price level containing orders at a single price point
 * 
 * Orders are maintained in FIFO order (time priority).
 * All operations are O(1).
 */
class PriceLevel {
public:
    explicit PriceLevel(Price price) noexcept
        : price_(price)
        , head_(nullptr)
        , tail_(nullptr)
        , total_quantity_(0)
        , order_count_(0)
    {}

    // Non-copyable, movable
    PriceLevel(const PriceLevel&) = delete;
    PriceLevel& operator=(const PriceLevel&) = delete;
    
    PriceLevel(PriceLevel&& other) noexcept
        : price_(other.price_)
        , head_(other.head_)
        , tail_(other.tail_)
        , total_quantity_(other.total_quantity_)
        , order_count_(other.order_count_)
    {
        other.head_ = nullptr;
        other.tail_ = nullptr;
        other.total_quantity_ = 0;
        other.order_count_ = 0;
    }
    
    PriceLevel& operator=(PriceLevel&& other) noexcept {
        if (this != &other) {
            price_ = other.price_;
            head_ = other.head_;
            tail_ = other.tail_;
            total_quantity_ = other.total_quantity_;
            order_count_ = other.order_count_;
            other.head_ = nullptr;
            other.tail_ = nullptr;
            other.total_quantity_ = 0;
            other.order_count_ = 0;
        }
        return *this;
    }

    /**
     * @brief Add an order to the back of the queue
     * @param node Order node (must be allocated externally)
     */
    void add_order(OrderNode* node) noexcept {
        node->prev = tail_;
        node->next = nullptr;
        
        if (tail_) {
            tail_->next = node;
        } else {
            head_ = node;
        }
        tail_ = node;
        
        total_quantity_ += node->order.remaining_quantity();
        ++order_count_;
    }

    /**
     * @brief Remove an order from the queue
     * @param node Order node to remove
     */
    void remove_order(OrderNode* node) noexcept {
        total_quantity_ -= node->order.remaining_quantity();
        --order_count_;
        
        if (node->prev) {
            node->prev->next = node->next;
        } else {
            head_ = node->next;
        }
        
        if (node->next) {
            node->next->prev = node->prev;
        } else {
            tail_ = node->prev;
        }
        
        node->prev = nullptr;
        node->next = nullptr;
    }

    /**
     * @brief Get the first order in the queue
     */
    [[nodiscard]] OrderNode* front() noexcept {
        return head_;
    }
    
    [[nodiscard]] const OrderNode* front() const noexcept {
        return head_;
    }

    /**
     * @brief Pop and return the first order
     */
    OrderNode* pop_front() noexcept {
        if (!head_) return nullptr;
        
        OrderNode* node = head_;
        remove_order(node);
        return node;
    }

    /**
     * @brief Update quantity after partial fill
     */
    void update_quantity(OrderNode* /* node */, Quantity filled) noexcept {
        total_quantity_ -= filled;
    }

    // Accessors
    [[nodiscard]] Price price() const noexcept { return price_; }
    [[nodiscard]] Quantity total_quantity() const noexcept { return total_quantity_; }
    [[nodiscard]] std::size_t order_count() const noexcept { return order_count_; }
    [[nodiscard]] bool empty() const noexcept { return head_ == nullptr; }

    /**
     * @brief Iterator for traversing orders at this price level
     */
    class Iterator {
    public:
        using value_type = OrderNode;
        using pointer = OrderNode*;
        using reference = OrderNode&;

        explicit Iterator(OrderNode* node = nullptr) noexcept : node_(node) {}

        reference operator*() const noexcept { return *node_; }
        pointer operator->() const noexcept { return node_; }

        Iterator& operator++() noexcept {
            node_ = node_->next;
            return *this;
        }

        Iterator operator++(int) noexcept {
            Iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        bool operator==(const Iterator& other) const noexcept {
            return node_ == other.node_;
        }

        bool operator!=(const Iterator& other) const noexcept {
            return node_ != other.node_;
        }

    private:
        OrderNode* node_;
    };

    [[nodiscard]] Iterator begin() noexcept { return Iterator(head_); }
    [[nodiscard]] Iterator end() noexcept { return Iterator(nullptr); }

private:
    Price price_;
    OrderNode* head_;
    OrderNode* tail_;
    Quantity total_quantity_;
    std::size_t order_count_;
};

/**
 * @brief Comparison functors for price level ordering
 */
struct BuyPriceLevelCompare {
    // Max heap: higher prices first
    bool operator()(const PriceLevel* a, const PriceLevel* b) const noexcept {
        return a->price() < b->price();
    }
    bool operator()(Price a, Price b) const noexcept {
        return a < b;  // Higher is better for bids
    }
};

struct SellPriceLevelCompare {
    // Min heap: lower prices first
    bool operator()(const PriceLevel* a, const PriceLevel* b) const noexcept {
        return a->price() > b->price();
    }
    bool operator()(Price a, Price b) const noexcept {
        return a > b;  // Lower is better for asks
    }
};

} // namespace hft

