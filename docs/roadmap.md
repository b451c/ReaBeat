# ReaBeat Roadmap

Tracking deferred fixes and feature requests by user/source. Anything actionable lives here so it doesn't fall out of memory between sessions.

Updated: 2026-07-31

## Done in v2.0.3 (unreleased — full audit round, see CHANGELOG)

- Daodan #4 — middle-mouse pan + draggable/clickable scroll thumb
- Cancel button during detection (cooperative cancellation runs through the whole pipeline)
- Daodan #9 + notabot — manual time signature dropdown (Auto/2/4/3/4/4/4/6/8/9/8/12/8)
- flark + poydepzaj1616 — UI scale 100-200% (title menu, persisted)
- poydepzaj1616 — keyboard hijack: JUCE message pump now gated on window visibility
- bobo198504 — Unicode home dir fix (MultiByteToWideChar) shipped
- Model download corruption class (append-to-partial, truncation passing validation, portable shadowing)
- takeOffset/srcpos convention fix (stretch markers on trimmed items) — NEEDS E2E TEST with slip-edited item before release
- Confidence vs octave-corrected tempo, tempo-map measure-per-beat, matchTempo length/dialog, sync effective BPM, OOM guards, drag OOB safety, N key, ruler double-click, playhead warp — full list in CHANGELOG

## Model research (2026-07-30)

No practically-available model beats beat_this final0. BeatFM (arXiv 2508.09790, ICME 2025: +0.6 beat F1 / +4.1 downbeat F1 on GTZAN vs Beat This trained WITHOUT its extra data) and HingeNet (2508.09788) have NO public code/checkpoints and need 250M+ param foundation models (MERT/MusicFM) + DBN — impractical for CPU plugin. Watch for code releases.

**Ensemble final0+1+2 ("High accuracy" mode): REJECTED 2026-07-31** (user decision) — expected ~+0.3-0.5% F1 does not justify 3x inference time, +158 MB model downloads, and a 3-model ModelManager/InferenceProcessor rework. Revisit only if a future model change re-opens the accuracy conversation.

---

## v2.0.3 candidates - UX polish (forum feedback)

### Daodan #4 - Middle-mouse pan + working scrollbar
**Source:** Daodan post #2 (forum t=308368)
**Effort:** ~2h
**Risk:** LOW
**Status:** Deferred from v2.0.2

Pan via middle-mouse drag (standard DAW behaviour) and make the existing scroll thumb at the bottom of the waveform clickable/draggable.

- `WaveformView.cpp` - add `mouseDown` / `mouseDrag` handling for `e.mods.isMiddleButtonDown()`
- Detect click on scrollbar Y region (bottom 4-6 px), allow drag to scrub view position
- Watch for trackpad emulation differences between macOS and Windows

### Daodan #5 - Item persistence after deselect
**Source:** Daodan post #2
**Effort:** ~30 min + careful pointer-safety testing
**Risk:** MEDIUM (cached MediaItem* can be deleted by user)
**Status:** Deferred from v2.0.2

Keep the last selected item visible in `updateSelectedItem` when count drops to 0, so users don't lose their detection state by clicking empty timeline.

- `MainComponent.cpp:869-882` - on count<=0, set sourceLabel to "<name> (deselected)" instead of full reset
- Before any Apply path, call `ValidatePtr2(nullptr, currentItem_.item, "MediaItem*")` and bail out if invalid
- Mark detected_ as "stale" (different colour) when deselected, refresh on next select

### Daodan #9 + notabot - Manual time signature dropdown
**Status:** DONE in v2.0.3 (see top of file). Remaining nice-to-have: persist the
pre-override neural downbeats per item so Auto can restore them after an
item switch (currently Auto after restore returns the overridden bars).

---

## v2.0.3 - Detection robustness

### Sliding-window BPM for variable-tempo material
**Source:** Alex (90-min symphony)
**Effort:** Large (1-2 days)
**Risk:** HIGH

Current `TempoEstimator` returns a single BPM for the whole detection. For Beethoven-length classical or any score with tempo changes this is meaningless. Compute BPM in sliding windows (e.g. 30 s) and let the user pick "constant" vs "variable" output.

