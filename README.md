# NetPulse: TCP vs UDP Network Performance Analyzer

A comprehensive Computer Networks project that experimentally compares TCP and UDP behavior using real socket communication on Windows (Winsock2).

---

## Table of Contents

1. [Abstract](#1-abstract)
2. [Problem Statement](#2-problem-statement)
3. [Objectives](#3-objectives)
4. [Technologies](#4-technologies)
5. [Architecture](#5-architecture)
6. [Folder Structure & Directory Roles](#6-folder-structure--directory-roles)
7. [TCP Implementation](#7-tcp-implementation)
8. [UDP Implementation](#8-udp-implementation)
9. [Packet Structure](#9-packet-structure)
10. [Chunking & Byte-Stream Semantics](#10-chunking--byte-stream-semantics)
11. [Sequence Numbers](#11-sequence-numbers)
12. [Metrics & Measurement Methodology](#12-metrics--measurement-methodology)
13. [File Transfer](#13-file-transfer)
14. [UDP Simulated Packet Loss](#14-udp-simulated-packet-loss)
15. [Wireshark Analysis](#15-wireshark-analysis)
16. [How to Compile](#16-how-to-compile)
17. [How to Run](#17-how-to-run)
18. [Example Experiments & Recommended Order](#18-example-experiments--recommended-order)
19. [Expected Observations](#19-expected-observations)
20. [Limitations](#20-limitations)
21. [Submission Guidelines](#21-submission-guidelines)
22. [Conclusion](#22-conclusion)

---

## 1. Abstract

NetPulse is an academic tool that allows a user to perform controlled TCP and UDP data-transfer experiments on a local machine, measure key network performance metrics (throughput, application-level latency estimates, jitter, packet loss), and compare the two transport-layer protocols side-by-side. All communication uses the Windows Winsock2 API with standard C++ — no external dependencies, frameworks, or databases.

The project demonstrates core Computer Networks concepts including:

- Client-server socket communication using Winsock2
- TCP byte-stream semantics vs. UDP datagram boundaries
- Logical application chunks vs. wire-level TCP segments
- Application-level packet framing (sequence numbers, headers for UDP)
- Configurable chunked data transfer and file transfer
- Performance measurement (throughput, application-level latency, jitter, loss)
- Wire-level network traffic inspection with Wireshark

---

## 2. Problem Statement

TCP and UDP are the two primary transport-layer protocols in the Internet protocol suite. While textbooks describe their theoretical differences (reliability, ordering, congestion control for TCP; low-overhead, connectionless delivery for UDP), students rarely get to observe these differences experimentally.

This project bridges that gap by providing a tool to run real socket-based experiments, measure actual performance metrics, and visualize traffic with Wireshark — all on a single Windows machine using localhost (`127.0.0.1`).

---

## 3. Objectives

| # | Objective | How Achieved |
|---|-----------|-------------|
| 1 | TCP data-transfer experiments | `tcp_server.exe` + `tcp_client.exe` |
| 2 | UDP data-transfer experiments | `udp_server.exe` + `udp_client.exe` |
| 3 | Configurable data sizes | User enters MB; synthetic data generated |
| 4 | Chunked transmission | Data divided into configurable-size logical chunks |
| 5 | UDP sequence numbers | Application-level 24-byte packet header |
| 6 | Packet loss detection | Server tracks received sequence numbers |
| 7 | Throughput measurement | `Total Bytes / Time` (MB/s and Mbps) |
| 8 | Application-level latency estimate | Observed inter-chunk / datagram arrival timing |
| 9 | Jitter measurement | Mean of consecutive latency differences (RFC 3550) |
| 10 | Packet loss measurement | `(Sent - Received) / Sent × 100` |
| 11 | File transfer + integrity | Byte-by-byte comparison of original vs received |
| 12 | TCP vs UDP comparison | Side-by-side table with all metrics |
| 13 | Wire-level packet inspection | Wireshark capture on ports 5000 (TCP) / 5001 (UDP) |
| 14 | Automatic result logging | CSV and TXT files generated automatically in `results/` |

---

## 4. Technologies

| Component | Technology |
|-----------|-----------|
| Language | C++ (C++17 compatible) |
| OS | Windows 10 / 11 |
| Compiler | MinGW g++ (tested with GCC 6.3+) |
| Networking API | Windows Winsock2 (`ws2_32.lib`) |
| Traffic Analysis | Wireshark (external tool) |
| Dependencies | **None** beyond C++ stdlib + Winsock2 |

---

## 5. Architecture

### 5.1 Execution Model

The project uses a **process-based architecture**:

```
User
  ↓
main.exe  (orchestrator — menu, process management, results display)
  ├── Launches → tcp_server.exe  (background process)
  ├── Runs    → tcp_client.exe   (captures stdout for RESULT line)
  ├── Reads   → results/tcp_server_last.txt  (server metrics)
  └── Displays formatted results + saves CSV
```

Each networking component compiles to its own executable. `main.exe` orchestrates the experiment lifecycle:

1. Launches the server as a background process (new console window).
2. Runs the client via `_popen()` and captures its stdout.
3. Waits for both to finish.
4. Reads the server's result file from `results/`.
5. Combines client timing + server metrics into a unified `ExperimentResult`.
6. Displays and saves.

### 5.2 Data Flow

```
┌──────────────┐     Control Header      ┌──────────────┐
│              │ ──────────────────────→  │              │
│  tcp_client  │     Data Chunks          │  tcp_server  │
│  udp_client  │ ──────────────────────→  │  udp_server  │
│              │                          │              │
│  Outputs:    │                          │  Writes:     │
│  RESULT line │                          │  result file │
└──────┬───────┘                          └──────┬───────┘
       │ stdout                                  │ file
       ↓                                         ↓
┌──────────────────────────────────────────────────────────┐
│                     main.exe                              │
│  • Parses RESULT line (packets, time, bytes)             │
│  • Reads server result file (latency, jitter, loss)      │
│  • Calls metrics module (throughput, comparisons)        │
│  • Displays formatted table                              │
│  • Saves CSV to results/                                 │
└──────────────────────────────────────────────────────────┘
```

### 5.3 Communication Protocol

**TCP Control Header** (sent before data):
```
MODE|DATA_SIZE|CHUNK_SIZE|FILE_NAME\r\n\r\n
```
Example: `SYNTHETIC|1048576|4096|NONE\r\n\r\n`

After the header, the client sends exactly `DATA_SIZE` raw bytes.

**UDP Application-Level Packet** (see [Section 9](#9-packet-structure) for wire format):
- `START` packet — carries metadata (total packets, data size, mode, filename).
- `DATA` packets — each carries a sequence number, timestamp, and payload.
- `END` packet — signals transfer completion.
- `ACK` packet — server sends back with count of received packets.

---

## 6. Folder Structure & Directory Roles

```
TCP-vs-UDP/
│
├── src/
│   ├── tcp/
│   │   ├── tcp_server.cpp      TCP stream server (bind, listen, accept, recv loop)
│   │   └── tcp_client.cpp      TCP client (connect, control header, sendAll chunks)
│   ├── udp/
│   │   ├── udp_server.cpp      UDP datagram server (bind, recvfrom, loss/order tracking)
│   │   └── udp_client.cpp      UDP client (sendto, 24-byte headers, simulated loss)
│   ├── analyzer/
│   │   ├── metrics.h           Metric calculation and display declarations
│   │   └── metrics.cpp         Throughput, loss, latency estimate, jitter, CSV logging
│   └── main.cpp                Interactive orchestrator CLI & process manager
│
├── include/
│   └── common.h                Shared constants, ports (5000/5001), packet header serialization
│
├── results/                    Auto-generated benchmark results (initially empty)
├── wireshark/                  Manual packet capture storage (initially empty)
├── test_files/
│   ├── sample.txt              ~95 KB structured text file for payload testing
│   └── sample.bin              100 KB binary file for byte-level integrity verification
│
├── README.md                   Technical documentation and experimental analysis
└── .gitignore                  Git ignore rules for build artifacts and runtime logs
```

### Directory Roles & Persistence

1. **`results/` Directory**:
   - **Initial State**: Intentionally empty prior to running experiments. Do not manually create dummy files.
   - **Runtime Generation**: Created automatically if not present; populated by `main.exe` and servers during runs:
     - `results/tcp_results.csv`: Cumulative historical log of all TCP experiment runs.
     - `results/udp_results.csv`: Cumulative historical log of all UDP experiment runs.
     - `results/comparison.csv`: Formatted side-by-side summary generated whenever Option 5 is selected.
     - `results/tcp_server_last.txt` & `results/udp_server_last.txt`: Transient key-value metrics written by the servers and read by `main.exe`.
     - `results/received_<filename>`: Reconstructed files written during file transfer benchmarks (Options 3 & 4) for byte-by-byte integrity verification.

2. **`wireshark/` Directory**:
   - **Initial State**: Intentionally empty. No synthetic or fake `.pcap` files are bundled.
   - **Purpose**: A dedicated destination folder for students/evaluators to save actual Wireshark loopback traces (`.pcapng` or `.pcap`) captured during live test runs.

3. **Compiled Executables (`.exe`)**:
   - `tcp_server.exe`, `tcp_client.exe`, `udp_server.exe`, `udp_client.exe`, and `main.exe` reside in the project root directory.
   - `main.exe` launches the server and client binaries as child processes using Windows process creation (`CreateProcessA` and `_popen`).

---

## 7. TCP Implementation

### Key Winsock Calls Used

| Server | Client |
|--------|--------|
| `WSAStartup()` | `WSAStartup()` |
| `socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)` | `socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)` |
| `bind()` | `connect()` |
| `listen()` | `sendAll()` → loops `send()` |
| `accept()` | `shutdown(SD_SEND)` |
| `recv()` in loop | `closesocket()` |
| `closesocket()` | `WSACleanup()` |
| `WSACleanup()` | |

### TCP as a Byte Stream

TCP does **not** preserve message boundaries. One `send(4096)` call may be delivered as multiple `recv()` calls returning fewer bytes, or multiple `send()` calls may be coalesced into one `recv()`.

**Our implementation handles this correctly:**

- **Client** uses `sendAll()` — loops `send()` until all bytes of a chunk are transmitted:
  ```cpp
  int sendAll(SOCKET sock, const char* data, int length) {
      int totalSent = 0;
      while (totalSent < length) {
          int sent = send(sock, data + totalSent, length - totalSent, 0);
          if (sent == SOCKET_ERROR) return SOCKET_ERROR;
          totalSent += sent;
      }
      return totalSent;
  }
  ```

- **Server** receives in a loop until exactly `dataSize` bytes are accumulated, regardless of how TCP fragments the stream.

### TCP Properties Observed

| Property | How Observed |
|----------|-------------|
| Connection-oriented | `connect()` + `accept()` handshake required |
| Three-way handshake | SYN → SYN-ACK → ACK visible in Wireshark |
| Reliable delivery | 0% packet loss in all experiments |
| Ordered delivery | Out-of-order count always 0 |
| Flow/congestion control | TCP stack handles internally; throughput adapts |
| Connection termination | FIN → ACK visible in Wireshark |

---

## 8. UDP Implementation

### Key Winsock Calls Used

| Server | Client |
|--------|--------|
| `WSAStartup()` | `WSAStartup()` |
| `socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)` | `socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)` |
| `bind()` | `sendto()` |
| `recvfrom()` | `recvfrom()` (for ACK) |
| `sendto()` (for ACK) | `closesocket()` |
| `closesocket()` | `WSACleanup()` |
| `WSACleanup()` | |

### UDP Datagram Semantics

UDP preserves datagram boundaries (one `sendto()` = one `recvfrom()`) but does **not** guarantee:
- Delivery
- Ordering
- Duplicate prevention

Our implementation adds an **application-level framing protocol** on top of raw UDP to detect these issues.

### UDP Properties Observed

| Property | How Observed |
|----------|-------------|
| Connectionless | No `connect()`/`accept()`; `sendto()` directly |
| Datagram boundaries | Each `recvfrom()` gets one complete packet |
| No delivery guarantee | Missing sequence numbers detected |
| No ordering guarantee | Out-of-order sequence numbers tracked |
| Lower overhead | Higher throughput than TCP in some tests |
| Application-level reliability | Sequence numbers + loss detection by server |

---

## 9. Packet Structure

### UDP Application-Level Packet Header (24 bytes, explicitly serialized)

```
Offset  Size  Field
──────  ────  ──────────────────────────
  0      4    sequenceNumber   (uint32_t)
  4      4    payloadSize      (uint32_t)
  8      8    timestampUs      (int64_t, microseconds since epoch)
 16      1    packetType       (uint8_t: 0=START, 1=DATA, 2=END, 3=ACK)
 17      4    totalPackets     (uint32_t)
 21      3    reserved         (padding)
──────  ────  ──────────────────────────
 24 bytes total header
```

**Followed by** `payloadSize` bytes of payload data.

### Why Explicit Serialization?

Sending raw C++ structs over the network can break due to:
- Compiler-specific struct padding/alignment
- Different endianness (less relevant on localhost but good practice)

We use `memcpy`-based serialization (`serializeUdpHeader` / `deserializeUdpHeader`) to guarantee exact byte layout.

### Packet Types

| Type | Purpose |
|------|---------|
| `START` (0) | Sent first; payload is metadata text: `TOTAL_PACKETS\|DATA_SIZE\|CHUNK_SIZE\|MODE\|FILENAME` |
| `DATA` (1) | Regular data packet carrying payload bytes |
| `END` (2) | Sent last; signals transfer completion |
| `ACK` (3) | Server → Client; carries count of received packets |

---

## 10. Chunking & Byte-Stream Semantics

Data is partitioned into fixed-size units before transmission:

```
Total Data: 10 MB = 10,485,760 bytes
Chunk Size: 4096 bytes

Number of chunks = ceil(10,485,760 / 4096) = 2560 chunks
```

### TCP Logical Chunks vs. Wire-Level Segments

- **TCP is a byte-stream protocol**: The application divides data into **logical application chunks** (buffers passed to `sendAll()`). TCP does not preserve message boundaries; the underlying operating system network stack may fragment, coalesce, or repackage these chunks into wire-level **TCP segments** based on the Maximum Segment Size (MSS) and window availability.
- **Wireshark Inspection**: Real network segments can be observed directly in Wireshark, while the application tracks logical chunk transmissions.
- **UDP Datagrams**: For UDP, each application chunk corresponds 1-to-1 to an independent datagram with its own 24-byte application header.

---

## 11. Sequence Numbers

UDP packets carry a `sequenceNumber` field (0-indexed) in the 24-byte header. This enables:

1. **Packet loss detection**: The receiver initializes a tracking array `receivedFlags[totalPackets]`. Any entry remaining `false` at the end of transmission denotes a lost datagram.
2. **Out-of-order detection**: The receiver tracks `highestSeqSeen`. If an arriving datagram has `seq < highestSeqSeen`, it is flagged as out-of-order.
3. **Data reassembly**: Payloads are written to `offset = seq × chunkSize` in the reassembly buffer, enabling correct file reconstruction regardless of arrival order.

TCP does not require application-level sequence numbers because reliable ordering is guaranteed by the OS transport stack.

---

## 12. Metrics & Measurement Methodology

### Throughput

```
Throughput (MB/s) = Total Bytes / (1024² × Time in seconds)
Throughput (Mbps) = Total Bytes × 8 / (10⁶ × Time in seconds)
```

Measured using high-resolution client timing (`std::chrono::high_resolution_clock`) across the active data transfer window.

### Observed Application-Level Latency Estimate

Latency reported by NetPulse is an **application-level latency estimate** (or **observed application-level packet/chunk latency**):

- **TCP**: Represents the observed inter-arrival timing between consecutive application-level chunks arriving at the receiver.
- **UDP**: Estimated using the difference between sender departure timestamp and receiver arrival timestamp (`nowUs - hdr.timestampUs`).
- **Localhost Notice**: Because experiments run on localhost (`127.0.0.1`), both client and server share the same system clock without physical wire transit. **These values should NOT be interpreted as physical network propagation delay.**
- **Measurement Methodology**: Accurate one-way physical network latency requires synchronized clocks (e.g., GPS or PTP) or specialized two-way hardware probing. NetPulse's metric provides an application-perspective timing profile suitable for academic comparative study.

### Jitter

Following the RFC 3550 statistical variance formulation:

```
D(i, i-1) = |latency(i) - latency(i-1)|
Jitter    = (1 / (N - 1)) × Σ D(i, i-1)
```

Measures the stability and delay variation across consecutive application arrivals.

### Packet / Chunk Loss

```
Loss % = (Sent - Received) / Sent × 100
```

- **TCP**: Always 0% in normal operation because the OS kernel retransmits unacknowledged segments.
- **UDP**: Reflects dropped datagrams detected via sequence numbers (or skipped via simulation).

### File Integrity

Byte-by-byte binary verification (`compareFiles()`) between the original source file and the received file in `results/`. Evaluates to `PASS` or `FAIL`.

---

## 13. File Transfer

The system supports transferring real files from `test_files/`:

1. Client reads the file from disk into memory.
2. Client sends a control header with `MODE=FILE` and the file basename.
3. Client sends data sequentially (in logical chunks for TCP, datagrams for UDP).
4. Server reconstructs the file and saves it to `results/received_<filename>`.
5. `main.exe` performs a byte-by-byte integrity comparison.

### Test Files Provided

| File | Size | Purpose |
|------|------|---------|
| `test_files/sample.txt` | ~95 KB | 1000 lines of structured text |
| `test_files/sample.bin` | 100 KB | High-entropy random binary data |

---

## 14. UDP Simulated Packet Loss

On localhost, the Windows networking stack rarely drops UDP datagrams under moderate loads. To demonstrate UDP's behavior on lossy channels, the client includes configurable simulated packet loss:

> **IMPORTANT:** This is **simulated/application-level packet loss, not actual packet loss caused by the network infrastructure.** The UDP client intentionally skips sending selected datagrams according to a pseudo-random probability threshold.

This feature is invaluable for demonstrating:
- **Missing packet detection**: How sequence-number gaps are identified at the receiver.
- **Loss percentage calculation**: Accurate verification that observed loss matches configured loss.
- **Degradation of file integrity**: Showing that UDP without ARQ fails integrity checks when packets are lost.
- **Behavioral comparison**: Directly contrasting TCP's transparent retransmissions against UDP's fire-and-forget unreliability.

An optional **artificial delay** (in milliseconds) can also be configured per packet to simulate high-delay paths.

---

## 15. Wireshark Analysis

Wireshark allows students and evaluators to observe **real wire-level network packets/segments**, contrasting with NetPulse's application-level metrics.

> **NOTE:** The `wireshark/` directory is **intentionally empty** before experiments. Do not create fake `.pcap` files. Furthermore, running Wireshark is an optional observation exercise and is **NOT required** to run the application.

### Step-by-Step Capture Workflow

1. **Start Wireshark**.
2. **Select the appropriate network interface**:
   - For `127.0.0.1` testing, select the **Npcap Loopback Adapter** (or *Adapter for loopback traffic capture*).
   - If testing over a LAN, select your active Ethernet or Wi-Fi adapter.
3. **Start capture** (click the blue shark fin icon).
4. **Run an experiment** from `main.exe` (e.g., Option 1 for TCP or Option 2 for UDP).
5. **Stop capture** immediately after the experiment finishes.
6. **Save the capture** into the project directory as:
   `wireshark/<appropriate_name>.pcapng` (e.g., `wireshark/tcp_1mb.pcapng` or `wireshark/udp_simloss_10pct.pcapng`).

### Useful Display Filters

| Filter Expression | Analysis Purpose |
|:---|:---|
| `tcp.port == 5000` | Isolate all TCP experiment traffic |
| `udp.port == 5001` | Isolate all UDP experiment traffic |
| `tcp.port == 5000 && tcp.flags.syn == 1` | Observe the TCP 3-way handshake initiation |
| `tcp.port == 5000 && tcp.flags.fin == 1` | Observe connection termination teardown |
| `tcp.port == 5000 && tcp.len > 0` | View actual TCP data segments and sequence numbers |
| `udp.port == 5001` | View independent UDP datagrams and 24-byte application headers |

### Key Observations to Make in Wireshark

- **TCP Analysis**: Observe the 3-way handshake (`SYN` → `SYN+ACK` → `ACK`), sliding window updates, piggybacked acknowledgments, and connection teardown (`FIN` → `ACK`).
- **UDP Analysis**: Note the absence of connection setup/teardown. Each datagram is an independent unit. Inspect the first 24 bytes of the UDP payload to see the NetPulse application header (sequence number, timestamp).
- **Application vs. Wire Distinction**: Contrast the logical application chunks reported on the console with the physical TCP segment lengths reported in Wireshark's packet list.

---

## 16. How to Compile

### Prerequisites

- Windows 10 or 11
- MinGW g++ (GCC 6.3+ recommended)
- `g++` available in PATH

### Compilation Commands

Open PowerShell in the project root directory and run:

```powershell
# TCP Server
g++ src/tcp/tcp_server.cpp -o tcp_server.exe -lws2_32 -std=c++17

# TCP Client
g++ src/tcp/tcp_client.cpp -o tcp_client.exe -lws2_32 -std=c++17

# TCP Server
g++ src/tcp/tcp_server.cpp -o tcp_server.exe -lws2_32 -std=c++14 -I include

# TCP Client
g++ src/tcp/tcp_client.cpp -o tcp_client.exe -lws2_32 -std=c++14 -I include

# UDP Server
g++ src/udp/udp_server.cpp -o udp_server.exe -lws2_32 -std=c++14 -I include

# UDP Client
g++ src/udp/udp_client.cpp -o udp_client.exe -lws2_32 -std=c++14 -I include

# Main Orchestrator (links with metrics module)
g++ src/main.cpp src/analyzer/metrics.cpp -o main.exe -lws2_32 -std=c++14 -I include
```

> **Note:** Both `-std=c++14` and `-std=c++17` are fully supported by all source files.

### Compile All at Once (PowerShell)

```powershell
g++ src/tcp/tcp_server.cpp -o tcp_server.exe -lws2_32 -std=c++14 -I include; `
g++ src/tcp/tcp_client.cpp -o tcp_client.exe -lws2_32 -std=c++14 -I include; `
g++ src/udp/udp_server.cpp -o udp_server.exe -lws2_32 -std=c++14 -I include; `
g++ src/udp/udp_client.cpp -o udp_client.exe -lws2_32 -std=c++14 -I include; `
g++ src/main.cpp src/analyzer/metrics.cpp -o main.exe -lws2_32 -std=c++14 -I include
```

---

## 17. How to Run

### Using the Orchestrator (Recommended)

From the project root directory, run:

```powershell
.\main.exe
```

This presents the interactive menu:

```
  ================================================================
                        N E T P U L S E
            TCP vs UDP Network Performance Analyzer
  ================================================================
  A Computer Networks experimental tool for comparing TCP and UDP
  behavior through real socket communication on Windows (Winsock2).
  ================================================================

  --------------------------------------------------------
  1. TCP Performance Test      (Synthetic Data)
  2. UDP Performance Test       (Synthetic Data)
  3. TCP File Transfer
  4. UDP File Transfer
  5. TCP vs UDP Comparison      (Synthetic Data)
  6. Chunk / Packet Size Experiment (Multiple chunk sizes)
  7. Exit
  --------------------------------------------------------
  Enter choice:
```

The orchestrator manages background server processes, client invocations, metric collection, and result exports automatically.

### Manual Mode (Standalone Terminal Execution)

To inspect individual processes or test across separate terminal windows:

**Terminal 1 — Server (Start First):**
```powershell
.\tcp_server.exe
# or for UDP:
.\udp_server.exe
```

**Terminal 2 — Client:**
```powershell
# TCP: mode, data_size, chunk_size, [file_path]
.\tcp_client.exe SYNTHETIC 1048576 4096

# UDP: mode, data_size, chunk_size, loss_rate, delay_ms, [file_path]
.\udp_client.exe SYNTHETIC 1048576 4096 0.0 0

# File transfer mode:
.\tcp_client.exe FILE 0 4096 test_files/sample.txt
.\udp_client.exe FILE 0 4096 0.0 0 test_files/sample.bin
```

---

## 18. Example Experiments & Recommended Order

For the most thorough and logical evaluation of the project on Windows, run the experiments in the following sequence:

### Recommended Testing Order

1. **Step 1: TCP Synthetic Benchmark (Option 1)**
   - Enter `1` MB, chunk size `4096`.
   - **Verification**: Validates reliable stream transmission, 0% chunk loss, and writes `results/tcp_results.csv`.
2. **Step 2: UDP Baseline Benchmark without Loss (Option 2)**
   - Enter `1` MB, chunk size `4096`, loss rate `0.0`, delay `0`.
   - **Verification**: Validates datagram transmission, sequence number tracking, and writes `results/udp_results.csv`.
3. **Step 3: UDP Benchmark with Simulated Loss (Option 2)**
   - Enter `1` MB, chunk size `4096`, loss rate `0.10` (10%), delay `0`.
   - **Verification**: Demonstrates sequence-number-based loss detection. Missing sequence numbers will be reported by the server, and the observed loss will closely match ~10%.
4. **Step 4: TCP File Transfer with Integrity Check (Option 3)**
   - Select file `test_files/sample.txt` or `test_files/sample.bin`, chunk size `4096`.
   - **Verification**: Transferred file saved to `results/received_<filename>` and evaluated byte-for-byte with `PASS`.
5. **Step 5: UDP File Transfer (Option 4)**
   - Select `test_files/sample.bin`, chunk size `4096`, loss rate `0.0`.
   - **Verification**: Validates datagram reassembly at byte offsets. Integrity evaluates to `PASS` under 0% loss. (Under simulated loss > 0%, integrity correctly evaluates to `FAIL`).
6. **Step 6: Automated Side-by-Side Comparison (Option 5)**
   - Enter `5` MB, chunk size `4096`, loss rate `0.05` (5%), delay `0`.
   - **Verification**: Executes TCP followed by UDP with identical parameters, displays side-by-side comparison table, and generates `results/comparison.csv`.
7. **Step 7: Chunk / Packet Size Experiment (Option 6)**
   - Enter `2` MB.
   - **Verification**: Runs sweeps across payload sizes (512 B to 32 KB), demonstrating how smaller chunks incur higher per-packet/chunk overhead.
8. **Step 8: Wire-Level Wireshark Verification (Optional)**
   - Start Wireshark on the Loopback adapter, run Option 1 or Option 2, and apply filter `tcp.port == 5000` or `udp.port == 5001`. Save trace to `wireshark/`.

---

## 19. Expected Observations

### TCP vs UDP Summary

| Aspect | TCP (Stream) | UDP (Datagram) |
|:---|:---|:---|
| Reliability | 100% delivery (OS retransmits) | Unreliable unless handled by app |
| Ordering | Guaranteed by byte-stream sequence | Unordered unless reassembled by app |
| Framing | Byte-stream (application chunks) | Preserved datagram boundaries |
| Connection | Connection-oriented (3-way handshake) | Connectionless (`sendto`/`recvfrom`) |
| Overhead | Higher (header options, ACKs, state) | Minimal (8-byte UDP + custom header) |
| Latency Metric | Inter-chunk arrival timing | Sender-receiver timestamp difference |
| Use Cases | Web (HTTP/HTTPS), file transfer, email | Real-time gaming, audio/video streaming, DNS |

---

## 20. Limitations

1. **Localhost Testing Environment**: All benchmark experiments execute across `127.0.0.1`. Traffic loops through the Windows network stack in memory rather than traversing physical routers or physical media.
2. **Application-Level Latency Estimate**: Latency metrics reflect observed application-level packet/chunk timing. They must NOT be interpreted as physical one-way network propagation delays, which require synchronized precision clocks (e.g. GPS/PTP) or specialized two-way hardware probing.
3. **Simulated Application Loss**: Packet loss is simulated at the application layer by the UDP client skipping transmissions, rather than physical packet drops by network routers.
4. **Byte-Stream vs. Segments**: The application counts logical application chunks passed to `sendAll()` / `recv()`. Actual physical TCP segments are governed by the OS TCP/IP stack and can be observed in Wireshark.
5. **Single Concurrent Client**: The servers are architected to service one benchmark transfer at a time sequentially.
6. **Platform Specifics**: Designed for Windows using the Winsock2 API (`ws2_32.lib`).

---

## 21. Submission Guidelines

When preparing this project for final academic submission:

- **Exclude `.git/`**: The `.git/` directory contains local version-control metadata and should be excluded from the final submission archive (ZIP).
- **Handling Executables (`.exe`)**:
  - If submitting **source code only**, omit root-level `.exe` binaries (`tcp_server.exe`, `tcp_client.exe`, `udp_server.exe`, `udp_client.exe`, `main.exe`). The evaluator can rebuild them using the compilation commands in [Section 16](#16-how-to-compile).
  - If submitting a **ready-to-run demonstration package**, retain the precompiled `.exe` binaries in the root directory so the orchestrator can run immediately.
- **Initial Directories**: The `results/` and `wireshark/` directories are intentionally empty prior to running experiments. Do not bundle synthetic or fake `.pcap` / `.csv` files.

---

## 22. Conclusion

NetPulse demonstrates the practical and architectural distinctions between TCP and UDP through direct Winsock2 network programming. By isolating throughput, application-level arrival timing, jitter, and loss behavior under controlled conditions, students observe how transport-layer design choices impact real application performance.

Key Takeaways:
- **TCP** guarantees complete, in-order byte delivery through transport-layer flow and congestion control, making it ideal for file transfers and reliable data exchange.
- **UDP** minimizes transport-layer overhead and latency, making it the protocol of choice for latency-sensitive applications that can tolerate loss or provide custom application-level framing.
- **Wireshark** provides indispensable insight into the hidden transport-layer handshakes, acknowledgments, and wire-level segmentations that operating systems abstract away from user-space applications.

---

*NetPulse: TCP vs UDP Network Performance Analyzer*  
*Academic Computer Networks Project*
