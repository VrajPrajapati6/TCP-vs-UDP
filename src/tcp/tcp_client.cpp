#include "../../include/common.h"

static void printUsage() {
    std::cerr << "Usage: tcp_client.exe <SYNTHETIC|FILE> <data_size> <chunk_size> [file_path]\n";
}

int main(int argc, char* argv[]) {
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

    std::string fileName = "NONE";
    if (mode == TransferMode::FILEXFER) {
        size_t pos = filePath.find_last_of("/\\");
        fileName = (pos != std::string::npos) ? filePath.substr(pos + 1) : filePath;
    }

    if (!initWinsock()) return 1;

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        printWinsockError("socket()");
        WSACleanup();
        return 1;
    }

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

    std::string header = buildTcpControlHeader(mode, dataSize, chunkSize, fileName);
    if (sendAll(sock, header.c_str(), static_cast<int>(header.size())) == SOCKET_ERROR) {
        printWinsockError("sendAll(header)");
        closesocket(sock);
        WSACleanup();
        return 1;
    }

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

    std::cout << "RESULT|" << packetsSent << "|"
              << std::fixed << std::setprecision(6) << transmissionTimeSec << "|"
              << bytesSent << std::endl;

    shutdown(sock, SD_SEND);
    Sleep(200);

    closesocket(sock);
    WSACleanup();

    std::cerr << "[TCP Client] Done.\n";
    return 0;
}
