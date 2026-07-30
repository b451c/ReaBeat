# ReaBeat

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Latest Release](https://img.shields.io/github/v/release/b451c/ReaBeat)](https://github.com/b451c/ReaBeat/releases/latest)
[![macOS](https://img.shields.io/badge/macOS-arm64%20%7C%20Intel-blue)]()
[![Windows](https://img.shields.io/badge/Windows-x64-blue)]()
[![Linux](https://img.shields.io/badge/Linux-x86__64-blue)]()

Neural beat detection and tempo mapping for [REAPER](https://www.reaper.fm/). State-of-the-art machine learning meets professional audio workflow.

![ReaBeat 2.0](images/baner.png)

## Why ReaBeat?

REAPER has no built-in beat detection. ReaBeat adds it as a native extension: one file in UserPlugins, zero dependencies. It uses [beat-this](https://github.com/CPJKU/beat_this) (CPJKU, ISMIR 2024) - the neural network with the best published F1 scores for beat and downbeat tracking.

**No Python. No server. No installer. Drop one file, detect beats.**

<div align="center">
  <img src="images/ReaBeat_3.0.png" alt="ReaBeat in action">
</div>

## Features

### Detection
- **Neural beat detection** - beat-this model (ISMIR 2024, state-of-the-art accuracy)
- **Automatic downbeat detection** - neural downbeats, not naive every-4th-beat
- **Time signature** - detected from downbeat spacing, plus a manual override dropdown (Auto / 2/4 / 3/4 / 4/4 / 6/8 / 9/8 / 12/8) that recomputes bars - use it when compound meters get detected as 4/4
- **Cancellable** - the Detect button turns into Cancel while detection runs
- **BPM from filename** - parses "120bpm" patterns, shows hint if different from detected
- **Per-item cache** - switch items without re-detecting, shows "(cached)"

### Three Action Modes
- **Match Tempo** - adjust playrate to target BPM (pitch preserved). Match to project tempo or to another detected item for multi-track sync.
- **Insert Tempo Map** - sync REAPER's grid to audio. Constant, variable-bars, or variable-beats. Only tempo markers inside the item are replaced (existing markers elsewhere in the project are preserved), and the item is locked to time so the audio doesn't stretch when the grid changes.
- **Insert Stretch Markers** - quantize audio to grid with four modes:
  - **Straight** - mathematical grid from detected BPM (default, best for modern music)
  - **Bars** - downbeat subdivision with variable bar lengths (live recordings)
  - **Project grid** - snap to REAPER's project grid (multi-track sync)
  - **Strength** slider (0-100%) - partial quantization, industry standard

### Interactive Waveform Editor
- **Beat editing** - drag, add (double-click), delete (right-click), toggle downbeat
- **Marker editing** - after Apply, edit individual stretch markers directly in REAPER
- **Gap highlighting** - red tint over missing-beat regions with teal suggestion lines at the strongest transients; an on-canvas hint shows the **N** shortcut to jump to the next gap
- **Seek** - click waveform to set REAPER cursor (accounts for stretch markers)
- **Zoom/scroll** - mouse wheel zoom, shift+scroll / trackpad swipe / middle-mouse drag to pan, clickable+draggable scroll thumb at the bottom edge
- **Playhead tracking** - auto-follow during playback
- **UI scale** - 100/125/150/200% (click the ReaBeat title), persisted across sessions

### Multi-Track Sync
- Select reference item from "Match to:" dropdown
- One click: tempo map + playrate + downbeat alignment + stretch markers on both tracks
- Single Ctrl+Z undoes the entire operation

### Quality
- **Onset refinement** - snaps each beat to nearest audio transient (+/-30ms, sample-level precision). Currently disabled past 10 minutes of audio to avoid a multi-gigabyte spectral buffer; the neural model alone still gives ~20 ms precision in that case. A streaming rewrite that lifts the length cap is on the [roadmap](docs/roadmap.md).
- **Beat interpolation** - fills gaps in quiet sections using sub-threshold model hints
- **Consistency pass** - removes isolated false-positive beats
- **Octave correction** - [/2] [x2] buttons, 78-185 BPM range
- **Editable BPM** - click to override detected tempo

## Keyboard Shortcuts

| Action | Shortcut |
|--------|----------|
| Play/Stop | Space |
| Apply action | Enter |
| Next gap | N |
| Undo | Cmd+Z / Ctrl+Z |
| Redo | Cmd+Shift+Z / Ctrl+Shift+Z |
| Pan waveform | Middle-mouse drag / Shift+scroll |

## Installation

**Requirements:** REAPER 7.0 or newer is recommended. On older REAPER versions the extension may silently fail to load (the extension API version won't match) — if ReaBeat doesn't appear in the Extensions menu after installing, update REAPER first.

### ReaPack (recommended)

1. Install [ReaPack](https://reapack.com/) if you haven't already
2. Extensions > ReaPack > Import repositories...
3. Paste: `https://raw.githubusercontent.com/b451c/ReaBeat/main/index.xml`
4. Extensions > ReaPack > Browse packages > search "ReaBeat"
5. Right-click > Install
6. Restart REAPER

### Manual Install

Download **both files** for your platform from [Releases](https://github.com/b451c/ReaBeat/releases/latest) and place them in REAPER's UserPlugins folder. Both files must be in the same folder.

**macOS (Apple Silicon)** — `~/Library/Application Support/REAPER/UserPlugins/`

| Download from Release | Save as |
|-----------------------|---------|
| `reaper_reabeat-arm64.dylib` | `reaper_reabeat-arm64.dylib` |
| `libonnxruntime-macOS-arm64.dylib` | `libonnxruntime.1.24.4.dylib` |

**macOS (Intel)** — `~/Library/Application Support/REAPER/UserPlugins/`

| Download from Release | Save as |
|-----------------------|---------|
| `reaper_reabeat-x86_64.dylib` | `reaper_reabeat-x86_64.dylib` |
| `libonnxruntime-macOS-x86_64.dylib` | `libonnxruntime.1.16.3.dylib` |

Intel builds bundle ONNX Runtime 1.16.3 - the last release that runs on macOS 10.15 (Catalina) - so older Macs are supported. Apple Silicon builds use ORT 1.24.4 (macOS 11+).

**Windows 64-bit** — `%APPDATA%\REAPER\UserPlugins\`

| Download from Release | Save as |
|-----------------------|---------|
| `reaper_reabeat-x64.dll` | `reaper_reabeat-x64.dll` |
| `onnxruntime.dll` | `onnxruntime.dll` |

**Linux x86_64** — `~/.config/REAPER/UserPlugins/`

| Download from Release | Save as |
|-----------------------|---------|
| `reaper_reabeat-x86_64.so` | `reaper_reabeat-x86_64.so` |
| `libonnxruntime.so.1-Linux-x86_64` | `libonnxruntime.so.1` |

**Linux aarch64** — `~/.config/REAPER/UserPlugins/`

| Download from Release | Save as |
|-----------------------|---------|
| `reaper_reabeat-aarch64.so` | `reaper_reabeat-aarch64.so` |
| `libonnxruntime.so.1-Linux-aarch64` | `libonnxruntime.so.1` |

> **Important:** On Linux, the ONNX Runtime library must be renamed — remove the platform suffix so the file is called `libonnxruntime.so.1`. ReaPack does this automatically; manual install requires the rename.

Restart REAPER after installing.

### Linux Requirements

Linux users may need to install graphics drivers and curl if not already present:

```bash
sudo apt install mesa-utils libcurl4
```

`mesa-utils` provides the OpenGL context JUCE needs for rendering. `libcurl4` is required for model auto-download. Most desktop Linux distributions have these pre-installed.

### First Run

On first launch, ReaBeat downloads the neural network model (~79MB) to `~/.reabeat/models/`. A progress bar in the plugin window shows the download status; the **Detect Beats** button is replaced with **Downloading model...** until the download completes. This happens once; subsequent launches use the cached model.

**Portable / offline install:** if `~/.reabeat/models/` is not usable (portable REAPER, restricted network), place `beat_this_final0.onnx` directly in your UserPlugins folder next to the plugin binary. ReaBeat will load the model from there and skip the download. The file is available from the [v2.0.0-model release](https://github.com/b451c/ReaBeat/releases/tag/v2.0.0-model).

**Long items:** detection has been tested on 30+ minute drum stems and full-length classical material. Items beyond 10 minutes skip per-transient refinement (which would otherwise demand multi-gigabyte spectral buffers) - the neural model alone already gives ~20 ms beat precision.

## Usage

1. Select a media item in REAPER
2. Extensions > ReaBeat (or assign to toolbar/shortcut)
3. Click **Detect Beats**
4. Review beats on the waveform - edit if needed (drag, double-click, right-click)
5. Choose action mode: Match Tempo, Tempo Map, or Stretch Markers
6. Adjust settings (quantize mode, strength, quality)
7. Click **Apply**
8. In marker edit mode: fine-tune individual stretch markers directly

## Building from Source

```bash
git clone https://github.com/b451c/ReaBeat.git
cd ReaBeat
git submodule update --init    # JUCE, WDL, reaper-sdk
```

Download [ONNX Runtime](https://github.com/microsoft/onnxruntime/releases) to `vendor/onnxruntime/` (headers in `include/`, library in `lib/`).

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

Copy the built binary to REAPER's UserPlugins folder and restart REAPER.

## Architecture

```
reaper_reabeat.dylib / .dll / .so
    |
    +-- ReaperPluginEntry()       REAPER extension entry point
    +-- JUCE UI                   Window, controls, waveform editor
    +-- MelSpectrogram            PocketFFT, Slaney mel, matching torchaudio
    +-- ONNX Inference            Chunked, 50fps, beat-this model
    +-- Postprocessing            Peak detection, interpolation, onset refinement
    +-- REAPER API                Direct calls (no TCP, no Lua wrapper)
```

| Module | Purpose |
|--------|---------|
| `BeatDetector` | High-level detection orchestration |
| `MelSpectrogram` | FFT + mel filterbank (PocketFFT) |
| `InferenceProcessor` | ONNX chunked inference with overlap |
| `Postprocessor` | Peak detection + deduplication |
| `TempoEstimator` | Phase-aware BPM (circular mean + regression) |
| `BeatInterpolator` | Fill missing beats in quiet sections |
| `OnsetRefinement` | Snap to audio transients (+/-30ms) |
| `ReaperActions` | All REAPER API operations (undo blocks) |
| `MainComponent` | Full UI with waveform editor |
| `ModelManager` | Model download and caching |

## Tested On

| Platform | OS | Architecture | REAPER |
|----------|----|-------------|--------|
| macOS | macOS 26.3.1 (Tahoe) | Apple M1 Pro (arm64) | v7.x |
| Windows | Windows 11 Enterprise | x64 | v7.6.8 |
| Linux | Ubuntu 25.10 (Questing Quokka), kernel 6.17.0-20-generic | aarch64 (ARM64) | v7.69 |

## Previous Version (v1.3.1)

The Lua/Python version is preserved on the [`v1-lua` branch](https://github.com/b451c/ReaBeat/tree/v1-lua) and at the [`v1.3.1` tag](https://github.com/b451c/ReaBeat/releases/tag/v1.3.1). It requires Python + uv + beat-this. See the [v1 README](https://github.com/b451c/ReaBeat/blob/v1-lua/README.md) for installation.

## Support

If ReaBeat saves you time, consider supporting development:

- [Ko-fi](https://ko-fi.com/quickmd)
- [Buy Me a Coffee](https://buymeacoffee.com/bsroczynskh)
- [PayPal](https://www.paypal.com/paypalme/b451c)

## License

[MIT](LICENSE) - ReaBeat is free and open source.

Uses [JUCE](https://juce.com/) (AGPL), [ONNX Runtime](https://onnxruntime.ai/) (MIT), [PocketFFT](https://gitlab.mpcdf.mpg.de/mtr/pocketfft) (BSD-3), [beat-this](https://github.com/CPJKU/beat_this) model (CC BY-NC-SA 4.0).

## Links

- [REAPER Forum Thread (v2)](https://forum.cockos.com/showthread.php?t=308368)
- [REAPER Forum Thread (v1, Lua/Python)](https://forum.cockos.com/showthread.php?t=308240)
- [GitHub Repository](https://github.com/b451c/ReaBeat)
- [beat-this Paper (ISMIR 2024)](https://github.com/CPJKU/beat_this)


---

Made by [falami.studio](https://falami.studio/lab/reabeat/) — audio production & engineering studio.
