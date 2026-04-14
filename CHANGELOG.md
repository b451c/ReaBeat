# Changelog

All notable changes to ReaBeat are documented here. Based on [Keep a Changelog](https://keepachangelog.com/). Adheres to [Semantic Versioning](https://semver.org/).

## [2.0.0] - 2026-04-14

Complete rewrite as native C++ REAPER extension. No Python, no server, no installer.

### Added
- **Native REAPER extension** - single .dylib/.dll/.so file in UserPlugins, zero dependencies
- **Interactive waveform editor** - mirrored RMS waveform with beat overlay, bar numbers, time ruler
- **Beat editing mode** - drag beats, add (double-click), delete (right-click), toggle downbeat status
- **Marker editing mode** - after Apply, edit individual REAPER stretch markers directly on waveform
- **Gap highlighting** - red tint over missing-beat regions with RMS peak suggestion lines
- **Click-to-seek** - click waveform to jump REAPER cursor (accounts for stretch markers)
- **Live playhead** - tracks playback position with auto-follow
- **Beat flash indicator** - gold dot pulses on each beat during playback
- **Metronome toggle** - button in header syncs REAPER metronome
- **Quantize Strength slider** (0-100%) - partial quantization matching Pro Tools, Ableton, Cubase, Logic, Studio One
- **Straight quantize mode** - mathematical grid from detected BPM, best for modern produced music
- **Bars quantize mode** - downbeat subdivision with variable bar lengths, for live recordings
- **Project grid quantize mode** - snap to REAPER's project grid for multi-track sync
- **Multi-track sync** - select reference item, one click syncs tempo map + playrate + alignment + stretch markers on both tracks
- **Set session tempo** - one button syncs REAPER project BPM
- **Beat interpolation** - fills missing beats in quiet sections using sub-threshold model logits
- **Beat consistency pass** - removes isolated false-positive beats (validated +0.5% on 53-track test)
- **Onset refinement** - snaps each beat to nearest audio transient (+/-30ms, spectral flux)
- **BPM from filename** - parses patterns like "120bpm", shows hint if different from detected
- **Keyboard shortcuts** - Space (play/stop), Enter (apply), N (next gap), Cmd+Z/Shift+Z (undo/redo)
- **Tooltips** - toggleable, 600ms delay
- **Support menu** - Ko-fi, Buy Me a Coffee, PayPal, GitHub links
- **Model auto-download** - 79MB ONNX model downloaded on first launch to ~/.reabeat/models/
- **Async model download** - UI stays responsive during model download with progress updates
- **Dockable window** - supports REAPER's docker on all platforms (macOS, Windows, Linux)
- **Per-item detection cache** - switch items without re-detecting

### Changed
- **Same beat-this model** (CPJKU, ISMIR 2024) - same accuracy, now running via ONNX Runtime instead of PyTorch
- **Same three action modes** - Match Tempo, Insert Tempo Map, Insert Stretch Markers
- **Same phase-aware BPM** - circular mean + linear regression, octave correction 78-185 BPM
- **Quantize modes renamed** - "Constant tempo" is now "Bars", "Session grid" is now "Project grid" (industry-standard terminology)

### Fixed
- **Mel spectrogram normalization** - restored sqrt(n_fft) division matching torchaudio normalized="frame_length". Without this, model hallucinated beats on some tracks (714 vs 413 on test case).
- **Multi-track sync undo** - entire pipeline now in single undo block (was: 3 separate operations, some without undo)
- **Null pointer crashes** - guards added for item deselection during detection, deleted items in sync, division by zero on tempo=0
- **PreventUIRefresh safety** - RAII guard ensures REAPER UI never freezes on exception
- **BeatInterpolator hint double-add** - prevented same logit hint from filling multiple expected positions
- **Detection cache limit** - evicts entries beyond 50 items to prevent unbounded memory growth
- **Windows ORT DLL conflict** - delay-load + pre-load from UserPlugins prevents System32 v1.17 mismatch
- **Linux window rendering** - SWELL_CreateXBridgeWindow bridges SWELL HWND to X11 for JUCE embedding
- **Windows docking** - dual-mode Win32 dialog (WS_CHILD for docked, WS_POPUP for floating)

### Removed
- Python backend, TCP server, Lua scripts, installer scripts (preserved on `v1-lua` branch)
- Experimental SuperFlux onset refinement (no measurable improvement)
- Experimental logit-guided downbeat fill (-0.1% regression on batch test)

---

## [1.3.1] - 2026-04-12

Restored Insert Tempo Map with improved reliability.

### Added
- Insert Tempo Map: three modes (Constant, Variable-bars, Variable-beats)
- Downbeat cleaning: removes erroneous extras, fills gaps

## [1.3.0] - 2026-04-11

Simplified UI, precision improvements, beat detection overhaul.

### Changed
- Removed Insert Tempo Map and Match & Quantize (edge case features)
- Clearer tooltips

### Added
- Onset refinement via librosa (+/-30ms snap)
- BPM editable by user
- GPU auto-detection and CUDA install

## [1.2.1] - 2026-04-10

Quantize to grid fix, multi-item cache.

## [1.1.0] - 2026-04-09

Stretch markers, quantize modes, downbeat support.

## [1.0.1] - 2026-04-08

Bug fixes for installer and server launch.

## [1.0.0] - 2026-04-08

Initial release. Lua UI + Python backend + TCP server.
