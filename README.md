# HTTP Key-Value Server (CS744 Project)

This repository contains the C++ source code for a multi-tier, HTTP-based key-value server with an in-memory cache and a persistent MySQL backend.It also includes a multi-threaded load generator for performance testing, as required by the CS744 project.

## System Architecture

This project implements a classic 3-tier architecture:
## Tier 1 (Web): A multi-threaded C++ HTTP server using (cpp-httplib). It exposes three RESTful API endpoints.
## Tier 2 (Cache): A thread-safe, in-memory cache (KVCache) built using (std::unordered_map) and (std::mutex).
## Tier 3 (Database):A persistent MySQL database (DBManager) that acts as the backing store for all data, ensuring durability.

## Features

**Multi-Tier Design:** Clear separation between the web, caching, and database layers.
**Thread-Safe:** Uses (std::mutex) and (std::shared_ptr) to safely handle concurrent requests from the thread pool.
**Persistent Storage:** All (create) and (delete) operations are synchronized with a standalone MySQL database.
**Cache Logic:** Implements the required "read-through" cache policy:
            **Read:** Check cache first.On a miss, read from the database and populate the cache.
            **Create:** Write to both the database and the cache.
            **Delete:** Remove from both the database and the cache.

## Components

1.  **server.cpp:** The main 3-tier KV server application.
2.  **load_generator.cpp:** A separate, multi-threaded, closed-loop load generator.
3.  **httplib.h:** The single-header library used for the HTTP server and client.

## How to Build and Run

### Prerequisites

* A C++17 compiler
* MySQL Server (running)
* MySQL C++ Connector (legacy) development libraries (libmysqlcppconn-dev)

### 1. Database Setup

Before running the server, you must create the database and a dedicated user.

```sql
-- 1. Log in as root
sudo mysql -u root -p

-- 2. Run the following SQL commands:
CREATE DATABASE kv_store;

-- 3. Create a user (replace with your own password)
CREATE USER 'kv_server_user'@'localhost' IDENTIFIED BY 'MyProjectPassword123!';
GRANT ALL PRIVILEGES ON kv_store.* TO 'kv_server_user'@'localhost';
FLUSH PRIVILEGES;
USE kv_store;

-- 4. Create the table
CREATE TABLE kv_pairs (
    id VARCHAR(255) PRIMARY KEY,
    value TEXT
);

exit;
```

### 2. Build the Server

**Important:** You must edit `server.cpp` and update the `DBManager` constructor with your MySQL username and password.

```bash
# Compile the server
g++ server.cpp -o kv_server -std=c++17 -lpthread -I/usr/include/cppconn -L/usr/lib/x86_64-linux-gnu -lmysqlcppconn
```

### 3. Build the Load Generator

```bash
# Compile the load generator
g++ load_generator.cpp -o load_generator -std=c++17 -lpthread
```

## How to Use

### 1. Run the Server

Open your first terminal and start the server:
```bash
./kv_server
```
**Expected Output:**
```
[DB] Successfully connected to MySQL.
Server starting on port 8080...
```

### 2. Run the Load Generator (in a new terminal)

Open a second terminal to run the load generator.

**Usage:**
```
./load_generator <host> <port> <threads> <duration_sec> <workload>
```
**workload:** (put_all) (I/O-Bound) or (get_popular) (CPU-Bound)


**Example Output (Load Generator):**
--- Starting Load Generator ---
Target: localhost:8080
Threads (Users): 8
Duration: 10 seconds
Workload: get_popular
Warming up server (populating 100 keys)... Done.

Test running... Done.

--- Load Test Finished ---
Total test duration: 10.0012 seconds
Total requests completed: 150234
Average Throughput: 15021.5 reqs/sec
Average Response Time: 0.0532 ms
