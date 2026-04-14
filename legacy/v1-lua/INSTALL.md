# ReaBeat — Installation Guide

## Quick Install

### macOS / Linux
Open Terminal, paste, press Enter:
```bash
curl -sSL https://raw.githubusercontent.com/b451c/ReaBeat/main/install.sh | bash
```

### Windows
Open PowerShell, paste, press Enter:
```powershell
irm https://raw.githubusercontent.com/b451c/ReaBeat/main/install.ps1 | iex
```

Both scripts will: install uv (if needed) → download ReaBeat → install Python dependencies → copy scripts to REAPER.

---

## Manual Install (all platforms)

### Step 1: Install uv (Python package manager)

**macOS / Linux:**
```bash
curl -LsSf https://astral.sh/uv/install.sh | sh
```

**Windows (PowerShell):**
```powershell
irm https://astral.sh/uv/install.ps1 | iex
```

### Step 2: Download ReaBeat

**macOS / Linux:**
```bash
cd ~/Documents
git clone https://github.com/b451c/ReaBeat.git
cd ReaBeat
```

**Windows:**
```powershell
cd $env:USERPROFILE\Documents
git clone https://github.com/b451c/ReaBeat.git
cd ReaBeat
```

No git? The quick install scripts download a ZIP automatically. For manual install, download: https://github.com/b451c/ReaBeat/archive/refs/heads/main.zip

### Step 3: Install Python dependencies

```bash
uv sync
```
Downloads Python, PyTorch, beat-this (~800MB, one-time).

Verify:
```bash
uv run python -m reabeat check
```
Expected: `OK: beat-this ready`

### Step 4: Install REAPER dependencies

Open REAPER:
1. **Extensions > ReaPack > Import repositories**
2. Paste URL, click OK:
   ```
   https://github.com/mavriq-dev/public-reascripts/raw/master/index.xml
   ```
3. **Extensions > ReaPack > Browse packages**
4. Install these two:
   - **ReaImGui** (required)
   - **mavriq-lua-sockets** (required)
5. Restart REAPER

Recommended: install [SWS Extension](https://www.sws-extension.org/) (enables Support menu links).

### Step 5: Add script to REAPER

1. **Actions > Show action list**
2. **New action... > Load ReaScript...**
3. Select `reabeat.lua`:
   - **Auto-installer path:**
     - macOS: `~/Library/Application Support/REAPER/Scripts/ReaBeat/reabeat.lua`
     - Windows: `%APPDATA%\REAPER\Scripts\ReaBeat\reabeat.lua`
     - Linux: `~/.config/REAPER/Scripts/ReaBeat/reabeat.lua`
   - **Manual install path:**
     - macOS/Linux: `~/Documents/ReaBeat/scripts/reaper/reabeat.lua`
     - Windows: `Documents\ReaBeat\scripts\reaper\reabeat.lua`
4. (Optional) Assign a keyboard shortcut

### Step 6: Use

1. Select an audio item on your timeline
2. Run ReaBeat from Actions menu
3. Click **Detect Beats** (~2-3 seconds)
4. Choose action:
   - **Match Tempo** - adjust item to project BPM or custom target
   - **Insert Tempo Map** - align REAPER grid to audio
   - **Insert Stretch Markers** - for manual timing editing
   - **Match & Quantize** - tempo map + quantized markers in one click
5. Click **Apply** (Ctrl+Z to undo)

Backend launches automatically. Shuts down after 5 min idle.

---

## Troubleshooting

### "Starting..." hangs (backend won't start)
```bash
cd ~/Documents/ReaBeat   # or wherever you installed
uv run python -m reabeat check
```
- `OK: beat-this ready` → backend works, issue is REAPER connection. Try: close ReaBeat, restart REAPER.
- Error message → follow instructions in the error.

### "beat-this not installed"
```bash
cd ~/Documents/ReaBeat
uv sync
```

### Port 9877 already in use
**macOS / Linux:**
```bash
kill $(lsof -ti:9877)
```
**Windows:**
```powershell
netstat -ano | findstr :9877
taskkill /PID <PID_NUMBER> /F
```

### ReaImGui missing
Extensions > ReaPack > Browse packages > search "ReaImGui" > Install > Restart REAPER

### mavriq-lua-sockets missing
Extensions > ReaPack > Import repositories > paste URL above > Browse > search "mavriq-lua-sockets" > Install

### Wrong BPM detected
beat-this works best with clear rhythmic content. Ambient, classical, or heavily rubato recordings may produce less accurate results.

### Server log (for bug reports)
- **macOS / Linux:** `/tmp/reabeat_server.log`
- **Windows:** `%TEMP%\reabeat_server.log`
