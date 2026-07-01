# Expression Calculator Server

Multithreaded C++ TCP server for calculating integer math expressions. Each request is one expression
terminated by `\n`; the server writes one result line back and keeps the socket open for more requests.

Supported grammar:

```text
expression = term (('+' | '-') term)*
term       = factor (('*' | '/') factor)*
factor     = number | '(' expression ')' | ('+' | '-') factor
number     = decimal int64
```

The evaluator uses integer division and reports errors such as malformed input, division by zero, and
`int64_t` overflow.

## Architecture

- `src/evaluator.cpp`: single-pass recursive descent parser with operator precedence.
- `src/thread_pool.cpp`: fixed-size worker pool. By default the server uses `std::thread::hardware_concurrency()`.
- `src/tcp_server.cpp`: Linux `epoll` non-blocking TCP server. One event loop handles sockets; worker threads only evaluate expressions.
- Per connection, expressions are processed in order. At most one expression per client is evaluated at a time, but many clients are served concurrently.
- Worker completion is reported back to the event loop through `eventfd`, avoiding socket writes from worker threads.

This design can keep many idle or slow clients connected without one thread per client, while CPU-heavy
calculation is limited to the configured number of workers.

## Dependencies

Ubuntu packages:

```bash
sudo apt update
sudo apt install -y build-essential cmake
```

No third-party C++ libraries are required.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Run Tests

```bash
ctest --test-dir build --output-on-failure
```

Or run directly:

```bash
./build/evaluator_tests
```

## Run Server

```bash
./build/expression_server [port] [workers] [max_expression_bytes]
```

Defaults:

- `port`: `5555`
- `workers`: number of CPU cores reported by C++
- `max_expression_bytes`: `1073741824` bytes

Example:

```bash
./build/expression_server 5555 8
```

Test with netcat:

```bash
nc localhost 5555
(3+2*4)*7
77
1+2*3
7
```

Send expressions from a text fixture:

```bash
nc localhost 5555 < test_data/expressions.txt
```

## Generate Test Expressions

```bash
./build/generate_expressions [count] [depth] [seed]
```

Example:

```bash
./build/generate_expressions 20 5 123
```

## Concurrent Load Test

`tools/load_test.py` is a standard-library Python client that opens many TCP connections at the same
time. It can read either `test_data/expressions.json` with expected responses or
`test_data/expressions.txt` as newline-delimited expressions.

Start the server:

```bash
./build/expression_server 5555 8
```

Run 10000 concurrent clients:

```bash
python3 tools/load_test.py --host 127.0.0.1 --port 5555 --clients 10000 --input test_data/expressions.json
```

To measure request handling on already-open sockets, exclude TCP connection setup from the timer:

```bash
python3 tools/load_test.py --host 127.0.0.1 --port 5555 --clients 10000 --input test_data/expressions.json --preconnect
```

Expected output shape:

```text
clients=10000 ok=10000 failed=0 elapsed_sec=... rate=.../s
```

## Large Expression Benchmark

The task allows a single expression up to 1GB. The short fixtures above test correctness and concurrency,
but they do not test the large-expression path. Use `tools/large_expression_benchmark.py` to stream one
large expression directly to the server without creating a temporary 1GB file.

Example with 64 MiB:

```bash
python3 tools/large_expression_benchmark.py --host 127.0.0.1 --port 5555 --bytes 67108864
```

Example with 1 GiB:

```bash
python3 tools/large_expression_benchmark.py --host 127.0.0.1 --port 5555 --bytes 1073741824 --timeout 600
```

The generated expression is `1*1*1*...*1`, so the expected response is always `1`.

## Mixed Workload Benchmark

`tools/mixed_workload_test.py` combines many small concurrent clients with one or more large-expression
clients. This is closer to a production stress case than testing only short requests or only one large
request.

Example: `9999` small clients plus `1` large 1GiB client at the same time:

```bash
python3 tools/mixed_workload_test.py \
  --host 127.0.0.1 \
  --port 5555 \
  --small-clients 9999 \
  --large-clients 1 \
  --large-bytes 1073741824 \
  --timeout 900
```

Example: `10000` small clients plus `50` large 64MiB clients:

```bash
python3 tools/mixed_workload_test.py \
  --host 127.0.0.1 \
  --port 5555 \
  --small-clients 10000 \
  --large-clients 50 \
  --large-bytes 67108864 \
  --timeout 900
```

## Notes For Large Scale

For more than 10000 concurrent clients, raise the process file descriptor limit before running:

```bash
ulimit -n 20000
```

For very large expressions, use a Release build. The parser is linear in expression length, so processing
time is dominated by scanning the input and integer operations.
