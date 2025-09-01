C++20 Price–Time Matching Engine (single-threaded)

Minimal exchange core: keeps a limit order book and matches trades with price–time priority (FIFO per price). Focused on determinism and predictable latency.

Features
 • Limit / Market / Cancel / Modify
 • Price → Time priority (FIFO per price level)
 • ~O(log L) per op (L = # of price levels)
 • Simple API: add_limit, add_market, modify, cancel, best_bid/ask

# Release (for benchmarks)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# Demo / smoke
./build/MatchingEngine

# Benchmarks (defaults if no args: burst 100000 42)
./build/me_bench burst   100000 42
./build/me_bench poisson 100000 42

Benchmark (examples, ops=100k, seed=42)
 • burst: throughput ≈ 1.58M ops/s
p50/p95/p99 (ns): post 200/400/600, add_limit 300/800/1200, add_market 300/1000/1600, modify 300/700/1100
 • poisson: throughput ≈ 1.50M ops/s
p50/p95/p99 (ns): post 200/400/600, add_limit 300/600/900, add_market 300/800/1200, cancel 100/400/600, modify 100/300/600

Architecture
 • Two price maps: bids (desc) / asks (asc) → best levels at begin().
 • Per price level: std::list<Order> (stable iterators) + total volume.
 • by_id index → O(1) cancel/modify by iterator handle.
