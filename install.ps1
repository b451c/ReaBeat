# REABeat Installer for Windows
# Run: powershell -ExecutionPolicy Bypass -File install.ps1
# Or:  irm https://raw.githubusercontent.com/USER/REABeat/main/install.ps1 | iex

$ErrorActionPreference = "Stop"

$REPO_URL = "https://github.com/USER/REABeat.git"  # TODO: update with real URL
$INSTALL_DIR = "$env:USERPROFILE\REABeat"
$REAPER_SCRIPTS = "$env:APPDATA\REAPER\Scripts"

Write-Host ""
Write-Host "  +======================================+" -ForegroundColor Cyan
Write-Host "  |     REABeat Installer                |" -ForegroundColor Cyan
Write-Host "  |     Neural beat detection for REAPER |" -ForegroundColor Cyan
Write-Host "  +======================================+" -ForegroundColor Cyan
Write-Host ""
Write-Host "  Platform: Windows"

# Step 1: Install uv if needed
Write-Host ""
Write-Host "  [1/4] Checking uv..." -ForegroundColor Yellow
$uvPath = Get-Command uv -ErrorAction SilentlyContinue
if ($uvPath) {
    Write-Host "         uv found: $($uvPath.Source)"
} else {
    Write-Host "         Installing uv (Python package manager)..."
    irm https://astral.sh/uv/install.ps1 | iex
    # Refresh PATH
    $env:PATH = "$env:USERPROFILE\.local\bin;$env:PATH"
    $uvPath = Get-Command uv -ErrorAction SilentlyContinue
    if (-not $uvPath) {
        Write-Host "  ERROR: uv installation failed." -ForegroundColor Red
        Write-Host "         Install manually: https://docs.astral.sh/uv/"
        exit 1
    }
    Write-Host "         uv installed: $($uvPath.Source)"
}

# Step 2: Clone or update repo
Write-Host ""
Write-Host "  [2/4] Getting REABeat..." -ForegroundColor Yellow
if (Test-Path $INSTALL_DIR) {
    Write-Host "         Updating existing installation..."
    Push-Location $INSTALL_DIR
    git pull --ff-only 2>$null
    Pop-Location
} else {
    Write-Host "         Downloading to $INSTALL_DIR..."
    git clone $REPO_URL $INSTALL_DIR
}
Push-Location $INSTALL_DIR

# Step 3: Install Python dependencies
Write-Host ""
Write-Host "  [3/4] Installing Python dependencies (torch + beat-this)..." -ForegroundColor Yellow
Write-Host "         This may take a few minutes on first install (~800MB)."
Write-Host ""
uv sync

Write-Host ""
Write-Host "         Verifying backend..."
uv run python -m reabeat check
Write-Host ""

# Step 4: Copy Lua scripts to REAPER
Write-Host ""
Write-Host "  [4/4] Installing REAPER scripts..." -ForegroundColor Yellow
$reaperDir = "$REAPER_SCRIPTS\REABeat"
if (-not (Test-Path $reaperDir)) {
    New-Item -ItemType Directory -Path $reaperDir -Force | Out-Null
}
Copy-Item "$INSTALL_DIR\scripts\reaper\*.lua" -Destination $reaperDir -Force
Write-Host "         Copied to: $reaperDir"

Pop-Location

Write-Host ""
Write-Host "  +======================================================+" -ForegroundColor Green
Write-Host "  |  Installation complete!                                |" -ForegroundColor Green
Write-Host "  |                                                        |" -ForegroundColor Green
Write-Host "  |  Next steps in REAPER:                                 |" -ForegroundColor Green
Write-Host "  |  1. Install ReaImGui & mavriq-lua-sockets via ReaPack  |" -ForegroundColor Green
Write-Host "  |  2. Actions > New action > Load ReaScript              |" -ForegroundColor Green
Write-Host "  |     Select: $reaperDir\reabeat.lua" -ForegroundColor Green
Write-Host "  |  3. Select an audio item and run REABeat               |" -ForegroundColor Green
Write-Host "  |                                                        |" -ForegroundColor Green
Write-Host "  |  ReaPack repo for sockets:                             |" -ForegroundColor Green
Write-Host "  |  https://github.com/mavriq-dev/public-reascripts/      |" -ForegroundColor Green
Write-Host "  |         raw/master/index.xml                           |" -ForegroundColor Green
Write-Host "  +======================================================+" -ForegroundColor Green
Write-Host ""
