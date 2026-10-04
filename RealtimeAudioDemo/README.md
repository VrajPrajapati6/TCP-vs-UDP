# RealtimeAudioDemo: TCP vs UDP Real-Time Audio Streaming Demonstration

An academic Computer Networks laboratory demonstration demonstrating the real-world, audible consequences of transport layer protocol selection (**TCP** vs **UDP**) in real-time media streaming.

---

## 1. Project Overview

In Computer Networks, textbooks state that:
- **TCP** is *connection-oriented, reliable, and byte-stream based*.
- **UDP** is *connectionless, unreliable, and datagram based*.

While textual metrics (such as throughput, round-trip time, and packet loss counts) explain these characteristics mathematically, they fail to convey how these protocols behave in user-facing applications. 

**RealtimeAudioDemo** is an independent, companion demonstration project that streams uncompressed PCM voice audio across local sockets and plays it through the computer's sound card in real time using the native Windows Multimedia API (`waveOut`). 

Listeners can **hear**:
1. **TCP Streaming**: Flawless, gap-free speech delivery where every byte arrives in strict sequential order.
2. **UDP Streaming (0% Loss)**: Smooth, low-latency playback matching TCP under ideal channel conditions.
3. **UDP Streaming (5% Loss)**: Subtle micro-clicks and brief syllable dropouts; speech remains completely intelligible.
4. **UDP Streaming (10% Loss)**: Noticeable audio gaps and clipped words, but the transmission never halts or delays.
5. **UDP Streaming (20% Loss)**: Heavy audio degradation and robotic stuttering, yet real-time progression is preserved without catastrophic buffer stalls.

---

## 2. Why This Demonstration Exists

Traditional network programming assignments demonstrate packet loss by printing console lines:
```text
[UDP Receiver] Packet 42 lost!
[UDP Receiver] Packet 89 lost!
```
While accurate, this does not answer the fundamental engineering question:

> *"Why does VoIP, Discord, Zoom, and online multiplayer gaming use UDP instead of TCP if UDP drops packets?"*

This project provides the answer through human perception:
- With **TCP**, if a segment is dropped or delayed, the OS transport layer halts application delivery until the missing segment is retransmitted and acknowledged (**Head-of-Line Blocking**). For audio, this causes noticeable pauses, buffering wheels, and accumulating latency.
- With **UDP**, when a datagram is dropped, the receiver detects the missing sequence number, replaces the missing interval with a concealment silence gap, and immediately continues playing subsequent datagrams. The listener hears a brief glitch, but the conversation remains strictly real-time.

---

## 3. TCP vs UDP Core Concepts

| Protocol Metric | TCP Mode (Port 6000) | UDP Mode (Port 6001) |
| :--- | :--- | :--- |
| **Connection Model** | 3-Way Handshake (`SYN`, `SYN-ACK`, `ACK`) | Connectionless (Immediate transmission) |
| **Data Framing** | Continuous Byte Stream | Discrete Datagram Units |
| **Reliability** | 100% Guaranteed by Kernel Transport Stack | Best-Effort Delivery |
| **Retransmissions** | Handled transparently by OS TCP stack | None (Application layer must handle loss) |
| **Sequence Ordering** | In-order delivery guaranteed | Datagrams may arrive out-of-order |
| **Application Reaction to Loss** | Application waits for retransmission | Application advances real-time playback |
| **Audible Artifacts** | Potential stalling / delay accumulation | Brief micro-clicks / audible syllable gaps |

---

## 4. Architecture

```
                       AUDIO SOURCE
              (PCM WAV: 22050 Hz, 16-bit Mono)
                            |
            ┌───────────────┴───────────────┐
            ↓                               ↓
       [TCP Mode]                      [UDP Mode]
      Port: 6000                      Port: 6001
            |                               |
    tcp_audio_client.exe            udp_audio_client.exe
 (Paced Frame-by-Frame Sender)   (Application Loss Simulator)
            |                               |
     TCP Byte Stream                 UDP Datagrams
(Length-prefixed PCM frames)    (Header: Seq, Time, Type, Len)
            |                               |
            ↓                               ↓
    tcp_audio_server.exe            udp_audio_server.exe
   (In-order TCP Receiver)        (Sequence Gap Detector)
            |                               |
            └───────────────┬───────────────┘
                            ↓
                  WINDOWS waveOut API
               (Real-time Ring Buffer)
                            ↓
                LOUDSPEAKER / HEADPHONES
```

