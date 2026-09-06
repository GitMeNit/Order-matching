#include "order_book.hpp"
#include <algorithm>

// Constructor: initialize the fixed price grid, allocate bid/ask level storage,
// and set up the bitmaps that efficiently track which price levels have active orders.
OrderBook::OrderBook(uint64_t min_price, uint64_t max_price, size_t order_pool_capacity)
    : min_price_(min_price),
      max_price_(max_price),
      levels_count_(max_price - min_price + 1),
      bids_(levels_count_),
      asks_(levels_count_),
      bid_bitmap_((levels_count_ + 63) / 64, 0),
      ask_bitmap_((levels_count_ + 63) / 64, 0),
      pool_(order_pool_capacity) {}

// Bitmap helper: set a bit for a price level.
// Each 64-bit word represents 64 price levels; idx / 64 picks the word,
// and idx % 64 picks the bit inside that word.
void OrderBook::set_bit(std::vector<uint64_t>& bm, size_t idx) {
    bm[idx / 64] |= (1ULL << (idx % 64));
}

// Bitmap helper: clear a bit when a price level becomes empty.
void OrderBook::clear_bit(std::vector<uint64_t>& bm, size_t idx) {
    bm[idx / 64] &= ~(1ULL << (idx % 64));
}

// Find the highest active price level at or below from_idx.
// This is used for bids because higher price is better, so we look downward
// from the current best bid to find the next valid price level.
int64_t OrderBook::find_highest_set(const std::vector<uint64_t>& bm, int64_t from_idx) const {
    if (from_idx < 0) return -1;
    size_t word = static_cast<size_t>(from_idx) / 64;
    int bit_in_word = static_cast<int>(from_idx % 64);
    uint64_t mask = (bit_in_word == 63) ? ~0ULL : ((1ULL << (bit_in_word + 1)) - 1);
    uint64_t v = bm[word] & mask;
    if (v) return static_cast<int64_t>(word * 64 + (63 - __builtin_clzll(v)));

    for (size_t w = word; w-- > 0;) {
        if (bm[w]) return static_cast<int64_t>(w * 64 + (63 - __builtin_clzll(bm[w])));
    }
    return -1;
}

// Find the lowest active price level at or above from_idx.
// This is used for asks because lower price is better, so we look upward
// from the current best ask to find the next valid level.
int64_t OrderBook::find_lowest_set(const std::vector<uint64_t>& bm, int64_t from_idx) const {
    if (from_idx < 0) return -1;
    size_t word = static_cast<size_t>(from_idx) / 64;
    int bit_in_word = static_cast<int>(from_idx % 64);
    uint64_t mask = (bit_in_word == 0) ? ~0ULL : (~0ULL << bit_in_word);
    uint64_t v = bm[word] & mask;
    if (v) return static_cast<int64_t>(word * 64 + __builtin_ctzll(v));

    for (size_t w = word + 1; w < bm.size(); ++w) {
        if (bm[w]) return static_cast<int64_t>(w * 64 + __builtin_ctzll(bm[w]));
    }
    return -1;
}

