#include "order_book.hpp"
#include <iostream>
#include <iomanip>
#include <vector>
#include <random>
#include <chrono>
#include <algorithm>
#include <cassert>
#include <thread>
#include <x86intrin.h>  // for __rdtsc()

#if defined(__x86_64__) || defined(_M_X64)

// Tiny helper to grab the CPU timestamp counter. This is just for measuring speed.
static inline uint64_t read_tsc() { return __rdtsc(); }
#define HAVE_TSC 1
#else
#define HAVE_TSC 0
#endif

using Clock = std::chrono::steady_clock;

// Quick sanity test: make sure the book behaves like we expect for basic buy/sell/match/cancel cases.
static void sanity_check() {
    OrderBook book(1, 1000, 1000);

    auto f1 = book.add_limit_order(1, Side::Buy, 100, 50);
    assert(f1.empty());
    auto f2 = book.add_limit_order(2, Side::Sell, 110, 50);
    assert(f2.empty());
    assert(book.best_bid_price().value() == 100);
    assert(book.best_ask_price().value() == 110);

    // A buy coming in at 110 should match against the sell sitting at 110, but only for part of the qty.
    auto f3 = book.add_limit_order(3, Side::Buy, 110, 30);
    assert(f3.size() == 1);
    assert(f3[0].resting_order_id == 2);
    assert(f3[0].qty == 30);
    assert(book.best_ask_price().value() == 110);   // order 2 still sits there with 20 left
    assert(book.resting_order_count() == 2);         // order 1 and the leftover part of order 2

    bool cancelled = book.cancel_order(1);
    assert(cancelled);
    assert(!book.best_bid_price().has_value());
    assert(book.resting_order_count() == 1);
    assert(!book.cancel_order(9999)); // cancelling a non-existent id fails

    std::cout << "sanity checks passed\n";
}

#if HAVE_TSC
// This little calibration step figures out how many CPU cycles fit into 1 nanosecond.
// It compares the TSC timer with a real wall-clock timer for a tiny sleep.
// Basically: "how fast is my timer vs actual time?"
static double calibrate_cycles_per_ns() {
    auto t0 = Clock::now();
    uint64_t c0 = read_tsc();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto t1 = Clock::now();
    uint64_t c1 = read_tsc();
    double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
    return static_cast<double>(c1 - c0) / ns;
}
#endif

// Just a bucket for all the benchmark numbers we want to print later.
struct LatencyStats {
    double p50, p90, p99, p999, max_v;
    double ops_per_sec;
};

