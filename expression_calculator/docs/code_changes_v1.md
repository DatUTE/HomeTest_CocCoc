# Code Changes V1: Harden Parser Against Deep Nesting DoS

## Issue

The previous evaluator used recursive parsing for factors. Each leading unary
operator and each nested parenthesis could add another call frame.

Problematic examples:

```text
-----...-----5
((((...((((5))))...))))
```

With enough `+`, `-`, or `(` tokens, one client could overflow a worker thread
stack. Because worker threads run in the same server process as the epoll loop,
this could crash the whole server and disconnect every client.

This was especially risky because the server supports very large expressions,
up to the configured `max_expression_bytes` limit.

## Root Cause

The parser used recursive descent:

- unary `+` / `-` recursively called factor parsing again.
- parenthesized expressions recursively called expression parsing.
- flat expressions such as `1*1*1*...` were mostly iterative, so existing large
  benchmarks did not catch the deep recursion case.

## Solution

The evaluator was rewritten to use an iterative shunting-yard style parser.

Changes:

- Replaced recursive expression parsing with explicit value and operator stacks.
- Preserved operator precedence for `+`, `-`, `*`, and `/`.
- Preserved support for unary `+` and unary `-`.
- Collapsed consecutive unary signs so long unary runs do not grow memory linearly.
- Kept existing overflow, division-by-zero, and malformed input validation.

Now deep nesting and long unary runs use heap-backed explicit stacks instead of
the thread call stack.

## Updated Files

- `src/evaluator.cpp`
- `tests/evaluator_tests.cpp`

## Verification

Added regression tests for:

- `10000` leading `-` operators followed by `5`.
- `10001` leading `-` operators followed by `5`.
- `5000` nested parentheses around `5`.

Commands:

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Result:

```text
100% tests passed
```
