#include "../../include/common.h"

static void printUsage() {
    std::cerr << "Usage: udp_client.exe <SYNTHETIC|FILE> <data_size> <chunk_size>"
              << " <loss_rate> <delay_ms> [file_path]\n";
}

int main(int argc, char* argv[]) {
    if (argc < 6) {
        printUsage();
        return 1;
    }

    std::string modeStr   = argv[1];
    int64_t dataSize      = std::atoll(argv[2]);
    int chunkSize         = std::atoi(argv[3]);
    double lossRate       = std::atof(argv[4]);
    int delayMs           = std::atoi(argv[5]);
    std::string filePath  = (argc >= 7) ? argv[6] : "";

    TransferMode mode = (modeStr == "FILE") ? TransferMode::FILEXFER
                                             : TransferMode::SYNTHETIC;

    if (chunkSize < MIN_CHUNK_SIZE) chunkSize = MIN_CHUNK_SIZE;
    if (chunkSize > MAX_UDP_PAYLOAD) chunkSize = MAX_UDP_PAYLOAD;
    if (lossRate < 0.0) lossRate = 0.0;
    if (lossRate > 1.0) lossRate = 1.0;

    std::vector<char> data;
    if (mode == TransferMode::FILEXFER) {
        if (filePath.empty()) {
            std::cerr << "[UDP Client] FILE mode requires a file path.\n";
            return 1;
        }
        if (!readFileToVector(filePath, data)) return 1;
        dataSize = static_cast<int64_t>(data.size());
    } else {
        if (dataSize <= 0) {
            std::cerr << "[UDP Client] Invalid data size.\n";
            return 1;
        }
        data = generateSyntheticData(dataSize);
    }

    std::string fileName = "NONE";
    if (mode == TransferMode::FILEXFER) {
        size_t pos = filePath.find_last_of("/\\");
        fileName = (pos != std::string::npos) ? filePath.substr(pos + 1) : filePath;
    }

    int totalPackets = static_cast<int>((dataSize + chunkSize - 1) / chunkSize);

    if (!initWinsock()) return 1;

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        printWinsockError("socket()");
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port   = htons(UDP_PORT);
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP);

    std::srand(static_cast<unsigned>(std::time(nullptr)));

    {
        std::ostringstream meta;
        meta << totalPackets << "|" << dataSize << "|" << chunkSize << "|"
             << (mode == TransferMode::FILEXFER ? "FILE" : "SYNTHETIC") << "|"
             << fileName;
        std::string metaStr = meta.str();

        UdpPacketHeader startHdr;
        std::memset(&startHdr, 0, sizeof(startHdr));
        startHdr.sequenceNumber = 0;
        startHdr.payloadSize    = static_cast<uint32_t>(metaStr.size());
        startHdr.timestampUs    = getCurrentTimestampUs();
        startHdr.packetType     = static_cast<uint8_t>(PacketType::START);
        startHdr.totalPackets   = static_cast<uint32_t>(totalPackets);

        std::vector<char> startBuf(UDP_HEADER_SIZE + metaStr.size());
        serializeUdpHeader(startHdr, startBuf.data());
        std::memcpy(startBuf.data() + UDP_HEADER_SIZE, metaStr.c_str(), metaStr.size());

        int sent = sendto(sock, startBuf.data(), static_cast<int>(startBuf.size()), 0,
                          reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr));
        if (sent == SOCKET_ERROR) {
            printWinsockError("sendto(START)");
            closesocket(sock);
            WSACleanup();
            return 1;
        }
    }

    Sleep(50);

    std::cerr << "[UDP Client] Sending " << totalPackets << " packets ("
              << dataSize << " bytes, " << chunkSize << " bytes/packet)\n";

    if (lossRate > 0.0) {
        std::cerr << "[UDP Client] Simulated application-level packet loss: "
                  << std::fixed << std::setprecision(1) << (lossRate * 100.0) << "%\n";
    }
    if (delayMs > 0) {
        std::cerr << "[UDP Client] Artificial per-packet delay: " << delayMs << " ms\n";
    }

    int packetsSent = 0;
    int packetsSkipped = 0;

    std::vector<char> sendBuf(UDP_HEADER_SIZE + chunkSize);

    auto startTime = std::chrono::high_resolution_clock::now();

    for (int seq = 0; seq < totalPackets; seq++) {
        if (lossRate > 0.0) {
            double r = static_cast<double>(std::rand()) / RAND_MAX;
            if (r < lossRate) {
                packetsSkipped++;
                continue;
            }
        }

        int64_t offset = static_cast<int64_t>(seq) * chunkSize;
        int payloadLen = static_cast<int>(
            std::min(static_cast<int64_t>(chunkSize), dataSize - offset));

        UdpPacketHeader hdr;
        std::memset(&hdr, 0, sizeof(hdr));
        hdr.sequenceNumber = static_cast<uint32_t>(seq);
        hdr.payloadSize    = static_cast<uint32_t>(payloadLen);
        hdr.timestampUs    = getCurrentTimestampUs();
        hdr.packetType     = static_cast<uint8_t>(PacketType::DATA);
        hdr.totalPackets   = static_cast<uint32_t>(totalPackets);

        serializeUdpHeader(hdr, sendBuf.data());
        std::memcpy(sendBuf.data() + UDP_HEADER_SIZE, data.data() + offset, payloadLen);

        int totalLen = UDP_HEADER_SIZE + payloadLen;
        int sent = sendto(sock, sendBuf.data(), totalLen, 0,
                          reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr));
        if (sent == SOCKET_ERROR) {
            printWinsockError("sendto(DATA)");
            break;
        }

        packetsSent++;

        if (delayMs > 0) {
            Sleep(delayMs);
        }

        if ((seq + 1) % 1000 == 0 || seq == totalPackets - 1) {
            std::cerr << "[UDP Client] Sent " << (seq + 1) << " / "
                      << totalPackets << " packets\r" << std::flush;
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    double transmissionTimeSec = std::chrono::duration<double>(endTime - startTime).count();

    std::cerr << "\n[UDP Client] Sent: " << packetsSent
              << "  Skipped (simulated loss): " << packetsSkipped << "\n";
    std::cerr << "[UDP Client] Transmission time: " << std::fixed
              << std::setprecision(4) << transmissionTimeSec << " sec\n";

    Sleep(50);
    {
        UdpPacketHeader endHdr;
        std::memset(&endHdr, 0, sizeof(endHdr));
        endHdr.sequenceNumber = static_cast<uint32_t>(totalPackets);
        endHdr.payloadSize    = 0;
        endHdr.timestampUs    = getCurrentTimestampUs();
        endHdr.packetType     = static_cast<uint8_t>(PacketType::END);
        endHdr.totalPackets   = static_cast<uint32_t>(totalPackets);

        char endBuf[UDP_HEADER_SIZE];
        serializeUdpHeader(endHdr, endBuf);
        sendto(sock, endBuf, UDP_HEADER_SIZE, 0,
               reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr));
    }

    {
        int ackTimeout = 3000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&ackTimeout), sizeof(ackTimeout));

        char ackBuf[UDP_HEADER_SIZE];
        sockaddr_in fromAddr;
        int fromLen = sizeof(fromAddr);
        int ackRecv = recvfrom(sock, ackBuf, UDP_HEADER_SIZE, 0,
                                reinterpret_cast<sockaddr*>(&fromAddr), &fromLen);
        if (ackRecv >= UDP_HEADER_SIZE) {
            UdpPacketHeader ackHdr;
            deserializeUdpHeader(ackBuf, ackHdr);
            if (static_cast<PacketType>(ackHdr.packetType) == PacketType::ACK) {
                std::cerr << "[UDP Client] ACK received. Server got "
                          << ackHdr.sequenceNumber << " packets.\n";
            }
        } else {
            std::cerr << "[UDP Client] No ACK received (timeout).\n";
        }
    }

    std::cout << "RESULT|" << packetsSent << "|" << totalPackets << "|"
              << std::fixed << std::setprecision(6) << transmissionTimeSec << "|"
              << dataSize << std::endl;

    closesocket(sock);
    WSACleanup();

    std::cerr << "[UDP Client] Done.\n";
    return 0;
}
