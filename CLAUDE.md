# REABeat — Agent Harness

Neural beat detection and tempo mapping for REAPER. Open-source community tool.

## Architecture

```
REAPER (Lua UI)  <--TCP:9877-->  Python Backend (auto-launched)
scripts/reaper/                   src/reabeat/
```

**Lua frontend** (7 files): UI, waveform, actions (tempo map + stretch markers), socket, server launcher, theme.
**Python backend** (4 files): beat detector (beat-this/madmom/librosa), TCP server, CLI, config.

## Key Files

| Purpose | File |
|---------|------|
| Entry point (UI) | `scripts/reaper/reabeat.lua` |
| UI drawing | `scripts/reaper/reabeat_ui.lua` |
| REAPER API actions | `scripts/reaper/reabeat_actions.lua` |
| Waveform + beats | `scripts/reaper/reabeat_waveform.lua` |
| Beat detection | `src/reabeat/detector.py` |
| TCP server | `src/reabeat/server.py` |
| CLI | `src/reabeat/cli.py` |

## Running

```bash
uv run python -m reabeat serve              # Start server
uv run python -m reabeat detect song.wav    # CLI detection
uv run pytest tests/ -q                     # Run tests
```

## Critical Rules

1. Port 9877 (not 9876 — that's REAmix)
2. All REAPER API actions wrapped in Undo blocks
3. Backend auto-launches with 5-min idle timeout
4. Beat-this > madmom > librosa fallback chain
5. All files lean — this is a focused tool, not a framework
