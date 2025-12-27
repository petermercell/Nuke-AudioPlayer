#include "audioHandler.h"

#include "DDImage/Iop.h"
#include "DDImage/Row.h"
#include "DDImage/Knobs.h"
#include "DDImage/Thread.h"

#include <Python.h>

#include <iostream>
#include <cstring>

static AudioHandler audioHandler;

static const char* const CLASS = "AudioPlayer";
static const char* const HELP = 
    "Plays audio synced to Nuke timeline.\n\n"
    "Frame-by-frame audio scrubbing.\n\n"
    "Supports WAV, MP3, FLAC, OGG.\n\n"
    "Version 2.5";

using namespace DD::Image;

class AudioPlayer : public Iop
{
    const char* _fileKnob;
    bool _enabled;
    bool _showWaveform;
    int _offset;
    float _fps;
    float _waveformHeight;

    Lock _lock;
    int _lastFrame;

public:
    int maximum_inputs() const override { return 1; }
    int minimum_inputs() const override { return 1; }

    AudioPlayer(Node* node) : Iop(node)
    {
        _fileKnob = "";
        _enabled = true;
        _showWaveform = true;
        _offset = 0;
        _fps = 25.0f;
        _waveformHeight = 1.0f;
        _lastFrame = -9999;
    }

    ~AudioPlayer() override = default;

    const char* input_label(int input, char* buffer) const override
    {
        return "input";
    }

    void knobs(Knob_Callback f) override
    {
        File_knob(f, &_fileKnob, "file_name", "Audio file");
        Tooltip(f, "Audio file (WAV, MP3, FLAC, OGG)");

        Bool_knob(f, &_enabled, "enabled", "Enable");
        SetFlags(f, Knob::STARTLINE);
        Tooltip(f, "Enable audio playback");

        Bool_knob(f, &_showWaveform, "show_waveform", "Waveform");
        Tooltip(f, "Show waveform overlay");

        Int_knob(f, &_offset, "offset", "Offset");
        SetFlags(f, Knob::STARTLINE);
        Tooltip(f, "Frame offset (+ delay, - advance)");

        Float_knob(f, &_fps, "fps", "FPS");
        SetFlags(f, Knob::STARTLINE);
        SetRange(f, 1.0, 120.0);
        Tooltip(f, "Timeline FPS - must match your project!");

        Float_knob(f, &_waveformHeight, "waveform_height", "Wave height");
        SetFlags(f, Knob::STARTLINE);
        SetRange(f, 0.0, 2.0);
        Tooltip(f, "Waveform scale (1.0 = full height)");

        Divider(f, "");
        
        Text_knob(f, "", "AudioPlayer v2.5\nby Hendrik Proosa & Peter Mercell");
        SetFlags(f, Knob::DISABLED);

        Iop::knobs(f);
    }

    int knob_changed(Knob* k) override
    {
        if (k->is("file_name")) {
            audioHandler.setFileLoaded(false);
            return 1;
        }
        if (k->is("enabled") && !_enabled) {
            audioHandler.stop();
            return 1;
        }
        if (k->is("fps")) {
            audioHandler.setFps(_fps);
            audioHandler.setFileLoaded(false);
            return 1;
        }
        return Iop::knob_changed(k);
    }

    void append(Hash& hash) override
    {
        // Include frame in hash - makes node time-varying
        hash.append(outputContext().frame());
    }

    void _validate(bool for_real) override
    {
        // Validate inputs
        for (int i = 0; i < maximum_inputs(); ++i) {
            if (input(i)) input(i)->validate(for_real);
        }
        copy_info();
        
        // Handle audio
        if (_enabled) {
            Guard guard(_lock);
            
            int currentFrame = (int)outputContext().frame();
            
            // Load file if needed
            if (!audioHandler.fileLoaded() && _fileKnob && _fileKnob[0]) {
                if (audioHandler.loadFile(_fileKnob, _fps)) {
                    if (input0().format().width() > 0) {
                        audioHandler.generateWaveform(input0().format().width());
                    }
                }
            }
            
            // Play audio at current frame (only if frame changed)
            if (audioHandler.fileLoaded() && currentFrame != _lastFrame) {
                int audioFrame = currentFrame - _offset;
                int fileLen = audioHandler.getFileLengthInFrames();
                
                // Play if in valid range
                if (audioFrame >= 0 && audioFrame < fileLen) {
                    audioHandler.playAtFrame(audioFrame);
                }
                
                _lastFrame = currentFrame;
                
                // Clear caches so next frame will trigger _validate again
                PyGILState_STATE gstate = PyGILState_Ensure();
                PyRun_SimpleString("import nuke; nuke.clearRAMCache(); nuke.clearDiskCache()");
                PyGILState_Release(gstate);
            }
        }
    }