// This is the big benchmark loop: add orders, sometimes cancel them, and measure how fast it all is.
static LatencyStats run_benchmark(size_t num_ops) {
    constexpr uint64_t MIN_PRICE = 1;
    constexpr uint64_t MAX_PRICE = 200000;
    OrderBook book(MIN_PRICE, MAX_PRICE, num_ops + 50000);

    std::mt19937_64 rng(42);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int> qty_dist(1, 500);
    std::uniform_int_distribution<int> price_offset_dist(-500, 500);
    std::uniform_real_distribution<double> action_dist(0.0, 1.0);

    const uint64_t mid_price = 100000;
    uint64_t next_order_id = 1;
    std::vector<uint64_t> active_ids;
    active_ids.reserve(num_ops);

    // Warm-up phase: do a bunch of random book actions before timing starts.
    // This helps avoid weird startup effects like cold cache or branch predictor noise.
    for (int i = 0; i < 20000; ++i) {
        Side side = side_dist(rng) ? Side::Sell : Side::Buy;
        int64_t offset = price_offset_dist(rng);
        uint64_t price = static_cast<uint64_t>(static_cast<int64_t>(mid_price) + offset);
        uint32_t qty = static_cast<uint32_t>(qty_dist(rng));
        auto fills = book.add_limit_order(next_order_id, side, price, qty);
        if (fills.empty()) active_ids.push_back(next_order_id);
        ++next_order_id;
    }

#if HAVE_TSC
    double cycles_per_ns = calibrate_cycles_per_ns();
    std::vector<uint64_t> cycles_samples;
    cycles_samples.reserve(num_ops);
#else
    std::vector<uint64_t> ns_samples;
    ns_samples.reserve(num_ops);
#endif

    // Main timed loop: sometimes cancel an active order, sometimes add a new one.
    for (size_t i = 0; i < num_ops; ++i) {
        double action = action_dist(rng);
        bool do_cancel = action < 0.35 && !active_ids.empty();

        uint64_t id_to_cancel = 0;
        size_t swap_idx = 0;
        Side side = side_dist(rng) ? Side::Sell : Side::Buy;
        int64_t offset = price_offset_dist(rng);
        uint64_t price = static_cast<uint64_t>(static_cast<int64_t>(mid_price) + offset);
        uint32_t qty = static_cast<uint32_t>(qty_dist(rng));

        if (do_cancel) {
            std::uniform_int_distribution<size_t> pick(0, active_ids.size() - 1);
            swap_idx = pick(rng);
            id_to_cancel = active_ids[swap_idx];
        }

#if HAVE_TSC
        uint64_t c0 = read_tsc();
#else
        auto t0 = Clock::now();
#endif

        if (do_cancel) {
            // Remove a random live order from the book.
            book.cancel_order(id_to_cancel);
        } else {
            // Add a new limit order. If it matches immediately and fully fills, it won't stay in the book.
            auto fills = book.add_limit_order(next_order_id, side, price, qty);
            if (fills.empty()) active_ids.push_back(next_order_id);
            ++next_order_id;
        }

#if HAVE_TSC
        uint64_t c1 = read_tsc();
        cycles_samples.push_back(c1 - c0);
#else
        auto t1 = Clock::now();
        ns_samples.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
#endif

        if (do_cancel) {
            // Swap the cancelled ID out of the tracking list so we keep the list tidy.
            active_ids[swap_idx] = active_ids.back();
            active_ids.pop_back();
        }
    }

#if HAVE_TSC
    std::sort(cycles_samples.begin(), cycles_samples.end());
    auto to_ns = [&](uint64_t cycles) { return cycles / cycles_per_ns; };
    auto pct = [&](double p) {
        size_t idx = static_cast<size_t>(p * cycles_samples.size());
        if (idx >= cycles_samples.size()) idx = cycles_samples.size() - 1;
        return to_ns(cycles_samples[idx]);
    };
    uint64_t total_cycles = 0;
    for (auto v : cycles_samples) total_cycles += v;
    double total_ns = to_ns(total_cycles);
    double max_ns = to_ns(cycles_samples.back());
#else
    std::sort(ns_samples.begin(), ns_samples.end());
    auto pct = [&](double p) -> double {
        size_t idx = static_cast<size_t>(p * ns_samples.size());
        if (idx >= ns_samples.size()) idx = ns_samples.size() - 1;
        return static_cast<double>(ns_samples[idx]);
    };
    uint64_t total_ns_i = 0;
    for (auto v : ns_samples) total_ns_i += v;
    double total_ns = static_cast<double>(total_ns_i);
    double max_ns = static_cast<double>(ns_samples.back());
#endif

    LatencyStats stats;
    stats.p50 = pct(0.50);
    stats.p90 = pct(0.90);
    stats.p99 = pct(0.99);
    stats.p999 = pct(0.999);
    stats.max_v = max_ns;
    stats.ops_per_sec = static_cast<double>(num_ops) / (total_ns / 1e9);
    return stats;
}

int main() {
    // First, make sure the book isn't obviously broken.
    sanity_check();

    const size_t num_ops = 1'000'000;
    std::cout << "\nRunning benchmark: " << num_ops
              << " ops (~65% new limit orders, ~35% cancels)"
#if HAVE_TSC
              << " [TSC timing]"
#else
              << " [chrono timing]"
#endif
              << "...\n";

    auto stats = run_benchmark(num_ops);

    std::cout << std::fixed << std::setprecision(1);
    std::cout << "\n--- Per-operation latency ---\n";
    std::cout << "p50:   " << stats.p50 << " ns\n";
    std::cout << "p90:   " << stats.p90 << " ns\n";
    std::cout << "p99:   " << stats.p99 << " ns\n";
    std::cout << "p99.9: " << stats.p999 << " ns\n";
    std::cout << "max:   " << stats.max_v << " ns\n";
    std::cout << "throughput: " << stats.ops_per_sec / 1e6 << " M ops/sec\n";

    return 0;
}