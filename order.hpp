// i was thinking about order : buy-sell, time at nanosecond precision but i cannot use time module ,
// update the current market value a person is ready to sell for or buy for, tickers- nor needed since the book is for single instrument
// if there are two people with the same price, the one who placed the order first gets priority, so we need to keep track of the order of placement
// but the orders, if in queue cannot be a linked list since the elements of ll are not contiguous in memory, so we need to use a vector or deque to store the orders, and we can use a priority queue to sort the orders based on price and time of placement
// order id will be used to map easily to the order in the vector or deque, so we can use a map to store the order id and the index of the order in the vector or deque
// ig there need to be, original qty, remaining qty, sequence
// free list built on top of contiguous memory
// and a fixed block of memory for the orders so the os/ dont assigns a random place in memory for each order, and we can use a free list to manage the memory for the orders, so we can reuse the memory for the orders that have been filled or canceled
// new orders / cancelled orders can be done by just changing the pointers of each of the neighbours and orders so its o(1)

#pragma once
#include <cstdint>

enum class Side : uint8_t { Buy= 0, Sell=1 };

struct Order {
    uint64_t  id = 0;
    uint64_t price= 0;
    uint64_t sequence=0;
    uint32_t original_qty=0;
    uint32_t remaining_qty=0;
    Side side=Side::Buy;
    bool active= false; // if the order is active or not, if it is not active then it can be reused

    // instead of another queue that just store free/ used orders we can just use the avilable order's next pointer to point ot free space

    Order* next = nullptr;
    Order* prev = nullptr;



};