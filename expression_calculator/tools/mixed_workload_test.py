#!/usr/bin/env python3
"""Mixed workload benchmark for the expression calculator server.

Examples:
  - 9999 small requests + 1 large 1GiB request
  - 10000 small requests + 50 large 64MiB requests

The script uses separate TCP connections for each client and validates every
response. Large expressions are generated as "1*1*1*...*1", so their expected
response is always "1".
"""

import argparse
import asyncio
import json
import time
from pathlib import Path


def load_small_cases(path: Path):
    if path.suffix != ".json":
        raise ValueError("small fixture must be a JSON file with expected responses")

    with path.open("r", encoding="utf-8") as f:
        cases = json.load(f)
    if not cases:
        raise ValueError("small fixture is empty")
    return cases


def response_matches(response: str, expected: str | None, expected_prefix: str | None) -> bool:
    if expected is not None:
        return response == expected
    if expected_prefix is not None:
        return response.startswith(expected_prefix)
    return bool(response)


async def run_small_client(client_id: int, args, cases):
    reader, writer = await asyncio.open_connection(args.host, args.port)
    return await run_connected_small_client(reader, writer, client_id, cases, args)


async def run_connected_small_client(
    reader: asyncio.StreamReader,
    writer: asyncio.StreamWriter,
    client_id: int,
    cases,
    args,
):
    case = cases[client_id % len(cases)]
    try:
        writer.write((case["expression"] + "\n").encode("utf-8"))
        await writer.drain()
        raw_response = await asyncio.wait_for(reader.readline(), timeout=args.timeout)
        response = raw_response.decode("utf-8", errors="replace").rstrip("\n")
        ok = response_matches(response, case.get("expected"), case.get("expected_prefix"))
        return {"kind": "small", "ok": ok, "response": response}
    finally:
        writer.close()
        await writer.wait_closed()


