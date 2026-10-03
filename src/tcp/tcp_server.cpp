/*
 * tcp_server.cpp - NetPulse TCP Server
 *
 * This server:
 *  1. Initializes Winsock and creates a TCP socket.
 *  2. Binds to TCP_PORT and listens for exactly one client.
 *  3. Reads a control header to learn transfer mode, data size, chunk size.
 *  4. Receives the complete byte stream, handling TCP's stream nature
 *     (does NOT assume one recv() == one send()).
 *  5. Measures timing and latency per chunk.
 *  6. Writes metrics to a result file for the orchestrator (main.exe).
 *  7. In file-transfer mode, saves the received data to disk.
 *
 * Compilation:
 *   g++ src/tcp/tcp_server.cpp -o tcp_server.exe -lws2_32 -std=c++17
 *
 * Usage:
 *   tcp_server.exe                     (waits for one transfer, then exits)
 */

#include "../../include/common.h"

// Read bytes from socket until we find "\r\n\r\n" (header delimiter)
static bool readControlHeader(SOCKET client, std::string& header) {
    header.clear();
    char byte;
    // Read one byte at a time until delimiter found (header is short)
    while (true) {
        int r = recv(client, &byte, 1, 0);
        if (r <= 0) return false;
        header.push_back(byte);
        if (header.size() >= 4) {
            std::string tail = header.substr(header.size() - 4);
            if (tail == "\r\n\r\n") return true;
        }
        if (header.size() > 4096) return false; // Safety limit
    }
}

