#!/usr/bin/env python3
"""Benchmark one large expression request against the TCP server.

The generated expression is logically:

    1*1*1*...*1

Spaces are appended if needed to reach the exact requested expression byte
count. The expected result is always "1". The trailing newline is sent after
the expression and is not counted in --bytes.
"""

import argparse
import socket
import time


def send_large_expression(sock: socket.socket, expression_bytes: int, chunk_size: int) -> None:
    if expression_bytes < 1:
        raise ValueError("--bytes must be at least 1")

    sent = 0
    sock.sendall(b"1")
    sent += 1

    pair_chunk = (b"*1" * max(1, chunk_size // 2))[:chunk_size]
    while sent + 2 <= expression_bytes:
        remaining_pair_bytes = expression_bytes - sent
        to_send = min(len(pair_chunk), remaining_pair_bytes - (remaining_pair_bytes % 2))
        if to_send == 0:
            break
        sock.sendall(pair_chunk[:to_send])
        sent += to_send

    if sent < expression_bytes:
        sock.sendall(b" " * (expression_bytes - sent))
    sock.sendall(b"\n")


def main() -> int:
    parser = argparse.ArgumentParser(description="Benchmark one large expression request")
    parser.add_argument("--host", default="127.0.0.1", help="server host")
    parser.add_argument("--port", type=int, default=5555, help="server port")
    parser.add_argument(
        "--bytes",
        type=int,
        default=1024 * 1024 * 1024,
        help="expression size before the trailing newline",
    )
    parser.add_argument("--chunk-size", type=int, default=1024 * 1024, help="send chunk size")
    parser.add_argument("--timeout", type=float, default=600.0, help="socket timeout in seconds")
    parser.add_argument("--preconnect", action="store_true", help="exclude TCP connection setup from total_sec")
    args = parser.parse_args()

    connect_start = time.perf_counter()
    with socket.create_connection((args.host, args.port), timeout=args.timeout) as sock:
        sock.settimeout(args.timeout)
        connected_at = time.perf_counter()
        start = connected_at if args.preconnect else connect_start
        send_large_expression(sock, args.bytes, args.chunk_size)
        sent_at = time.perf_counter()
        response = b""
        while not response.endswith(b"\n"):
            chunk = sock.recv(4096)
            if not chunk:
                break
            response += chunk
        finished_at = time.perf_counter()

    connect_seconds = connected_at - connect_start
    send_seconds = sent_at - connected_at
    total_seconds = finished_at - start
    response_text = response.decode("utf-8", errors="replace").rstrip("\n")
    mib = args.bytes / (1024 * 1024)

    print(
        f"bytes={args.bytes} mib={mib:.2f} response={response_text!r} "
        f"connect_sec={connect_seconds:.3f} send_sec={send_seconds:.3f} total_sec={total_seconds:.3f} "
        f"send_mib_per_sec={mib / send_seconds:.2f} total_mib_per_sec={mib / total_seconds:.2f}"
    )
    return 0 if response_text == "1" else 1


if __name__ == "__main__":
    raise SystemExit(main())
