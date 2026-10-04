#ifndef COMMON_H
#define COMMON_H

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
#include <cmath>

#pragma comment(lib, "ws2_32.lib")

static const int TCP_PORT       = 5000;
static const int UDP_PORT       = 5001;
static const char* SERVER_IP    = "127.0.0.1";
static const int BACKLOG        = 1;
static const int SOCKET_TIMEOUT = 10000;

static const int DEFAULT_CHUNK_SIZE   = 4096;
static const int MAX_UDP_PAYLOAD      = 60000;
static const int MIN_CHUNK_SIZE       = 64;
static const int MAX_CHUNK_SIZE       = 65000;
static const int HEADER_DELIMITER_LEN = 4;
static const int RESULT_FILE_WAIT_MS  = 500;

enum class Protocol {
    TCP,
    UDP
};

enum class TransferMode {
    SYNTHETIC,
    FILEXFER
};

enum class PacketType : uint8_t {
    START = 0,
    DATA  = 1,
    END   = 2,
    ACK   = 3
};

static const int UDP_HEADER_SIZE = 24;

struct UdpPacketHeader {
    uint32_t sequenceNumber;
    uint32_t payloadSize;
    int64_t  timestampUs;
    uint8_t  packetType;
    uint32_t totalPackets;
    uint8_t  reserved[3];
};

inline void serializeUdpHeader(const UdpPacketHeader& hdr, char* buf) {
    std::memcpy(buf + 0,  &hdr.sequenceNumber, 4);
    std::memcpy(buf + 4,  &hdr.payloadSize,    4);
    std::memcpy(buf + 8,  &hdr.timestampUs,    8);
    std::memcpy(buf + 16, &hdr.packetType,     1);
    std::memcpy(buf + 17, &hdr.totalPackets,   4);
    std::memset(buf + 21, 0, 3);
}

inline void deserializeUdpHeader(const char* buf, UdpPacketHeader& hdr) {
    std::memcpy(&hdr.sequenceNumber, buf + 0,  4);
    std::memcpy(&hdr.payloadSize,    buf + 4,  4);
    std::memcpy(&hdr.timestampUs,    buf + 8,  8);
    std::memcpy(&hdr.packetType,     buf + 16, 1);
    std::memcpy(&hdr.totalPackets,   buf + 17, 4);
    std::memset(hdr.reserved, 0, 3);
}

struct ExperimentConfig {
    Protocol     protocol;
    TransferMode mode;
    int64_t      dataSize;
    int          chunkSize;
    std::string  filePath;
    double       packetLossRate;
    int          artificialDelayMs;
};

struct ExperimentResult {
    Protocol protocol;
    TransferMode mode;
    int64_t  totalBytes;
    int      chunkSize;
    int      packetsSent;
    int      packetsReceived;
    int      packetsLost;
    double   packetLossPercent;
    double   transmissionTimeSec;
    double   throughputMBps;
    double   throughputMbps;
    double   averageLatencyMs;
    double   jitterMs;
    int      outOfOrderPackets;
    bool     fileIntegrityPass;
    std::string  filePath;
    std::string  timestamp;
};

inline int64_t getCurrentTimestampUs() {
    using namespace std::chrono;
    return duration_cast<microseconds>(
        high_resolution_clock::now().time_since_epoch()
    ).count();
}

inline std::string getTimestampString() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    return std::string(buf);
}

inline bool initWinsock() {
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "[ERROR] WSAStartup failed with error: " << result << std::endl;
        return false;
    }
    return true;
}

inline void printWinsockError(const std::string& context) {
    int err = WSAGetLastError();
    std::cerr << "[ERROR] " << context << " failed. WSA Error: " << err << std::endl;
}

inline int sendAll(SOCKET sock, const char* data, int length) {
    int totalSent = 0;
    while (totalSent < length) {
        int sent = send(sock, data + totalSent, length - totalSent, 0);
        if (sent == SOCKET_ERROR) {
            return SOCKET_ERROR;
        }
        if (sent == 0) {
            break;
        }
        totalSent += sent;
    }
    return totalSent;
}

inline int recvExact(SOCKET sock, char* buffer, int length) {
    int totalReceived = 0;
    while (totalReceived < length) {
        int received = recv(sock, buffer + totalReceived, length - totalReceived, 0);
        if (received == SOCKET_ERROR) {
            return SOCKET_ERROR;
        }
        if (received == 0) {
            break;
        }
        totalReceived += received;
    }
    return totalReceived;
}