- New `TempoEstimator::computeVariable(beats, windowSec)` returning `vector<{time, bpm}>`
- "Insert Tempo Map" - Variable mode already exists per-beat; add windowed mid-grain mode
- UI: small line plot under BPM label showing tempo curve

### OnsetRefinement memory fix instead of skip
**Source:** Alex
**Effort:** Medium
**Risk:** MEDIUM

Currently we skip refinement past 10 minutes (saves the 3.8 GB magnitudes matrix). Better: stream the spectral flux frame-by-frame so only the running window is in RAM.

- `OnsetRefinement.cpp` - rewrite `detectOnsets` to keep only `kAdaptiveWindow` (7) frames + previous magnitudes for diff
- Removes the matrix allocation entirely
- Should restore sample-level precision on long files

---

## Open user issues (no fix yet)

### bobo198504 - Detect Beats stays grayed out on Windows 11 (Unicode home dir suspected)
**Source:** GitHub issue #1
**Status:** Awaiting v2.0.2 retest by reporter

User reports Detect Beats button disabled after selecting an audio item, no progress bar visible. Two likely causes:

1. **Model download silently failing in v2.0.1** - no visible progress feedback, user can't tell. **v2.0.2 fixes the symptom** (visual progress bar, "Downloading model..." button label, portable model fallback).

2. **Unicode home directory path bug** in `BeatDetector::loadModel` (src/BeatDetector.cpp:37):
   ```cpp
   std::wstring widePath(modelPath.begin(), modelPath.end());
   ```
   This copies bytes 1:1 from `std::string` (UTF-8 from JUCE) to `std::wstring` (UTF-16). Works only for ASCII. A Windows username with Chinese/Cyrillic/Polish characters produces a corrupted wide string, ORT throws, `modelLoaded_` stays false, button stays grey.

**Fix (hotfix candidate for v2.0.3):**
```cpp
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, modelPath.c_str(), -1, nullptr, 0);
    std::wstring widePath(wlen > 0 ? wlen - 1 : 0, L'\0');
    if (wlen > 1)
        MultiByteToWideChar(CP_UTF8, 0, modelPath.c_str(), -1, widePath.data(), wlen);
    session_ = std::make_unique<Ort::Session>(*env_, widePath.c_str(), opts);
#endif
```

Also audit `ModelManager` paths - JUCE's `String::toStdString()` returns UTF-8 but the assumption should be explicit.

Trigger hotfix if bobo confirms v2.0.2 download succeeds but plugin still doesn't load.

**UPDATE 2026-07-31:** Both suspected causes are fixed in v2.0.3 - the
MultiByteToWideChar conversion shipped in `BeatDetector::loadModel`, and the
download-corruption class (append-to-partial `.part` handling) is fixed in
`ModelManager`. Awaiting bobo's retest once v2.0.3 is released.

### reaperfreaker - Debian Trixie not loading
**Source:** Forum post #6
**Status:** Awaiting diagnostics

Need from user:
- `ldd ~/.config/REAPER/UserPlugins/reaper_reabeat-x86_64.so`
- `REAPER --loglevel=verbose 2>&1 | grep -i reabeat` (if REAPER supports a verbose flag)
- `dmesg | tail -20` after attempting to load

Candidate causes:
- glibc/libstdc++ ABI: we build on Ubuntu 22.04 (glibc 2.35) - Debian Trixie has 2.41, should be forward-compatible
- Wayland-only session: REAPER under native Wayland can't open X11 socket, our XBridge fails
- Missing system libs: `libfreetype6`, `libcurl4`, `mesa-utils`

Mitigations to consider:
- Build on Ubuntu 20.04 instead of 22.04 (older glibc baseline)
- `-static-libstdc++ -static-libgcc` link flags

### reaperfreaker - macOS Mojave 10.14 support
**Source:** Forum post #6 (separate from Debian)
**Status:** Open, not yet implemented

Plugin currently builds with `CMAKE_OSX_DEPLOYMENT_TARGET=10.15` for the Intel build. ORT 1.16.3 (now bundled for darwin64) actually supports `minos 10.14` per `otool -l`, so the ORT dep is fine. Only the plugin's own deployment target is blocking Mojave.

