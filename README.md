# CrackStore: Distributed Columnar Storage with Adaptive Indexing

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen. svg)]()
[![License](https://img.shields.io/badge/license-MIT-blue.svg)]()
[![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)]()
[![gRPC](https://img.shields. io/badge/gRPC-1.51-orange.svg)]()

CrackStore is a distributed columnar store that brings **adaptive indexing** (database cracking) to distributed environments. Unlike traditional indexing approaches that require upfront index construction, CrackStore automatically optimizes data organization based on query patterns, achieving self-tuning behavior without administrator intervention.

## Table of Contents

- [Overview](#overview)
- [Key Features](#key-features)
- [Architecture](#architecture)
- [Performance Highlights](#performance-highlights)
- [Getting Started](#getting-started)
  - [Prerequisites](#prerequisites)
  - [Building from Source](#building-from-source)
  - [Running with Docker](#running-with-docker)
- [Usage](#usage)
  - [Starting the Cluster](#starting-the-cluster)
  - [Loading Data](#loading-data)
  - [Executing Queries](#executing-queries)
  - [Running Benchmarks](#running-benchmarks)
- [Configuration](#configuration)
- [API Reference](#api-reference)
- [Benchmarks](#benchmarks)
- [Project Structure](#project-structure)
- [Research Background](#research-background)
- [Contributing](#contributing)
- [License](#license)
- [Citation](#citation)
- [Acknowledgments](#acknowledgments)

## Overview

Modern analytical workloads demand ad-hoc exploratory queries over massive datasets with minimal latency. Traditional database indexing requires administrators to anticipate query patterns and pre-build indexes—a process poorly suited for exploratory analytics where access patterns are unknown a priori.

**Database cracking** addresses this through adaptive indexing, where the physical data organization evolves incrementally during query processing. However, existing cracking implementations operate exclusively on single nodes, limiting their applicability in distributed environments.

CrackStore bridges this gap by combining:

- **Stochastic Database Cracking** algorithms from [scrack](https://github.com/felix-halim/scrack)
- **Distributed Systems** architecture with gRPC-based communication
- **Intelligent Query Routing** through partition pruning and parallel execution

## Key Features

| Feature | Description |
|---------|-------------|
| **Zero-Configuration Indexing** | No upfront index specification required; indexes build automatically based on query patterns |
| **Adaptive Query Performance** | First query on a range is slow; subsequent queries on similar ranges are fast |
| **Horizontal Scalability** | Add storage nodes to increase throughput and capacity |
| **Partition Pruning** | Intelligent query routing minimizes network overhead for selective queries |
| **Parallel Execution** | Asynchronous fan-out to storage nodes reduces query latency |
| **Fault Tolerance** | Graceful degradation when nodes fail; automatic recovery on restart |
| **Observable Statistics** | Per-query metrics for tuples touched, cracks created, and execution time |

## Architecture

CrackStore follows a coordinator-worker architecture:

```
┌─────────────────────────────────────────────────────────────────┐
│                         CLIENT                                  │
│  load_column() / range_query() / benchmark()                    │
└─────────────────────┬───────────────────────────────────────────┘
                      │ gRPC
                      ▼
┌─────────────────────────────────────────────────────────────────┐
│                      COORDINATOR                                │
│  • Node registry with health tracking                           │
│  • Partition metadata cache                                     │
│  • Query pruning and parallel fan-out                           │
│  • Result aggregation                                           │
└───────┬─────────────────┬─────────────────┬─────────────────────┘
        │ gRPC            │ gRPC            │ gRPC
        ▼                 ▼                 ▼
┌───────────────┐ ┌───────────────┐ ┌───────────────┐
│ Storage Node 1│ │ Storage Node 2│ │ Storage Node 3│
│ ┌───────────┐ │ │ ┌───────────┐ │ │ ┌───────────┐ │
│ │ Cracking  │ │ │ │ Cracking  │ │ │ │ Cracking  │ │
│ │  Engine   │ │ │ │  Engine   │ │ │ │  Engine   │ │
│ └───────────┘ │ │ └───────────┘ │ │ └───────────┘ │
│ Partition 0   │ │ Partition 1   │ │ Partition 2   │
└───────────────┘ └───────────────┘ └───────────────┘
```

### Components

- **Coordinator**: Central control plane managing node registration, query routing, and result aggregation
- **Storage Nodes**: Worker processes holding data partitions with embedded CrackingEngine instances
- **Client**: Command-line interface and library for interacting with the cluster
- **CrackingEngine**: Core adaptive indexing implementation adapted from scrack

## Performance Highlights

Experimental results on 100 million integers distributed across 8 nodes:

| Metric | Value |
|--------|-------|
| **Adaptation Speedup** | 23. 98× (first query to steady-state) |
| **Scaling Efficiency** | 83% at 8 nodes |
| **Network Reduction** | Up to 75% through partition pruning |
| **Steady-State Latency** | ~12ms for repeated queries |

### Adaptation Behavior

```
Query Latency Over Repeated Executions:

  Iteration 0:  ████████████████████████████████████████ 287. 4 ms
  Iteration 1:  ██ 12.3 ms
  Iteration 2:  ██ 11.8 ms
  Iteration 3:  ██ 12.1 ms
  ... 
  Iteration 19: ██ 11.7 ms

  Speedup: 23.98×
```

## Getting Started

### Prerequisites

- **Operating System**: Linux (Ubuntu 20.04+ recommended) or macOS
- **Compiler**: g++ 11+ or clang++ 13+ with C++17 support
- **Build Tools**: CMake 3.16+, Make
- **Dependencies**:
  - gRPC 1.51+
  - Protocol Buffers 3.21+
  - zlib

For Docker-based deployment:
- Docker 24.0+
- Docker Compose 2.20+

### Building from Source

1. **Clone the repository**:

```bash
git clone https://github.com/yourusername/crackstore.git
cd crackstore
```

2. **Install dependencies** (Ubuntu):

```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    libgrpc++-dev \
    libprotobuf-dev \
    protobuf-compiler \
    protobuf-compiler-grpc \
    zlib1g-dev
```

3. **Build the project**:

```bash
# Build original scrack components
make -s -j4

# Build distributed components
cd distributed
mkdir -p build && cd build
cmake ..
make -j4
```

4. **Verify the build**:

```bash
# Run unit tests
./test_engine
./test_proto
```

### Running with Docker

The easiest way to get started is using Docker Compose:

1. **Build the Docker image**:

```bash
docker compose build
```

2. **Run the test suite**:

```bash
docker compose run --rm cracking-app bash -c "
    cd /app/distributed/build &&
    ./test_engine &&
    ./test_proto
"
```

3. **Start an interactive session**:

```bash
docker compose run --rm cracking-app bash
```

## Usage

### Starting the Cluster

**Option 1: Manual startup**

```bash
# Terminal 1: Start coordinator
./distributed/build/coordinator --port 50050

# Terminal 2: Start storage node 1
./distributed/build/storage_node --port 50051 --coordinator localhost:50050

# Terminal 3: Start storage node 2
./distributed/build/storage_node --port 50052 --coordinator localhost:50050

# Terminal 4: Start storage node 3
./distributed/build/storage_node --port 50053 --coordinator localhost:50050
```

**Option 2: Using the cluster script**

```bash
./distributed/scripts/start_cluster.sh 3  # Start coordinator + 3 storage nodes
```

**Option 3: Standalone mode (single node, no coordinator)**

```bash
./distributed/build/storage_node --standalone --port 50051
```

### Loading Data

Load a binary data file into the cluster:

```bash
# Load 100M integers distributed across all nodes
./distributed/build/client load prices /app/data/100000000. data

# Output:
# Loading column 'prices' from /app/data/100000000. data
# Read 100000000 integers from file
# Distributing to 3 nodes...
#   node-1: loaded 33333334 rows
#   node-2: loaded 33333333 rows
#   node-3: loaded 33333333 rows
# Load complete
```

### Executing Queries

Execute range queries:

```bash
# Query for values in range [100000000, 200000000)
./distributed/build/client query prices 100000000 200000000

# Output:
# === Query Results ===
# Total count: 4651234
# Nodes queried: 2
# Server time: 45.23 ms
# Client time: 47.56 ms
#
# Per-node results:
#   node-1: count=2341567, touched=12456, cracks=24, time=42.1ms
#   node-2: count=2309667, touched=0, cracks=24, time=2.3ms
```

### Checking Cluster Status

```bash
./distributed/build/client status

# Output:
# === Cluster Status ===
# Total nodes: 3
# Healthy nodes: 3
#
#   node-1 [localhost:50051] HEALTHY (last heartbeat: 234ms ago)
#   node-2 [localhost:50052] HEALTHY (last heartbeat: 156ms ago)
#   node-3 [localhost:50053] HEALTHY (last heartbeat: 189ms ago)
```

### Running Benchmarks

Run repeated queries to observe adaptation:

```bash
./distributed/build/client benchmark prices 100000000 200000000 10

# Output:
# === Running Benchmark ===
# Query: [100000000, 200000000) x 10 iterations
#
# Iteration 0: count=4651234, touched=24912345, cracks=4, time=287.4ms
# Iteration 1: count=4651234, touched=0, cracks=4, time=12.1ms
# Iteration 2: count=4651234, touched=0, cracks=4, time=11.8ms
# ... 
# Iteration 9: count=4651234, touched=0, cracks=4, time=11.9ms
#
# (Tuples touched should decrease after first query)
```

### Running Original scrack Experiments

The original scrack benchmarks are still available:

```bash
# Run cracking experiment
docker compose run --rm experiment 100000000. data crack 1000 Random 1e-2 NOUP 60

# Parameters:
#   100000000. data  - Dataset file
#   crack           - Algorithm (crack, sort, scan, dd1r, etc.)
#   1000            - Number of queries
#   Random          - Query workload pattern
#   1e-2            - Selectivity (1% of value range)
#   NOUP            - Update workload (no updates)
#   60              - Time limit in seconds
```

## Configuration

### Coordinator Options

| Option | Default | Description |
|--------|---------|-------------|
| `--port` | 50050 | Port to listen on |
| `--health-check-interval` | 10 | Seconds between health checks |

### Storage Node Options

| Option | Default | Description |
|--------|---------|-------------|
| `--port` | 50051 | Port to listen on |
| `--coordinator` | localhost:50050 | Coordinator address |
| `--node-id` | auto | Node identifier |
| `--heartbeat` | 5 | Heartbeat interval in seconds |
| `--standalone` | false | Run without coordinator |

### Client Options

| Option | Default | Description |
|--------|---------|-------------|
| `--coordinator` | localhost:50050 | Coordinator address |

## API Reference

### gRPC Services

#### StorageService

```protobuf
service StorageService {
    rpc LoadColumn(LoadColumnRequest) returns (LoadColumnResponse);
    rpc RangeQuery(RangeQueryRequest) returns (RangeQueryResponse);
    rpc GetNodeInfo(NodeInfoRequest) returns (NodeInfoResponse);
    rpc HealthCheck(Empty) returns (StatusResponse);
}
```

#### CoordinatorService

```protobuf
service CoordinatorService {
    rpc RegisterNode(RegisterNodeRequest) returns (RegisterNodeResponse);
    rpc Heartbeat(HeartbeatRequest) returns (HeartbeatResponse);
    rpc LoadData(DistributedLoadRequest) returns (DistributedLoadResponse);
    rpc RangeQuery(DistributedRangeQueryRequest) returns (DistributedRangeQueryResponse);
    rpc GetClusterStatus(ClusterStatusRequest) returns (ClusterStatusResponse);
}
```

### CrackingEngine C++ API

```cpp
#include "cracking_engine.h"

using namespace crackstore;

// Create engine with data
std::vector<int> data = {5, 2, 8, 1, 9, 3, 7, 4, 6, 0};
CrackingEngine engine(data. data(), data.size());

// Execute range query
int count = engine.range_query(3, 7);  // Returns count in [3, 7)

// Get statistics
CrackingStats stats = engine.get_stats();
std::cout << "Tuples touched: " << stats.last_tuples_touched << std::endl;
std::cout << "Total cracks: " << engine.get_crack_count() << std::endl;

// Queue updates (lazy evaluation)
engine.insert(10);
engine.remove(5);
```

## Benchmarks

### Datasets

| Dataset | Size | Description |
|---------|------|-------------|
| `uniform_100M. data` | 100M integers | Uniform random distribution |
| `skewed_100M.data` | 100M integers | Zipfian distribution (α=1.0) |
| `skyserver. data` | 585M integers | Real astronomical data |

### Generating Test Data

```bash
# Generate uniform random data
make data/100000000.data

# Generate custom size
./bin/gen_data 50000000  # Generates 50000000. data
```

### Running Comprehensive Benchmarks

```bash
# Adaptation benchmark
./distributed/scripts/run_benchmark.sh adaptation

# Scalability benchmark
./distributed/scripts/run_benchmark.sh scalability

# Workload pattern comparison
./distributed/scripts/run_benchmark.sh patterns

# Full benchmark suite
./distributed/scripts/run_benchmark.sh all
```

### Benchmark Results

Results are written to `results/` directory in CSV format:

```
results/
├── adaptation_results.csv
├── scalability_results.csv
├── patterns_results.csv
└── summary.json
```

## Project Structure

```
crackstore/
├── src/                          # Original scrack source code
│   ├── crackers. h                # Core partitioning algorithms
│   ├── crack. h                   # Cracking implementation
│   ├── tester.h                  # Test harness
│   ├── workload.h                # Query workload generators
│   └── ... 
├── distributed/                  # Distributed system components
│   ├── CMakeLists.txt            # CMake build configuration
│   ├── core/
│   │   ├── cracking_engine.h     # CrackingEngine wrapper class
│   │   └── test_engine.cpp       # Unit tests
│   ├── proto/
│   │   ├── crackstore.proto      # gRPC service definitions
│   │   └── test_proto.cpp        # Proto tests
│   ├── coordinator/
│   │   └── coordinator.cpp       # Coordinator implementation
│   ├── storage/
│   │   └── storage_node.cpp      # Storage node implementation
│   ├── client/
│   │   └── client.cpp            # Client implementation
│   └── scripts/
│       ├── start_cluster.sh      # Cluster startup script
│       ├── run_benchmark.sh      # Benchmark runner
│       └── test_cluster.sh       # Integration tests
├── data/                         # Test datasets
├── res/                          # Experiment results
├── bin/                          # Compiled binaries
├── Dockerfile                    # Docker build configuration
├── docker-compose.yml            # Docker Compose configuration
├── makefile                      # Original scrack makefile
├── run. sh                        # Original scrack runner
└── README.md                     # This file
```

## Research Background

CrackStore is based on the following research:

### Database Cracking

Database cracking was introduced by Idreos, Kersten, and Manegold at CIDR 2007. The key insight is that traditional index construction represents a "one-size-fits-all" approach poorly suited for exploratory workloads.  Cracking treats each query as an opportunity to incrementally refine physical data organization.

**Key papers:**

1. S. Idreos, M. L. Kersten, and S. Manegold. "Database Cracking." CIDR 2007. 
2. F. Halim, S. Idreos, P. Karras, and R. H. C. Yap. "Stochastic Database Cracking: Towards Robust Adaptive Indexing in Main-Memory Column-Stores." PVLDB 5(6), 2012. 

### Stochastic Cracking

Stochastic cracking addresses vulnerability to adversarial query sequences through randomized pivot selection, guaranteeing expected O(n log n) total work for any sequence of n queries.

The original implementation is available at: https://github.com/felix-halim/scrack

## Contributing

We welcome contributions!  Please follow these steps:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4.  Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

### Development Setup

```bash
# Install development dependencies
sudo apt-get install -y clang-format valgrind

# Format code
find distributed -name "*.cpp" -o -name "*.h" | xargs clang-format -i

# Run with memory checking
valgrind --leak-check=full ./distributed/build/test_engine
```

### Running Tests

```bash
# Unit tests
cd distributed/build
ctest --output-on-failure

# Integration tests
./distributed/scripts/test_cluster. sh
```

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details. 

The original scrack implementation is used under its original license terms. 

## Citation

If you use CrackStore in your research, please cite:

```bibtex
@inproceedings{crackstore2024,
  title={CrackStore: Distributed Columnar Storage with Adaptive Indexing},
  author={Author Names},
  booktitle={Proceedings of Conference Name},
  year={2024}
}
```

Also cite the original database cracking papers:

```bibtex
@inproceedings{idreos2007database,
  title={Database Cracking},
  author={Idreos, Stratos and Kersten, Martin L and Manegold, Stefan},
  booktitle={CIDR},
  pages={68--78},
  year={2007}
}

@article{halim2012stochastic,
  title={Stochastic Database Cracking: Towards Robust Adaptive Indexing in Main-Memory Column-Stores},
  author={Halim, Felix and Idreos, Stratos and Karras, Panagiotis and Yap, Roland HC},
  journal={Proceedings of the VLDB Endowment},
  volume={5},
  number={6},
  pages={502--513},
  year={2012}
}
```

## Acknowledgments

- **Felix Halim** and colleagues: 
[Stochastic Database Cracking: Towards Robust Adaptive Indexing in Main-Memory Column-Stores](http://vldb.org/pvldb/vol5/p502_felixhalim_vldb2012.pdf)
- **Stratos Idreos** and the CWI Database Architectures group for pioneering database cracking research
- The **gRPC** and **Protocol Buffers** teams for excellent distributed systems tooling
