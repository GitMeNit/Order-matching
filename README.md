# Order Matching Engine

A compact C++ implementation of a limit order book for a single instrument. The engine supports buy/sell orders, price-time priority, matching against the opposite side, partial fills, and cancellation. It is designed for fast operation with a fixed-size memory pool and bitmaps to track active price levels efficiently.

## Features

- Limit order entry for buy and sell orders
- Price-time priority within each price level
- Immediate matching against the opposite side
- Partial fill support
- Order cancellation
- Best bid / best ask lookup
- Fixed-size pool-based allocation for orders
- Bitmap-based tracking of non-empty price levels for efficient price discovery

## Architecture

The project is organized into a few core components:

- `order.hpp` — order representation and side enum
- `pricelevel.hpp` — per-price queue semantics and quantity tracking
- `orderpool.hpp` — free-list based memory pool for reusing order slots
- `order_book.hpp` — public order book interface and matching logic contract
- `OB.cpp` — order book implementation and matching logic
- `main.cpp` — sanity checks and latency benchmark

## How it works

- Each order has an `id`, `price`, `side`, `original_qty`, `remaining_qty`, and `sequence`.
- Orders at the same price are kept in a linked list ordered by time priority.
- A price level stores the head and tail of that queue and tracks total quantity at the level.
- The order book uses bitmaps to quickly find the best active bid or ask without scanning the entire price range.
- When an incoming order is added, it matches against the opposite side while the book is crossed.
- Any unmatched quantity remains resting in the appropriate price level.
- Canceling an order removes it from its level and recycles its slot back into the pool.

## Build and run

From the project root:

```bash
g++ -std=c++17 -O2 main.cpp OB.cpp -o main.exe
./main.exe
```

On Windows in PowerShell:

```powershell
g++ -std=c++17 -O2 main.cpp OB.cpp -o main.exe
.\main.exe
```

## Example behavior

The program runs a sanity test first to validate:

- basic buy/sell matching
- partial fills
- best bid / ask updates
- order cancellation correctness

Then it executes a benchmark with randomized operations to estimate latency and throughput.

## Sample output

```text
sanity checks passed

Running benchmark: 1000000 ops (~65% new limit orders, ~35% cancels) [TSC timing]...

--- Per-operation latency ---
p50:   84.8 ns
p90:   243.7 ns
p99:   653.0 ns
p99.9: 2069.3 ns
max:   1236427.3 ns
throughput: 7.5 M ops/sec
```

## Notes

- This is a single-instrument order book and does not include exchange-wide market state, multiple instruments, or networked trading interfaces.
- The implementation is optimized for speed and simplicity rather than full production exchange features.
- The use of a fixed memory pool avoids per-order heap allocation and makes queue operations more predictable.

## Potential extensions

- Add market orders
- Support multiple instruments or symbol-level books
- Add order modification (reduce quantity / amend price)
- Add more complete trade reporting and execution logs
- Integrate with a matching engine test harness or CLI interface