// Main order-entry logic.
// This function tries to match the incoming order against the opposite side first.
// If the incoming order still has quantity left after matching, it is placed in the book.
std::vector<Fill> OrderBook::add_limit_order(uint64_t order_id, Side side, uint64_t price, uint32_t original_qty) {
    std::vector<Fill> fills;
    uint32_t remaining = original_qty;

    // Buy order path: match against resting asks while the best ask is at or below the buyer's price.
    if (side == Side::Buy) {
        while (remaining > 0 && best_ask_idx_ != -1 && index_to_price(best_ask_idx_) <= price) {
            PriceLevel& level = asks_[static_cast<size_t>(best_ask_idx_)];
            while (remaining > 0 && level.head) {
                Order* resting = level.head;
                uint32_t traded = std::min(remaining, resting->remaining_qty);
                fills.push_back({order_id, resting->id, index_to_price(best_ask_idx_), traded});
                remaining -= traded;
                if (traded == resting->remaining_qty) {
                    level.unlink(resting);
                    id_to_order_.erase(resting->id);
                    pool_.release(resting);
                } else {
                    level.reduce_qty(resting, traded);
                }
            }
            if (level.empty()) {
                clear_bit(ask_bitmap_, static_cast<size_t>(best_ask_idx_));
                best_ask_idx_ = find_lowest_set(ask_bitmap_, best_ask_idx_ + 1);
            }
        }

        // If there is still quantity left after matching, add the remaining buy order to the bid book.
        if (remaining > 0) {
            Order* o = pool_.acquire();
            o->id = order_id;
            o->price = price;
            o->original_qty = remaining;
            o->remaining_qty = remaining;
            o->side = Side::Buy;
            o->sequence = sequence_++;
            size_t idx = price_to_index(price);
            bids_[idx].push_back(o);
            set_bit(bid_bitmap_, idx);
            if (best_bid_idx_ == -1 || static_cast<int64_t>(idx) > best_bid_idx_) {
                best_bid_idx_ = static_cast<int64_t>(idx);
            }
            id_to_order_[order_id] = o;
        }
    } else {
        // Sell order path: match against resting bids while the best bid is at or above the seller's price.
        while (remaining > 0 && best_bid_idx_ != -1 && index_to_price(best_bid_idx_) >= price) {
            PriceLevel& level = bids_[static_cast<size_t>(best_bid_idx_)];
            while (remaining > 0 && level.head) {
                Order* resting = level.head;
                uint32_t traded = std::min(remaining, resting->remaining_qty);
                fills.push_back({resting->id, order_id, index_to_price(best_bid_idx_), traded});
                remaining -= traded;
                if (traded == resting->remaining_qty) {
                    level.unlink(resting);
                    id_to_order_.erase(resting->id);
                    pool_.release(resting);
                } else {
                    level.reduce_qty(resting, traded);
                }
            }
            if (level.empty()) {
                clear_bit(bid_bitmap_, static_cast<size_t>(best_bid_idx_));
                best_bid_idx_ = find_highest_set(bid_bitmap_, best_bid_idx_ - 1);
            }
        }

        // If the sell order still has quantity after matching, place it in the ask book.
        if (remaining > 0) {
            Order* o = pool_.acquire();
            o->id = order_id;
            o->price = price;
            o->original_qty = remaining;
            o->remaining_qty = remaining;
            o->side = Side::Sell;
            o->sequence = sequence_++;
            size_t idx = price_to_index(price);
            asks_[idx].push_back(o);
            set_bit(ask_bitmap_, idx);
            if (best_ask_idx_ == -1 || static_cast<int64_t>(idx) < best_ask_idx_) {
                best_ask_idx_ = static_cast<int64_t>(idx);
            }
            id_to_order_[order_id] = o;
        }
    }

    return fills;
}

// Cancellation path: look up the order by id, remove it from its price level,
// update the bitmap if that level becomes empty, and then recycle the memory back to the pool.
bool OrderBook::cancel_order(uint64_t order_id) {
    auto it = id_to_order_.find(order_id);
    if (it == id_to_order_.end()) return false;
    Order* o = it->second;
    size_t idx = price_to_index(o->price);

    if (o->side == Side::Buy) {
        bids_[idx].unlink(o);
        if (bids_[idx].empty()) {
            clear_bit(bid_bitmap_, idx);
            if (static_cast<int64_t>(idx) == best_bid_idx_) {
                best_bid_idx_ = find_highest_set(bid_bitmap_, static_cast<int64_t>(idx) - 1);
            }
        }
    } else {
        asks_[idx].unlink(o);
        if (asks_[idx].empty()) {
            clear_bit(ask_bitmap_, idx);
            if (static_cast<int64_t>(idx) == best_ask_idx_) {
                best_ask_idx_ = find_lowest_set(ask_bitmap_, static_cast<int64_t>(idx) + 1);
            }
        }
    }

    id_to_order_.erase(it);
    pool_.release(o);
    return true;
}

// Return the current best bid if one exists; otherwise return std::nullopt.
std::optional<uint64_t> OrderBook::best_bid_price() const {
    if (best_bid_idx_ == -1) return std::nullopt;
    return index_to_price(best_bid_idx_);
}

// Return the current best ask if one exists; otherwise return std::nullopt.
std::optional<uint64_t> OrderBook::best_ask_price() const {
    if (best_ask_idx_ == -1) return std::nullopt;
    return index_to_price(best_ask_idx_);
}
