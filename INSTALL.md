# REABeat — Installation Guide

## The Fastest Way (macOS, recommended)

### Step 1: Install uv (Python package manager, 10 seconds)
Open Terminal and paste:
```bash
curl -LsSf https://astral.sh/uv/install.sh | sh
```

### Step 2: Download REABeat
```bash
cd ~/Documents
git clone https://github.com/YOUR_USERNAME/REABeat.git
cd REABeat
uv sync
```
This downloads Python, torch, beat-this, and all dependencies automatically (~800MB, one-time).

### Step 3: Install REAPER dependencies
Open REAPER:
1. **Extensions > ReaPack > Import repositories**
2. Paste this URL and click OK:
   ```
   https://github.com/mavriq-dev/public-reascripts/raw/master/index.xml
   ```
3. **Extensions > ReaPack > Browse packages**
4. Search and install:
   - **ReaImGui** (required)
   - **mavriq-lua-sockets** (required)
   - **SWS Extension** (recommended — install from https://www.sws-extension.org/)

### Step 4: Add REABeat to REAPER
1. **Actions > Show action list**
2. **New action > Load ReaScript**
3. Navigate to `~/Documents/REABeat/scripts/reaper/reabeat.lua`
4. Click OK
5. (Optional) Assign a keyboard shortcut

### Step 5: Run
Select an audio item on your timeline, then run REABeat from the Actions menu.
The Python backend starts automatically on first use.

---

## Troubleshooting

### "Starting backend..." hangs
The Python server couldn't launch. Check:
```bash
cd ~/Documents/REABeat
uv run python -m reabeat check
```
If it prints "OK: beat-this ready" — the backend works. Problem is in REAPER's connection.
If it prints an error — follow the instructions in the error message.

### "beat-this not installed" error
```bash
cd ~/Documents/REABeat
uv sync
```

### Server won't start (port 9877 in use)
```bash
kill $(lsof -ti:9877)
```
Then restart REABeat in REAPER.

### ReaImGui error on launch
Install via ReaPack:
- Extensions > ReaPack > Browse packages > search "ReaImGui" > Install

### Socket error / mavriq-lua-sockets
Install via ReaPack:
- Extensions > ReaPack > Import repositories
- URL: `https://github.com/mavriq-dev/public-reascripts/raw/master/index.xml`
- Browse packages > search "mavriq-lua-sockets" > Install

---

## Windows

Same steps, but:
- Install uv: `powershell -c "irm https://astral.sh/uv/install.ps1 | iex"`
- Use `cd %USERPROFILE%\Documents` instead of `~/Documents`
- Everything else is identical

## Linux

Same as macOS. uv works on Linux natively.