    void _request(int x, int y, int r, int t, ChannelMask channels, int count) override
    {
        input(0)->request(input0().info().channels(), count);
    }

    void _open() override
    {
        if (audioHandler.fileLoaded() && 
            audioHandler.getWaveformWidth() != input0().format().width()) {
            audioHandler.generateWaveform(input0().format().width());
        }
    }

    void engine(int y, int x, int r, ChannelMask channels, Row& row) override
    {
        Row in(x, r);
        in.get(input0(), y, x, r, channels);
        if (aborted()) return;

        // Draw waveform
        if (audioHandler.fileLoaded() && _showWaveform) {
            int maxWidth = input0().format().width();
            int maxHeight = input0().format().height();
            float* waveL = audioHandler.getWaveformL();
            float* waveR = audioHandler.getWaveformR();
            int waveWidth = audioHandler.getWaveformWidth();

            int currentFrame = (int)outputContext().frame() - _offset;
            int fileLen = audioHandler.getFileLengthInFrames();
            int cursorPos = fileLen > 0 ? (currentFrame * maxWidth / fileLen) : 0;
            
            // Waveform center line (middle of image)
            int centerY = maxHeight / 2;
            float waveScale = _waveformHeight;

            foreach(z, channels) {
                float* CUR = row.writable(z) + x;
                const float* inptr = in[z] + x;
                const float* END = row[z] + r;
                int pos = x;

                while (CUR < END) {
                    float out = *inptr;

                    if (waveL && waveR && waveWidth > 0) {
                        // Map pixel position to waveform position
                        int wavePos = (pos * waveWidth) / maxWidth;
                        if (wavePos >= waveWidth) wavePos = waveWidth - 1;
                        if (wavePos < 0) wavePos = 0;
                        
                        // Left channel amplitude (goes UP from center) - RED
                        float leftAmp = waveL[wavePos];
                        float leftHeight = leftAmp * waveScale * centerY;
                        // Right channel amplitude (goes DOWN from center) - GREEN
                        float rightAmp = waveR[wavePos];
                        float rightHeight = rightAmp * waveScale * centerY;
                        
                        // Draw left channel (RED) - above center line
                        if (z == Chan_Red) {
                            if (y >= centerY && y <= centerY + leftHeight) {
                                // Brighter when louder (amplitude-based intensity)
                                float intensity = 0.4f + 0.6f * leftAmp;
                                out = std::max(out, intensity);
                            }
                            // Peak edge - brightest
                            if (leftHeight > 1 && y >= (int)(centerY + leftHeight - 2) && y <= (int)(centerY + leftHeight)) {
                                out = 1.0f;
                            }
                        }
                        
                        // Draw right channel (GREEN) - below center line  
                        if (z == Chan_Green) {
                            if (y <= centerY && y >= centerY - rightHeight) {
                                // Brighter when louder
                                float intensity = 0.4f + 0.6f * rightAmp;
                                out = std::max(out, intensity);
                            }
                            // Peak edge - brightest
                            if (rightHeight > 1 && y <= (int)(centerY - rightHeight + 2) && y >= (int)(centerY - rightHeight)) {
                                out = 1.0f;
                            }
                        }
                    }
                    
                    // Playhead cursor (BLUE vertical line)
                    if (z == Chan_Blue && pos >= cursorPos - 1 && pos <= cursorPos + 1) {
                        out = 1.0f;
                    }

                    *CUR++ = out;
                    inptr++;
                    pos++;
                }
            }
        } else {
            foreach(z, channels) {
                memcpy(row.writable(z) + x, in[z] + x, (r - x) * sizeof(float));
            }
        }
    }

    static const Description desc;
    const char* Class() const override { return CLASS; }
    const char* node_help() const override { return HELP; }
    const char* displayName() const override { return "AudioPlayer"; }
};

static Iop* build(Node* node) { return new AudioPlayer(node); }
const Iop::Description AudioPlayer::desc(CLASS, "Other/AudioPlayer", build);
