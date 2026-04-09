# Changelog

## v1.0.0 (2026-04-09)

Initial release.

### Features
- Beat detection using **beat-this** (CPJKU, ISMIR 2024) — state-of-the-art neural model
- **Insert Tempo Map** — constant BPM or variable per-bar tempo markers
- **Insert Stretch Markers** — at every beat or downbeats only
- Time signature estimation (4/4, 3/4)
- Confidence score based on tempo consistency
- Warns before overwriting existing tempo markers or stretch markers
- Full undo support (Ctrl+Z)

### UI
- REAPER-native dark theme with warm gold accent
- Compact, focused interface — no unnecessary controls
- Support menu with Ko-fi, Buy Me a Coffee, PayPal links
- Connection status with elapsed time counter during startup
- Clear error messages for every failure mode

### Architecture
- Python backend with auto-launch and 5-minute idle timeout
- TCP localhost:9877, line-delimited JSON protocol
- Cross-platform: macOS, Windows, Linux
- 22 automated tests

### Backend
- beat-this as sole detection engine — no silent fallbacks
- GPU auto-detection with CPU fallback on CUDA failure
- Silent audio detection (RMS < 0.001 → clear error)
- MIDI item rejection with helpful message
