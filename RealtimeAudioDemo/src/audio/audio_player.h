#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <windows.h>
#include <mmsystem.h>
#include <cstdint>
#include <vector>

class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();

    bool init(uint16_t channels, uint32_t sampleRate, uint16_t bitsPerSample, int bufferCount = 64);
    void writeFrame(const char* pcmData, int byteCount);
    void writeSilence(int byteCount);
    void flush();
    void close();
    bool isInitialized() const { return m_initialized; }

private:
    struct AudioBufferBlock {
        WAVEHDR header;
        std::vector<char> pcm;
        bool prepared;
    };

    HWAVEOUT m_hWaveOut;
    bool m_initialized;
    uint16_t m_channels;
    uint32_t m_sampleRate;
    uint16_t m_bitsPerSample;
    int m_bufferCount;
    int m_writeIndex;
    std::vector<AudioBufferBlock> m_blocks;
};

#endif // AUDIO_PLAYER_H
