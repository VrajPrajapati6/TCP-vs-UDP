#ifndef AUDIO_COMMON_H
#define AUDIO_COMMON_H

#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <chrono>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <ctime>
#include <algorithm>

#pragma comment(lib, "ws2_32.lib")

static const int TCP_AUDIO_PORT     = 6000;
static const int UDP_AUDIO_PORT     = 6001;
static const char* AUDIO_SERVER_IP  = "127.0.0.1";
static const int AUDIO_BACKLOG      = 1;
static const int AUDIO_SOCK_TIMEOUT = 8000;

static const int DEFAULT_FRAME_MS   = 20;
static const int UDP_AUDIO_HDR_SIZE = 24;

enum class AudioPacketType : uint8_t {
    START = 0,
    DATA  = 1,
    END   = 2,
    ACK   = 3
};

struct AudioMetadata {
    uint32_t sampleRate;
    uint16_t channels;
    uint16_t bitsPerSample;
    uint32_t totalBytes;
    uint32_t frameSize;
    uint32_t totalFrames;
    uint16_t frameDurationMs;
};

inline void serializeAudioMetadata(const AudioMetadata& meta, char* buf) {
    std::memcpy(buf + 0,  &meta.sampleRate, 4);
    std::memcpy(buf + 4,  &meta.channels, 2);
    std::memcpy(buf + 6,  &meta.bitsPerSample, 2);
    std::memcpy(buf + 8,  &meta.totalBytes, 4);
    std::memcpy(buf + 12, &meta.frameSize, 4);
    std::memcpy(buf + 16, &meta.totalFrames, 4);
    std::memcpy(buf + 20, &meta.frameDurationMs, 2);
}

inline void deserializeAudioMetadata(const char* buf, AudioMetadata& meta) {
    std::memcpy(&meta.sampleRate, buf + 0, 4);
    std::memcpy(&meta.channels, buf + 4, 2);
    std::memcpy(&meta.bitsPerSample, buf + 6, 2);
    std::memcpy(&meta.totalBytes, buf + 8, 4);
    std::memcpy(&meta.frameSize, buf + 12, 4);
    std::memcpy(&meta.totalFrames, buf + 16, 4);
    std::memcpy(&meta.frameDurationMs, buf + 20, 2);
}

static const int AUDIO_METADATA_SIZE = 22;

struct UdpAudioHeader {
    uint32_t sequenceNumber;
    uint32_t payloadSize;
    int64_t  timestampUs;
    uint8_t  packetType;
    uint32_t frameIndex;
    uint8_t  reserved[3];
};

inline void serializeUdpAudioHeader(const UdpAudioHeader& hdr, char* buf) {
    std::memcpy(buf + 0,  &hdr.sequenceNumber, 4);
    std::memcpy(buf + 4,  &hdr.payloadSize, 4);
    std::memcpy(buf + 8,  &hdr.timestampUs, 8);
    std::memcpy(buf + 16, &hdr.packetType, 1);
    std::memcpy(buf + 17, &hdr.frameIndex, 4);
    std::memset(buf + 21, 0, 3);
}

inline void deserializeUdpAudioHeader(const char* buf, UdpAudioHeader& hdr) {
    std::memcpy(&hdr.sequenceNumber, buf + 0, 4);
    std::memcpy(&hdr.payloadSize, buf + 4, 4);
    std::memcpy(&hdr.timestampUs, buf + 8, 8);
    std::memcpy(&hdr.packetType, buf + 16, 1);
    std::memcpy(&hdr.frameIndex, buf + 17, 4);
    std::memset(hdr.reserved, 0, 3);
}

inline int64_t getCurrentTimestampUs() {
    using namespace std::chrono;
    return duration_cast<microseconds>(
        high_resolution_clock::now().time_since_epoch()
    ).count();
}

inline bool initWinsock() {
    WSADATA wsaData;
    int res = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (res != 0) {
        std::cerr << "[ERROR] WSAStartup failed: " << res << std::endl;
        return false;
    }
    return true;
}

inline void printWinsockError(const std::string& ctx) {
    std::cerr << "[ERROR] " << ctx << " failed. WSA Error: " << WSAGetLastError() << std::endl;
}

inline int sendAll(SOCKET sock, const char* data, int len) {
    int totalSent = 0;
    while (totalSent < len) {
        int sent = send(sock, data + totalSent, len - totalSent, 0);
        if (sent == SOCKET_ERROR) return SOCKET_ERROR;
        if (sent == 0) break;
        totalSent += sent;
    }
    return totalSent;
}

inline int recvExact(SOCKET sock, char* buf, int len) {
    int totalRecv = 0;
    while (totalRecv < len) {
        int r = recv(sock, buf + totalRecv, len - totalRecv, 0);
        if (r == SOCKET_ERROR) return SOCKET_ERROR;
        if (r == 0) break;
        totalRecv += r;
    }
    return totalRecv;
}

#endif // AUDIO_COMMON_H
