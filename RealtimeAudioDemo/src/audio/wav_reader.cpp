#include "wav_reader.h"
#include <iostream>
#include <cstring>

WavReader::WavReader()
    : m_open(false), m_channels(0), m_sampleRate(0),
      m_bitsPerSample(0), m_dataSize(0), m_dataStartPos(0), m_bytesRead(0)
{
}

WavReader::~WavReader() {
    close();
}

void WavReader::close() {
    if (m_file.is_open()) {
        m_file.close();
    }
    m_open = false;
    m_bytesRead = 0;
}

bool WavReader::open(const std::string& filepath) {
    close();
    m_file.open(filepath, std::ios::binary);
    if (!m_file.is_open()) {
        std::cerr << "[WavReader] Error: Cannot open file: " << filepath << "\n";
        return false;
    }

    char riffHeader[12];
    if (!m_file.read(riffHeader, 12)) {
        std::cerr << "[WavReader] Error: File too small for RIFF header.\n";
        close();
        return false;
    }

    if (std::memcmp(riffHeader, "RIFF", 4) != 0 || std::memcmp(riffHeader + 8, "WAVE", 4) != 0) {
        std::cerr << "[WavReader] Error: Not a valid RIFF/WAVE file.\n";
        close();
        return false;
    }

    bool foundFmt = false;
    bool foundData = false;

    while (m_file && (!foundFmt || !foundData)) {
        char chunkHeader[8];
        if (!m_file.read(chunkHeader, 8)) break;

        char chunkId[5] = {0};
        std::memcpy(chunkId, chunkHeader, 4);
        uint32_t chunkSize = 0;
        std::memcpy(&chunkSize, chunkHeader + 4, 4);

        if (std::memcmp(chunkId, "fmt ", 4) == 0) {
            std::vector<char> fmtData(chunkSize);
            if (!m_file.read(fmtData.data(), chunkSize)) break;

            uint16_t audioFormat = 0;
            std::memcpy(&audioFormat, fmtData.data(), 2);
            if (audioFormat != 1) {
                std::cerr << "[WavReader] Error: Only uncompressed PCM WAV (format 1) is supported.\n";
                close();
                return false;
            }

            std::memcpy(&m_channels, fmtData.data() + 2, 2);
            std::memcpy(&m_sampleRate, fmtData.data() + 4, 4);
            std::memcpy(&m_bitsPerSample, fmtData.data() + 14, 2);

            foundFmt = true;
        } else if (std::memcmp(chunkId, "data", 4) == 0) {
            m_dataSize = chunkSize;
            m_dataStartPos = m_file.tellg();
            foundData = true;
            break;
        } else {
            m_file.seekg(chunkSize, std::ios::cur);
        }
    }

    if (!foundFmt || !foundData) {
        std::cerr << "[WavReader] Error: Could not locate fmt or data chunk in WAV.\n";
        close();
        return false;
    }

    m_open = true;
    m_bytesRead = 0;
    m_file.seekg(m_dataStartPos, std::ios::beg);
    return true;
}

double WavReader::getDurationSec() const {
    if (!m_open || m_channels == 0 || m_sampleRate == 0 || m_bitsPerSample == 0) return 0.0;
    uint32_t bytesPerSec = m_sampleRate * m_channels * (m_bitsPerSample / 8);
    if (bytesPerSec == 0) return 0.0;
    return static_cast<double>(m_dataSize) / bytesPerSec;
}

int WavReader::readFrame(char* buffer, int maxBytes) {
    if (!m_open || !m_file.is_open() || m_bytesRead >= m_dataSize) {
        return 0;
    }

    uint32_t remaining = m_dataSize - m_bytesRead;
    int toRead = static_cast<int>(std::min(static_cast<uint32_t>(maxBytes), remaining));

    m_file.read(buffer, toRead);
    std::streamsize actual = m_file.gcount();
    m_bytesRead += static_cast<uint32_t>(actual);
    return static_cast<int>(actual);
}

bool WavReader::isEof() const {
    if (!m_open) return true;
    return m_bytesRead >= m_dataSize;
}

void WavReader::reset() {
    if (m_open && m_file.is_open()) {
        m_file.clear();
        m_file.seekg(m_dataStartPos, std::ios::beg);
        m_bytesRead = 0;
    }
}
