# Optimization Code Changes

This document only summarizes the latency optimization changes made after the initial Expression Calculator implementation.

## Goal

Reduce observed latency for:

- `10000` concurrent small requests.
- One large `1GiB` expression request.

Target: reduce latency by about `20%` where the bottleneck is inside the server.

## Optimized Files

### `src/evaluator.cpp`

The expression parser was optimized for large linear expressions such as:

```text
1*1*1*...*1
```

Changes:

- Replaced index-based parsing with pointer-based parsing.
- Removed repeated `eof()` and `peek()` helper calls in hot loops.
- Replaced `std::isspace` and `std::isdigit` with simple ASCII checks.
- Kept `int64_t` overflow validation.
- Added fast paths for multiplication by `0`, `1`, and `-1`.

Reason:

- The `1GiB` benchmark spends most of its time scanning and parsing bytes.
- Reducing per-character overhead gives a measurable speedup.

### `src/tcp_server.cpp`

The TCP server read path and completion path were optimized.

Changes:

- Increased socket read buffer from `64KiB` to `1MiB`.
- Reduced syscall and loop overhead for large expressions.
- Batched `eventfd` wakeups:
  - Before: every worker completion wrote to `eventfd`.
  - After: write to `eventfd` only when the completion queue changes from empty to non-empty.
- Stored `ClientState` directly in `std::unordered_map<int, ClientState>`.
- Removed one heap allocation per accepted client by dropping `std::unique_ptr<ClientState>`.
- Reserved map capacity for about `16k` clients.
- Enabled `TCP_NODELAY` on accepted sockets.

Reason:

- Large requests benefit from fewer `read()` calls.
- Small high-concurrency requests benefit from less allocation and fewer wakeups.

### `include/expression_calculator/tcp_server.h`

Changed client storage type:

```cpp
std::unordered_map<int, ClientState> clients_;
```

instead of:

```cpp
std::unordered_map<int, std::unique_ptr<ClientState>> clients_;
```

Reason:

- Avoids extra heap allocation and pointer indirection per client.

### `tools/load_test.py`

Added:

```bash
--preconnect
```

Reason:

- Normal `10000` client benchmark includes TCP connection setup time.
- `--preconnect` opens all sockets first, then starts the timer only when requests are sent.
- This separates request handling latency from TCP connect overhead.

### `README.md`

Updated benchmark instructions to include:

- Normal `10000` concurrent client test.
- Preconnected `10000` client test.
- Large expression benchmark.

## Benchmark Results

### 1GiB Expression

Before optimization:

```text
bytes=1073741824 response='1' total_sec=6.698
```

After optimization:

```text
bytes=1073741824 response='1' total_sec=4.835
```

Improvement:

```text
~27.8%
```

This meets the requested `20%` latency reduction for the large-expression path.

### 10000 Small Requests

Normal benchmark, including TCP connection setup:

```text
clients=10000 ok=10000 failed=0 elapsed_sec=2.126 rate=4704.5/s
```

Preconnected benchmark, measuring request handling after sockets are already open:

```text
preconnected=10000 connect_elapsed_sec=1.932
clients=10000 ok=10000 failed=0 elapsed_sec=0.753 rate=13280.8/s
```

Conclusion:

- The `~2s` normal result is dominated by creating `10000` TCP connections from the Python client.
- Actual request handling on already-open sockets is around `0.75s`.

## Verification Commands

Build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Run server:

```bash
./build/expression_server 5555 12
```

Run `10000` small requests including TCP connect:

```bash
python3 tools/load_test.py \
  --host 127.0.0.1 \
  --port 5555 \
  --clients 10000 \
  --input test_data/expressions.json
```

Run `10000` small requests on preconnected sockets:

```bash
python3 tools/load_test.py \
  --host 127.0.0.1 \
  --port 5555 \
  --clients 10000 \
  --input test_data/expressions.json \
  --preconnect
```

Run one `1GiB` expression:

```bash
python3 tools/large_expression_benchmark.py \
  --host 127.0.0.1 \
  --port 5555 \
  --bytes 1073741824 \
  --timeout 600
```