int main() {
    // ---- Winsock Init ----
    if (!initWinsock()) return 1;

    // ---- Create TCP Socket ----
    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET) {
        printWinsockError("socket()");
        WSACleanup();
        return 1;
    }

    // Allow address reuse to avoid "address already in use" errors
    int optval = 1;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&optval), sizeof(optval));

    // ---- Bind ----
    sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family      = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port        = htons(TCP_PORT);

    if (bind(listenSock, reinterpret_cast<sockaddr*>(&serverAddr),
             sizeof(serverAddr)) == SOCKET_ERROR) {
        printWinsockError("bind()");
        closesocket(listenSock);
        WSACleanup();
        return 1;
    }

    // ---- Listen ----
    if (listen(listenSock, BACKLOG) == SOCKET_ERROR) {
        printWinsockError("listen()");
        closesocket(listenSock);
        WSACleanup();
        return 1;
    }

    std::cout << "[TCP Server] Listening on port " << TCP_PORT << " ...\n";

    // ---- Accept one client ----
    sockaddr_in clientAddr;
    int addrLen = sizeof(clientAddr);
    SOCKET clientSock = accept(listenSock,
                               reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
    if (clientSock == INVALID_SOCKET) {
        printWinsockError("accept()");
        closesocket(listenSock);
        WSACleanup();
        return 1;
    }

    std::cout << "[TCP Server] Client connected.\n";

    // ---- Read Control Header ----
    std::string headerStr;
    if (!readControlHeader(clientSock, headerStr)) {
        std::cerr << "[TCP Server] Failed to read control header.\n";
        closesocket(clientSock);
        closesocket(listenSock);
        WSACleanup();
        return 1;
    }

    TransferMode mode;
    int64_t dataSize;
    int chunkSize;
    std::string fileName;

    if (!parseTcpControlHeader(headerStr, mode, dataSize, chunkSize, fileName)) {
        std::cerr << "[TCP Server] Invalid control header.\n";
        closesocket(clientSock);
        closesocket(listenSock);
        WSACleanup();
        return 1;
    }

    std::cout << "[TCP Server] Mode: "
              << (mode == TransferMode::SYNTHETIC ? "SYNTHETIC" : "FILE")
              << "  Size: " << dataSize << " bytes"
              << "  Chunk: " << chunkSize << " bytes\n";

    // ---- Receive Data ----
    /*
     * TCP is a byte stream — we simply receive until we have
     * exactly dataSize bytes.  We track how many "chunks" worth
     * of data arrive and record per-chunk timing for latency
     * calculation.
     *
     * IMPORTANT: recv() may return fewer bytes than requested;
     * that is normal TCP behavior, NOT an error.
     */

    std::vector<char> receiveBuffer(dataSize);
    std::vector<double> chunkLatencies;  // milliseconds per logical chunk
    int64_t totalReceived = 0;
    int chunksReceived = 0;

    auto transferStart = std::chrono::high_resolution_clock::now();
    auto lastChunkTime = transferStart;

    char* recvBuf = new char[chunkSize];

    while (totalReceived < dataSize) {
        int toRead = static_cast<int>(
            std::min(static_cast<int64_t>(chunkSize), dataSize - totalReceived));

        int r = recv(clientSock, recvBuf, toRead, 0);
        if (r == SOCKET_ERROR) {
            printWinsockError("recv()");
            break;
        }
        if (r == 0) {
            std::cout << "[TCP Server] Connection closed by client.\n";
            break;
        }

        // Copy received bytes into buffer
        std::memcpy(receiveBuffer.data() + totalReceived, recvBuf, r);
        totalReceived += r;

        // Track per-chunk latency (time between consecutive recv completions)
        auto now = std::chrono::high_resolution_clock::now();
        double chunkMs = std::chrono::duration<double, std::milli>(now - lastChunkTime).count();
        chunkLatencies.push_back(chunkMs);
        lastChunkTime = now;
        chunksReceived++;
    }

    auto transferEnd = std::chrono::high_resolution_clock::now();
    double transferTimeSec = std::chrono::duration<double>(transferEnd - transferStart).count();

    delete[] recvBuf;

    std::cout << "[TCP Server] Received " << totalReceived << " / " << dataSize << " bytes"
              << " in " << chunksReceived << " chunks.\n";
    std::cout << "[TCP Server] Transfer time: " << std::fixed << std::setprecision(4)
              << transferTimeSec << " sec\n";

    // ---- Calculate latency & jitter ----
    double avgLatency = 0.0;
    double jitter = 0.0;
    if (!chunkLatencies.empty()) {
        // Skip the first sample (includes connection overhead)
        std::vector<double> trimmed;
        if (chunkLatencies.size() > 1) {
            trimmed.assign(chunkLatencies.begin() + 1, chunkLatencies.end());
        } else {
            trimmed = chunkLatencies;
        }
        double sum = 0.0;
        for (double l : trimmed) sum += l;
        avgLatency = sum / trimmed.size();

        // Jitter = mean of |D(i) - D(i-1)| for consecutive inter-chunk delays
        if (trimmed.size() >= 2) {
            double jitterSum = 0.0;
            for (size_t i = 1; i < trimmed.size(); i++) {
                jitterSum += std::fabs(trimmed[i] - trimmed[i - 1]);
            }
            jitter = jitterSum / static_cast<double>(trimmed.size() - 1);
        }
    }

    // ---- Save received file if file-transfer mode ----
    std::string receivedFilePath;
    if (mode == TransferMode::FILEXFER && totalReceived > 0) {
        receivedFilePath = "results/received_" + fileName;
        std::ofstream outFile(receivedFilePath, std::ios::binary);
        if (outFile.is_open()) {
            outFile.write(receiveBuffer.data(), totalReceived);
            outFile.close();
            std::cout << "[TCP Server] File saved: " << receivedFilePath << "\n";
        } else {
            std::cerr << "[TCP Server] Failed to save received file.\n";
        }
    }

    // ---- Write result file for orchestrator ----
    /*
     * The total number of logical packets for TCP is computed by the client
     * (it knows how many send() calls it made). The server reports:
     *   - chunks_received (recv calls that returned data)
     *   - total bytes received
     *   - latency / jitter
     *   - transfer time
     *   - received file path
     */
    int logicalPackets = static_cast<int>(
        (totalReceived + chunkSize - 1) / chunkSize);

    std::vector<std::pair<std::string, std::string>> kvs;
    kvs.push_back({"packets_received",   std::to_string(logicalPackets)});
    kvs.push_back({"bytes_received",     std::to_string(totalReceived)});
    kvs.push_back({"transfer_time_sec",  std::to_string(transferTimeSec)});
    kvs.push_back({"avg_latency_ms",     std::to_string(avgLatency)});
    kvs.push_back({"jitter_ms",          std::to_string(jitter)});
    kvs.push_back({"out_of_order",       "0"}); // TCP guarantees ordering
    kvs.push_back({"received_file",      receivedFilePath});
    writeResultFile(TCP_SERVER_RESULT_FILE, kvs);

    // ---- Cleanup ----
    closesocket(clientSock);
    closesocket(listenSock);
    WSACleanup();

    std::cout << "[TCP Server] Done.\n";
    return 0;
}
