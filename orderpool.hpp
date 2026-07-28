#pragma once
#include <vector>
#include "order.hpp"
#include <stdexcept>
#include <cstdint>
#include <cstddef>

class OrderPool {
private:
    std::vector<Order> slots_;
    Order* free_head_ = nullptr;
    size_t free_count_ = 0;

// chaining the orders in the pool using the next pointer of the order struct to create a free list
public:
    explicit OrderPool(size_t capacity) : slots_(capacity), free_count_(capacity) {
        for (size_t i = 0; i + 1 < capacity; ++i) {
            slots_[i].next = &slots_[i + 1];
        }
        free_head_ = capacity > 0 ? &slots_[0] : nullptr;
    }
// take order from the pool if not available throw exception
    Order* acquire() {
        if (!free_head_) {
            throw std::runtime_error("No free orders available");
          }
        //   order o is taken formm free list then the free head is pointed to the next free order in the list and then the free count is substracted  
        Order* o = free_head_;
        free_head_ = free_head_->next;
        // reset the next and prev pointers of the acquired order to nullptr 
        o->next = nullptr;
        o->prev = nullptr;
        --free_count_;
        return o;
    }
// release the order back to the pool and add it to the free list
    void release(Order* o) {
        o->next = free_head_;
        free_head_ = o;
        ++free_count_;
    }
// return the capacity of the pool and the number of available os in the pool
    size_t capacity() const { return slots_.size(); }
    size_t available() const { return free_count_; }
};

