# Handover Report - 2026-04-12

## Session Summary
**Focus**: Built ReaBeat v1.0.0→v1.3.1, restored Insert Tempo Map, explored CoreML/ONNX (not shipped), community engagement
**Branch**: main (feature/coreml-onnx branch preserved but not merged)
**Version**: v1.3.1
**Tests**: 36 passing, 0 failures
**Repo**: https://github.com/b451c/ReaBeat
**Forum**: https://forum.cockos.com/showthread.php?t=308240

## What Was Built

### Complete standalone project (not part of RemixTool)
- 6 Lua modules, 4 Python modules, 2 test files
- beat-this neural beat detection (sole backend, no fallbacks)
- Two action modes: Match Tempo, Insert Stretch Markers (with optional quantize to REAPER grid)
- Neural downbeats from beat-this dedicated model head
- Hybrid tempo calculation (span/mean, immune to 20ms quantization)
- Editable BPM with original detection reference
- Auto-align first downbeat to bar after Match Tempo
- Stretch quality mode (Balanced/Transient/Tonal via I_STRETCHFLAGS)
- Multi-item detection cache (per GUID, survives item switching)
- Grid-independent quantization everywhere (TimeMap2, not BR_GetClosestGridDivision)
- "Quantize to grid" snaps to REAPER's project grid via TimeMap2 per-bar
- Cross-platform server launcher (macOS/Windows/Linux, wscript hidden console on Windows)
- NVIDIA CUDA auto-detection in installer (skips if already installed)
- Installer works without git (ZIP fallback, preserves .venv on update)
- REAPER-native UI with warm gold theme
- Support menu (Ko-fi, Buy Me a Coffee, PayPal)

### Release History
- **v1.0.0**: initial release - beat detection, tempo map, stretch markers, match tempo
- **v1.0.1**: Windows hotfix - SCRIPT_DIR backslash, project root discovery
- **v1.1.0**: Match & Quantize, snap to grid, multi-item cache, reordered UI
- **v1.2.0**: Precision improvements, neural downbeats, editable BPM, auto-align, stretch quality
- **v1.2.1**: Windows server fix (wscript), CUDA auto-detection, installer without git
- **v1.3.0**: Simplified to 2 modes (removed Tempo Map + Match & Quantize), onset refinement, phase-aware BPM, grid quantization fixed to use REAPER grid, pos/srcpos separation
- **v1.3.1**: Restored Insert Tempo Map (3 modes: constant/variable-bars/variable-beats), neural downbeat cleaning, octave correction for variable tempo

