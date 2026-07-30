# ReaBeat E2E specs (ReaProof)

Drives a **real REAPER** with the built extension and asserts on state read
back through REAPER's own API. Written against the v2.0.3 audit-round fixes.

## Requirements

- macOS, REAPER 7.x installed, js_ReaScriptAPI installed in your REAPER
- [ReaProof](https://github.com/b451c/reaproof) cloned + installed
  (`pip install -e .` in a venv), `reaproof doctor` green
- ReaBeat built: `build/reaper_reabeat-arm64.dylib` and
  `vendor/onnxruntime/lib/libonnxruntime.1.24.4.dylib`
- The beat_this model cached at `~/.reabeat/models/beat_this_final0.onnx`
  (run one detection manually first, or copy it there)
- Screen Recording + Accessibility permission for your terminal
  (input synthesis + window capture)

## Run

```bash
export REAPROOF_REAPER_APP=/Applications/REAPER.app
# optional overrides:
#   export REABEAT_DYLIB=/path/to/reaper_reabeat-arm64.dylib
#   export REABEAT_ORT=/path/to/libonnxruntime.1.24.4.dylib
cd /path/to/reaproof
.venv/bin/python -m pytest -v -s /path/to/ReaBeat/tests/reaproof/test_reabeat_e2e.py
```

The module runs ONE REAPER session; tests are ordered phases (window toggle,
keyboard, detection+apply on a trimmed item, tempo map, GUID guard).
Full run ≈ 3-6 min (two neural detections included). Run it twice — results
must agree (ReaProof doctrine).

Do not touch mouse/keyboard while it runs — the specs synthesize real input.

## What is covered / not covered

See `reaproof_features.json` (validated with
`reaproof features-report reaproof_features.json --tests .`) and `REPORT.md`
for the latest honest run results, including the untestable list with reasons
(model download corruption, combo-driven modes, UI scale).

## Layout coordinates

Click targets are located from pixels at runtime: the gold "Detect Beats"
button calibrates the window-frame offset, and the full-width gold Apply bar
anchors the radio rows (spacings from `MainComponent::resized()`). This
survives title-bar/multi-display quirks and most layout tweaks. If you
restyle the gold accent (0xC8A040) or the Detect/Apply buttons, update the
color mask in `Ui._calibrate` / `Ui.locate_apply`.

Evidence screenshots of every phase land in `/tmp/reabeat-e2e-shots/`
(override with `REABEAT_TEST_SHOTS`).