---

## 5. Audio Streaming Pipeline

Unlike file-transfer utilities that transfer an entire file before invoking media players, `RealtimeAudioDemo` uses a streaming pipeline:

```
[Sender Disk] 
      │ (Read 20ms audio frame = 882 bytes)
      ▼
[Sender Frame Pacer] 
      │ (Sleep remainder of 20ms window)
      ▼
[Network Sockets] 
      │ (Winsock2 loopback: TCP 6000 or UDP 6001)
      ▼
[Receiver Socket Buffer]
      │ (Extract header & payload)
      ▼
[Receiver WaveOut Ring Buffer] (64 x 882-byte audio blocks)
      │
      ▼
[DAC / Sound Card] ──► Real-time sound emitted to speakers
```

Playback begins as soon as the first few frames arrive, ensuring realistic streaming latency.

---

## 6. WAV Format Specifications

The audio file located at `audio/network_demo.wav` was generated using a native Windows speech synthesis script (`generate_audio.ps1`) reading an educational Computer Networks lecture text.

- **Container**: Microsoft RIFF WAV (Uncompressed PCM)
- **Audio Format Tag**: `0x0001` (WAVE_FORMAT_PCM)
- **Channels**: 1 (Mono)
- **Sample Rate**: 22,050 Hz
- **Bits Per Sample**: 16-bit signed integer (Little-Endian)
- **Byte Rate**: 44,100 bytes/sec
- **Block Align**: 2 bytes (1 sample)
- **Audio Duration**: ~30.05 seconds (1,325,416 total bytes)
- **Frame Duration**: 20 milliseconds per frame
- **Frame Payload Size**: `22050 * 1 * (16 / 8) * 0.020 = 882 bytes`

---

## 7. TCP Implementation Details

- **Executable (Receiver)**: `tcp_audio_server.exe`
- **Executable (Sender)**: `tcp_audio_client.exe`
- **Port**: `6000`

### Byte-Stream Framing Protocol
TCP does not preserve datagram boundaries. A single `send()` of 882 bytes might be split into multiple `recv()` calls, or multiple `send()` calls coalesced into one buffer. To guarantee stream synchronization:
1. **Metadata Handshake**: Upon connection, client transmits a 22-byte `AudioMetadata` header containing sample rate, channels, bits per sample, and frame size.
2. **Length-Prefixed Framing**: Each subsequent audio frame is transmitted as:
   ```
   [4-byte Frame Length (uint32_t)] + [N bytes Raw PCM Audio Data]
   ```
3. **Stream Helpers**: Uses `sendAll()` and `recvExact()` loops to read exact byte counts across socket buffers without boundary slippage.
4. **End-of-Stream**: A frame length prefix of `0` signals clean termination.

---

## 8. UDP Implementation Details

- **Executable (Receiver)**: `udp_audio_server.exe`
- **Executable (Sender)**: `udp_audio_client.exe`
- **Port**: `6001`

### Datagram Protocol
UDP preserves datagram boundaries. Each call to `sendto()` generates an independent IP datagram.
1. **START Signal**: Client transmits a control packet with `packetType = START` containing `AudioMetadata` (repeated 3 times to mitigate single-packet dropouts at stream initiation).
2. **DATA Packets**: Each 20ms audio frame is packed with a custom 24-byte header followed by 882 bytes of PCM audio.
3. **END Signal**: A control packet with `packetType = END` notifies receiver to flush audio buffers.

---

## 9. UDP Packet Format

