# --concurrency 10意味着benchmark同时建立10个TCP connection
# --requests 1000表示总共发送 1000 个请求
# latency表示每一个请求的端到端请求延迟
# 吞吐率throughput = successful_requests / elapsed_time

# ===== Benchmark Result =====
#  Requests:      总请求数
#  Concurrency:   并发数
#  Successful:    成功请求
#  Failed:        失败请求
#  Elapsed:       总耗时
#  Throughput:    吞吐量 req/s——每秒服务器大概可以处理多少请求
#  Average:       平均单次请求延迟
#  P50:           中位数延迟
#  P95:           95分位延迟
#  P99:           99分位延迟

import argparse
import socket
import threading
import time


def worker(host, port, request, requests, results):
    try:
        with socket.create_connection((host, port)) as sock:
            for _ in range(requests):
                start = time.perf_counter()

                sock.sendall((request + "\n").encode())

                data = b""
                while not data.endswith(b"\n"):
                    chunk = sock.recv(4096)

                    if not chunk:
                        raise RuntimeError("connection closed")

                    data += chunk

                end = time.perf_counter()

                latency_ms = (end - start) * 1000

                results.append(latency_ms)

    except Exception as e:
        print(f"worker error: {e}")


def percentile(values, p):
    if not values:
        return 0.0

    values = sorted(values)

    index = int(len(values) * p / 100)

    if index >= len(values):
        index = len(values) - 1

    return values[index]


def main():
    parser = argparse.ArgumentParser()

    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--requests", type=int, default=1000)
    parser.add_argument("--concurrency", type=int, default=10)
    parser.add_argument("--request", default="PRIME 100000")

    args = parser.parse_args()

    if args.requests <= 0:
        raise ValueError("requests must be positive")

    if args.concurrency <= 0:
        raise ValueError("concurrency must be positive")

    results = []

    base = args.requests // args.concurrency
    remainder = args.requests % args.concurrency

    threads = []

    start_time = time.perf_counter()

    for i in range(args.concurrency):
        worker_requests = base

        if i < remainder:
            worker_requests += 1

        if worker_requests == 0:
            continue

        thread = threading.Thread(
            target=worker,
            args=(
                args.host,
                args.port,
                args.request,
                worker_requests,
                results,
            ),
        )

        thread.start()
        threads.append(thread)

    for thread in threads:
        thread.join()

    end_time = time.perf_counter()

    elapsed = end_time - start_time

    successful = len(results)
    failed = args.requests - successful

    throughput = successful / elapsed if elapsed > 0 else 0

    average = (
        sum(results) / len(results)
        if results
        else 0
    )

    p50 = percentile(results, 50)
    p95 = percentile(results, 95)
    p99 = percentile(results, 99)

    print()
    print("===== Benchmark Result =====")
    print(f"Requests:      {args.requests}")
    print(f"Concurrency:   {args.concurrency}")
    print(f"Successful:    {successful}")
    print(f"Failed:        {failed}")
    print(f"Elapsed:       {elapsed:.3f} s")
    print(f"Throughput:    {throughput:.2f} req/s")
    print(f"Average:       {average:.2f} ms")
    print(f"P50:           {p50:.2f} ms")
    print(f"P95:           {p95:.2f} ms")
    print(f"P99:           {p99:.2f} ms")


if __name__ == "__main__":
    main()