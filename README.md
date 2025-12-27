# Nuke AudioPlayer v2.5

**Frame-by-frame audio scrubbing for Foundry Nuke**

A Nuke plugin that plays audio synchronized to the timeline, allowing you to hear audio while scrubbing through frames. Essential for animation, lip-sync, and music video work.

## Features

- **Per-frame audio playback** - Hear audio when scrubbing through timeline
- **Stereo waveform visualization** - Red (Left channel up) / Green (Right channel down)
- **Amplitude-based rendering** - Louder parts appear brighter
- **Frame offset control** - Adjust audio sync with +/- frame offset
- **Multiple format support** - WAV, MP3, FLAC, OGG
- **Cross-platform** - Linux, Windows, macOS
- **No external dependencies** - Uses miniaudio (header-only, compiled in)

## Installation

### Pre-built Binaries

Download from [Releases](https://github.com/petermercell/Nuke-AudioPlayer/releases):

| Platform | File | Install Location |
|----------|------|------------------|
| Linux | `AudioPlayer.so` | `~/.nuke/` |
| Windows | `AudioPlayer.dll` | `%USERPROFILE%\.nuke\` |
| macOS (Apple Silicon) | `AudioPlayer.dylib` | `~/.nuke/` |

## Usage

1. Create an **AudioPlayer** node: `Tab` → `Other` → `AudioPlayer`
2. Connect your image/video to the input
3. Select an audio file (WAV, MP3, FLAC, OGG)
4. **Set FPS to match your project** (important!)
5. Scrub through the timeline to hear audio

### Node Controls

| Knob | Description |
|------|-------------|
| **Audio file** | Path to audio file |
| **Enable** | Toggle audio playback on/off |
| **Waveform** | Show/hide waveform overlay |
| **Offset** | Frame offset (+ delays audio, - advances audio) |
| **FPS** | Timeline FPS - must match your project! |
| **Wave height** | Waveform vertical scale (0.0 - 2.0) |

### Waveform Display

- **Red (up from center)** - Left audio channel
- **Green (down from center)** - Right audio channel  
- **Blue vertical line** - Current playhead position
- **Brightness** - Based on amplitude (louder = brighter)

## Requirements

### Runtime
- Nuke 14.1 or later
- No additional libraries required

### Building
| Platform | Requirements |
|----------|--------------|
| Linux | GCC 9+, CMake 3.15+ |
| Windows | Visual Studio 2022, CMake 3.15+, Python 3.x installed |
| macOS | Xcode 14+ / Clang, CMake 3.15+ |

## Supported Versions

| Nuke Version | Python | Status |
|--------------|--------|--------|
| 16.0+ | 3.11 | ✅ Tested |
| 15.0+ | 3.10 | ✅ Tested|
| 14.1+ | 3.9 | ✅ Tested|

## Project Structure

```
Nuke-AudioPlayer/
├── include/
│   ├── audioHandler.h
│   └── miniaudio.h
├── src/
│   ├── audioplayer.cpp
│   └── audioHandler.cpp
├── CMakeLists.txt          # Linux
├── CMakeLists_windows.txt  # Windows
├── CMakeLists_macos.txt    # macOS
├── build.sh                # Linux build script
└── README.md
```

## Technical Details

### Audio Backend

This version uses [miniaudio](https://miniaud.io/) - a single-file, public domain audio library:
- No external dependencies (header-only)
- Modern audio API support (WASAPI, CoreAudio, PulseAudio/ALSA)
- Low-latency playback optimized for scrubbing

### How It Works

1. Audio is loaded and decoded when you select a file
2. On each frame change, a short audio snippet (1 frame duration) is played
3. Viewer cache is cleared via Python to ensure playback on cached frames
4. Waveform is generated from audio peaks and rendered as overlay

## Credits

**Original Author:** [Hendrik Proosa](https://gitlab.com/hendrikproosa/nuke-audioplayer)

**v2.5 Modernization:** [Peter Mercell](https://github.com/petermercell)
- Replaced OpenAL/ALUT with miniaudio
- Added stereo waveform with amplitude-based brightness
- Cross-platform support (Windows, macOS)
- Per-frame scrubbing improvements
- Automatic viewer cache handling

## License

**MIT License**

miniaudio is public domain / MIT No Attribution - free for commercial use.

## Links

- **GitHub:** https://github.com/petermercell/Nuke-AudioPlayer
- **Original:** https://gitlab.com/hendrikproosa/nuke-audioplayer
- **Nukepedia:** https://www.nukepedia.com/tools/plugins/other/audioplayer/
- **miniaudio:** https://miniaud.io/

## Changelog

### v2.5 (2024)
- Replaced OpenAL/ALUT with miniaudio (no dependencies)
- Added stereo waveform with amplitude-based brightness
- Cross-platform: Linux, Windows, macOS
- Per-frame audio scrubbing
- Automatic viewer cache clearing
- Wave height control

### v1.0 (Original by Hendrik Proosa)
- OpenAL-based audio playback
- Basic waveform display
- Linux support
