# REABeat

**Neural beat detection and tempo mapping for REAPER.**

REABeat detects beats, downbeats, tempo, and time signature in any audio using [beat-this](https://github.com/CPJKU/beat_this) (CPJKU, ISMIR 2024), then writes results to REAPER as tempo markers or stretch markers.

## What It Does

- **Detect beats** — state-of-the-art neural beat tracking, ~2-3 seconds per song
- **Insert tempo map** — constant BPM or variable per-bar tempo markers
- **Insert stretch markers** — at every beat or every downbeat
- **Cross-platform** — macOS, Windows, Linux

## Use Cases

- **Tempo-map a live recording** — align REAPER's grid to freely played audio
- **Quantize timing** — insert stretch markers, then fine-tune in REAPER
- **Sync to picture** — create accurate tempo map for scoring workflows
- **Prep for editing** — know the BPM and bar structure before you start cutting

---

## Installation

### Quick Install (macOS / Linux)

Open Terminal, paste this, press Enter:
```bash
curl -sSL https://raw.githubusercontent.com/b451c/REABeat/main/install.sh | bash
```

### Quick Install (Windows)

Open PowerShell, paste this, press Enter:
```powershell
irm https://raw.githubusercontent.com/b451c/REABeat/main/install.ps1 | iex
```

### Step-by-Step Install (all platforms)

<details>
<summary>Click to expand full manual installation guide</summary>

#### Step 1: Install uv (Python package manager)

**macOS / Linux:**
```bash
curl -LsSf https://astral.sh/uv/install.sh | sh
```

**Windows (PowerShell):**
```powershell
irm https://astral.sh/uv/install.ps1 | iex
```

#### Step 2: Download REABeat

**macOS / Linux:**
```bash
cd ~/Documents
git clone https://github.com/b451c/REABeat.git
cd REABeat
```

**Windows (PowerShell):**
```powershell
cd $env:USERPROFILE\Documents
git clone https://github.com/b451c/REABeat.git
cd REABeat
```

> Don't have git? Download ZIP: https://github.com/b451c/REABeat/archive/refs/heads/main.zip

#### Step 3: Install Python dependencies

```bash
uv sync
```

This downloads Python, PyTorch, beat-this and all dependencies (~800MB, one-time).

Verify it works:
```bash
uv run python -m reabeat check
```
You should see: `OK: beat-this ready`

#### Step 4: Install REAPER dependencies

Open REAPER, then:

1. **Extensions > ReaPack > Import repositories**
2. Paste this URL, click OK:
   ```
   https://github.com/mavriq-dev/public-reascripts/raw/master/index.xml
   ```
3. **Extensions > ReaPack > Browse packages**
4. Search and install these two:
   - **ReaImGui** (required — the UI framework)
   - **mavriq-lua-sockets** (required — backend communication)
5. Restart REAPER after installing both

Recommended: also install [SWS Extension](https://www.sws-extension.org/) (enables URL opening from Support menu).

#### Step 5: Add REABeat script to REAPER

1. **Actions > Show action list**
2. Click **New action... > Load ReaScript...**
3. Navigate to the `reabeat.lua` file:
   - **macOS / Linux:** `~/Documents/REABeat/scripts/reaper/reabeat.lua`
   - **Windows:** `Documents\REABeat\scripts\reaper\reabeat.lua`
   - If you used the auto-installer, scripts are already in REAPER's Scripts folder:
     - **macOS:** `~/Library/Application Support/REAPER/Scripts/REABeat/reabeat.lua`
     - **Windows:** `%APPDATA%\REAPER\Scripts\REABeat\reabeat.lua`
     - **Linux:** `~/.config/REAPER/Scripts/REABeat/reabeat.lua`
4. Click **OK**
5. (Optional) Select the new action and click **Add shortcut...** to assign a key

#### Step 6: Run

1. Select an audio item on your REAPER timeline
2. Run REABeat from the Actions menu (or press your shortcut)
3. Click **Detect Beats**
4. Choose your action (Tempo Map or Stretch Markers)
5. Click **Apply**

The Python backend launches automatically on first use and shuts down after 5 minutes of inactivity.

</details>

---

## Updating

```bash
cd ~/Documents/REABeat    # or wherever you cloned it
git pull
uv sync
```

Then restart REABeat in REAPER (close and reopen the script window).

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| "Starting..." hangs | Run `uv run python -m reabeat check` in terminal to verify backend |
| "Offline" status | Kill old server: `kill $(lsof -ti:9877)` (macOS/Linux) or restart REAPER |
| "beat-this not installed" | Run `uv sync` in the REABeat directory |
| ReaImGui error | Install via ReaPack: Extensions > ReaPack > Browse > "ReaImGui" |
| Socket error | Install via ReaPack: Extensions > ReaPack > Browse > "mavriq-lua-sockets" |
| Wrong BPM detected | Try a different section of the song (beat-this works best with clear rhythmic content) |

Server log location:
- **macOS / Linux:** `/tmp/reabeat_server.log`
- **Windows:** `%TEMP%\reabeat_server.log`

---

## Architecture

```
REAPER (Lua UI)  <--TCP:9877-->  Python Backend (auto-launched)
scripts/reaper/                   src/reabeat/
```

All processing is **local** — no cloud, no data sent anywhere.

---

## Support Development

REABeat is free and open source (MIT license).

If it saves you time, consider supporting:

- [Ko-fi](https://ko-fi.com/quickmd)
- [Buy Me a Coffee](https://buymeacoffee.com/bsroczynskh)
- [PayPal](https://www.paypal.com/paypalme/b451c)

Or: star this repo, report bugs, suggest features.

---

## License

[MIT](LICENSE) — use it however you want.
