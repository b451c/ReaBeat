# ReaBeat Progress Log

## Current State (2026-04-12)

**Version**: v1.3.1
**Branch**: main (feature/coreml-onnx preserved but not merged)
**Repo**: https://github.com/b451c/ReaBeat
**Tests**: 36 passing (10.9s)
**Forum**: https://forum.cockos.com/showthread.php?t=308240
**Status**: v1.3.1 released. Restored Insert Tempo Map (3 modes), neural downbeat cleaning. CoreML/ONNX explored but not shipped (lossy conversion). 12+ engaged users, 33+ forum posts.

## v1.3.1 - Insert Tempo Map Restored (2026-04-12)

### Insert Tempo Map (restored)
- Three modes: Constant (single BPM), Variable - bars (per-downbeat), Variable - beats (per-beat)
- Syncs REAPER's grid to audio without modifying the item
- Octave correction (78-185 BPM) and BPM filtering (>25% deviation rejected) for robust variable tempo
- Community-requested by Daodan (forum post #33)

### Neural Downbeat Cleaning
- Removes erroneous extra downbeats (<60% expected bar duration)
- Fills missing downbeats (>160% expected bar duration)
- Uses detected BPM or median interval as reference
- Improves tempo map accuracy, bar count, downbeat-only stretch markers

### CoreML/ONNX Exploration (NOT shipped)
- Built full infrastructure: CoreML backend, ONNX backend, shared postprocessing, model manager, conversion scripts
- CoreML: 2.5x faster on macOS, 7x smaller install, but model conversion is lossy (beat positions drift)
- ONNX: works cross-platform but slower than PyTorch (3-model ensemble overhead)
- 3-model ensemble: BPM identical on 8/10 tracks, confidence mixed (better on 4/10, worse on 3/10)
- Decision: PyTorch single final0 model remains the best option. Branch preserved for future
- Blockers: coremltools PR #2632 (lossless conversion), no competing model with released code beats beat-this

### Research Findings
- No model beats beat-this with available code (BeatFM, HingeNet, BEAST have no released weights)
- SuperFlux onset detection, onset backtracking: not verified in isolation, not shipped
- Phase-aware BPM from CPJKU/beat_this#13 already implemented in v1.3.0
- Paulllux (BeatForge) uses same model with CoreML in JUCE plugin (92% Giant Steps accuracy)

## v1.0.0 - Initial Release (2026-04-09)

### Project Setup
- Created standalone project (separate from RemixTool/reamix.me)
- Python backend: beat detector, TCP server, CLI
- Lua frontend: UI, actions, socket, server launcher, theme
- pyproject.toml with beat-this + torch as core deps
- MIT license, open source

### Beat Detection
- beat-this (CPJKU, ISMIR 2024) as sole backend
- No fallback chain - clear errors instead of silent degradation
- GPU auto-detection with CPU fallback
- Silent audio detection, MIDI item rejection

### Three Action Modes (v1.0.0)
- Insert Tempo Map: constant or variable per-bar
- Insert Stretch Markers: every beat or downbeats only
- Match Tempo: adjust playrate to project BPM (pitch preserved)

### Cross-Platform
- Server launcher with OS detection (macOS/Windows/Linux)
- Install scripts: install.sh (bash) + install.ps1 (PowerShell)

### Forum Launch
- Cited nofish and todd_r from REAmix thread as inspiration
- Community response: Lunar Ladder, saxmand, X-Raym, Hipox

## v1.0.1 - Windows Hotfix (2026-04-09)

### Fixes (reported by Hipox)
- SCRIPT_DIR: forward slash only -> matches backslashes
- Project root: added ~/ReaBeat/ and ~/Documents/ReaBeat/ search
- Branding unified: REABeat -> ReaBeat

## v1.1.0 - Community Features (2026-04-09)

### New Features (all requested by Hipox)
- **Match & Quantize mode**: combo action - variable tempo map first, then stretch markers quantized to that grid
- **Snap first beat to bar**: tempo map aligns first beat to nearest REAPER grid division
- **Multi-item detection cache**: results stored per item GUID

### UI Improvements
- Reordered actions: Match Tempo (default) > Tempo Map > Stretch Markers > Match & Quantize
- Each mode has tooltips explaining what it does and when to use it

## v1.2.0 - Precision & Quality (2026-04-09)

### Precision Improvements
- **Hybrid tempo calculation**: span/mean instead of median. Eliminates systematic error from beat-this 20ms quantization (e.g. Rise: 100.0 → 101.81 BPM, true 102)
- **Neural downbeats**: uses beat-this dedicated downbeat head instead of naive every-4th-beat. 0% agreement on drum beat (neural correctly identified pickup, naive assumed first beat = downbeat)
- **TimeMap2 quantization**: stretch markers snap to nearest beat regardless of REAPER grid setting. Fixed 0.52x stretch ratio bug

### New Features
- **Editable BPM**: click detected value to override. Shows "(was X)". Affects Match Tempo rate and constant tempo map only — beat positions are absolute seconds
- **Auto-align to bar**: Match Tempo shifts item so first neural downbeat lands on nearest bar line (on by default)
- **Stretch quality mode**: Balanced (default) / Transient (drums) / Tonal (vocals) via I_STRETCHFLAGS

### Fixes
- **Windows server launch**: .bat file instead of nested cmd quotes (fixed "Server did not start within 30s" for Bassman002)
- **Tooltip**: removed false "madmom > librosa fallback" claim
- **Windows installer (v1.1.0+)**: fixed nested irm|iex causing PowerShell to close

### Community (new in v1.2.0 cycle)
- **Bassman002**: Windows installer worked but server failed (cmd quoting) — fixed
- **JetRed**: REAPER Portable — answered with manual install instructions, added to README
- **dukati**: "pretty amazing", reported 2/4 vs 3/4 and 200 vs 100 BPM — addressed with neural downbeats + editable BPM

### Research
- Analyzed beat-this paper (arXiv:2407.21658) and source code in detail
- Confirmed: 50fps, 20ms frames, hop=441, dbn=False best for varied music
- Discovered discarded neural downbeats → now using them
- Researched all competitors (BeatFM, HingeNet, BeatNet, BEAST, etc.) — beat-this remains best practical choice
- HingeNet's harmonic-aware mechanism interesting but requires foundation model — can't apply as post-processing

## v1.2.1 - Windows & Platform Fix (2026-04-10)

### Windows Server Launch (Critical)
- **Root cause discovered**: `start /B` shares parent's transient console. When Lua `os.execute()` returns, console destroyed, uv.exe killed by CTRL_CLOSE_EVENT before Python starts. Intel Fortran Runtime (MKL) also crashes with "forrtl: error (200)"
- **Fix**: wscript hidden console — persistent, independent of parent lifecycle
- **Secondary**: `is_port_open()` used os.execute(netstat) fallback without LuaSocket → visible console windows every ~30ms. Fixed with shared socket paths, no os.execute fallback

### Quantization Fixes
- **BR_GetClosestGridDivision → TimeMap2** for tempo map snap (was grid-dependent, same bug as v1.2.0 stretch marker fix but in tempo map code)
- **Constant tempo marker position** bug when downbeats[1] > 0 (pickup)
- **Defensive UpdateTimeline()** between tempo map and stretch markers in Match & Quantize
- **"Quantize to grid" checkbox** in Insert Stretch Markers mode (EXPERIMENTAL - results 0.57x-0.97x in testing; per-bar tempo map can't capture within-bar timing. Match & Quantize is better. May remove based on community feedback)

### GPU Acceleration
- **Installer auto-detects NVIDIA GPU** (nvidia-smi) → installs CUDA PyTorch from cu124 index
- Skips CUDA download if already installed (checks torch.cuda.is_available())
- Device priority: CUDA > CPU

### Installer Robustness
- ZIP fallback when git not installed
- ZIP update preserves .venv (800MB)
- Detects ZIP vs git installs (no "not a git repository" error)
- beat-this via archive URL (no git needed for uv sync)
- Simplified update: re-run same install command

### UI
- Min width 360→420px (prevents overlap)
- Filename truncation with tooltip
- Version number displayed in header ("ReaBeat v1.2.1")
- Warning dialog when "Quantize to grid" used with mismatched tempo (>10%)

### Code Cleanup
- Removed unused `beats` parameter from `insert_tempo_map` (only uses downbeats)
- Removed dead `connecting` state field
- Removed unused `fmt_time_ms` function
- Fixed error messages referencing nonexistent `--extra neural`

### Community (v1.2.1 cycle)
- **Bassman002**: server finally starts after wscript fix (3 iterations of debugging)
- **squibs**: NEW user, reported Match & Quantize drift → TimeMap2 fix
- **Hipox**: detailed feedback on quantization, GUI overlap, feature requests
- **todd_r**: returning, will test
- **80icio**: NEW user, enthusiastic
- **nofish**: recommended MPL Quantize as workaround (now built-in)

## v1.3.0 - Simplified UI & Precision (2026-04-11)

### UI Simplified
- Removed Insert Tempo Map and Match & Quantize modes (edge cases, confusing workflow)
- Two clear modes: Match Tempo + Insert Stretch Markers
- Clearer tooltips

### Beat Detection Precision
- **Onset refinement (beta)**: librosa onset detection snaps each beat to nearest transient (~0.05ms)
- **Phase-aware BPM (beta)**: circular mean + linear regression (CPJKU/beat_this#13)
- **Octave correction 78-185 BPM**: optimized for modern music

### Quantization Rewritten
- Root cause found: was quantizing to internal downbeat grid instead of REAPER's project grid
- pos/srcpos separation: source position (where audio IS) vs target position (where it should PLAY)
- Smart threshold: half beat interval instead of fixed 50ms
- Correct handling with D_PLAYRATE != 1.0

### Community (v1.3.0 cycle)
- **squibs**: persistent quantization reports led to discovery of fundamental grid alignment bug
- **Video demo**: recorded and posted (https://youtu.be/sDLToLtF4C8)

## What Needs Attention Next

### Priority 1: Community
- Monitor forum for v1.3.1 feedback (Insert Tempo Map, Daodan)
- squibs never confirmed v1.3.0 quantization fix works
- Linux testing needed (no testers yet)
- Windows testing possible (dedicated test machine set up 2026-04-10)

### Priority 2: Quality exploration (test IN ISOLATION before shipping)
- SuperFlux onset detection - might help, NOT verified in isolation
- Onset backtracking - might help, NOT verified in isolation
- Each change must be tested separately on 10+ tracks in REAPER
- Confidence metric is NOT a reliable quality proxy (Bishop Briggs: lower confidence = better result)

### Priority 3: CoreML/ONNX (branch feature/coreml-onnx)
- NOT ready to ship - model conversion is lossy
- Wait for coremltools PR #2632 or new beat-this model release
- Infrastructure code preserved on branch for future use

### Priority 4: Distribution
- ReaPack integration (auto-install + auto-update Lua scripts)
- PyInstaller binary (eliminate Python dependency)

### Architecture Notes
- JSON parser edge case with \\" - not a real issue with our data
- No connection limit on server - no abuse seen
- Tempo precision ~0.2% due to beat-this 20ms quantization (fundamental limit)
- Meter ambiguity (2/4 vs 3/4) is fundamentally hard - neural helps but can't solve all cases
- MPS (Apple Silicon) intentionally not used - CPU is fast enough on M-series, MPS adds risk of PyTorch compatibility issues
- No competing model with released code beats beat-this (researched 2026-04-12)
