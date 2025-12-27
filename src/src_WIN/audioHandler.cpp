#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING

#include "miniaudio.h"
#include "audioHandler.h"

#include <iostream>
#include <cmath>
#include <algorithm>

AudioHandler::AudioHandler()
    : _engine(nullptr)
    , _sound(nullptr)
    , _decoder(nullptr)
    , _initialized(false)
    , _fileLoaded(false)
    , _lastPlayedFrame(-9999)
    , _sampleRate(48000)
    , _channels(2)
    , _totalPcmFrames(0)
    , _fps(25.0f)
    , waveformDataL(nullptr)
    , waveformDataR(nullptr)
    , waveformWidth(0)
{
    // DO NOT call initEngine() here!
    // Lazy init when first needed - prevents Windows freeze at DLL load
}

AudioHandler::~AudioHandler()
{
    cleanup();
}

bool AudioHandler::initEngine()
{
    if (_initialized.load()) return true;
    
    std::cout << "AudioHandler: Initializing audio engine..." << std::endl;
    
    _engine = new ma_engine();
    
    ma_engine_config config = ma_engine_config_init();
    config.channels = 2;
    config.sampleRate = 48000;
    
#ifdef _WIN32
    // Windows WASAPI needs larger buffer to avoid glitches/freezes
    config.periodSizeInFrames = 512;
    std::cout << "AudioHandler: Using Windows settings (512 frame buffer)" << std::endl;
#else
    // Linux/macOS can handle smaller buffer
    config.periodSizeInFrames = 128;
#endif
    
    ma_result result = ma_engine_init(&config, _engine);
    if (result != MA_SUCCESS) {
        std::cerr << "AudioHandler: Failed to init engine, error: " << result << std::endl;
        delete _engine;
        _engine = nullptr;
        return false;
    }
    
    _sampleRate = ma_engine_get_sample_rate(_engine);
    _initialized.store(true);
    std::cout << "AudioHandler: Engine ready @ " << _sampleRate << " Hz" << std::endl;
    return true;
}

void AudioHandler::cleanup()
{
    _fileLoaded.store(false);
    _initialized.store(false);
    
    // Don't use mutex in destructor - can cause deadlock
    if (_sound) {
        ma_sound_stop(_sound);
        ma_sound_uninit(_sound);
        delete _sound;
        _sound = nullptr;
    }
    
    if (_decoder) {
        ma_decoder_uninit(_decoder);
        delete _decoder;
        _decoder = nullptr;
    }
    
    if (_engine) {
        ma_engine_uninit(_engine);
        delete _engine;
        _engine = nullptr;
    }
    
    delete[] waveformDataL;
    delete[] waveformDataR;
    waveformDataL = nullptr;
    waveformDataR = nullptr;
    
    _audioData.clear();
}

bool AudioHandler::loadFile(const char* fileName, float fps)
{
    std::lock_guard<std::mutex> lock(_mutex);
    
    // Set FPS first
    _fps = std::max(1.0f, fps);
    
    // Lazy init engine on first file load
    if (!_initialized.load()) {
        if (!initEngine()) {
            std::cerr << "AudioHandler: Cannot load - engine init failed" << std::endl;
            return false;
        }
    }
    
    // Cleanup previous
    if (_sound) {
        ma_sound_stop(_sound);
        ma_sound_uninit(_sound);
        delete _sound;
        _sound = nullptr;
    }
    
    if (_decoder) {
        ma_decoder_uninit(_decoder);
        delete _decoder;
        _decoder = nullptr;
    }
    
    _audioData.clear();
    _fileLoaded.store(false);
    _lastPlayedFrame.store(-9999);
    
    // Load sound
    _sound = new ma_sound();
    
    ma_uint32 flags = MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_DECODE;
    ma_result result = ma_sound_init_from_file(_engine, fileName, flags, nullptr, nullptr, _sound);
    
    if (result != MA_SUCCESS) {
        std::cerr << "AudioHandler: Failed to load " << fileName << " (error " << result << ")" << std::endl;
        delete _sound;
        _sound = nullptr;
        return false;
    }
    
    // Get info via decoder
    _decoder = new ma_decoder();
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 2, _sampleRate);
    
    if (ma_decoder_init_file(fileName, &cfg, _decoder) == MA_SUCCESS) {
        _sampleRate = _decoder->outputSampleRate;
        _channels = _decoder->outputChannels;
        ma_decoder_get_length_in_pcm_frames(_decoder, &_totalPcmFrames);
        
        // Load audio data for waveform (limit to ~10 min stereo @ 48kHz)
        if (_totalPcmFrames > 0 && _totalPcmFrames < 30000000) {
            _audioData.resize(_totalPcmFrames * _channels);
            ma_uint64 framesRead;
            ma_decoder_seek_to_pcm_frame(_decoder, 0);
            ma_decoder_read_pcm_frames(_decoder, _audioData.data(), _totalPcmFrames, &framesRead);
        }
        
        float duration = (float)_totalPcmFrames / _sampleRate;
        int lengthInFrames = (int)(duration * _fps);
        
        std::cout << "AudioHandler: Loaded " << fileName << std::endl;
        std::cout << "  " << _sampleRate << " Hz, " << _channels << " ch, " 
                  << duration << "s (" << lengthInFrames << " frames @ " << _fps << " fps)" << std::endl;
    }
    
    _currentFile = fileName;
    _fileLoaded.store(true);
    return true;
}

