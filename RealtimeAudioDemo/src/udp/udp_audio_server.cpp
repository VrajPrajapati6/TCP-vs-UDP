#include "../common/audio_common.h"
#include "../audio/audio_player.h"

int main(int argc, char* argv[]) {
    int port = UDP_AUDIO_PORT;
    if (argc >= 2) {
        port = std::atoi(argv[1]);
    }

    if (!initWinsock()) return 1;

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        printWinsockError("socket(UDP Server)");
        WSACleanup();
        return 1;
    }

    int optval = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&optval), sizeof(optval));

    // Increase UDP receive buffer size to 1MB to prevent OS dropping datagrams under burst
    int rcvBufSize = 1024 * 1024;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF,
               reinterpret_cast<const char*>(&rcvBufSize), sizeof(rcvBufSize));

    // Set socket receive timeout to 8 seconds
    DWORD timeout = AUDIO_SOCK_TIMEOUT;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout), sizeof(timeout));

    sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family      = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port        = htons(port);

    if (bind(sock, reinterpret_cast<sockaddr*>(&serverAddr),
             sizeof(serverAddr)) == SOCKET_ERROR) {
        printWinsockError("bind(UDP Server)");
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    std::cout << "========================================================\n";
    std::cout << "            UDP REAL-TIME AUDIO RECEIVER                \n";
    std::cout << "========================================================\n";
    std::cout << "[UDP Server] Listening for datagrams on UDP port " << port << " ...\n";
    std::cout << "[UDP Server] Waiting for START control packet from client...\n";

    sockaddr_in senderAddr;
    int senderAddrLen = sizeof(senderAddr);
    std::vector<char> recvBuffer(65536);

    AudioMetadata meta;
    std::memset(&meta, 0, sizeof(meta));
    bool streamStarted = false;

    // Wait for START packet
    while (!streamStarted) {
        int bytes = recvfrom(sock, recvBuffer.data(), static_cast<int>(recvBuffer.size()), 0,
                             reinterpret_cast<sockaddr*>(&senderAddr), &senderAddrLen);
        if (bytes == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT) {
                std::cout << "[UDP Server] Waiting for audio sender...\n";
                continue;
            }
            printWinsockError("recvfrom(START)");
            closesocket(sock);
            WSACleanup();
            return 1;
        }

        if (bytes < UDP_AUDIO_HDR_SIZE + AUDIO_METADATA_SIZE) {
            continue;
        }

        UdpAudioHeader hdr;
        deserializeUdpAudioHeader(recvBuffer.data(), hdr);

        if (hdr.packetType == static_cast<uint8_t>(AudioPacketType::START)) {
            deserializeAudioMetadata(recvBuffer.data() + UDP_AUDIO_HDR_SIZE, meta);
            streamStarted = true;
            char* clientIpStr = inet_ntoa(senderAddr.sin_addr);
            std::cout << "[UDP Server] Received START signal from " << clientIpStr
                      << ":" << ntohs(senderAddr.sin_port) << "\n";
        }
    }

    std::cout << "\n--------------------------------------------------------\n";
    std::cout << "  UDP AUDIO STREAM SPECIFICATIONS\n";
    std::cout << "--------------------------------------------------------\n";
    std::cout << "  Sample Rate     : " << meta.sampleRate << " Hz\n";
    std::cout << "  Channels        : " << meta.channels << " (Mono)\n";
    std::cout << "  Bits Per Sample : " << meta.bitsPerSample << "-bit PCM\n";
    std::cout << "  Total Audio Size: " << meta.totalBytes << " bytes ("
              << std::fixed << std::setprecision(2)
              << (static_cast<double>(meta.totalBytes) / (1024.0 * 1024.0)) << " MB)\n";
    std::cout << "  Frame Size      : " << meta.frameSize << " bytes (" << meta.frameDurationMs << " ms)\n";
    std::cout << "  Expected Frames : " << meta.totalFrames << "\n";
    std::cout << "  Transport       : UDP (Unreliable Datagram Stream)\n";
    std::cout << "  Loss Handling   : Real-time Concealment / Audible Gaps\n";
    std::cout << "--------------------------------------------------------\n";
    std::cout << "[UDP Server] Initializing Windows waveOut audio player...\n";

    AudioPlayer player;
    if (!player.init(meta.channels, meta.sampleRate, meta.bitsPerSample, 64)) {
        std::cerr << "[UDP Server] Failed to initialize Windows audio device.\n";
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    std::cout << "[UDP Server] Audio device ready. Playing stream in real time...\n\n";

    uint32_t expectedSeq = 1;
    uint32_t packetsReceived = 0;
    uint32_t packetsLost = 0;
    uint32_t totalBytesReceived = 0;
    uint32_t outOfOrderCount = 0;

    // Prepare a silence frame for missing packet concealment/gap insertion
    std::vector<char> silenceFrame(meta.frameSize, 0);

    auto streamStart = std::chrono::high_resolution_clock::now();

    while (true) {
        int bytes = recvfrom(sock, recvBuffer.data(), static_cast<int>(recvBuffer.size()), 0,
                             reinterpret_cast<sockaddr*>(&senderAddr), &senderAddrLen);
        if (bytes == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT) {
                std::cout << "\n[UDP Server] Stream timed out (no data for 8s). Ending session.\n";
                break;
            }
            printWinsockError("recvfrom(DATA)");
            break;
        }

        if (bytes < UDP_AUDIO_HDR_SIZE) {
            continue;
        }

        UdpAudioHeader hdr;
        deserializeUdpAudioHeader(recvBuffer.data(), hdr);

        if (hdr.packetType == static_cast<uint8_t>(AudioPacketType::END)) {
            std::cout << "\n[UDP Server] Received END packet from sender.\n";
            break;
        }

        if (hdr.packetType == static_cast<uint8_t>(AudioPacketType::DATA)) {
            packetsReceived++;
            totalBytesReceived += hdr.payloadSize;

            // Packet Loss Detection via Sequence Number Gap
            if (hdr.sequenceNumber > expectedSeq) {
                uint32_t missing = hdr.sequenceNumber - expectedSeq;
                packetsLost += missing;

                // For real-time playback, audio cannot pause to retransmit missing datagrams.
                // Insert a silence/gap frame corresponding to the missing duration
                // so playback timing remains aligned with real-time progression.
                for (uint32_t m = 0; m < missing && m < 10; ++m) {
                    player.writeFrame(silenceFrame.data(), static_cast<uint32_t>(silenceFrame.size()));
                }
                expectedSeq = hdr.sequenceNumber + 1;
            } else if (hdr.sequenceNumber < expectedSeq) {
                // Out-of-order or duplicate datagram
                outOfOrderCount++;
            } else {
                expectedSeq++;
            }

            // Write actual received PCM audio payload to the live audio playback buffer
            const char* audioPayload = recvBuffer.data() + UDP_AUDIO_HDR_SIZE;
            player.writeFrame(audioPayload, hdr.payloadSize);

            if (packetsReceived % 25 == 0 || packetsReceived == meta.totalFrames) {
                uint32_t totalObserved = packetsReceived + packetsLost;
                double lossPct = (totalObserved > 0) ? (static_cast<double>(packetsLost) * 100.0 / totalObserved) : 0.0;

                std::cout << "  [UDP Receiver] Received: " << packetsReceived
                          << " | Lost: " << packetsLost << " (" << std::fixed << std::setprecision(1) << lossPct << "%)"
                          << " | Out-of-order: " << outOfOrderCount
                          << " | Playback: ACTIVE \r" << std::flush;
            }
        }
    }

    auto streamEnd = std::chrono::high_resolution_clock::now();
    double streamSec = std::chrono::duration<double>(streamEnd - streamStart).count();

    std::cout << "\n[UDP Server] Finalizing audio playback...\n";
    player.flush();
    player.close();

    uint32_t totalSentEstimated = packetsReceived + packetsLost;
    double finalLossRate = (totalSentEstimated > 0) ?
                           (static_cast<double>(packetsLost) * 100.0 / totalSentEstimated) : 0.0;

    std::cout << "========================================================\n";
    std::cout << "              UDP AUDIO STREAM COMPLETED                \n";
    std::cout << "========================================================\n";
    std::cout << "  Packets Received    : " << packetsReceived << "\n";
    std::cout << "  Packets Lost (Gaps) : " << packetsLost << "\n";
    std::cout << "  Total Packets Sent  : " << totalSentEstimated << "\n";
    std::cout << "  Observed Loss Rate  : " << std::fixed << std::setprecision(2) << finalLossRate << "%\n";
    std::cout << "  Out-of-order Packets: " << outOfOrderCount << "\n";
    std::cout << "  Total Audio Received: " << (totalBytesReceived / 1024) << " KB\n";
    std::cout << "  Stream Duration     : " << std::fixed << std::setprecision(2) << streamSec << " seconds\n";
    std::cout << "  Real-time Status    : Stream completed without waiting for missing data\n";
    std::cout << "========================================================\n\n";

    closesocket(sock);
    WSACleanup();
    return 0;
}
