#ifndef AUDIOHANDLER_H
#define AUDIOHANDLER_H

#include <string>
#include <vector>
#include <cstdint>
#include <mutex>
#include <atomic>

typedef unsigned long long ma_uint64;
typedef unsigned int ma_uint32;

typedef struct ma_engine ma_engine;
typedef struct ma_sound ma_sound;
typedef struct ma_decoder ma_decoder;

class AudioHandler
{
public:
    AudioHandler();
    ~AudioHandler();

    bool loadFile(const char* fileName, float fps);
    void releaseFile();
    
    // Play audio at specific frame - call when frame changes
    void playAtFrame(int frame);
    void stop();
    
    void setFps(float fps);
    
    // Waveform
    void generateWaveform(int pixelWidth);
    float* getWaveformL() { return waveformDataL; }
    float* getWaveformR() { return waveformDataR; }
    int getWaveformWidth() const { return waveformWidth; }
    
    // Info
    bool fileLoaded() const { return _fileLoaded.load(); }
    void setFileLoaded(bool loaded) { _fileLoaded.store(loaded); }
    int getFileLengthInFrames() const;
    float getFps() const { return _fps; }

private:
    ma_engine* _engine;
    ma_sound* _sound;
    ma_decoder* _decoder;
    
    std::atomic<bool> _initialized;
    std::atomic<bool> _fileLoaded;
    std::atomic<int> _lastPlayedFrame;
    
    std::string _currentFile;
    std::mutex _mutex;
    
    ma_uint32 _sampleRate;
    ma_uint32 _channels;
    ma_uint64 _totalPcmFrames;
    
    float _fps;
    
    float* waveformDataL;
    float* waveformDataR;
    int waveformWidth;
    std::vector<float> _audioData;
    
    void cleanup();
    bool initEngine();
};

#endif
