# Code Changes V2: Add Backpressure For Pipelined Requests

## Issue

The TCP server allowed one connection to pipeline complete expressions without
any queue limit.

Example payload:

```text
1
1
1
...
```

The server would keep reading from the socket and push every complete line into
`ClientState::pendingExpressions`, even if the shared worker pool was already
behind.

`maxExpressionBytes` only limited the size of one expression. It did not limit:

- the number of queued expressions per client.
- the total queued expression bytes per client.
- the amount of response data that could later be generated from that queue.

As a result, one aggressive client could grow server memory without bound and
affect all other clients.

## Root Cause

The read path drained sockets whenever `EPOLLIN` was available.

Before this change:

- `handleClientReadable()` kept reading until `EAGAIN`.
- Every complete line was appended to `pendingExpressions`.
- `submitNextExpression()` only ran one expression per client at a time.
- Nothing paused socket reads when a client had too much queued work.

This created an unbounded producer-consumer gap between socket input and worker
completion.

## Solution

Added per-connection backpressure.

Changes:

- Track queued expression bytes with `ClientState::pendingExpressionBytes`.
- Cap queued expression count per connection with `kMaxPendingExpressionsPerClient`.
- Cap queued expression bytes per connection using the configured
  `m_maxExpressionBytes`, so the server still supports a single expression up to
  the advertised limit.
- Stop watching `EPOLLIN` for a client while that client's queue is above the
  threshold.
- Re-enable `EPOLLIN` after worker completions drain the queue.
- Keep `EPOLLOUT` enabled when a client has pending output.

This pushes pressure back to the TCP socket instead of allowing unbounded server
memory growth.

## Updated Files

- `include/tcp_server.h`
- `src/tcp_server.cpp`

## Behavior

The server does not close a client just because it is backpressured. It simply
stops reading more data from that socket until queued work drops below the
threshold.

This preserves valid large-expression support:

- A `1GiB` expression is still allowed when `max_expression_bytes` is `1GiB`.
- Many small pipelined expressions are limited by queue count.
- Multiple large pipelined expressions are limited by queued bytes.

## Verification

Commands:

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Result:

```text
100% tests passed
```

## Notes

This fix protects against a single-connection memory-exhaustion DoS. A stronger
production hardening step would add a global server-wide memory or backlog limit
across all clients.