### CoreML/ONNX exploration (branch feature/coreml-onnx, NOT merged)
- Built CoreML and ONNX inference backends with 3-model ensemble
- Tested on 10 tracks: 2.5x faster but conversion was lossy (beat positions drifted)
- Ensemble gave mixed confidence results (better on 4/10, worse on 3/10)
- Decision: not ready to ship. Branch preserved for future when coremltools fixes lossless conversion (PR #2632)

### Community Response (33 posts, 12+ engaged users)
- **Lunar Ladder**: "VERY COOL!"
- **saxmand**: praised dependency management approach
- **X-Raym**: asked about video demo — invited community to record one
- **Hipox**: found 2 Windows bugs (fixed v1.0.1), requested 3 features (shipped v1.1.0), detailed v1.2.1 feedback (quantization, GUI overlap, feature requests)
- **Bassman002**: Windows server crash — 3 iterations of debugging across v1.2.0-v1.2.1, finally fixed with wscript
- **JetRed**: asked about REAPER Portable support (answered with manual install instructions)
- **dukati**: "pretty amazing", reported 2/4 vs 3/4 and octave error issues (addressed with neural downbeats + editable BPM)
- **80icio**: "OMG I've seen so many people asking for something like this" (new in v1.2.1 cycle)
- **squibs**: "AMAZING", reported quantization drift (fully fixed in v1.3.0 with REAPER grid quantization)
- **todd_r**: original inspiration, returning to test
- **nofish**: original inspiration, recommended MPL Quantize as workaround (now built-in)
- **Daodan**: disappointed by removal of Insert Tempo Map (restored in v1.3.1)

## Files

| File | Purpose |
|------|---------|
| `scripts/reaper/reabeat.lua` | Entry point, state machine, multi-item cache, apply logic |
| `scripts/reaper/reabeat_ui.lua` | All UI drawing (4 action modes, editable BPM, stretch mode, align checkbox) |
| `scripts/reaper/reabeat_actions.lua` | Tempo map, stretch markers, match tempo (with auto-align), I_STRETCHFLAGS |
| `scripts/reaper/reabeat_socket.lua` | TCP client + JSON encode/decode |
| `scripts/reaper/reabeat_server.lua` | Cross-platform auto-launch (.bat on Windows) |
| `scripts/reaper/reabeat_theme.lua` | REAPER-native dark theme |
| `src/reabeat/detector.py` | Beat detection, neural downbeats, hybrid tempo, time sig from downbeats |
| `src/reabeat/server.py` | TCP server (thread-safe) |
| `src/reabeat/cli.py` | CLI: serve, detect, check |
| `src/reabeat/config.py` | Port 9877, detection params |
| `tests/test_detector.py` | 22 detector tests (incl. quantized tempo, neural downbeats) |
| `tests/test_server.py` | 4 server protocol tests |

## Known Issues

1. **Linux untested** - server launcher should work but no real testing
2. **JSON parser edge case** - `\\"` not handled, our data doesn't trigger it
3. **No reconnect-on-loss** - server crash requires manual retry
4. **Tempo precision limited to ~0.2%** - beat-this 20ms quantization; hybrid helps but can't fully eliminate
5. **Meter ambiguity** - 2/4 vs 3/4 vs 4/4 is fundamentally hard; neural downbeats help but some tracks remain ambiguous
6. **No detection progress %** - shows "Detecting..." without percentage (beat-this File2Beats is a black box)
7. **"Quantize to grid" experimental** - stretch ratios 0.57x-0.97x in testing; per-bar tempo map can't capture within-bar timing. Consider removing if community feedback is negative

## What Next Agent Should Do

### Startup Sequence
1. Read `CLAUDE.md` - architecture, key files, rules, technical details
2. Read this `HANDOVER.md`
3. Read `PROGRESS.md` - full history, priorities
4. Read all memory files in `~/.claude/projects/-Volumes--Basic-Projekty-REABeat/memory/`
5. `git log --oneline -15`
6. `uv run pytest tests/ -q` - confirm 36 tests pass
7. Check forum: https://forum.cockos.com/showthread.php?t=308240

### Priority 1: Community
- Monitor forum for v1.3.1 feedback (Insert Tempo Map, Daodan's response)
- squibs never confirmed if v1.3.0 quantization fix works for him
- Daodan should test Insert Tempo Map and report back
- Linux testing needed (no testers yet)
- ReaPack integration would solve the manual script copy pain

### Priority 2: Quality exploration (careful, test in isolation)
- SuperFlux onset detection (lag=2, max_size=3) - might reduce false positive onset snapping, NOT verified
- Onset backtracking (librosa.onset.onset_backtrack) - might improve stretch marker alignment, NOT verified
- Each must be tested IN ISOLATION on 10+ tracks IN REAPER before shipping
- Read memory file feedback_test_before_claim.md before claiming improvements

### Priority 3: CoreML/ONNX (branch feature/coreml-onnx, NOT ready)
- Conversion is lossy with current coremltools (beat positions drift)
- Wait for coremltools PR #2632 fix (squeeze in _cast)
- Or wait for beat-this v2 / new model release
- Full results in memory file project_coreml_onnx_results.md

### Priority 4: Distribution
- ReaPack integration (auto-install + auto-update Lua)
- PyInstaller binary (eliminate Python dependency)

### Priority 5: reamix.me
- ReaBeat is the community funnel toward reamix.me

## Research: beat-this Alternatives (2026-04-12, updated)

beat-this remains the best choice with available code. Thorough research confirmed:
- BeatFM, HingeNet: marginal improvements (+0.6-0.8% beat F1) but NO released code or weights
- BeatNet+: empty repo, no code
- BEAST: partial code, research-only, below beat-this accuracy
- madmom: broken on Python >= 3.10
- No model to switch to. Full analysis in `drafts/beat-this-known-issues.txt` and memory file project_coreml_onnx_results.md

## Business Context

ReaBeat exists for three reasons:
1. **Community goodwill** - free tool solving real problems (nofish, todd_r, Hipox, dukati, Daodan)
2. **Installation pipeline test** - validates uv + Python + Lua on all platforms before reamix.me
3. **Funnel** - users who like ReaBeat may want reamix.me's full remixing

Forum response validates the approach: 12+ community members engaged across 33 posts, bugs found and fixed same day, feature requests driving development. Windows test machine set up 2026-04-10 for dual-platform testing.
