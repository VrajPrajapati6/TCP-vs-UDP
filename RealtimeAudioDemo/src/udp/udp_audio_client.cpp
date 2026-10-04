#include "../common/audio_common.h"
#include "../audio/wav_reader.h"
#include <random>

int main(int argc, char* argv[]) {
    std::string wavPath = "audio/network_demo.wav";
    double lossRate = 0.0;
    std::string serverIp = AUDIO_SERVER_IP;
    int port = UDP_AUDIO_PORT;
    int artificialDelayMs = 0;

    if (argc >= 2) wavPath = argv[1];
    if (argc >= 3) lossRate = std::atof(argv[2]);
    if (argc >= 4) serverIp = argv[3];
    if (argc >= 5) port = std::atoi(argv[4]);
    if (argc >= 6) artificialDelayMs = std::atoi(argv[5]);

    if (lossRate < 0.0) lossRate = 0.0;
    if (lossRate > 1.0) lossRate = 1.0;

    WavReader reader;
    if (!reader.open(wavPath)) {
        std::cerr << "[UDP Client] Error: Failed to open WAV file: " << wavPath << "\n";
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
    std::cout << "             UDP REAL-TIME AUDIO SENDER                 \n";
    std::cout << "========================================================\n";
    std::cout << "  Audio File      : " << wavPath << "\n";
    std::cout << "  Duration        : " << std::fixed << std::setprecision(2) << reader.getDurationSec() << " sec\n";
    std::cout << "  Sample Rate     : " << sampleRate << " Hz\n";
    std::cout << "  Channels        : " << channels << " (Mono)\n";
    std::cout << "  Bits Per Sample : " << bitsPerSample << "-bit\n";
    std::cout << "  Frame Duration  : " << frameMs << " ms (" << frameSize << " bytes/frame)\n";
    std::cout << "  Total Frames    : " << totalFrames << "\n";
    std::cout << "  Destination     : " << serverIp << ":" << port << "\n";
    std::cout << "  App Loss Rate   : " << std::fixed << std::setprecision(1) << (lossRate * 100.0) << "%\n";
    if (artificialDelayMs > 0) {
        std::cout << "  Artificial Delay: " << artificialDelayMs << " ms (simulating network jitter)\n";
    }
    std::cout << "========================================================\n";

    if (!initWinsock()) return 1;

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        printWinsockError("socket(UDP Client)");
        WSACleanup();
        return 1;
    }

    // Set send buffer size to 512KB
    int sndBufSize = 512 * 1024;
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF,
               reinterpret_cast<const char*>(&sndBufSize), sizeof(sndBufSize));

    sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family      = AF_INET;
    serverAddr.sin_port        = htons(port);
    serverAddr.sin_addr.s_addr = inet_addr(serverIp.c_str());

    // 1. Send START Control Datagram containing audio stream metadata
    std::cout << "[UDP Client] Sending stream START header...\n";

    AudioMetadata meta;
    meta.sampleRate      = sampleRate;
    meta.channels        = channels;
    meta.bitsPerSample   = bitsPerSample;
    meta.totalBytes      = totalBytes;
    meta.frameSize       = frameSize;
    meta.totalFrames     = totalFrames;
    meta.frameDurationMs = frameMs;

    std::vector<char> packetBuf(UDP_AUDIO_HDR_SIZE + std::max(frameSize, static_cast<uint32_t>(AUDIO_METADATA_SIZE)));

    UdpAudioHeader startHdr;
    startHdr.sequenceNumber = 0;
    startHdr.payloadSize    = AUDIO_METADATA_SIZE;
    startHdr.timestampUs    = getCurrentTimestampUs();
    startHdr.packetType     = static_cast<uint8_t>(AudioPacketType::START);
    startHdr.frameIndex     = 0;

    serializeUdpAudioHeader(startHdr, packetBuf.data());
    serializeAudioMetadata(meta, packetBuf.data() + UDP_AUDIO_HDR_SIZE);

    int startPacketSize = UDP_AUDIO_HDR_SIZE + AUDIO_METADATA_SIZE;
    // Send START packet multiple times (3x) to guarantee delivery over UDP without retransmissions
    for (int i = 0; i < 3; ++i) {
        sendto(sock, packetBuf.data(), startPacketSize, 0,
               reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr));
        Sleep(10);
    }

    std::cout << "[UDP Client] START packet transmitted. Streaming frames in real time...\n\n";

    // Setup pseudo-random number generator for application-level loss simulation
    std::mt19937 rng(1337 + static_cast<unsigned int>(std::time(nullptr)));
    std::uniform_real_distribution<double> dist(0.0, 1.0);

    std::vector<char> frameBuffer(frameSize);
    uint32_t seqNum = 1;
    uint32_t framesRead = 0;
    uint32_t packetsSent = 0;
    uint32_t packetsDropped = 0;
    uint32_t bytesSent = 0;

    auto streamStart = std::chrono::high_resolution_clock::now();

    // Request 1ms timer resolution from Windows multimedia timer system
    timeBeginPeriod(1);

    while (!reader.isEof()) {
        auto frameStart = std::chrono::high_resolution_clock::now();

        int bytesRead = reader.readFrame(frameBuffer.data(), frameSize);
        if (bytesRead <= 0) break;

        framesRead++;

        // Application-Level Packet Loss Simulation
        // If lossRate > 0 and random draw falls below lossRate, the datagram is intentionally suppressed.
        // The sequence number still increments, allowing the receiver to detect the missing frame.
        bool shouldDrop = false;
        if (lossRate > 0.0) {
            double roll = dist(rng);
            if (roll < lossRate) {
                shouldDrop = true;
            }
        }

        if (shouldDrop) {
            packetsDropped++;
            // Datagram is dropped at application layer; receiver will experience sequence gap
        } else {
            UdpAudioHeader dataHdr;
            dataHdr.sequenceNumber = seqNum;
            dataHdr.payloadSize    = static_cast<uint32_t>(bytesRead);
            dataHdr.timestampUs    = getCurrentTimestampUs();
            dataHdr.packetType     = static_cast<uint8_t>(AudioPacketType::DATA);
            dataHdr.frameIndex     = framesRead;

            serializeUdpAudioHeader(dataHdr, packetBuf.data());
            std::memcpy(packetBuf.data() + UDP_AUDIO_HDR_SIZE, frameBuffer.data(), bytesRead);

            int totalPacketSize = UDP_AUDIO_HDR_SIZE + bytesRead;
            int sent = sendto(sock, packetBuf.data(), totalPacketSize, 0,
                              reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr));
            if (sent == SOCKET_ERROR) {
                printWinsockError("sendto(DATA)");
                break;
            }

            packetsSent++;
            bytesSent += bytesRead;
        }

        seqNum++;

        if (framesRead % 25 == 0 || framesRead == totalFrames) {
            double currentLossPct = (framesRead > 0) ? (static_cast<double>(packetsDropped) * 100.0 / framesRead) : 0.0;
            std::cout << "  [UDP Sender] Frame " << framesRead << " / " << totalFrames
                      << " | Sent: " << packetsSent
                      << " | Dropped: " << packetsDropped << " (" << std::fixed << std::setprecision(1) << currentLossPct << "%)"
                      << " | Bytes: " << (bytesSent / 1024) << " KB\r" << std::flush;
        }

        if (artificialDelayMs > 0 && framesRead % 50 == 0) {
            Sleep(artificialDelayMs);
        }

        // Real-Time Pacing
        // Ensure sender sleeps for the remaining frame duration (~20ms) so audio isn't blasted at infinite speed
        auto frameEnd = std::chrono::high_resolution_clock::now();
        double elapsedMs = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();
        double sleepNeeded = static_cast<double>(frameMs) - elapsedMs;
        if (sleepNeeded > 0.0) {
            Sleep(static_cast<DWORD>(sleepNeeded));
        }
    }

    timeEndPeriod(1);

    // Send END Control Datagram
    UdpAudioHeader endHdr;
    endHdr.sequenceNumber = seqNum;
    endHdr.payloadSize    = 0;
    endHdr.timestampUs    = getCurrentTimestampUs();
    endHdr.packetType     = static_cast<uint8_t>(AudioPacketType::END);
    endHdr.frameIndex     = framesRead;

    serializeUdpAudioHeader(endHdr, packetBuf.data());
    for (int i = 0; i < 3; ++i) {
        sendto(sock, packetBuf.data(), UDP_AUDIO_HDR_SIZE, 0,
               reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr));
        Sleep(10);
    }

    auto streamEnd = std::chrono::high_resolution_clock::now();
    double totalSec = std::chrono::duration<double>(streamEnd - streamStart).count();

    std::cout << "\n\n========================================================\n";
    std::cout << "             UDP STREAMING COMPLETE                     \n";
    std::cout << "========================================================\n";
    std::cout << "  Total Frames Processed: " << framesRead << "\n";
    std::cout << "  Datagrams Sent        : " << packetsSent << "\n";
    std::cout << "  Datagrams Dropped     : " << packetsDropped << "\n";
    std::cout << "  Configured Loss Rate  : " << std::fixed << std::setprecision(1) << (lossRate * 100.0) << "%\n";
    double actualLoss = (framesRead > 0) ? (static_cast<double>(packetsDropped) * 100.0 / framesRead) : 0.0;
    std::cout << "  Effective Loss Rate   : " << std::fixed << std::setprecision(2) << actualLoss << "%\n";
    std::cout << "  Total Audio Payload   : " << bytesSent << " bytes (" << (bytesSent / 1024) << " KB)\n";
    std::cout << "  Streaming Duration    : " << std::fixed << std::setprecision(2) << totalSec << " seconds\n";
    std::cout << "========================================================\n\n";

    Sleep(500);
    closesocket(sock);
    WSACleanup();
    return 0;
}