inline std::vector<char> generateSyntheticData(int64_t size) {
    std::vector<char> data(size);
    for (int64_t i = 0; i < size; i++) {
        data[i] = static_cast<char>('A' + (i % 26));
    }
    return data;
}

inline bool readFileToVector(const std::string& path, std::vector<char>& data) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "[ERROR] Cannot open file: " << path << std::endl;
        return false;
    }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    data.resize(static_cast<size_t>(size));
    if (!file.read(data.data(), size)) {
        std::cerr << "[ERROR] Failed to read file: " << path << std::endl;
        return false;
    }
    return true;
}

inline bool compareFiles(const std::string& file1, const std::string& file2) {
    std::ifstream f1(file1, std::ios::binary);
    std::ifstream f2(file2, std::ios::binary);
    if (!f1.is_open() || !f2.is_open()) {
        return false;
    }
    f1.seekg(0, std::ios::end);
    f2.seekg(0, std::ios::end);
    if (f1.tellg() != f2.tellg()) {
        return false;
    }
    f1.seekg(0, std::ios::beg);
    f2.seekg(0, std::ios::beg);

    const int BUF_SIZE = 8192;
    char buf1[8192], buf2[8192];
    while (f1 && f2) {
        f1.read(buf1, BUF_SIZE);
        f2.read(buf2, BUF_SIZE);
        std::streamsize count = f1.gcount();
        if (count != f2.gcount()) return false;
        if (count == 0) break;
        if (std::memcmp(buf1, buf2, static_cast<size_t>(count)) != 0) {
            return false;
        }
    }
    return true;
}

inline std::string buildTcpControlHeader(TransferMode mode, int64_t dataSize,
                                          int chunkSize, const std::string& fileName) {
    std::ostringstream oss;
    oss << (mode == TransferMode::SYNTHETIC ? "SYNTHETIC" : "FILE")
        << "|" << dataSize
        << "|" << chunkSize
        << "|" << (fileName.empty() ? "NONE" : fileName)
        << "\r\n\r\n";
    return oss.str();
}

inline bool parseTcpControlHeader(const std::string& header, TransferMode& mode,
                                   int64_t& dataSize, int& chunkSize,
                                   std::string& fileName) {
    std::string h = header;
    size_t pos = h.find("\r\n\r\n");
    if (pos != std::string::npos) h = h.substr(0, pos);

    std::istringstream iss(h);
    std::string modeStr, sizeStr, chunkStr;
    if (!std::getline(iss, modeStr, '|')) return false;
    if (!std::getline(iss, sizeStr, '|')) return false;
    if (!std::getline(iss, chunkStr, '|')) return false;
    if (!std::getline(iss, fileName)) return false;

    mode = (modeStr == "FILE") ? TransferMode::FILEXFER : TransferMode::SYNTHETIC;
    try {
        dataSize  = std::stoll(sizeStr);
        chunkSize = std::stoi(chunkStr);
    } catch (...) {
        return false;
    }
    return true;
}

static const char* TCP_SERVER_RESULT_FILE = "results/tcp_server_last.txt";
static const char* UDP_SERVER_RESULT_FILE = "results/udp_server_last.txt";

inline void writeResultFile(const std::string& path,
                             const std::vector<std::pair<std::string, std::string>>& kvs) {
    std::ofstream out(path);
    if (!out.is_open()) {
        std::cerr << "[WARN] Cannot write result file: " << path << std::endl;
        return;
    }
    for (auto& kv : kvs) {
        out << kv.first << "=" << kv.second << "\n";
    }
}

inline bool readResultFile(const std::string& path,
                            std::vector<std::pair<std::string, std::string>>& kvs) {
    std::ifstream in(path);
    if (!in.is_open()) return false;
    std::string line;
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (eq != std::string::npos) {
            kvs.push_back({line.substr(0, eq), line.substr(eq + 1)});
        }
    }
    return true;
}

inline std::string kvLookup(const std::vector<std::pair<std::string, std::string>>& kvs,
                             const std::string& key, const std::string& def = "") {
    for (auto& kv : kvs) {
        if (kv.first == key) return kv.second;
    }
    return def;
}

#endif // COMMON_H