To enable Mojave:
- Lower `CMAKE_OSX_DEPLOYMENT_TARGET` to 10.14 in `.github/workflows/build.yml` macOS-x86_64 matrix entry
- Verify no C++20 features used that require macOS 10.15+ (probably fine - main culprit would be filesystem APIs)
- Test that built plugin actually loads on Mojave (REAPER 7+ supports Mojave per their forums)

Effort: ~30 min. Risk: LOW (worst case we revert to 10.15). Trigger if reaperfreaker confirms he still uses Mojave.

### flark - Font size adjustment
**Status:** DONE in v2.0.3 - UI scale 100/125/150/200% via `setTransform` on
MainComponent (title menu > UI scale), persisted in ExtState "ReaBeat/uiscale".

---

## Long-term ideas

- **getProjectBpm ignores the tempo map** - `GetProjectTimeSignature2` "does not reflect tempo envelopes" (SDK), so the Match-to-Project default target and the Project-grid mismatch warning report the wrong tempo whenever the project has a tempo map (including one ReaBeat inserted). Fix: `TimeMap_GetDividedBpmAtTime` at the item position. Deferred from the 2026-07 audit (needs API availability check + behavior decision).
- **Exception-safe undo blocks** - Undo_BeginBlock/EndBlock pairs are not RAII; a bad_alloc between them leaks an open undo block. Low practical risk, noted in audit.
- **"Set session" with compound meter** - sets the dotted-quarter BPM as session tempo while the sig is /8; should scale by 1.5 like the tempo map path does.
- **C++ unit tests** - none exist today. At minimum: MelSpectrogram numerical regression test vs Python output, BeatInterpolator gap-fill logic, ReaperActions stretch marker construction (without actually calling REAPER API).
- **Cancel button during detection** - reuse "Detect Beats" as "Cancel" while detecting. Pattern from reamix.me_native: `alive_` shared atomic + `threadShouldExit()` between stages.
- **Free `mono` buffer after resample** - release ~900 MB on long files. Requires changing detect() signature to take rvalue ref or moving the call site.
- **Compound meter heuristic in `TimeSigDetector`** (BPM > 130 && bar=2 → 6/8 hint). Better than nothing, but Daodan #9 dropdown is more honest.
- **CI maintenance**:
  - `ubuntu-22.04` GitHub-hosted runners will be removed at some point - bump to 24.04 when needed (or stay on older for glibc compat)
  - `actions/checkout@v4`, `actions/upload-artifact@v4` use Node 20 which is deprecated June 2026
  - `windows-latest` redirects to `windows-2025-vs2026` by June 15, 2026 - confirm v2026 toolset works
- **Forum**:
  - Post v2.0.2 announcement (draft ready in `drafts/forum_post_v2.0.2.txt`)
  - Email Alex Shturmak (draft ready in `drafts/email_alex_shturmak_v2.0.2.txt`)
  - Reply per user after v2.0.2 retest reports come in
- **Sample-perfect alignment for long files** - if streaming OnsetRefinement still can't keep up, alternative: run refinement only within a small window around each model-predicted beat (sparse, O(beats x small window) instead of O(audio_length)).

---

## Post-v2.0.2 verification still needed

Just shipped, not yet confirmed by external users:
- 80icio retest on Catalina Intel (was ORT 1.20.1 incompatibility - now 1.16.3)
- fightclxb / squibs retest on Windows 11 (was missing VC++ Redist - now statically linked)
- plush2 retest of Insert Stretch Markers on Windows 11 (was silent fail - now status messages)
- Mercado_Negro retest on macOS Tahoe + M4 (was MIDI item false-enable - now disabled)
- gkurtenbach retest of Insert Tempo Map preserving other-item markers
- bobo198504 retest of Detect Beats button (waiting since GitHub Issue #1 reply)
- Daodan retest of 5 UX points (item drag, suggestion color, N hint, dock activate, download progress)
- Alex Shturmak retest of long-file detection (waiting since email reply queued in drafts/)
