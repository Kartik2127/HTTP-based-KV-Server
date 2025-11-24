## High-Performance Multi-Tier Key-Value Store
A high-throughput, low-latency Key-Value store engineered to demonstrate System Design and High-Performance Computing principles. This project implements a tiered architecture with Sharded Caching, Asynchronous WAL, and Database Connection Pooling to maximize CPU saturation and minimize I/O blocking.

## Key Architectural Optimizations
**Sharded In-Memory Cache**: Implements 16-way software sharding (hash-based) to slash lock contention by 90%+ compared to a global mutex.

**Bounded Async WAL**: Decouples request processing from disk I/O using a background logger thread with batch processing (Limit: 100) and fdatasync.

**MySQL Connection Pooling**: Pre-allocated pool of 20 connections eliminates TCP handshake overhead on cache misses.

**CPU Pinning (HPC)**: Benchmarking scripts utilize taskset to isolate Server and Load Generator threads on separate cores, preventing cache thrashing.

## Tech Stack
**Core**: C++17 (multithreading, smart pointers, mutex/cond_vars)

**Networking**: cpp-httplib (Blocking I/O model)

**Storage**: In-Memory Heap, Disk (WAL), MySQL 8.0 (Persistent)

**Analysis**: Python, wandb (Weights & Biases), psutil

## Setup & Compilation
**Prerequisites**
g++, libmysqlcppconn-dev, python3
Python lib: wandb, psutil

1. **Database Setup**
```bash
mysql -u root -p < reset_db.sql
```
2. **Build Core System**
Note: -O3 is critical for loop vectorization and optimization.

```Bash

# Compile Server
g++ server.cpp -o server -lpthread -lmysqlcppconn -O3

# Compile Load Generator
g++ load_generator.cpp -o load_generator -lpthread -O3

```
## Benchmarking & Analysis
This project includes automated suites to stress test CPU vs I/O bottlenecks.

1. **CPU Scalability Sweep**
Ramps up threads while pinning processes to specific cores to measure pure throughput scaling.

```Bash
# Terminal 1: Start Server
./server
# Terminal 2: Run Automation
python auto_wandb.py put_all (Create a Virtual Environment and do wandb login )

```
2. **Disk/Persistence Bottleneck Sweep**
Stresses the Write-Ahead Log and Database persistence layers.

```Bash

# Terminal 1: Start Server
./server

# Terminal 2: Run Automation
python auto_wandb_disk.py put_all(Create a Virtual Environment and do wandb login)

```
## Performance Metrics
The Python automation integrates with Weights & Biases to visualize:

**Throughput (Req/s)**: Monitoring saturation points.

**Tail Latency**: Observing queue depth impact on response time.

**CPU Utilization**: Correlating core usage with thread contention.