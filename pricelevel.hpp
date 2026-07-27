#pragma once
#include "order.hpp"
#include <cstdint>

// same price ? time priority
// pushback and delete functions live inside the order itsel so its no allocation and deallocations, no search

struct PriceLevel { 
    Order* head = nullptr;
    Order* tail = nullptr; 
    uint64_t total_qty = 0;
    uint32_t order_count = 0;

    bool empty() const {return head == nullptr;}

    void push_back (Order* o){
        o-> next =nullptr;
        o-> prev = tail;
        if (tail) tail->next = o;
        else head = o;  
        tail = o;
        total_qty += o->remaining_qty;
        order_count++;

    }

    void unlink (Order* o){
        // if we want to  remove we point the order's next and prev to each other and then we can just set the order's next and prev to nullptr so it can be reused
        if (o->prev) o->prev->next = o->next;
        else head= o->next;
        if (o->next) o->next->prev = o->prev;
        else tail = o->prev;
        total_qty = total_qty - o->remaining_qty;
        --order_count;
        o->next = o->prev = nullptr;


    }
     void reduce_qty(Order* o, uint32_t by) {
        o->remaining_qty -= by;
        total_qty -= by; 
    }


};