Every UDP packet is serialized using fixed-width little-endian integers:

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       Sequence Number                         | (4 bytes)
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         Payload Size                          | (4 bytes)
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                      Timestamp (Microseconds)                 | (8 bytes)
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  Packet Type  |                  Frame Index                  | (1 + 4 bytes)
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|    Reserved   |                 PCM Audio Data ...            | (3 + N bytes)
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

Total datagram size = `24 bytes (Header) + 882 bytes (Audio) = 906 bytes`. This is well below the Ethernet MTU of 1500 bytes, completely avoiding IP-level packet fragmentation.

---

## 10. Sequence Numbers & Loss Detection

1. Sender maintains a monotonic sequence counter starting at `1`.
2. Receiver tracks `expectedSeq`.
3. When receiver receives packet $K$:
   - If $K == expectedSeq$: Packet arrived in order. $expectedSeq \leftarrow K + 1$.
   - If $K > expectedSeq$: Missing packet gap detected! Missing count = $K - expectedSeq$.
   - If $K < expectedSeq$: Duplicate or out-of-order packet arrived.

---

## 11. Controlled Packet Loss Simulation

Natural packet loss on localhost (`127.0.0.1`) is 0.00%. To demonstrate real-time UDP behavior, the client implements an application-level loss simulator:

```cpp
bool shouldDrop = false;
if (lossRate > 0.0) {
    double roll = dist(rng);
    if (roll < lossRate) shouldDrop = true;
}
if (!shouldDrop) {
    sendto(sock, packetBuf.data(), totalSize, 0, ...);
}
// seqNum increments regardless of drop!
```

> [!NOTE]
> This simulates packet loss at the application layer by intentionally suppressing `sendto()` calls. It does not claim that the physical Windows loopback adapter dropped packets.

---

## 12. Playback Buffer & Concealment Mechanics

Windows `waveOut` audio operates using an asynchronous ring buffer of 64 pre-allocated headers (`WAVEHDR`).

When UDP packet loss occurs:
- The receiver does NOT pause or request retransmission.
- It inserts a zeroed PCM frame (silence) for each missing 20ms duration into the playback queue.
- This preserves the temporal alignment of the speech: words following the lost segment play at their exact correct time.
- The human listener hears a 20ms to 40ms micro-gap or phoneme clip, rather than an accumulating delay.

---

## 13. TCP vs UDP Behavior Comparison

### What You Will Hear
1. **TCP (Option 1)**: Pristine audio quality. Zero distortion, zero clicks.
2. **UDP 0% Loss (Option 2)**: Sounds identical to TCP. Shows that UDP itself does not degrade quality if the network is healthy.
3. **UDP 5% Loss (Option 3)**: Clean speech punctuated by faint, occasional clicks or slight syllable cuts. Intelligibility is ~98%.
4. **UDP 10% Loss (Option 4)**: Noticeable missing phonemes (e.g., words like *"retransmission"* sounding like *"re...smission"*). Intelligibility is ~85%.
5. **UDP 20% Loss (Option 5)**: Significant stuttering and choppy audio. Words are fractured, but the flow of audio never stalls.

---

## 14. Wireshark Capture Instructions

Separate dedicated ports are used to prevent interference with NetPulse:
- **TCP Audio Stream**: Port `6000`
- **UDP Audio Stream**: Port `6001`

### Step-by-Step Packet Inspection
1. Open Wireshark.
2. Select the **Adapter for loopback traffic capture** (`Npcap Loopback Adapter`).
3. Set the capture filter or display filter:
   ```wireshark
   tcp.port == 6000 or udp.port == 6001
   ```
4. Run `audio_demo.exe` and select Option 1 (TCP) or Option 4 (UDP 10%).
5. Stop Wireshark capture.

### Key Observations
- **TCP**:
  - Filter: `tcp.port == 6000`
  - Observe `[SYN]`, `[SYN, ACK]`, `[ACK]` 3-way handshake.
  - Observe `[PSH, ACK]` packets containing audio frames with steadily increasing Byte Sequence Numbers.
  - Observe `[FIN, ACK]` closing handshake.
