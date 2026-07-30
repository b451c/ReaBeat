# ReaBeat v2.0.3 — ReaProof E2E report (2026-07-31)

> **Final confirmation runs:** after the version bump to 2.0.3 in
> CMakeLists (label-only change) the suite ran 2 more times against
> `build/reaper_reabeat-arm64.dylib` — **5/5 PASS both runs** (63.6 s /
> 62.2 s), results in agreement per the doctrine. The two flaky failures
> seen between the authoring runs and these were DRIVER-side (a one-shot
> CGWindowList OnScreenOnly lookup after a fixed 0.3 s sleep, while the
> machine was actively in use); the window/calibration lookups are now
> retry-with-timeout (`wait_for_reabeat_window`, 12 s) and a final failure
> is labelled explicitly as a driver failure with a screenshot artifact,
> never as a subject verdict. Run the suite on an idle desktop.
>
> **Post-feature runs (Cancel button + middle-mouse pan added):** the suite
> caught a real interaction — the driver's blind "safety" re-click on
> Detect now hits the ENABLED Cancel button and aborts detection
> (incidentally a live end-to-end proof the Cancel path works). Driver
> updated to re-click only when detection demonstrably did not start.
> Final binary with both features: **5/5 PASS ×2 agreeing runs**
> (61.1 s / 61.5 s).

Subject: `build/reaper_reabeat-arm64.dylib` (v2.0.3, uncommitted audit round)
+ `libonnxruntime.1.24.4.dylib`, driven in an isolated REAPER 7.75
(pinned profile) on macOS / Apple Silicon. Model: cached
`~/.reabeat/models/beat_this_final0.onnx`.

## Zero-code extension battery

| Check | Result |
|---|---|
| naming: `reaper_*.dylib` contract | PASS |
| load: attempted by REAPER (splash log) | PASS |
| register: observable action diff (`_ReaBeat_ShowWindow`) | PASS |
| action smoke (`--run-actions`) | expected "window left open" hygiene note — the action's PURPOSE is opening a window; covered properly by F1 below |

Battery ran with a local ReaProof patch adding `companions=` (installs the
ORT dylib next to the subject in UserPlugins — worth upstreaming).

## Authored specs (test_reabeat_e2e.py) — final three runs all fully green

| Spec | Result | Evidence |
|---|---|---|
| F1 window toggle lifecycle | **PASS** ×3 | JS_Window_Find/IsVisible readback after each toggle |
| F2 keyboard reaches REAPER with window closed | **PASS** ×3 | CGEvent Space (kVK_Space) → GetPlayState==1 |
| F3 **srcpos source-absolute on trimmed item (C1)** | **PASS** ×3 | 40 s / 120 BPM synthetic click WAV, slip-edited D_STARTOFFS=1.75 s; detection: 81 beats, 120.0 BPM, 100% confidence, 1.9 s; every marker srcpos within 60 ms of the k*0.5 s SOURCE grid; mutation check killed both mutations (`+1.75 s` old-bug shift and `+100 ms` drift) |
| F4 tempo map (constant) in item range | **PASS** ×3 | CountTempoTimeSigMarkers >= 1, positions within item, ~120 BPM |
| F5 detection GUID guard | **PASS** ×3 | detection started on A, selection switched to B mid-run: B has 0 markers and playrate 1.0; re-selecting A restores the CACHED result and Apply inserts markers with no re-detect |

Runtime: ~61 s per full run. Per doctrine, consecutive runs agreed.
Phase screenshots: `/tmp/reabeat-e2e-shots/`.

## Bonus real-world observations from the run

- Detection on the synthetic click track: 120.0 BPM, confidence **100%
  (green)** — also live evidence for the v2.0.3 confidence fix (interval
  consistency vs median).
- The time-sig combo read "2/4" with 81 bars = 81 beats on pure clicks
  (every accented pattern beat became a downbeat). Not a defect assertion —
  synthetic clicks are out-of-domain for the downbeat head — but worth
  remembering when eyeballing click-track detections.

## Not automated (honest)

- **F6 model download corruption** — needs a controllable download endpoint
  + mid-flight kill; the model is pre-cached here so the path never runs.
  Suggested seam: env override for the model URL.
- **F7 time-sig dropdown / F8 combo-driven modes & strength** — JUCE/SWELL
  popup menus are transient OS windows without stable coordinates; needs a
  gated test seam or golden-image flows.
- **F9 UI scale** — trigger sits in an OS-drawn popup menu; same limitation.
- **Windows/Linux** — everything here is macOS; the srcpos/GUID logic is
  platform-independent C++, but keyboard (F2) explicitly does NOT reproduce
  the original Windows symptom, it guards the invariant on macOS only.

## Assumptions (auto mode)

See `reaproof_features.json` (validated with `reaproof features-report` —
5 covered / 0 spec-needed / 4 untestable): F2 macOS-invariant framing, F4
constant-mode-only coverage.

## Flakiness fixed while authoring (test-side, not subject bugs)

1. Bridge Lua chunks starting with a function call are wrapped as
   expressions — statement chunks need an explicit `return`.
2. REAPER's transport Space requires a virtual-keycode CGEvent, not a
   unicode-string event.
3. The session can own ANOTHER window whose title contains "ReaBeat"
   (observed 481 pt wide) — window lookup must match the exact title AND
   the 500 pt width, or clicks land one UI row off depending on z-order.
4. Derived absolute coordinates below the waveform were fragile; click
   targets are now pixel-located (gold Detect button calibration + gold
   Apply bar anchor).
