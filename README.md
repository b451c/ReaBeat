# REABeat

Neural beat detection and tempo mapping for REAPER.

REABeat detects beats, downbeats, tempo, and time signature in any audio file using state-of-the-art neural models, then writes the results to REAPER as tempo markers or stretch markers.

## What It Does

- **Detect beats** using beat-this (CPJKU, ISMIR 2024) — state-of-the-art neural beat tracking
- **Insert tempo map** — constant BPM or variable per-bar tempo markers
- **Insert stretch markers** — at every beat or every downbeat for timing correction
- **Visualize** — waveform with beat/bar overlay in a clean ReaImGui interface

## Use Cases

- **Tempo-map a live recording** — align REAPER's grid to freely played audio
- **Quantize timing** — insert stretch markers, then adjust in REAPER's stretch marker editor
- **Sync to picture** — create accurate tempo map for scoring workflows
- **Prep for editing** — know the BPM and bar structure before you start cutting

## Requirements

### REAPER
- REAPER v7.0+
- [ReaImGui](https://forum.cockos.com/showthread.php?t=250419) 0.9+ (install via ReaPack)
- [mavriq-lua-sockets](https://github.com/mavriq-dev/public-reascripts) (install via ReaPack)

### Python Backend
- Python 3.10–3.12
- `pip install -e .` (installs everything: beat-this + torch + librosa)

Or with [uv](https://docs.astral.sh/uv/) (recommended):
```bash
cd REABeat
uv sync
```

## Installation

1. **Install Python dependencies:**
   ```bash
   cd REABeat
   pip install -e .  # Or: uv sync
   ```

2. **Install REAPER dependencies:**
   - Extensions > ReaPack > Import repositories
   - Add: `https://github.com/mavriq-dev/public-reascripts/raw/master/index.xml`
   - Browse packages > install `ReaImGui` and `mavriq-lua-sockets`

3. **Add script to REAPER:**
   - Actions > Show action list > New action > Load ReaScript
   - Select `scripts/reaper/reabeat.lua`
   - Assign a keyboard shortcut if desired

4. **Run:** Select a media item, run the REABeat action. The Python backend launches automatically.

## Architecture

```
REAPER (Lua UI)  ←TCP:9877→  Python Backend (auto-launched)
scripts/reaper/               src/reabeat/
  reabeat.lua (entry)           detector.py (beat detection)
  reabeat_ui.lua (drawing)      server.py (TCP server)
  reabeat_actions.lua (API)     cli.py (CLI entry)
  reabeat_waveform.lua          config.py
  reabeat_socket.lua
  reabeat_server.lua
  reabeat_theme.lua
```

- All processing is **local** — no cloud, no data sent anywhere
- Backend **auto-launches** and shuts down after 5 minutes idle
- Communication: line-delimited JSON over TCP localhost

## CLI Usage

```bash
# Start server (normally auto-launched by REAPER)
uv run python -m reabeat serve

# Detect beats from command line
uv run python -m reabeat detect /path/to/song.wav
```

## Beat Detection

REABeat uses **beat-this** (CPJKU, ISMIR 2024) — a neural beat/downbeat tracker trained on thousands of annotated songs. ~2-3 seconds per track, sub-10ms accuracy.

No fallback chain. If beat-this can't detect beats in your file, you get a clear error explaining why (silent audio, too short, not music).

## Support Development

REABeat is free and open source. If it saves you time, consider supporting development:

- [Ko-fi](https://ko-fi.com/reabeat) — Buy us a coffee
- Star this repo on GitHub
- Report issues and feature requests

## License

MIT — use it however you want.
