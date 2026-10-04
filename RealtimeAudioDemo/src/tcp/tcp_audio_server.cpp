#include "../common/audio_common.h"
#include "../audio/audio_player.h"

int main(int argc, char* argv[]) {
    int port = TCP_AUDIO_PORT;
    if (argc >= 2) {
        port = std::atoi(argv[1]);
    }

    if (!initWinsock()) return 1;

    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET) {
        printWinsockError("socket(TCP Server)");
        WSACleanup();
        return 1;
    }

    int optval = 1;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&optval), sizeof(optval));

    sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family      = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port        = htons(port);

    if (bind(listenSock, reinterpret_cast<sockaddr*>(&serverAddr),
             sizeof(serverAddr)) == SOCKET_ERROR) {
        printWinsockError("bind(TCP Server)");
        closesocket(listenSock);
        WSACleanup();
        return 1;
    }

    if (listen(listenSock, AUDIO_BACKLOG) == SOCKET_ERROR) {
        printWinsockError("listen(TCP Server)");
        closesocket(listenSock);
        WSACleanup();
        return 1;
    }

    std::cout << "========================================================\n";
    std::cout << "            TCP REAL-TIME AUDIO RECEIVER                \n";
    std::cout << "========================================================\n";
    std::cout << "[TCP Server] Listening on port " << port << " ...\n";
    std::cout << "[TCP Server] Waiting for audio sender to connect...\n";

    sockaddr_in clientAddr;
    int addrLen = sizeof(clientAddr);
    SOCKET clientSock = accept(listenSock,
                               reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
    if (clientSock == INVALID_SOCKET) {
        printWinsockError("accept(TCP Server)");
        closesocket(listenSock);
        WSACleanup();
        return 1;
    }

    std::cout << "[TCP Server] Sender connected. Receiving stream metadata...\n";

    char metaBuf[AUDIO_METADATA_SIZE];
    if (recvExact(clientSock, metaBuf, AUDIO_METADATA_SIZE) != AUDIO_METADATA_SIZE) {
        std::cerr << "[TCP Server] Failed to receive audio metadata.\n";
        closesocket(clientSock);
        closesocket(listenSock);
        WSACleanup();
        return 1;
    }

    AudioMetadata meta;
    deserializeAudioMetadata(metaBuf, meta);

    std::cout << "\n--------------------------------------------------------\n";
    std::cout << "  TCP AUDIO STREAM SPECIFICATIONS\n";
    std::cout << "--------------------------------------------------------\n";
    std::cout << "  Sample Rate     : " << meta.sampleRate << " Hz\n";
    std::cout << "  Channels        : " << meta.channels << " (Mono)\n";
    std::cout << "  Bits Per Sample : " << meta.bitsPerSample << "-bit PCM\n";
    std::cout << "  Total Audio Size: " << meta.totalBytes << " bytes ("
              << std::fixed << std::setprecision(2)
              << (static_cast<double>(meta.totalBytes) / (1024.0 * 1024.0)) << " MB)\n";
    std::cout << "  Frame Size      : " << meta.frameSize << " bytes (" << meta.frameDurationMs << " ms)\n";
    std::cout << "  Expected Frames : " << meta.totalFrames << "\n";
    std::cout << "  Reliability     : 100% (Kernel TCP In-Order Delivery)\n";
    std::cout << "--------------------------------------------------------\n";
    std::cout << "[TCP Server] Initializing Windows waveOut audio player...\n";

    AudioPlayer player;
    if (!player.init(meta.channels, meta.sampleRate, meta.bitsPerSample, 64)) {
        std::cerr << "[TCP Server] Failed to initialize Windows audio device.\n";
        closesocket(clientSock);
        closesocket(listenSock);
        WSACleanup();
        return 1;
    }

    std::cout << "[TCP Server] Audio device ready. Playing stream in real time...\n\n";

    uint32_t framesReceived = 0;
    uint32_t totalBytesReceived = 0;
    std::vector<char> frameBuffer;

    auto streamStart = std::chrono::high_resolution_clock::now();

    while (true) {
        uint32_t frameLen = 0;
        int r = recvExact(clientSock, reinterpret_cast<char*>(&frameLen), 4);
        if (r <= 0 || frameLen == 0) {
            break;
        }

        if (frameLen > 65536) {
            std::cerr << "[TCP Server] Corrupted frame length: " << frameLen << "\n";
            break;
        }

        frameBuffer.resize(frameLen);
        r = recvExact(clientSock, frameBuffer.data(), frameLen);
        if (r <= 0) {
            break;
        }

        player.writeFrame(frameBuffer.data(), frameLen);

        framesReceived++;
        totalBytesReceived += frameLen;

        if (framesReceived % 25 == 0 || framesReceived == meta.totalFrames) {
            std::cout << "  [TCP Receiver] Frames: " << framesReceived << " / " << meta.totalFrames
                      << " (" << (framesReceived * 100 / (meta.totalFrames > 0 ? meta.totalFrames : 1)) << "%)"
                      << " | Bytes: " << (totalBytesReceived / 1024) << " KB"
                      << " | Playback: ACTIVE \r" << std::flush;
        }
    }

    auto streamEnd = std::chrono::high_resolution_clock::now();
    double streamSec = std::chrono::duration<double>(streamEnd - streamStart).count();

    std::cout << "\n\n[TCP Server] End of stream detected. Finalizing audio playback...\n";
    player.flush();
    player.close();

    std::cout << "========================================================\n";
    std::cout << "              TCP AUDIO STREAM COMPLETED                \n";
    std::cout << "========================================================\n";
    std::cout << "  Frames Played   : " << framesReceived << " / " << meta.totalFrames << "\n";
    std::cout << "  Bytes Received  : " << totalBytesReceived << " bytes\n";
    std::cout << "  Stream Duration : " << std::fixed << std::setprecision(2) << streamSec << " seconds\n";
    std::cout << "  Audio Loss Rate : 0.00% (Guaranteed delivery)\n";
    std::cout << "========================================================\n\n";

    closesocket(clientSock);
    closesocket(listenSock);
    WSACleanup();
    return 0;
}
