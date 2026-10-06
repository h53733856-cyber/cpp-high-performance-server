# C++ High Performance TCP Server

一个基于 **C++17 + Linux + epoll** 实现的高性能 TCP Server 学习与实践项目。

项目从最基础的 TCP Echo Server 开始，逐步实现非阻塞 I/O、epoll、多连接管理、线程池、异步任务处理以及性能测试。

## Features

- TCP Socket
- Non-blocking I/O
- Linux `epoll`
- Partial `recv` / `send`
- Connection management
- ThreadPool
- TaskQueue
- CompletionQueue
- `eventfd` Worker → I/O 线程通知
- Connection ID 防止 fd reuse 导致的错误响应
- CPU-bound workload
- Benchmark & latency analysis

## Architecture

```text
                    ┌──────────────┐
                    │  Client(s)   │
                    └──────┬───────┘
                           │
                           ▼
                    ┌──────────────┐
                    │  epoll I/O   │
                    │    Thread    │
                    └──────┬───────┘
                           │
                     submit task
                           │
                           ▼
                  ┌─────────────────┐
                  │    ThreadPool   │
                  │ ┌───┬───┬───┐  │
                  │ │ W │ W │ W │… │
                  │ └───┴───┴───┘  │
                  └────────┬────────┘
                           │
                      Completion
                           │
                           ▼
                  ┌─────────────────┐
                  │ CompletionQueue │
                  └────────┬────────┘
                           │
                         eventfd
                           │
                           ▼
                    ┌──────────────┐
                    │  epoll I/O   │
                    │    Thread    │
                    └──────┬───────┘
                           │
                        send()
                           │
                           ▼
                         Client
```

Worker 线程只负责处理请求并产生结果，不直接操作 `Connection`。

`CompletionQueue + eventfd` 将 Worker 与 I/O 线程解耦，由 I/O 线程统一管理连接和网络发送。

每个连接拥有唯一 `connection_id`。即使旧连接关闭后 fd 被新连接复用，也可以通过 `connection_id` 判断异步任务结果是否属于当前连接。

## Protocol

当前支持：

```text
ECHO hello
REVERSE hello
CALC 1 + 2
SLEEP 100
PRIME 100000
```

请求和响应以 `\n` 分隔。

其中 `PRIME N` 用于产生 CPU-bound workload，主要用于测试 ThreadPool 的并行处理能力。

## Benchmark

Benchmark 工具：

```text
benchmark/benchmark.py
```

示例：

```bash
python3 benchmark/benchmark.py \
    --requests 1000 \
    --concurrency 10 \
    --request "PRIME 100000"
```

### Worker Scaling

固定：

- Requests: 1000
- Concurrency: 10
- Request: `PRIME 100000`

| Workers | Throughput | Avg | P50 | P95 | P99 |
|---:|---:|---:|---:|---:|---:|
| 1 | 202.91 req/s | 48.97 ms | 49.00 ms | 55.70 ms | 59.92 ms |
| 2 | 382.94 req/s | 25.92 ms | 25.58 ms | 29.02 ms | 31.39 ms |
| 4 | 693.65 req/s | 14.29 ms | 14.14 ms | 16.61 ms | 24.62 ms |
| 8 | 1004.47 req/s | 9.70 ms | 9.29 ms | 13.13 ms | 21.80 ms |

Worker 数量增加可以明显提高 CPU-bound workload 的吞吐，但收益逐渐递减。

### Concurrency Scaling

固定：

- Workers: 8
- Requests: 1000
- Request: `PRIME 100000`

| Concurrency | Throughput | Avg | P50 | P95 | P99 |
|---:|---:|---:|---:|---:|---:|
| 2 | 333.02 req/s | 5.98 ms | 5.69 ms | 7.41 ms | 13.42 ms |
| 4 | 692.82 req/s | 5.73 ms | 5.61 ms | 6.33 ms | 9.49 ms |
| 8 | 1003.51 req/s | 7.88 ms | 7.47 ms | 10.24 ms | 16.06 ms |
| 16 | 1016.20 req/s | 15.43 ms | 14.80 ms | 18.52 ms | 34.22 ms |
| 32 | 966.43 req/s | 31.00 ms | 31.42 ms | 36.11 ms | 38.25 ms |

在当前测试环境下，并发达到约 8 后吞吐已经接近饱和。

继续增加并发并没有带来明显吞吐提升，反而导致延迟明显增加，说明系统开始进入 CPU 饱和和任务排队阶段。

## Version Progress

- **V0.1** — Blocking TCP Echo Server
- **V0.2** — Multi-client support
- **V0.3** — Partial recv/send & output buffer
- **V0.4** — Error handling
- **V0.5** — `select`
- **V0.6** — `epoll` + non-blocking I/O
- **V0.7** — Object-oriented connection/server architecture
- **V0.8** — ThreadPool + asynchronous request processing + CompletionQueue + eventfd + connection lifecycle safety
- **V0.9** — CPU-bound workload + benchmark + performance analysis

## Build

```bash
mkdir -p build
cd build
cmake ..
make
```

Run:

```bash
./tcp_server
```

Specify worker count:

```bash
./tcp_server 8
```

The default worker count is `4`.

## Tests

Current test targets include:

```text
task_queue_test
thread_pool_test
request_parser_test
request_handler_test
```

Build and run the tests after compilation.

## Future Work

- RAII and resource management improvements
- Better error handling
- Logging system
- Configuration
- Graceful shutdown
- Benchmark reproducibility improvements
- Further performance optimization
- Final architecture and documentation cleanup
