# NetPulse: TCP vs UDP Network Performance Analyzer

A comprehensive Computer Networks project that experimentally compares TCP and UDP behavior using real socket communication on Windows (Winsock2).

---

## Table of Contents

1. [Abstract](#1-abstract)
2. [Problem Statement](#2-problem-statement)
3. [Objectives](#3-objectives)
4. [Technologies](#4-technologies)
5. [Architecture](#5-architecture)
6. [Folder Structure](#6-folder-structure)
7. [TCP Implementation](#7-tcp-implementation)
8. [UDP Implementation](#8-udp-implementation)
9. [Packet Structure](#9-packet-structure)
10. [Chunking](#10-chunking)
11. [Sequence Numbers](#11-sequence-numbers)
12. [Metrics](#12-metrics)
13. [File Transfer](#13-file-transfer)
14. [Packet-Loss Simulation](#14-packet-loss-simulation)
15. [Wireshark Analysis](#15-wireshark-analysis)
16. [How to Compile](#16-how-to-compile)
17. [How to Run](#17-how-to-run)
18. [Example Experiments](#18-example-experiments)
19. [Expected Observations](#19-expected-observations)
20. [Limitations](#20-limitations)
21. [Future Improvements](#21-future-improvements)
22. [Conclusion](#22-conclusion)

---

## 1. Abstract

NetPulse is an academic tool that allows a user to perform controlled TCP and UDP data-transfer experiments on a local machine, measure key network performance metrics (throughput, latency, jitter, packet loss), and compare the two transport-layer protocols side-by-side. All communication uses the Windows Winsock2 API with standard C++ — no external dependencies, frameworks, or databases.

The project demonstrates core Computer Networks concepts including:

- Client-server socket communication
- TCP byte-stream semantics vs. UDP datagram boundaries
- Application-level packet framing (sequence numbers, headers)
- Chunked data transfer and file transfer
- Performance measurement and statistical comparison
- Network traffic inspection with Wireshark

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
| 4 | Chunked transmission | Data divided into configurable-size chunks |
| 5 | UDP sequence numbers | Application-level 24-byte packet header |
| 6 | Packet loss detection | Server tracks received sequence numbers |
| 7 | Throughput measurement | `Total Bytes / Time` |
| 8 | Latency measurement | Per-chunk inter-arrival timing |
| 9 | Jitter measurement | Mean of consecutive latency differences |
| 10 | Packet loss measurement | `(Sent - Received) / Sent × 100` |
| 11 | File transfer + integrity | Byte-by-byte comparison of original vs received |
| 12 | TCP vs UDP comparison | Side-by-side table with all metrics |
| 13 | Wireshark capture | Real traffic on ports 5000 (TCP) / 5001 (UDP) |
| 14 | Result persistence | CSV files in `results/` |

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

## 6. Folder Structure

```
TCP-vs-UDP/
│
├── src/
│   ├── tcp/
│   │   ├── tcp_server.cpp      TCP server (bind, listen, accept, recv)
│   │   └── tcp_client.cpp      TCP client (connect, send, timing)
│   ├── udp/
│   │   ├── udp_server.cpp      UDP server (bind, recvfrom, tracking)
│   │   └── udp_client.cpp      UDP client (sendto, seq numbers, loss sim)
│   ├── analyzer/
│   │   ├── metrics.h           Metric calculation declarations
│   │   └── metrics.cpp         Throughput, loss, latency, jitter, display, CSV
│   └── main.cpp                Orchestrator with console menu
│
├── include/
│   └── common.h                Shared constants, enums, structs, utilities
│
├── results/                    Runtime CSV/TXT output (git-ignored)
├── wireshark/                  Place for Wireshark .pcapng captures
├── test_files/
│   ├── sample.txt              ~95 KB text file for testing
│   └── sample.bin              100 KB binary file for testing
│
├── README.md                   This document
└── .gitignore
```

### File Responsibilities

| File | Responsibility |
|------|---------------|
| `common.h` | Ports, constants, packet header struct, serialization, utility functions (`sendAll`, `recvExact`, `generateSyntheticData`, file I/O helpers) |
| `tcp_server.cpp` | Winsock init → bind → listen → accept → read control header → receive byte stream → track per-chunk latency → save file → write result file |
| `tcp_client.cpp` | Winsock init → connect → send control header → send data in chunks via `sendAll()` → measure time → output RESULT to stdout |
| `udp_server.cpp` | Winsock init → bind → receive START/DATA/END datagrams → track sequence numbers → detect loss/out-of-order → compute latency/jitter → reassemble data → write result file → send ACK |
| `udp_client.cpp` | Winsock init → build packets with headers → send START → send DATA with optional loss simulation → send END → wait for ACK → output RESULT to stdout |
| `metrics.h/.cpp` | `calculateThroughputMBps()`, `calculatePacketLoss()`, `calculateAverageLatency()`, `calculateJitter()`, `displayResult()`, `displayComparison()`, `saveResultCSV()`, `buildResultFromKV()` |
| `main.cpp` | Console menu → launch server background process → run client capturing stdout → read server result file → combine into `ExperimentResult` → display + save |

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

## 10. Chunking

Data is divided into fixed-size chunks before transmission:

```
Total Data: 10 MB = 10,485,760 bytes
Chunk Size: 4096 bytes

Number of chunks = ceil(10,485,760 / 4096) = 2560 chunks

Last chunk may be smaller: 10,485,760 mod 4096 = 0 (exact fit in this case)
```

The user can configure chunk size from 64 to 60,000 bytes.

For TCP, chunks are conceptual (TCP treats everything as a byte stream). For UDP, each chunk becomes one datagram with its own header.

---

## 11. Sequence Numbers

UDP packets carry a `sequenceNumber` field (0-indexed) in the header. This enables:

1. **Packet loss detection**: Server initializes a boolean array `receivedFlags[totalPackets]`. After transfer, any `false` entry identifies a missing packet.

2. **Out-of-order detection**: Server tracks `highestSeqSeen`. If a received packet's sequence number is less than `highestSeqSeen`, it arrived out of order.

3. **Data reassembly**: Payload is copied to `offset = seq × chunkSize` in the reassembly buffer, allowing correct reconstruction regardless of arrival order.

TCP does not need application-level sequence numbers because the OS TCP stack provides reliable, ordered delivery.

---

## 12. Metrics

### Throughput

```
Throughput (MB/s) = Total Bytes / (1024² × Time in seconds)
Throughput (Mbps) = Total Bytes × 8 / (10⁶ × Time in seconds)
```

Measured using client-side `high_resolution_clock` around the send loop.

### Packet Loss

```
Packet Loss % = (Packets Sent - Packets Received) / Packets Sent × 100
```

For TCP, this is always 0% (TCP retransmits). For UDP, this depends on network conditions or simulated loss.

### Latency

Measured as **inter-chunk arrival time** on the server:
- For each received chunk, the time since the previous chunk arrival is recorded.
- Average of these samples gives average latency per chunk.

> **Note:** This is NOT one-way network propagation delay (which requires synchronized clocks). It is labeled as "localhost clock-based inter-chunk timing" throughout the code.

For UDP, the packet header carries a `timestampUs` field. On localhost (same clock), the difference `now - timestamp` gives a one-way latency estimate. This is only valid on localhost and is clearly labeled.

### Jitter

Following a simplified RFC 3550 approach:

```
J(i) = |latency(i) - latency(i-1)|
Jitter = mean of all J(i) for i = 1..N
```

This measures variation in inter-packet delay, which is the standard definition of jitter.

### File Integrity

Byte-by-byte comparison of the original file and the received file using `compareFiles()`. Returns PASS or FAIL.

---

## 13. File Transfer

The system supports transferring actual files from `test_files/`:

1. Client reads the file into memory.
2. Client sends a control header with `MODE=FILE` and the filename.
3. Client sends the file data in chunks.
4. Server receives all data and saves it to `results/received_<filename>`.
5. After transfer, `main.exe` compares the original file with the received copy.

### Provided Test Files

| File | Size | Purpose |
|------|------|---------|
| `test_files/sample.txt` | ~95 KB | Text file with 1000 lines |
| `test_files/sample.bin` | 100 KB | Random binary data |

You can add any file to `test_files/` and use it in experiments.

---

## 14. Packet-Loss Simulation

On localhost, UDP rarely experiences real packet loss. To demonstrate UDP's unreliable nature, the client supports **application-level simulated packet loss**:

```
User enters: Packet loss rate = 0.10 (10%)

Client generates random number r for each packet:
  if r < 0.10: skip sending this packet
  else: send normally
```

The server detects the missing sequence numbers and reports the loss.

> **IMPORTANT:** This is **simulated application-level loss**, not real network infrastructure loss. The client intentionally skips sending certain packets. This is clearly labeled in all output.

Artificial per-packet delay is also supported to simulate slower/congested networks.

---

## 15. Wireshark Analysis

### Setup

1. Open Wireshark.
2. Select the **Loopback adapter** (Npcap Loopback Adapter / Adapter for loopback traffic capture).
3. Start capturing.
4. Run an experiment from `main.exe`.
5. Stop capture after the experiment completes.
6. Save capture to `wireshark/` directory.

### Useful Filters

| Filter | Purpose |
|--------|---------|
| `tcp.port == 5000` | All TCP traffic for the experiment |
| `udp.port == 5001` | All UDP traffic for the experiment |
| `tcp.port == 5000 && tcp.flags.syn == 1` | TCP SYN packets (handshake) |
| `tcp.port == 5000 && tcp.flags.fin == 1` | TCP FIN packets (connection close) |
| `tcp.port == 5000 && tcp.len > 0` | TCP data segments only |
| `udp.port == 5001 && udp.length > 32` | UDP data packets (with header) |

### What to Observe

**TCP Capture:**
- Three-way handshake: SYN → SYN-ACK → ACK
- Data segments with TCP sequence numbers and acknowledgements
- TCP window sizes (flow control)
- Retransmissions (if any — unlikely on localhost)
- FIN → ACK connection termination

**UDP Capture:**
- Individual datagrams (each `sendto()` = one datagram)
- Source/destination ports (5001)
- Packet lengths (should be `UDP_HEADER_SIZE + payload`)
- Application-level sequence numbers visible in packet payload (first 4 bytes)
- Missing datagrams (if simulated loss is enabled)
- No handshake, no ACKs, no connection state

### Procedure

1. Start Wireshark capture on loopback adapter.
2. In `main.exe`, select **Option 1** (TCP Performance Test), enter 1 MB, 4096 chunk.
3. Stop capture. Apply filter `tcp.port == 5000`.
4. Observe handshake, data segments, and termination.
5. Restart capture.
6. Select **Option 2** (UDP Performance Test), enter 1 MB, 4096 chunk, 0.1 loss rate.
7. Stop capture. Apply filter `udp.port == 5001`.
8. Count datagrams. Note missing packets vs. expected count.
9. Save both captures as `.pcapng` in the `wireshark/` folder.

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

# UDP Server
g++ src/udp/udp_server.cpp -o udp_server.exe -lws2_32 -std=c++17

# UDP Client
g++ src/udp/udp_client.cpp -o udp_client.exe -lws2_32 -std=c++17

# Main Orchestrator (links with metrics module)
g++ src/main.cpp src/analyzer/metrics.cpp -o main.exe -lws2_32 -std=c++17
```

If your MinGW does not support `-std=c++17`, try `-std=c++14` or omit the flag (the code avoids C++17-only features).

### Compile All at Once

```powershell
g++ src/tcp/tcp_server.cpp -o tcp_server.exe -lws2_32 -std=c++17; `
g++ src/tcp/tcp_client.cpp -o tcp_client.exe -lws2_32 -std=c++17; `
g++ src/udp/udp_server.cpp -o udp_server.exe -lws2_32 -std=c++17; `
g++ src/udp/udp_client.cpp -o udp_client.exe -lws2_32 -std=c++17; `
g++ src/main.cpp src/analyzer/metrics.cpp -o main.exe -lws2_32 -std=c++17
```

---

## 17. How to Run

### Using the Orchestrator (Recommended)

```powershell
.\main.exe
```

This presents a menu:

```
  ================================================================
                        N E T P U L S E
            TCP vs UDP Network Performance Analyzer
  ================================================================

  1. TCP Performance Test      (Synthetic Data)
  2. UDP Performance Test       (Synthetic Data)
  3. TCP File Transfer
  4. UDP File Transfer
  5. TCP vs UDP Comparison      (Synthetic Data)
  6. Packet Size Experiment     (Multiple chunk sizes)
  7. Exit

  Enter choice:
```

The orchestrator automatically starts servers and clients in the correct order.

### Manual Mode (Advanced / Debugging)

You can run servers and clients manually in separate terminals:

**Terminal 1 — Server:**
```powershell
.\tcp_server.exe
# or
.\udp_server.exe
```

**Terminal 2 — Client:**
```powershell
# TCP: mode, data_size, chunk_size, [file_path]
.\tcp_client.exe SYNTHETIC 1048576 4096

# UDP: mode, data_size, chunk_size, loss_rate, delay_ms, [file_path]
.\udp_client.exe SYNTHETIC 1048576 4096 0.0 0

# File transfer examples:
.\tcp_client.exe FILE 0 4096 test_files/sample.txt
.\udp_client.exe FILE 0 4096 0.0 0 test_files/sample.bin
```

---

## 18. Example Experiments

### Test 1: TCP 1 MB Synthetic Data

```
Choice: 1
Data size: 1 MB
Chunk size: 4096
```

**Expected:** Fast transfer, 0% loss, ordered delivery.

### Test 2: UDP 1 MB Synthetic Data (No Loss)

```
Choice: 2
Data size: 1 MB
Chunk size: 4096
Loss rate: 0.0
Delay: 0
```

**Expected:** Fast transfer, likely 0% loss on localhost, datagram delivery.

### Test 3: UDP 1 MB with 10% Simulated Loss

```
Choice: 2
Data size: 1 MB
Chunk size: 4096
Loss rate: 0.10
Delay: 0
```

**Expected:** ~10% of packets missing, server reports lost sequence numbers.

### Test 4: TCP 10 MB

```
Choice: 1
Data size: 10 MB
Chunk size: 4096
```

**Expected:** Reliable transfer, measurable throughput.

### Test 5: UDP 10 MB

```
Choice: 2
Data size: 10 MB
Chunk size: 4096
Loss rate: 0.0
Delay: 0
```

**Expected:** May show some real loss under high volume; compare throughput with TCP.

### Test 6: Different Chunk Sizes

```
Choice: 6
Data size: 5 MB
```

Automatically tests: 512, 1024, 2048, 4096, 8192, 16384, 32768 bytes.

**Expected:** Larger chunks generally yield higher throughput due to less per-packet overhead.

### Test 7: TCP File Transfer

```
Choice: 3
File: test_files/sample.bin
Chunk size: 4096
```

**Expected:** File transferred, integrity PASS.

### Test 8: UDP File Transfer

```
Choice: 4
File: test_files/sample.bin
Chunk size: 4096
Loss rate: 0.0
Delay: 0
```

**Expected:** File transferred, integrity PASS (if no packets lost).

### Test 9: TCP vs UDP Comparison

```
Choice: 5
Data size: 5 MB
Chunk size: 4096
Loss rate: 0.05 (5% simulated for UDP)
Delay: 0
```

**Expected:** Side-by-side comparison table showing TCP with 0% loss vs UDP with ~5% loss.

### Test 10: Wireshark Capture

Follow the [Wireshark Procedure](#15-wireshark-analysis) while running Tests 1 and 2.

---

## 19. Expected Observations

### TCP vs UDP Summary

| Aspect | TCP | UDP |
|--------|-----|-----|
| Reliability | 100% delivery (OS retransmits) | May lose packets |
| Ordering | Guaranteed | Not guaranteed |
| Overhead | Higher (handshake, ACKs, headers) | Lower |
| Throughput | Generally lower than UDP | Generally higher |
| Latency | Higher (connection + ACK overhead) | Lower |
| Jitter | Lower (flow control smooths delivery) | Higher |
| Connection | Required (3-way handshake) | Not required |
| Use case | Web, email, file transfer | Streaming, gaming, DNS |

### Localhost Considerations

- On localhost, UDP loss is rare without simulation because traffic never leaves the machine.
- Throughput differences may be small on localhost compared to real networks.
- TCP's overhead (handshake, ACKs) is observable in Wireshark even on localhost.
- Simulated loss effectively demonstrates what happens on lossy networks.

---

## 20. Limitations

1. **Localhost only** — All experiments run on `127.0.0.1`. Real network conditions (latency, congestion, loss) are not captured.

2. **Simulated loss** — Packet loss is application-level simulation, not real network loss. Clearly labeled throughout.

3. **Latency measurement** — Inter-chunk arrival time is used as a proxy for latency. True one-way network delay requires synchronized clocks.

4. **Single client** — Servers accept one client at a time. No concurrent experiment support.

5. **No retransmission in UDP** — The application does not implement UDP-level retransmission (ARQ). Lost packets remain lost.

6. **Windows-only** — Uses Winsock2 API. Does not compile on Linux/macOS without porting to POSIX sockets.

---

## 21. Future Improvements

1. **Remote testing** — Allow client and server on different machines for real network measurements.
2. **Selective Repeat ARQ** — Implement application-level retransmission for UDP.
3. **Bandwidth throttling** — Simulate limited bandwidth environments.
4. **Multi-threaded server** — Accept concurrent experiments.
5. **Graphical output** — Generate throughput/latency graphs (e.g., via gnuplot or CSV import to Excel).
6. **Cross-platform** — Port to POSIX sockets for Linux/macOS.
7. **Checksum verification** — Add per-packet checksums for UDP integrity.
8. **Real-time dashboard** — Display live metrics during transfer.

---

## 22. Conclusion

NetPulse demonstrates the fundamental differences between TCP and UDP through real socket programming experiments. By measuring throughput, latency, jitter, and packet loss under controlled conditions, the project bridges the gap between theoretical networking concepts and practical implementation.

Key takeaways:

- **TCP** provides reliability and ordering at the cost of overhead (handshake, ACKs, flow control). Applications that need guaranteed delivery (web, file transfer) use TCP.
- **UDP** provides lower overhead and potentially higher throughput, but the application must handle loss and ordering. Applications that prioritize speed over reliability (streaming, gaming) use UDP.
- **Application-level framing** (sequence numbers, packet headers) is essential when using UDP to detect and handle loss.
- **Wireshark** reveals the underlying protocol mechanics (SYN/ACK, retransmissions, datagram boundaries) that are invisible at the application level.

---

*NetPulse — TCP vs UDP Network Performance Analyzer*
*Computer Networks Project*
