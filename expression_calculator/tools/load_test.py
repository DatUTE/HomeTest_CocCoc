#!/usr/bin/env python3
"""Async TCP load test client for the expression calculator server.

The script intentionally uses only Python's standard library so it can run on a
plain Ubuntu machine. It can validate responses from a JSON fixture or simply
send newline-delimited expressions from a text fixture.
"""

import argparse
import asyncio
import json
import time
from pathlib import Path


def load_cases(path: Path):
    if path.suffix == ".json":
        with path.open("r", encoding="utf-8") as f:
            cases = json.load(f)
        return [
            {
                "expression": item["expression"],
                "expected": item.get("expected"),
                "expected_prefix": item.get("expected_prefix"),
            }
            for item in cases
        ]

    cases = []
    with path.open("r", encoding="utf-8") as f:
        for line in f:
            expression = line.rstrip("\n")
            if expression:
                cases.append({"expression": expression, "expected": None, "expected_prefix": None})
    return cases


def response_matches(response: str, expected: str | None, expected_prefix: str | None) -> bool:
    if expected is not None:
        return response == expected
    if expected_prefix is not None:
        return response.startswith(expected_prefix)
    return bool(response)


async def run_client(client_id: int, args, cases):
    case = cases[client_id % len(cases)]
    reader, writer = await asyncio.open_connection(args.host, args.port)
    return await run_connected_client(reader, writer, case)


async def run_connected_client(reader: asyncio.StreamReader, writer: asyncio.StreamWriter, case):
    try:
        writer.write((case["expression"] + "\n").encode("utf-8"))
        await writer.drain()
        raw_response = await asyncio.wait_for(reader.readline(), timeout=case["timeout"])
        response = raw_response.decode("utf-8", errors="replace").rstrip("\n")
        ok = response_matches(response, case["expected"], case["expected_prefix"])
        return {
            "ok": ok,
            "expression": case["expression"],
            "response": response,
        }
    finally:
        writer.close()
        await writer.wait_closed()


async def run_load_test(args):
    cases = load_cases(Path(args.input))
    if not cases:
        raise ValueError("input file does not contain any expressions")

    for case in cases:
        case["timeout"] = args.timeout

    preconnected = []
    if args.preconnect:
        connect_start = time.perf_counter()
        preconnected = await asyncio.gather(
            *[asyncio.open_connection(args.host, args.port) for _ in range(args.clients)]
        )
        connect_elapsed = time.perf_counter() - connect_start
        print(f"preconnected={args.clients} connect_elapsed_sec={connect_elapsed:.3f}")

    start = time.perf_counter()
    if args.preconnect:
        tasks = [
            asyncio.create_task(run_connected_client(reader, writer, cases[i % len(cases)]))
            for i, (reader, writer) in enumerate(preconnected)
        ]
    else:
        tasks = [asyncio.create_task(run_client(i, args, cases)) for i in range(args.clients)]
    results = await asyncio.gather(*tasks, return_exceptions=True)
    elapsed = time.perf_counter() - start

    ok_count = sum(1 for r in results if isinstance(r, dict) and r["ok"])
    failed_count = args.clients - ok_count
    print(
        f"clients={args.clients} ok={ok_count} failed={failed_count} "
        f"elapsed_sec={elapsed:.3f} rate={args.clients / elapsed:.1f}/s"
    )

    samples = []
    exception_counts = {}
    for result in results:
        if isinstance(result, Exception):
            key = f"{type(result).__name__}: {result}"
            exception_counts[key] = exception_counts.get(key, 0) + 1
        elif not result["ok"] and len(samples) < 5:
            samples.append(result)

    for error, count in sorted(exception_counts.items(), key=lambda item: -item[1])[:10]:
        print(f"exception {count}x {error}")

    for sample in samples:
        print(
            "bad_response "
            f"expression={sample['expression']!r} response={sample['response']!r}"
        )

    return 0 if failed_count == 0 else 1


def parse_args():
    parser = argparse.ArgumentParser(description="Load test expression calculator TCP server")
    parser.add_argument("--host", default="127.0.0.1", help="server host")
    parser.add_argument("--port", type=int, default=5555, help="server port")
    parser.add_argument("--clients", type=int, default=10000, help="number of concurrent clients")
    parser.add_argument(
        "--input",
        default="test_data/expressions.json",
        help="JSON or text fixture containing expressions",
    )
    parser.add_argument("--timeout", type=float, default=10.0, help="per-client response timeout")
    parser.add_argument(
        "--preconnect",
        action="store_true",
        help="open all sockets before starting the request timer",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    return asyncio.run(run_load_test(args))


if __name__ == "__main__":
    raise SystemExit(main())