async def write_large_expression(writer: asyncio.StreamWriter, expression_bytes: int, chunk_size: int):
    if expression_bytes < 1:
        raise ValueError("--large-bytes must be at least 1")

    sent = 0
    writer.write(b"1")
    sent += 1

    pair_chunk = (b"*1" * max(1, chunk_size // 2))[:chunk_size]
    while sent + 2 <= expression_bytes:
        remaining_pair_bytes = expression_bytes - sent
        to_send = min(len(pair_chunk), remaining_pair_bytes - (remaining_pair_bytes % 2))
        if to_send == 0:
            break
        writer.write(pair_chunk[:to_send])
        sent += to_send
        if writer.transport.get_write_buffer_size() >= args_write_buffer_limit(chunk_size):
            await writer.drain()

    if sent < expression_bytes:
        writer.write(b" " * (expression_bytes - sent))
    writer.write(b"\n")
    await writer.drain()


def args_write_buffer_limit(chunk_size: int) -> int:
    return max(chunk_size * 4, 4 * 1024 * 1024)


async def run_large_client(client_id: int, args):
    reader, writer = await asyncio.open_connection(args.host, args.port)
    return await run_connected_large_client(reader, writer, client_id, args)


async def run_connected_large_client(
    reader: asyncio.StreamReader,
    writer: asyncio.StreamWriter,
    client_id: int,
    args,
):
    start = time.perf_counter()
    try:
        await write_large_expression(writer, args.large_bytes, args.chunk_size)
        sent_at = time.perf_counter()
        raw_response = await asyncio.wait_for(reader.readline(), timeout=args.timeout)
        finished_at = time.perf_counter()
        response = raw_response.decode("utf-8", errors="replace").rstrip("\n")
        return {
            "kind": "large",
            "ok": response == "1",
            "response": response,
            "send_sec": sent_at - start,
            "total_sec": finished_at - start,
            "client_id": client_id,
        }
    finally:
        writer.close()
        await writer.wait_closed()


async def run(args):
    small_cases = load_small_cases(Path(args.small_input))
    preconnected = []
    if args.preconnect:
        connect_start = time.perf_counter()
        preconnected = await asyncio.gather(
            *[
                asyncio.open_connection(args.host, args.port)
                for _ in range(args.small_clients + args.large_clients)
            ]
        )
        connect_elapsed = time.perf_counter() - connect_start
        print(
            f"preconnected={args.small_clients + args.large_clients} "
            f"connect_elapsed_sec={connect_elapsed:.3f}"
        )

    start = time.perf_counter()
    tasks = []
    if args.preconnect:
        for i in range(args.small_clients):
            reader, writer = preconnected[i]
            tasks.append(asyncio.create_task(run_connected_small_client(reader, writer, i, small_cases, args)))
        for i in range(args.large_clients):
            reader, writer = preconnected[args.small_clients + i]
            tasks.append(asyncio.create_task(run_connected_large_client(reader, writer, i, args)))
    else:
        for i in range(args.small_clients):
            tasks.append(asyncio.create_task(run_small_client(i, args, small_cases)))
        for i in range(args.large_clients):
            tasks.append(asyncio.create_task(run_large_client(i, args)))
    results = await asyncio.gather(*tasks, return_exceptions=True)
    elapsed = time.perf_counter() - start

    summary = {
        "small_ok": 0,
        "small_failed": 0,
        "large_ok": 0,
        "large_failed": 0,
        "exceptions": 0,
    }
    exception_counts = {}
    bad_samples = []
    large_totals = []

    for result in results:
        if isinstance(result, Exception):
            summary["exceptions"] += 1
            key = f"{type(result).__name__}: {result}"
            exception_counts[key] = exception_counts.get(key, 0) + 1
            continue

        key = f"{result['kind']}_ok" if result["ok"] else f"{result['kind']}_failed"
        summary[key] += 1
        if result["kind"] == "large":
            large_totals.append(result["total_sec"])
        if not result["ok"] and len(bad_samples) < 5:
            bad_samples.append(result)

    total_clients = args.small_clients + args.large_clients
    mib_per_large = args.large_bytes / (1024 * 1024)
    total_large_mib = mib_per_large * args.large_clients

    print(
        f"small_clients={args.small_clients} large_clients={args.large_clients} "
        f"large_mib_each={mib_per_large:.2f} total_clients={total_clients}"
    )
    print(
        f"ok={summary['small_ok'] + summary['large_ok']} "
        f"failed={summary['small_failed'] + summary['large_failed'] + summary['exceptions']} "
        f"elapsed_sec={elapsed:.3f} client_rate={total_clients / elapsed:.1f}/s"
    )
    print(
        f"small_ok={summary['small_ok']} small_failed={summary['small_failed']} "
        f"large_ok={summary['large_ok']} large_failed={summary['large_failed']} "
        f"exceptions={summary['exceptions']}"
    )
    if args.large_clients:
        print(
            f"total_large_mib={total_large_mib:.2f} "
            f"large_throughput_mib_per_sec={total_large_mib / elapsed:.2f}"
        )
    if large_totals:
        print(
            f"large_total_sec_min={min(large_totals):.3f} "
            f"large_total_sec_max={max(large_totals):.3f} "
            f"large_total_sec_avg={sum(large_totals) / len(large_totals):.3f}"
        )

    for error, count in sorted(exception_counts.items(), key=lambda item: -item[1])[:10]:
        print(f"exception {count}x {error}")
    for sample in bad_samples:
        print(f"bad_response kind={sample['kind']} response={sample['response']!r}")

    return 0 if summary["small_failed"] == 0 and summary["large_failed"] == 0 and summary["exceptions"] == 0 else 1


def parse_args():
    parser = argparse.ArgumentParser(description="Run mixed small/large expression workload")
    parser.add_argument("--host", default="127.0.0.1", help="server host")
    parser.add_argument("--port", type=int, default=5555, help="server port")
    parser.add_argument("--small-clients", type=int, default=9999, help="number of small clients")
    parser.add_argument("--large-clients", type=int, default=1, help="number of large clients")
    parser.add_argument(
        "--small-input",
        default="test_data/expressions.json",
        help="JSON fixture for small requests",
    )
    parser.add_argument(
        "--large-bytes",
        type=int,
        default=1024 * 1024 * 1024,
        help="large expression size before newline",
    )
    parser.add_argument("--chunk-size", type=int, default=1024 * 1024, help="large request chunk size")
    parser.add_argument("--timeout", type=float, default=900.0, help="per-client timeout")
    parser.add_argument("--preconnect", action="store_true", help="exclude TCP connection setup from elapsed_sec")
    return parser.parse_args()


def main():
    return asyncio.run(run(parse_args()))


if __name__ == "__main__":
    raise SystemExit(main())
