/*
 * tcp_client.cpp - NetPulse TCP Client
 *
 * This client:
 *  1. Initializes Winsock and connects to the TCP server on TCP_PORT.
 *  2. Sends a control header describing the transfer (mode, size, chunk size).
 *  3. Generates synthetic data OR reads a file, then sends it in chunks
 *     using sendAll() to handle partial send() calls.
 *  4. Measures total transmission time.
 *  5. Reports packets sent and timing to stdout (read by main.exe).
 *
 * Compilation:
 *   g++ src/tcp/tcp_client.cpp -o tcp_client.exe -lws2_32 -std=c++17
 *
 * Usage:
 *   tcp_client.exe <mode> <data_size> <chunk_size> [file_path]
 *
 *   mode       : SYNTHETIC or FILE
 *   data_size  : total bytes (ignored for FILE mode, auto-detected)
 *   chunk_size : bytes per send chunk
 *   file_path  : path to file (FILE mode only)
 *
 * Output (to stdout, parsed by main.exe):
 *   RESULT|packets_sent|transmission_time_sec|total_bytes
 */

#include "../../include/common.h"

static void printUsage() {
    std::cerr << "Usage: tcp_client.exe <SYNTHETIC|FILE> <data_size> <chunk_size> [file_path]\n";
}

int main(int argc, char* argv[]) {
    // ---- Parse Arguments ----
    if (argc < 4) {
        printUsage();
        return 1;
    }

    std::string modeStr   = argv[1];
    int64_t dataSize      = std::atoll(argv[2]);
    int chunkSize         = std::atoi(argv[3]);
    std::string filePath  = (argc >= 5) ? argv[4] : "";

    TransferMode mode;
    if (modeStr == "FILE") {
        mode = TransferMode::FILEXFER;
    } else {
        mode = TransferMode::SYNTHETIC;
    }

    if (chunkSize < MIN_CHUNK_SIZE) chunkSize = MIN_CHUNK_SIZE;
    if (chunkSize > MAX_CHUNK_SIZE) chunkSize = MAX_CHUNK_SIZE;

    // ---- Prepare Data ----
    std::vector<char> data;
    if (mode == TransferMode::FILEXFER) {
        if (filePath.empty()) {
            std::cerr << "[TCP Client] FILE mode requires a file path.\n";
            return 1;
        }
        if (!readFileToVector(filePath, data)) {
            return 1;
        }
        dataSize = static_cast<int64_t>(data.size());
    } else {
        if (dataSize <= 0) {
            std::cerr << "[TCP Client] Invalid data size.\n";
            return 1;
        }
        data = generateSyntheticData(dataSize);
    }

    // Extract just the filename for the header
    std::string fileName = "NONE";
    if (mode == TransferMode::FILEXFER) {
        size_t pos = filePath.find_last_of("/\\");
        fileName = (pos != std::string::npos) ? filePath.substr(pos + 1) : filePath;
    }

    // ---- Winsock Init ----
    if (!initWinsock()) return 1;

    // ---- Create TCP Socket ----
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        printWinsockError("socket()");
        WSACleanup();
        return 1;
    }

    // ---- Connect to Server ----
    sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port   = htons(TCP_PORT);
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP);

    std::cerr << "[TCP Client] Connecting to " << SERVER_IP << ":" << TCP_PORT << " ...\n";

    if (connect(sock, reinterpret_cast<sockaddr*>(&serverAddr),
                sizeof(serverAddr)) == SOCKET_ERROR) {
        printWinsockError("connect()");
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    std::cerr << "[TCP Client] Connected.\n";

    // ---- Send Control Header ----
    std::string header = buildTcpControlHeader(mode, dataSize, chunkSize, fileName);
    if (sendAll(sock, header.c_str(), static_cast<int>(header.size())) == SOCKET_ERROR) {
        printWinsockError("sendAll(header)");
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    // ---- Send Data in Chunks ----
    /*
     * We divide the data into chunks and call sendAll() for each.
     * sendAll() handles partial send() returns, ensuring all bytes
     * of a chunk are transmitted before moving on.
     *
     * packetsSent counts the number of complete chunks transmitted.
     */
    int packetsSent = 0;
    int64_t bytesSent = 0;

    std::cerr << "[TCP Client] Sending " << dataSize << " bytes in "
              << chunkSize << "-byte chunks...\n";

    auto startTime = std::chrono::high_resolution_clock::now();

    while (bytesSent < dataSize) {
        int toSend = static_cast<int>(
            std::min(static_cast<int64_t>(chunkSize), dataSize - bytesSent));

        int sent = sendAll(sock, data.data() + bytesSent, toSend);
        if (sent == SOCKET_ERROR) {
            printWinsockError("sendAll(data)");
            break;
        }
        bytesSent += sent;
        packetsSent++;
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    double transmissionTimeSec = std::chrono::duration<double>(endTime - startTime).count();

    std::cerr << "[TCP Client] Sent " << bytesSent << " bytes in "
              << packetsSent << " chunks.\n";
    std::cerr << "[TCP Client] Transmission time: " << std::fixed
              << std::setprecision(4) << transmissionTimeSec << " sec\n";

    // ---- Output result line to stdout (parsed by main.exe) ----
    std::cout << "RESULT|" << packetsSent << "|"
              << std::fixed << std::setprecision(6) << transmissionTimeSec << "|"
              << bytesSent << std::endl;

    // ---- Cleanup ----
    // Graceful shutdown: signal end of transmission
    shutdown(sock, SD_SEND);
    // Brief wait for server to finish receiving
    Sleep(200);

    closesocket(sock);
    WSACleanup();

    std::cerr << "[TCP Client] Done.\n";
    return 0;
}
