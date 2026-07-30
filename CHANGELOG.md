# Changelog

All notable changes to ReaBeat are documented here. Based on [Keep a Changelog](https://keepachangelog.com/). Adheres to [Semantic Versioning](https://semver.org/).

## [2.0.3] - 2026-07-31

Full-code correctness audit round: 5 critical fixes, ~30 bug fixes, 4 new features. Verified by an automated end-to-end suite driving a real REAPER instance (`tests/reaproof/`) - including stretch markers on trimmed items, detection mid-selection-change, tempo map ranges and cancellation.

### Fixed - critical
- **Stretch markers landed `D_STARTOFFS` seconds late on trimmed items** - detection beat times are source-file positions, but every Insert Stretch Markers mode added the take offset on top when writing `srcpos`. Invisible in ReaBeat's own display (the readback subtracted it again) but wrong against the actual audio for any slip-edited/trimmed item, and it made multi-track sync mix two coordinate conventions (tempo map used the correct one) - guaranteed drift. All marker read/write paths now share one convention: `srcpos` = source-absolute beat time, no offset.
- **Detection result could attach to the wrong item** - selecting another item while detection ran displayed and cached item A's beats under item B's GUID (Apply would then stretch B with A's beats, and B's cache was poisoned). Results now carry the GUID they were started for; on mismatch they're cached for the original item only, with a status note.
- **Crash risk while dragging markers** - the periodic ~1 s marker re-read (and the forced re-read after add/move/delete) could replace/shrink the marker arrays mid-gesture; a stale drag index then read out of bounds. Re-reads are now deferred while any mouse gesture is active and all drag indices are bounds-checked.
- **Corrupt model cached as valid forever** - a killed download left a partial file that the next download APPENDED to (JUCE `FileOutputStream` appends to existing files); the resulting oversized file could pass the size check. Downloads now go to a temp `.part` file with completeness verification (Content-Length + write status) and atomic rename. Likely the root cause of several "Detect stays grey / model not loaded" reports.
- **Out-of-memory during resample/mel could kill all of REAPER** - the v2.0.2 OOM guard covered only the file-read stage; `bad_alloc` from the resampler or mel spectrogram on very long files escaped the thread and terminated the process. Now caught and reported like the read stage.

### Fixed
- **Windows: non-ASCII user folders broke model loading** (GitHub #1, bobo198504) - the model path was byte-copied into a wide string; now converted properly with `MultiByteToWideChar(CP_UTF8)`. A Windows username with Polish/Cyrillic/CJK characters no longer prevents the model from loading.
- **REAPER shortcuts needed several presses; persisted after closing ReaBeat** (poydepzaj1616) - the JUCE message pump ran unconditionally forever and could dispatch REAPER's own keyboard events past REAPER's accelerator handling. The pump now runs only while the ReaBeat window is visible.
- **Confidence reported ~0% for clean slow/fast tracks** - it compared beat intervals against the octave-corrected BPM (a perfect 70 BPM track was scored against 140). Now measured against the median inter-beat interval.
- **"Variable - beats" tempo map created a new measure on every beat** - the time signature was passed with every tempo marker; now only the first marker carries it.
- **Match Tempo extended trimmed items to full source length** - item length is now scaled from its current length (`oldLen * oldRate / newRate`), not recomputed from the source file. Same fix in the multi-track sync pipeline.
- **Match Tempo could show a blocking dialog hidden behind the plugin window** - extreme-ratio rejection now reports via the status bar (the always-on-top window made the modal invisible and REAPER appeared frozen).
- **Tempo map wrote markers outside the item** - beats from trimmed-away source regions inserted markers beyond the cleared range (even piling up at position 0 for pre-trim beats). Insertion is now clipped to the item's window.
- **Multi-track sync used two different reference tempos** when the reference item's playrate wasn't 1.0 - the grid got the effective BPM but the slave playrate targeted the raw BPM. Both now use the effective BPM, and a failed reference lookup no longer leaves an empty undo point.
- **Manual beat edits were lost when clicking empty arrange space** - deselecting saves edits to the cache like switching items always did; the item-deleted path does too.
- **Pre-detection errors were invisible** - the status bar (and tooltip toggle) had no layout before the first detection, so "Failed to load model" / "Download failed" rendered into a zero-size label. Now always laid out.
- **N key never advanced past the current gap** - the scan skipped by the gap's end beat, which always re-found the same gap after zooming to it. It now advances by gap start, and wrap-around actually works.
- **Double-click in the ruler inserted beats/markers** - the "guaranteed safe seek zone" now also guards double-clicks (rapid seek-clicking could add a real REAPER stretch marker).
- **Playhead drifted visually in marker mode** - it was drawn through the linear time map, ignoring stretch markers; seek used the marker-aware map. Both now share the same mapping (beat-flash timing fixed too).
- **Auto-follow overrode manual scrolling before the first beat** - follow is now re-enabled only on the stop-to-play transition.
- **Filename BPM hints misread numbers** - "1984_bpm_120" now reads 120 (all matches are tried, not just the first), "loop_44100_bpm" no longer reads 100 out of a sample rate, and date-like tokens no longer set the meter hint.
- **Gap filling could accept a gap while under-filling it** - logit-hint matching counted two hints near one grid slot twice; slots are now matched one-to-one (this re-created the exact 0.51x stretch-ratio symptom the interpolator exists to fix).
- Assorted hardening: take pointer re-validated every poll (stale-take crash after take deletion/switch), detection/download threads are now cooperatively cancellable (closing the window no longer force-kills a thread mid-inference), stretch-marker dedup iterates to a fixpoint, Bars/Project-grid source matching respects the false-positive filter, `interpolateDst` preserves the stretch offset with a single marker, marker drags clamp between neighbors, density-culled beats are no longer invisible drag targets, mel/inference/tempo/meter modules guard degenerate inputs, BPM octave buttons clamp to 20-999, dedup/cache side-maps are pruned with the cache, dead docker window handles are cleared on `WM_DESTROY`, and the Windows floating window now sizes its client area (not the outer frame) to the designed 500x660.

### Added
- **Time signature dropdown** (Daodan #9, notabot) - "Auto / 2/4 / 3/4 / 4/4 / 6/8 / 9/8 / 12/8" next to BPM. Auto shows the neural meter; picking a meter recomputes downbeats from it (compound meters treat the model's dotted-quarter taps as main beats: 6/8 = 2 per bar). Tempo map insertion converts to REAPER's quarter-note BPM for /8 meters. The override persists per item.
- **UI scale** (flark, poydepzaj1616) - 100/125/150/200% under the ReaBeat title menu; scales all text, controls and the waveform together. Persisted across sessions.
- **Cancel button** (Alex S. and every long-file user) - the Detect button turns into Cancel while detection runs; the whole pipeline (file read, inference, refinement, model download) stops cooperatively within a moment. No more waiting out a mis-clicked detection of a 90-minute stem.
- **Middle-mouse pan + draggable scroll thumb** (Daodan #4) - drag the waveform with the middle button (standard DAW gesture), or grab/click the position bar at the bottom edge to scrub the view when zoomed.

---

## [2.0.2] - 2026-05-20

Forum feedback round: load failures, long-file crashes, Apply silent failures, UX polish.

### Fixed
- **Long-file detection crash** (Alex S.) - chunked audio read (60 s blocks instead of one huge allocation) plus `OnsetRefinement` skipped past 10 min. A 90-minute drum stem now detects instead of immediately re-enabling the Detect button after a `bad_alloc`.
- **Plugin not loading on Windows** (fightclxb, squibs) - MSVC C/C++ runtime is now statically linked, so users no longer need to install the Visual C++ Redistributable separately.
- **Plugin not loading on macOS Catalina / Intel** (80icio, reaperfreaker) - bundled ONNX Runtime downgraded to 1.16.3 for `darwin64`, the last release that runs on macOS 10.15. ARM build stays on 1.24.4.
- **Apply Stretch Markers silent failure** (plush2) - status bar now reports "Inserted N markers" or an explicit error ("No beats detected", "Failed to insert stretch markers") instead of doing nothing.
- **Tempo Map wiped unrelated markers** (gkurtenbach) - only tempo markers inside the item's time range are deleted now; markers belonging to other items or to manual edits are preserved.
- **Tempo Map stretched the source item** (Daodan #1) - `C_BEATATTACHMODE` is set to Time after inserting tempo markers, so audio doesn't rescale when the project grid changes.
- **Detect Beats enabled on MIDI items** (Mercado_Negro) - button is now disabled when the selected item has no audio source; the source label shows `[No audio source]` and a tooltip explains why.

### Added
- **Visual download progress bar** (Daodan #7) - first-run model download now shows the same progress bar used by detection, not just a status text.
- **"Press N for next gap" hint** (Daodan #6) - small hint under the BEATS badge while gaps exist, so the shortcut is discoverable.
- **Portable model location** (akademie) - model can be placed next to the plugin binary (`UserPlugins/beat_this_final0.onnx` or `UserPlugins/ReaBeat/models/`) for portable REAPER setups; `~/.reabeat/models/` still works as the default download target.

### Changed
- **Beat drag hit-test** widened from 8 px to 12 px (Daodan #2) - easier to grab beats in clustered passages without being overly ambiguous.
- **Gap suggestion lines** changed from red (`0x40d94848`) to teal (`0xc080e0d0`) - readable against the red gap tint instead of blending into it (Daodan #3).
- **Docked window auto-activates** after `DockWindowAddEx` so it shows up immediately after toggling, no extra tab click needed (Daodan #8).

---

## [2.0.1] - 2026-04-17

Cross-platform fixes and improvements.

### Fixed
- **Dockable window on all platforms** - Linux uses SWELL_CreateXBridgeWindow, Windows uses dual-mode Win32 dialog (WS_CHILD for docked, WS_POPUP for floating)
- **macOS rpath** - CI build path no longer leaks into binary (fixes Intel Mac loading)
- **Async model download** - UI stays responsive during 79MB model download
- **ARM build** - `-fsigned-char` flag (WDL/SWELL assumes signed char)
- **Windows ORT pre-load** - cleaner delay-load via shlwapi (prevents System32 v1.17 conflict)
- **Debug logging removed** - no more `/tmp/reabeat_debug.log` on Linux

---

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
