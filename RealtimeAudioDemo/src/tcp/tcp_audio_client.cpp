#include "../common/audio_common.h"
#include "../audio/wav_reader.h"
#include <thread>

int main(int argc, char* argv[]) {
    std::string wavPath = "audio/network_demo.wav";
    std::string serverIp = AUDIO_SERVER_IP;
    int port = TCP_AUDIO_PORT;
    int artificialDelayMs = 0;

    if (argc >= 2) wavPath = argv[1];
    if (argc >= 3) serverIp = argv[2];
    if (argc >= 4) port = std::atoi(argv[3]);
    if (argc >= 5) artificialDelayMs = std::atoi(argv[4]);

    WavReader reader;
    if (!reader.open(wavPath)) {
        std::cerr << "[TCP Client] Error: Failed to open WAV file: " << wavPath << "\n";
        return 1;
    }

    uint16_t channels = reader.getChannels();
    uint32_t sampleRate = reader.getSampleRate();
    uint16_t bitsPerSample = reader.getBitsPerSample();
    uint32_t totalBytes = reader.getDataSize();

    uint16_t frameMs = DEFAULT_FRAME_MS;
    uint32_t bytesPerSec = sampleRate * channels * (bitsPerSample / 8);
    uint32_t frameSize = (bytesPerSec * frameMs) / 1000;
    if (frameSize == 0) frameSize = 882;
    uint32_t totalFrames = (totalBytes + frameSize - 1) / frameSize;

    std::cout << "========================================================\n";
    std::cout << "             TCP REAL-TIME AUDIO SENDER                 \n";
    std::cout << "========================================================\n";
    std::cout << "  Audio File      : " << wavPath << "\n";
    std::cout << "  Duration        : " << std::fixed << std::setprecision(2) << reader.getDurationSec() << " sec\n";
    std::cout << "  Sample Rate     : " << sampleRate << " Hz\n";
    std::cout << "  Channels        : " << channels << " (Mono)\n";
    std::cout << "  Bits Per Sample : " << bitsPerSample << "-bit\n";
    std::cout << "  Frame Duration  : " << frameMs << " ms (" << frameSize << " bytes/frame)\n";
    std::cout << "  Total Frames    : " << totalFrames << "\n";
    std::cout << "  Destination     : " << serverIp << ":" << port << "\n";
    if (artificialDelayMs > 0) {
        std::cout << "  Artificial Delay: " << artificialDelayMs << " ms (simulating network jitter)\n";
    }
    std::cout << "========================================================\n";

    if (!initWinsock()) return 1;

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        printWinsockError("socket(TCP Client)");
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family      = AF_INET;
    serverAddr.sin_port        = htons(port);
    serverAddr.sin_addr.s_addr = inet_addr(serverIp.c_str());

    std::cout << "[TCP Client] Connecting to " << serverIp << ":" << port << " ...\n";

    if (connect(sock, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
        printWinsockError("connect(TCP Client)");
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    std::cout << "[TCP Client] Connected. Sending audio metadata...\n";

    AudioMetadata meta;
    meta.sampleRate      = sampleRate;
    meta.channels        = channels;
    meta.bitsPerSample   = bitsPerSample;
    meta.totalBytes      = totalBytes;
    meta.frameSize       = frameSize;
    meta.totalFrames     = totalFrames;
    meta.frameDurationMs = frameMs;

    char metaBuf[AUDIO_METADATA_SIZE];
    serializeAudioMetadata(meta, metaBuf);

    if (sendAll(sock, metaBuf, AUDIO_METADATA_SIZE) != AUDIO_METADATA_SIZE) {
        std::cerr << "[TCP Client] Failed to send metadata.\n";
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    std::cout << "[TCP Client] Metadata transmitted. Starting paced real-time streaming...\n\n";

    std::vector<char> frameBuffer(frameSize);
    uint32_t framesSent = 0;
    uint32_t bytesSent = 0;

    auto streamStart = std::chrono::high_resolution_clock::now();

    // Request 1ms timer resolution from Windows multimedia timer system
    timeBeginPeriod(1);

    while (!reader.isEof()) {
        auto frameStart = std::chrono::high_resolution_clock::now();

        int bytesRead = reader.readFrame(frameBuffer.data(), frameSize);
        if (bytesRead <= 0) break;

        uint32_t fLen = static_cast<uint32_t>(bytesRead);
        if (sendAll(sock, reinterpret_cast<const char*>(&fLen), 4) != 4) {
            std::cerr << "[TCP Client] Failed to send frame header.\n";
            break;
        }

        if (sendAll(sock, frameBuffer.data(), bytesRead) != bytesRead) {
            std::cerr << "[TCP Client] Failed to send audio frame payload.\n";
            break;
        }

        framesSent++;
        bytesSent += bytesRead;

        if (framesSent % 25 == 0 || framesSent == totalFrames) {
            std::cout << "  [TCP Sender] Streaming frame " << framesSent << " / " << totalFrames
                      << " (" << (framesSent * 100 / totalFrames) << "%)"
                      << " | Bytes sent: " << (bytesSent / 1024) << " KB\r" << std::flush;
        }

        if (artificialDelayMs > 0 && framesSent % 50 == 0) {
            Sleep(artificialDelayMs);
        }

        auto frameEnd = std::chrono::high_resolution_clock::now();
        double elapsedMs = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();
        double sleepNeeded = static_cast<double>(frameMs) - elapsedMs;
        if (sleepNeeded > 0.0) {
            Sleep(static_cast<DWORD>(sleepNeeded));
        }
    }

    timeEndPeriod(1);

    uint32_t endMarker = 0;
    sendAll(sock, reinterpret_cast<const char*>(&endMarker), 4);

    auto streamEnd = std::chrono::high_resolution_clock::now();
    double totalSec = std::chrono::duration<double>(streamEnd - streamStart).count();

    std::cout << "\n\n========================================================\n";
    std::cout << "             TCP STREAMING COMPLETE                     \n";
    std::cout << "========================================================\n";
    std::cout << "  Total Frames Sent : " << framesSent << " / " << totalFrames << "\n";
    std::cout << "  Total Bytes Sent  : " << bytesSent << " bytes\n";
    std::cout << "  Streaming Duration: " << std::fixed << std::setprecision(2) << totalSec << " seconds\n";
    std::cout << "========================================================\n\n";

    shutdown(sock, SD_SEND);
    Sleep(500);
    closesocket(sock);
    WSACleanup();
    return 0;
}
