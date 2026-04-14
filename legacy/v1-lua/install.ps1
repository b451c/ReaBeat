# ReaBeat Installer for Windows
# Run: powershell -ExecutionPolicy Bypass -File install.ps1
# Or:  irm https://raw.githubusercontent.com/b451c/ReaBeat/main/install.ps1 | iex

$REPO_URL = "https://github.com/b451c/ReaBeat.git"
$INSTALL_DIR = "$env:USERPROFILE\ReaBeat"

function Abort($msg) {
    Write-Host ""
    Write-Host "  ERROR: $msg" -ForegroundColor Red
    Write-Host ""
    Write-Host "  Press any key to exit..." -ForegroundColor Gray
    $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
    exit 1
}

Write-Host ""
Write-Host "  +======================================+" -ForegroundColor Cyan
Write-Host "  |     ReaBeat Installer                |" -ForegroundColor Cyan
Write-Host "  |     Neural beat detection for REAPER |" -ForegroundColor Cyan
Write-Host "  +======================================+" -ForegroundColor Cyan
Write-Host ""
Write-Host "  Platform: Windows"

# Find REAPER resource path
$REAPER_RESOURCE = "$env:APPDATA\REAPER"
if (-not (Test-Path $REAPER_RESOURCE)) {
    Write-Host ""
    Write-Host "  REAPER not found at default location:"
    Write-Host "    $REAPER_RESOURCE"
    Write-Host ""
    Write-Host "  To find your REAPER resource path:"
    Write-Host "    REAPER > Options > Show REAPER resource path"
    Write-Host ""
    $REAPER_RESOURCE = Read-Host "  Enter path"

    if (-not (Test-Path (Join-Path $REAPER_RESOURCE "reaper.ini"))) {
        Abort "reaper.ini not found in: $REAPER_RESOURCE`n         Make sure REAPER is installed and has been run at least once."
    }
}
$REAPER_SCRIPTS = "$REAPER_RESOURCE\Scripts"

# Step 1: Install uv if needed
Write-Host ""
Write-Host "  [1/4] Checking uv..." -ForegroundColor Yellow
$uvPath = Get-Command uv -ErrorAction SilentlyContinue
if ($uvPath) {
    Write-Host "         uv found: $($uvPath.Source)"
} else {
    Write-Host "         Installing uv (Python package manager)..."
    try {
        $uvInstaller = Join-Path $env:TEMP "uv_install.ps1"
        Invoke-WebRequest -Uri "https://astral.sh/uv/install.ps1" -OutFile $uvInstaller -UseBasicParsing
        & powershell -ExecutionPolicy Bypass -File $uvInstaller
        Remove-Item $uvInstaller -ErrorAction SilentlyContinue
    } catch {
        Abort "Failed to download/run uv installer: $_`n         Install manually: https://docs.astral.sh/uv/"
    }
    # Refresh PATH (uv installs to ~/.local/bin or ~/.cargo/bin)
    $env:PATH = "$env:USERPROFILE\.local\bin;$env:USERPROFILE\.cargo\bin;$env:PATH"
    $uvPath = Get-Command uv -ErrorAction SilentlyContinue
    if (-not $uvPath) {
        Abort "uv installed but not found in PATH.`n         Close this window, open a new PowerShell, and run the installer again."
    }
    Write-Host "         uv installed: $($uvPath.Source)"
}

