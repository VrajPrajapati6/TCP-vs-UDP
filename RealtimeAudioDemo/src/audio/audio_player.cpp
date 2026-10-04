#include "audio_player.h"
#include <iostream>
#include <cstring>

AudioPlayer::AudioPlayer()
    : m_hWaveOut(NULL), m_initialized(false), m_channels(0),
      m_sampleRate(0), m_bitsPerSample(0), m_bufferCount(64), m_writeIndex(0)
{
}

AudioPlayer::~AudioPlayer() {
    close();
}

bool AudioPlayer::init(uint16_t channels, uint32_t sampleRate, uint16_t bitsPerSample, int bufferCount) {
    close();

    m_channels = channels;
    m_sampleRate = sampleRate;
    m_bitsPerSample = bitsPerSample;
    m_bufferCount = bufferCount;
    m_writeIndex = 0;

    WAVEFORMATEX wfx;
    std::memset(&wfx, 0, sizeof(wfx));
    wfx.wFormatTag      = WAVE_FORMAT_PCM;
    wfx.nChannels       = m_channels;
    wfx.nSamplesPerSec  = m_sampleRate;
    wfx.wBitsPerSample  = m_bitsPerSample;
    wfx.nBlockAlign     = (m_channels * m_bitsPerSample) / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    wfx.cbSize          = 0;

    MMRESULT res = waveOutOpen(&m_hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL);
    if (res != MMSYSERR_NOERROR) {
        std::cerr << "[AudioPlayer] Error: waveOutOpen failed with error code: " << res << "\n";
        return false;
    }

    m_blocks.resize(m_bufferCount);
    for (int i = 0; i < m_bufferCount; i++) {
        std::memset(&m_blocks[i].header, 0, sizeof(WAVEHDR));
        m_blocks[i].prepared = false;
    }

    m_initialized = true;
    return true;
}

void AudioPlayer::writeFrame(const char* pcmData, int byteCount) {
    if (!m_initialized || !m_hWaveOut || byteCount <= 0) return;

    AudioBufferBlock& block = m_blocks[m_writeIndex];

    if (block.prepared) {
        while (!(block.header.dwFlags & WHDR_DONE)) {
            Sleep(2);
        }
        waveOutUnprepareHeader(m_hWaveOut, &block.header, sizeof(WAVEHDR));
        block.prepared = false;
    }

    block.pcm.resize(byteCount);
    std::memcpy(block.pcm.data(), pcmData, byteCount);

    std::memset(&block.header, 0, sizeof(WAVEHDR));
    block.header.lpData = block.pcm.data();
    block.header.dwBufferLength = byteCount;
    block.header.dwFlags = 0;

    waveOutPrepareHeader(m_hWaveOut, &block.header, sizeof(WAVEHDR));
    waveOutWrite(m_hWaveOut, &block.header, sizeof(WAVEHDR));
    block.prepared = true;

    m_writeIndex = (m_writeIndex + 1) % m_bufferCount;
}

void AudioPlayer::writeSilence(int byteCount) {
    if (!m_initialized || byteCount <= 0) return;
    std::vector<char> silence(byteCount, 0);
    writeFrame(silence.data(), byteCount);
}

void AudioPlayer::flush() {
    if (!m_initialized || !m_hWaveOut) return;

    for (int i = 0; i < m_bufferCount; i++) {
        if (m_blocks[i].prepared) {
            while (!(m_blocks[i].header.dwFlags & WHDR_DONE)) {
                Sleep(5);
            }
            waveOutUnprepareHeader(m_hWaveOut, &m_blocks[i].header, sizeof(WAVEHDR));
            m_blocks[i].prepared = false;
        }
    }
}

void AudioPlayer::close() {
    if (m_initialized && m_hWaveOut) {
        flush();
        waveOutReset(m_hWaveOut);
        waveOutClose(m_hWaveOut);
        m_hWaveOut = NULL;
    }
    m_blocks.clear();
    m_initialized = false;
    m_writeIndex = 0;
}
