# Changelog

## v1.3.0 (2026-04-11)

Simplified UI, precision improvements, beat detection overhaul.

### UI Simplified
- **Removed Insert Tempo Map and Match & Quantize modes** — edge case features that confused the workflow. Two clear modes remain:
  - **Match Tempo** — adjust playrate to project BPM with pitch preserved and auto-alignment
  - **Insert Stretch Markers** — mark detected beats, optionally quantize to REAPER's project grid
- **Clearer tooltips** — Match & Quantize tooltip no longer misleads about "aligning REAPER grid"

### Beat Detection Precision
- **Onset refinement** — after beat-this detection (20ms resolution), each beat position is snapped to the nearest audio transient using librosa onset detection. Sample-level accuracy (~0.05ms) instead of frame-level (~20ms)
- **Phase-aware BPM calculation** — based on CPJKU/beat_this#13: circular mean for optimal phase + linear regression on beat grid. More precise than the previous hybrid span/mean method
- **Octave correction range 78-185 BPM** — optimized for modern music (captures 140 BPM trap, filters 200+ artifacts). Was 40-240

### Quantization Fixed
- **"Quantize to grid" now uses REAPER's project grid** (TimeMap2 per-bar) — previously used internal downbeat calculation that didn't match REAPER's ruler
- **Correct pos/srcpos handling** — stretch markers now properly separate source position (where audio IS) from target position (where it should PLAY). Fixes markers appearing at wrong positions after Match Tempo
- **Smart threshold** — corrections limited to half a beat interval (not fixed 50ms). Prevents snapping to wrong grid beat while allowing larger corrections when needed

### Removed
- Insert Tempo Map mode (constant and variable)
- Match & Quantize mode
- Per-downbeat quantization (replaced by REAPER grid quantization)

### Tests
- 36 tests passing (was 26): added 10 quantization math tests

## v1.2.1 (2026-04-10)

Stability and platform update. Windows server launch completely reworked, GPU acceleration auto-detected, installer works without git. Based on community testing (Bassman002, squibs, Hipox).

### Windows Server Fix
- **Fixed server crash on launch** — Intel Fortran Runtime (PyTorch MKL) killed the server when REAPER's transient console was destroyed. Root cause: `start /B` shares parent console; when `os.execute()` returns, console is destroyed, `uv.exe` receives CTRL_CLOSE_EVENT and terminates before Python starts
- **New launch method: wscript** — server now launches in a persistent hidden console via VBScript. Zero visible windows, zero freeze, server survives independently
- **Fixed multiplying console windows** — port check fallback used `os.execute(netstat)` every ~30ms when LuaSocket was missing, creating visible windows. Now uses same mavriq-lua-sockets paths as the socket client, no os.execute fallback