# Step 2: Clone or update repo
Write-Host ""
Write-Host "  [2/4] Getting ReaBeat..." -ForegroundColor Yellow
$gitPath = Get-Command git -ErrorAction SilentlyContinue
$hasGitRepo = (Test-Path $INSTALL_DIR) -and (Test-Path (Join-Path $INSTALL_DIR ".git"))
if ($gitPath -and $hasGitRepo) {
    # Existing git repo — pull updates
    Write-Host "         Updating existing installation..."
    Push-Location $INSTALL_DIR
    git pull --ff-only
    if ($LASTEXITCODE -ne 0) {
        Pop-Location
        Abort "git pull failed. Try deleting $INSTALL_DIR and running installer again."
    }
    Pop-Location
} elseif ($gitPath -and -not (Test-Path $INSTALL_DIR)) {
    # Fresh install with git
    Write-Host "         Downloading to $INSTALL_DIR..."
    git clone $REPO_URL $INSTALL_DIR
    if ($LASTEXITCODE -ne 0) {
        Abort "git clone failed. Check your internet connection."
    }
} else {
    # No git — download ZIP archive instead
    $zipUrl = "https://github.com/b451c/ReaBeat/archive/refs/heads/main.zip"
    $zipFile = Join-Path $env:TEMP "ReaBeat-main.zip"
    $extractDir = Join-Path $env:TEMP "ReaBeat-extract"
    try {
        Write-Host "         git not found — downloading ZIP..."
        Invoke-WebRequest -Uri $zipUrl -OutFile $zipFile -UseBasicParsing
        if (Test-Path $extractDir) { Remove-Item $extractDir -Recurse -Force }
        Expand-Archive -Path $zipFile -DestinationPath $extractDir -Force
        $extracted = Join-Path $extractDir "ReaBeat-main"
        if (-not (Test-Path $extracted)) {
            Abort "ZIP extraction failed — expected ReaBeat-main folder not found."
        }
        if (Test-Path $INSTALL_DIR) {
            # Update: overwrite files but preserve .venv (800MB Python deps)
            Get-ChildItem $extracted | ForEach-Object {
                Copy-Item $_.FullName -Destination $INSTALL_DIR -Recurse -Force
            }
            Remove-Item $extracted -Recurse -Force
        } else {
            Move-Item $extracted $INSTALL_DIR
        }
        Remove-Item $zipFile -ErrorAction SilentlyContinue
        Remove-Item $extractDir -Recurse -Force -ErrorAction SilentlyContinue
        Write-Host "         Downloaded to $INSTALL_DIR"
    } catch {
        Abort "Failed to download ReaBeat: $_`n         Try manually: $zipUrl"
    }
}
Push-Location $INSTALL_DIR

# Step 3: Install Python dependencies
Write-Host ""
Write-Host "  [3/4] Installing Python dependencies (torch + beat-this)..." -ForegroundColor Yellow
Write-Host "         This may take a few minutes on first install (~800MB)."
Write-Host ""
uv sync
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    Abort "uv sync failed. Check the output above for details."
}

# Install CUDA PyTorch if NVIDIA GPU detected and not already installed
$nvidiaSmi = Get-Command nvidia-smi -ErrorAction SilentlyContinue
if ($nvidiaSmi) {
    $cudaReady = uv run python -c "import torch; print(torch.cuda.is_available())" 2>$null
    if ($cudaReady -ne "True") {
        Write-Host ""
        Write-Host "         NVIDIA GPU detected — installing CUDA acceleration (~2.5GB)..."
        Write-Host "         This enables 10-50x faster beat detection."
        Write-Host ""
        uv pip install torch torchaudio --index-url https://download.pytorch.org/whl/cu124 --reinstall-package torch --reinstall-package torchaudio
        if ($LASTEXITCODE -ne 0) {
            Write-Host "         CUDA install failed — continuing with CPU (still works, just slower)." -ForegroundColor Yellow
        }
    } else {
        Write-Host "         NVIDIA GPU + CUDA PyTorch already installed."
    }
}

Write-Host ""
Write-Host "         Verifying backend..."
uv run python -m reabeat check
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    Abort "Backend verification failed. Check the output above for details."
}
Write-Host ""

# Step 4: Copy Lua scripts to REAPER
Write-Host ""
Write-Host "  [4/4] Installing REAPER scripts..." -ForegroundColor Yellow
$reaperDir = "$REAPER_SCRIPTS\ReaBeat"
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
Write-Host "  |  3. Select an audio item and run ReaBeat               |" -ForegroundColor Green
Write-Host "  |                                                        |" -ForegroundColor Green
Write-Host "  |  ReaPack repo for sockets:                             |" -ForegroundColor Green
Write-Host "  |  https://github.com/mavriq-dev/public-reascripts/      |" -ForegroundColor Green
Write-Host "  |         raw/master/index.xml                           |" -ForegroundColor Green
Write-Host "  +======================================================+" -ForegroundColor Green
Write-Host ""
Write-Host "  Press any key to exit..." -ForegroundColor Gray
$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