- **UDP**:
  - Filter: `udp.port == 6001`
  - Observe UDP datagrams arriving without handshakes or acknowledgments.
  - Inspect UDP payload: the first 4 bytes show the incrementing `sequenceNumber`. Notice gaps in sequence numbers corresponding to dropped packets.

---

## 15. Compilation & Build Guide

### Prerequisites
- Windows 10 or 11
- MinGW GCC (`g++`) version 6.3.0 or higher
- Windows Multimedia API (`winmm`) and Winsock2 (`ws2_32`)

### Build Commands
To build all 5 executables from the `RealtimeAudioDemo/` folder:

```powershell
# 1. Compile TCP Audio Server (Receiver / Player)
g++ -std=c++14 -O2 src/common/audio_common.cpp src/audio/audio_player.cpp src/audio/wav_reader.cpp src/tcp/tcp_audio_server.cpp -o tcp_audio_server.exe -lws2_32 -lwinmm

# 2. Compile TCP Audio Client (Sender)
g++ -std=c++14 -O2 src/common/audio_common.cpp src/audio/wav_reader.cpp src/tcp/tcp_audio_client.cpp -o tcp_audio_client.exe -lws2_32 -lwinmm

# 3. Compile UDP Audio Server (Receiver / Player)
g++ -std=c++14 -O2 src/common/audio_common.cpp src/audio/audio_player.cpp src/udp/udp_audio_server.cpp -o udp_audio_server.exe -lws2_32 -lwinmm

# 4. Compile UDP Audio Client (Sender with Loss Simulator)
g++ -std=c++14 -O2 src/common/audio_common.cpp src/audio/wav_reader.cpp src/udp/udp_audio_client.cpp -o udp_audio_client.exe -lws2_32 -lwinmm

# 5. Compile Main Interactive Console Orchestrator
g++ -std=c++14 -O2 src/main.cpp -o audio_demo.exe
```

---

## 16. Running the Demonstration

### Method A: Interactive Menu (Recommended)
Simply launch:
```powershell
.\audio_demo.exe
```
This presents an interactive menu that automatically manages server and client process lifecycles.

### Method B: Manual Command-Line Execution

#### TCP Audio Streaming:
Terminal 1 (Start Server first):
```powershell
.\tcp_audio_server.exe 6000
```
Terminal 2 (Start Client):
```powershell
.\tcp_audio_client.exe audio/network_demo.wav 127.0.0.1 6000
```

#### UDP Audio Streaming (0% Loss):
Terminal 1:
```powershell
.\udp_audio_server.exe 6001
```
Terminal 2:
```powershell
.\udp_audio_client.exe audio/network_demo.wav 0.0 127.0.0.1 6001
```

#### UDP Audio Streaming with 10% Simulated Packet Loss:
Terminal 1:
```powershell
.\udp_audio_server.exe 6001
```
Terminal 2:
```powershell
.\udp_audio_client.exe audio/network_demo.wav 0.10 127.0.0.1 6001
```

---

## 17. Generating Custom Audio

A PowerShell script is provided to regenerate speech audio:
```powershell
powershell -ExecutionPolicy Bypass -File generate_audio.ps1
```
You can also substitute any 16-bit PCM uncompressed WAV file (mono, 22050 Hz or 44100 Hz) directly into `audio/network_demo.wav`.

---

## 18. Technical Distinctions (Academic Rigor)

1. **Simulated vs Real Loss**:
   - The packet loss in UDP mode is simulated at the application layer by dropping datagrams before calling `sendto()`.
   - The audio playback, buffer ring queues, Winsock socket communication, and audible dropouts are **100% real**.
2. **TCP Retransmission on Localhost**:
   - In a local loopback environment, the Windows OS TCP stack does not drop segments because there is no congested physical wire or router buffer.
   - We do not fake TCP retransmissions with artificial application code. TCP's guaranteed reliability is observed directly through its 0% loss and strict byte sequencing.
