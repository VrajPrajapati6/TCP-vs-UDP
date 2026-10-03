/*
 * udp_server.cpp - NetPulse UDP Server
 *
 * This server:
 *  1. Initializes Winsock and creates a UDP socket bound to UDP_PORT.
 *  2. Waits for a START packet that carries transfer metadata
 *     (total packets, data size, chunk size, mode, filename).
 *  3. Receives DATA packets, each carrying a sequence number,
 *     timestamp, and payload.
 *  4. Tracks which sequence numbers were received to detect:
 *        - Missing packets (packet loss)
 *        - Out-of-order delivery
 *  5. Measures one-way latency (labeled as localhost-only clock-based)
 *     and calculates jitter from consecutive latency samples.
 *  6. Detects the END packet signaling transfer completion.
 *  7. Reconstructs received data and optionally saves a file.
 *  8. Writes all metrics to a result file for the orchestrator.
 *  9. Sends an ACK back to the client with summary info.
 *
 * Compilation:
 *   g++ src/udp/udp_server.cpp -o udp_server.exe -lws2_32 -std=c++17
 *
 * Usage:
 *   udp_server.exe       (waits for one transfer, then exits)
 */

#include "../../include/common.h"

int main() {
    // ---- Winsock Init ----
    if (!initWinsock()) return 1;

    // ---- Create UDP Socket ----
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        printWinsockError("socket()");
        WSACleanup();
        return 1;
    }

    // ---- Bind ----
    sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family      = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port        = htons(UDP_PORT);

    if (bind(sock, reinterpret_cast<sockaddr*>(&serverAddr),
             sizeof(serverAddr)) == SOCKET_ERROR) {
        printWinsockError("bind()");
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    // Set receive timeout so server doesn't hang forever
    int timeout = SOCKET_TIMEOUT;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout), sizeof(timeout));

    std::cout << "[UDP Server] Listening on port " << UDP_PORT << " ...\n";

    // ---- State Variables ----
    int totalExpectedPackets = 0;
    int64_t totalExpectedBytes = 0;
    int expectedChunkSize = DEFAULT_CHUNK_SIZE;
    TransferMode mode = TransferMode::SYNTHETIC;
    std::string fileName;
    bool transferStarted = false;
    bool transferDone = false;

    // Tracking structures
    std::vector<bool> receivedFlags;       // receivedFlags[seq] = true if received
    std::vector<char> reassemblyBuffer;    // Reconstructed data
    std::vector<int> receptionOrder;       // Order in which sequence numbers arrived
    std::vector<double> latencies;         // One-way latency per packet (ms)
    int packetsReceived = 0;
    int outOfOrderCount = 0;
    int highestSeqSeen = -1;

    // Receive buffer: header + max payload
    const int MAX_PACKET = UDP_HEADER_SIZE + MAX_UDP_PAYLOAD;
    char* recvBuf = new char[MAX_PACKET];

    sockaddr_in clientAddr;
    int clientAddrLen = sizeof(clientAddr);

    auto transferStart = std::chrono::high_resolution_clock::now();
    auto transferEnd   = transferStart;

    // ---- Main Receive Loop ----
    int consecutiveTimeouts = 0;
    const int MAX_TIMEOUTS = 3; // Exit after 3 consecutive timeouts

    while (!transferDone) {
        std::memset(&clientAddr, 0, sizeof(clientAddr));
        clientAddrLen = sizeof(clientAddr);

        int bytesRecv = recvfrom(sock, recvBuf, MAX_PACKET, 0,
                                  reinterpret_cast<sockaddr*>(&clientAddr),
                                  &clientAddrLen);

        if (bytesRecv == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT) {
                consecutiveTimeouts++;
                if (transferStarted && consecutiveTimeouts >= MAX_TIMEOUTS) {
                    std::cout << "[UDP Server] Timeout — assuming transfer complete.\n";
                    transferDone = true;
                }
                continue;
            }
            printWinsockError("recvfrom()");
            break;
        }

        consecutiveTimeouts = 0; // Reset on successful receive

        if (bytesRecv < UDP_HEADER_SIZE) {
            std::cerr << "[UDP Server] Received undersized packet (" << bytesRecv << " bytes), skipping.\n";
            continue;
        }

        // ---- Deserialize Header ----
        UdpPacketHeader hdr;
        deserializeUdpHeader(recvBuf, hdr);
        PacketType ptype = static_cast<PacketType>(hdr.packetType);

        if (ptype == PacketType::START) {
            // ---- START Packet ----
            /*
             * The START packet carries metadata in its payload as text:
             *   TOTAL_PACKETS|DATA_SIZE|CHUNK_SIZE|MODE|FILENAME
             */
            std::string meta(recvBuf + UDP_HEADER_SIZE, hdr.payloadSize);
            std::istringstream iss(meta);
            std::string tok;

            std::getline(iss, tok, '|');
            totalExpectedPackets = std::atoi(tok.c_str());
            std::getline(iss, tok, '|');
            totalExpectedBytes = std::atoll(tok.c_str());
            std::getline(iss, tok, '|');
            expectedChunkSize = std::atoi(tok.c_str());
            std::getline(iss, tok, '|');
            mode = (tok == "FILE") ? TransferMode::FILEXFER : TransferMode::SYNTHETIC;
            std::getline(iss, fileName, '|');

            // Initialize tracking
            receivedFlags.assign(totalExpectedPackets, false);
            reassemblyBuffer.resize(totalExpectedBytes, 0);

            transferStarted = true;
            transferStart = std::chrono::high_resolution_clock::now();

            std::cout << "[UDP Server] Transfer started. Expecting "
                      << totalExpectedPackets << " packets, "
                      << totalExpectedBytes << " bytes.\n";
        }
        else if (ptype == PacketType::DATA) {
            // ---- DATA Packet ----
            if (!transferStarted) {
                std::cerr << "[UDP Server] DATA packet before START, ignoring.\n";
                continue;
            }

            int seq = static_cast<int>(hdr.sequenceNumber);

            // Bounds check
            if (seq < 0 || seq >= totalExpectedPackets) {
                std::cerr << "[UDP Server] Sequence " << seq << " out of range, ignoring.\n";
                continue;
            }

            // Duplicate check
            if (receivedFlags[seq]) {
                continue; // Silently ignore duplicates
            }

            receivedFlags[seq] = true;
            receptionOrder.push_back(seq);
            packetsReceived++;

            // Out-of-order detection
            if (seq < highestSeqSeen) {
                outOfOrderCount++;
            }
            if (seq > highestSeqSeen) {
                highestSeqSeen = seq;
            }

            // Calculate one-way latency (localhost clock-based, NOT true network latency)
            int64_t nowUs = getCurrentTimestampUs();
            double latencyMs = static_cast<double>(nowUs - hdr.timestampUs) / 1000.0;
            if (latencyMs < 0) latencyMs = 0; // Guard against clock issues
            latencies.push_back(latencyMs);

            // Copy payload into reassembly buffer at the correct offset
            int64_t offset = static_cast<int64_t>(seq) * expectedChunkSize;
            int payloadLen = static_cast<int>(hdr.payloadSize);
            int copyLen = payloadLen;
            if (offset + copyLen > totalExpectedBytes) {
                copyLen = static_cast<int>(totalExpectedBytes - offset);
            }
            if (copyLen > 0 && offset >= 0 && offset < totalExpectedBytes) {
                std::memcpy(reassemblyBuffer.data() + offset,
                            recvBuf + UDP_HEADER_SIZE, copyLen);
            }

            // Progress report every 1000 packets
            if (packetsReceived % 1000 == 0) {
                std::cout << "[UDP Server] Received " << packetsReceived
                          << " / " << totalExpectedPackets << " packets\r" << std::flush;
            }
        }
        else if (ptype == PacketType::END) {
            // ---- END Packet ----
            transferEnd = std::chrono::high_resolution_clock::now();
            transferDone = true;
            std::cout << "\n[UDP Server] END packet received.\n";
        }
    }

    if (!transferDone) {
        transferEnd = std::chrono::high_resolution_clock::now();
    }

    delete[] recvBuf;

    double transferTimeSec = std::chrono::duration<double>(transferEnd - transferStart).count();

    // ---- Detect Missing Packets ----
    std::vector<int> missingPackets;
    for (int i = 0; i < totalExpectedPackets; i++) {
        if (!receivedFlags[i]) {
            missingPackets.push_back(i);
        }
    }

    int packetsLost = static_cast<int>(missingPackets.size());
    double lossPercent = 0.0;
    if (totalExpectedPackets > 0) {
        lossPercent = (static_cast<double>(packetsLost) / totalExpectedPackets) * 100.0;
    }

    // ---- Calculate Latency and Jitter ----
    double avgLatency = 0.0;
    double jitter = 0.0;
    if (!latencies.empty()) {
        double sum = 0.0;
        for (double l : latencies) sum += l;
        avgLatency = sum / latencies.size();
    }
    if (latencies.size() >= 2) {
        double jitterSum = 0.0;
        for (size_t i = 1; i < latencies.size(); i++) {
            jitterSum += std::fabs(latencies[i] - latencies[i - 1]);
        }
        jitter = jitterSum / static_cast<double>(latencies.size() - 1);
    }

    // ---- Display Summary ----
    std::cout << "\n[UDP Server] ---- Transfer Summary ----\n";
    std::cout << "  Expected packets : " << totalExpectedPackets << "\n";
    std::cout << "  Received packets : " << packetsReceived << "\n";
    std::cout << "  Lost packets     : " << packetsLost << "\n";
    std::cout << "  Packet loss      : " << std::fixed << std::setprecision(2)
              << lossPercent << " %\n";
    std::cout << "  Out-of-order     : " << outOfOrderCount << "\n";
    std::cout << "  Transfer time    : " << std::setprecision(4)
              << transferTimeSec << " sec\n";
    std::cout << "  Avg latency      : " << std::setprecision(4)
              << avgLatency << " ms (localhost clock-based)\n";
    std::cout << "  Jitter           : " << std::setprecision(4)
              << jitter << " ms\n";

    if (!missingPackets.empty()) {
        std::cout << "  Missing seq#     : ";
        int showMax = 20; // Show at most 20 missing sequence numbers
        for (int i = 0; i < std::min(static_cast<int>(missingPackets.size()), showMax); i++) {
            if (i > 0) std::cout << ", ";
            std::cout << missingPackets[i];
        }
        if (static_cast<int>(missingPackets.size()) > showMax) {
            std::cout << " ... (" << missingPackets.size() << " total)";
        }
        std::cout << "\n";
    }

    // ---- Save Received File ----
    std::string receivedFilePath;
    if (mode == TransferMode::FILEXFER && packetsReceived > 0) {
        receivedFilePath = "results/received_" + fileName;
        std::ofstream outFile(receivedFilePath, std::ios::binary);
        if (outFile.is_open()) {
            outFile.write(reassemblyBuffer.data(), totalExpectedBytes);
            outFile.close();
            std::cout << "[UDP Server] File saved: " << receivedFilePath << "\n";
        }
    }

    // ---- Write Result File ----
    std::vector<std::pair<std::string, std::string>> kvs;
    kvs.push_back({"packets_received",   std::to_string(packetsReceived)});
    kvs.push_back({"packets_lost",       std::to_string(packetsLost)});
    kvs.push_back({"loss_percent",       std::to_string(lossPercent)});
    kvs.push_back({"out_of_order",       std::to_string(outOfOrderCount)});
    kvs.push_back({"bytes_received",     std::to_string(totalExpectedBytes)});
    kvs.push_back({"transfer_time_sec",  std::to_string(transferTimeSec)});
    kvs.push_back({"avg_latency_ms",     std::to_string(avgLatency)});
    kvs.push_back({"jitter_ms",          std::to_string(jitter)});
    kvs.push_back({"received_file",      receivedFilePath});
    writeResultFile(UDP_SERVER_RESULT_FILE, kvs);

    // ---- Send ACK back to client ----
    if (transferStarted) {
        UdpPacketHeader ackHdr;
        std::memset(&ackHdr, 0, sizeof(ackHdr));
        ackHdr.packetType = static_cast<uint8_t>(PacketType::ACK);
        ackHdr.sequenceNumber = static_cast<uint32_t>(packetsReceived);
        ackHdr.payloadSize = 0;
        ackHdr.timestampUs = getCurrentTimestampUs();
        ackHdr.totalPackets = static_cast<uint32_t>(totalExpectedPackets);

        char ackBuf[UDP_HEADER_SIZE];
        serializeUdpHeader(ackHdr, ackBuf);

        sendto(sock, ackBuf, UDP_HEADER_SIZE, 0,
               reinterpret_cast<sockaddr*>(&clientAddr), sizeof(clientAddr));
    }

    // ---- Cleanup ----
    closesocket(sock);
    WSACleanup();

    std::cout << "[UDP Server] Done.\n";
    return 0;
}
