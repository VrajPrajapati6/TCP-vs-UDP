#ifndef WAV_READER_H
#define WAV_READER_H

#include <string>
#include <fstream>
#include <cstdint>
#include <vector>

class WavReader {
public:
    WavReader();
    ~WavReader();

    bool open(const std::string& filepath);
    void close();

    bool isOpen() const { return m_open; }
    uint16_t getChannels() const { return m_channels; }
    uint32_t getSampleRate() const { return m_sampleRate; }
    uint16_t getBitsPerSample() const { return m_bitsPerSample; }
    uint32_t getDataSize() const { return m_dataSize; }
    double getDurationSec() const;

    int readFrame(char* buffer, int maxBytes);
    bool isEof() const;
    void reset();

private:
    std::ifstream m_file;
    bool m_open;
    uint16_t m_channels;
    uint32_t m_sampleRate;
    uint16_t m_bitsPerSample;
    uint32_t m_dataSize;
    std::streampos m_dataStartPos;
    uint32_t m_bytesRead;
};

#endif // WAV_READER_H