### Quantization Fixes
- **Grid-independent tempo map snap** — replaced BR_GetClosestGridDivision (depends on user's grid setting) with TimeMap2_timeToBeats/beatsToTime. Always snaps to nearest bar line regardless of grid resolution
- **Fixed constant tempo marker position** — marker was placed at wrong position when first downbeat was not at item start (pickup/anacrusis)
- **Defensive timeline refresh** — explicit UpdateTimeline() between tempo map insertion and stretch marker quantization in Match & Quantize mode
- **New: "Quantize to grid" checkbox** in Insert Stretch Markers mode — snaps markers to existing tempo map without modifying it (requested by Hipox, squibs)

### GPU Acceleration
- **Auto-detect NVIDIA GPU** — installer checks for nvidia-smi and installs CUDA PyTorch (~2.5GB) from PyTorch cu124 index. Significantly faster detection, especially on longer tracks. Falls back to CPU gracefully

### Installer Improvements
- **No git required** — downloads ZIP archive when git is not installed (Windows + macOS/Linux)
- **ZIP update preserves .venv** — re-running installer doesn't re-download 800MB Python deps
- **Detects ZIP vs git installs** — no more "fatal: not a git repository" when updating ZIP installs
- **beat-this via archive URL** — `uv sync` no longer requires git for dependencies
- **Simplified update** — re-run the same install command to update (documented in README)

### UI
- **Minimum window width 360→420px** — prevents element overlap reported by Hipox
- **Long filename truncation** — names truncated with tooltip on hover

### Tests
- 26 tests passing (unchanged)

## v1.2.0 (2026-04-09)

Precision and quality update based on deep analysis of beat-this internals and community feedback (dukati, Bassman002, JetRed).

### Precision Improvements
- **Improved tempo accuracy** — hybrid span/mean calculation instead of median. Eliminates systematic error from beat-this 20ms frame quantization (e.g. 100 BPM → 101.8 BPM for a true 102 BPM track)
- **Neural downbeats** — uses beat-this's dedicated downbeat detection head instead of naive every-4th-beat. Correctly identifies bar boundaries even when audio doesn't start on beat 1
- **Fixed stretch marker quantization** — uses TimeMap2 instead of BR_GetClosestGridDivision. Markers now snap to nearest beat regardless of REAPER grid setting (was causing 0.52x stretch ratios)

### New Features
- **Editable BPM** — click detected tempo to override manually. Shows "(was X)" when edited. Useful when detection is close but not exact
- **Auto-align to bar** — Match Tempo automatically shifts item so first downbeat lands on nearest bar line. No more manual alignment (on by default)
- **Stretch quality mode** — choose Balanced, Transient, or Tonal algorithm for stretch markers via dropdown. Transient best for drums, Tonal for vocals/melodic

### Fixes
- **Windows server launch** — fixed nested cmd quotes that prevented server startup when scripts installed via installer (reported by Bassman002)
- **Tooltip** — corrected "madmom > librosa fallback" to accurately reflect beat-this as sole backend

### Tests
- 22 → 26 tests (quantized tempo, neural downbeats 4/4/3/4/empty)

## v1.1.0 (2026-04-09)

Feature update based on community feedback (Hipox).

### New Features
- **Match & Quantize mode** - new combo action: inserts variable tempo map first (aligns grid to audio), then inserts stretch markers quantized to that grid. Result: minimal stretching (0.99x-1.01x) instead of drastic corrections.
- **Snap first beat to bar** - tempo map automatically aligns first detected beat to nearest REAPER grid division (uses BR_GetClosestGridDivision with SnapToGrid fallback). No more floating tempo maps at random positions.
- **Multi-item detection cache** - switching between items preserves detection results. Come back to a previously analyzed item and beats/tempo/downbeats are instantly restored (shows "cached" in status). Cache clears on script exit.

### UI Improvements
- **Reordered actions** from simplest to most advanced: Match Tempo > Insert Tempo Map > Insert Stretch Markers > Match & Quantize
- **Match Tempo is now the default** action (most common use case)
- Each mode has clear tooltips explaining what it does and when to use it

## v1.0.1 (2026-04-09)

Hotfix for Windows.

### Fixes
- **SCRIPT_DIR detection** - pattern now matches Windows backslashes (reported by Hipox)
- **Project root discovery** - searches `~/ReaBeat/` and `~/Documents/ReaBeat/` when scripts are installed separately from repo
- **Branding** - unified naming to ReaBeat everywhere (ReaPack convention)

## v1.0.0 (2026-04-09)

Initial release.

### Features
- Beat detection using **beat-this** (CPJKU, ISMIR 2024) — state-of-the-art neural model, ~2-3s per song
- **Insert Tempo Map** — constant BPM or variable per-bar tempo markers
- **Insert Stretch Markers** — at every beat or downbeats only
- **Match Tempo** — adjust item playrate to project BPM or custom target, pitch preserved (elastique)
- Time signature estimation (4/4, 3/4)
- Confidence score based on tempo consistency
- Warns before overwriting existing tempo markers or stretch markers
- Full undo support (Ctrl+Z) for all actions

### UI
- REAPER-native dark theme with warm gold accent
- Three action modes: Tempo Map, Stretch Markers, Match Tempo
- "Match to project" one-click button reads current session BPM
- Custom BPM input field with live preview (shows rate change)
- Support menu with Ko-fi, Buy Me a Coffee, PayPal links
- Connection status with elapsed time counter during startup
- Clear error messages: missing deps, silent audio, MIDI items, file not found
- Compact, focused interface — no unnecessary controls

### Architecture
- Python backend with auto-launch and 5-minute idle timeout
- TCP localhost:9877, line-delimited JSON protocol
- Cross-platform: macOS, Windows, Linux
- 22 automated tests

### Backend
- beat-this as sole detection engine — no silent fallbacks to lower quality
- GPU auto-detection with automatic CPU fallback on CUDA failure
- Silent audio detection (RMS < 0.001 → clear error)
- MIDI item rejection with helpful message
- Audio too short (<2s) detection with clear error