void AudioHandler::releaseFile()
{
    std::lock_guard<std::mutex> lock(_mutex);
    
    if (_sound) {
        ma_sound_stop(_sound);
        ma_sound_uninit(_sound);
        delete _sound;
        _sound = nullptr;
    }
    
    _fileLoaded.store(false);
    _lastPlayedFrame.store(-9999);
}

void AudioHandler::playAtFrame(int frame)
{
    if (!_fileLoaded.load() || !_sound || !_engine) return;
    
    int lastFrame = _lastPlayedFrame.load();
    
    // Skip if same frame
    if (frame == lastFrame) return;
    
    std::lock_guard<std::mutex> lock(_mutex);
    
    // Double-check after acquiring lock
    if (!_sound || !_engine) return;
    
    // Calculate PCM position for this frame
    double secondsPerFrame = 1.0 / _fps;
    double startSeconds = frame * secondsPerFrame;
    ma_uint64 pcmStart = (ma_uint64)(startSeconds * _sampleRate);
    
    // How many PCM samples = 1 video frame
    ma_uint64 samplesPerVideoFrame = (ma_uint64)(_sampleRate / _fps);
    
    // Handle out of bounds
    if (_totalPcmFrames > 0 && pcmStart >= _totalPcmFrames) {
        ma_sound_stop(_sound);
        _lastPlayedFrame.store(frame);
        return;
    }
    
    // Stop current playback and clear stop time
    ma_sound_stop(_sound);
    ma_sound_set_stop_time_in_pcm_frames(_sound, (ma_uint64)-1);
    
    // Seek to frame position
    ma_sound_seek_to_pcm_frame(_sound, pcmStart);
    
    // Get current engine time and set stop time (one video frame duration)
    ma_uint64 engineTime = ma_engine_get_time_in_pcm_frames(_engine);
    ma_sound_set_stop_time_in_pcm_frames(_sound, engineTime + samplesPerVideoFrame);
    
    // Start playback
    ma_sound_start(_sound);
    
    _lastPlayedFrame.store(frame);
}

void AudioHandler::stop()
{
    std::lock_guard<std::mutex> lock(_mutex);
    
    if (_sound) {
        ma_sound_stop(_sound);
    }
    _lastPlayedFrame.store(-9999);
}

void AudioHandler::setFps(float fps)
{
    _fps = std::max(1.0f, fps);
}

int AudioHandler::getFileLengthInFrames() const
{
    if (!_fileLoaded.load() || _sampleRate == 0 || _fps <= 0) return 0;
    double durationSeconds = (double)_totalPcmFrames / (double)_sampleRate;
    return (int)(durationSeconds * _fps);
}

void AudioHandler::generateWaveform(int pixelWidth)
{
    std::lock_guard<std::mutex> lock(_mutex);
    
    delete[] waveformDataL;
    delete[] waveformDataR;
    waveformDataL = nullptr;
    waveformDataR = nullptr;
    waveformWidth = 0;
    
    if (!_fileLoaded.load() || _audioData.empty() || pixelWidth <= 0) return;
    
    waveformWidth = pixelWidth;
    waveformDataL = new float[pixelWidth]();
    waveformDataR = new float[pixelWidth]();
    
    size_t totalSamples = _audioData.size() / _channels;
    size_t samplesPerPixel = std::max(size_t(1), totalSamples / pixelWidth);
    
    for (int x = 0; x < pixelWidth; x++) {
        size_t start = x * samplesPerPixel;
        size_t end = std::min(start + samplesPerPixel, totalSamples);
        
        float maxL = 0, maxR = 0;
        
        for (size_t i = start; i < end; i++) {
            size_t idx = i * _channels;
            maxL = std::max(maxL, std::abs(_audioData[idx]));
            if (_channels >= 2)
                maxR = std::max(maxR, std::abs(_audioData[idx + 1]));
        }
        
        waveformDataL[x] = maxL;
        waveformDataR[x] = (_channels >= 2) ? maxR : maxL;
    }
}