#!/bin/bash
# ReaBeat Installer for macOS / Linux
# One command: curl -sSL https://raw.githubusercontent.com/USER/ReaBeat/main/install.sh | bash

set -e

REPO_URL="https://github.com/b451c/ReaBeat.git"
INSTALL_DIR="$HOME/ReaBeat"

echo ""
echo "  ╔══════════════════════════════════════╗"
echo "  ║     ReaBeat Installer                ║"
echo "  ║     Neural beat detection for REAPER ║"
echo "  ╚══════════════════════════════════════╝"
echo ""

# Detect OS
OS="$(uname -s)"
case "$OS" in
    Darwin) PLATFORM="macOS" ;;
    Linux)  PLATFORM="Linux" ;;
    *)      echo "ERROR: Unsupported OS: $OS"; exit 1 ;;
esac
echo "  Platform: $PLATFORM"

# Find REAPER resource path
if [ "$PLATFORM" = "macOS" ]; then
    REAPER_RESOURCE="$HOME/Library/Application Support/REAPER"
else
    REAPER_RESOURCE="$HOME/.config/REAPER"
fi

if [ ! -d "$REAPER_RESOURCE" ]; then
    echo ""
    echo "  REAPER not found at default location:"
    echo "    $REAPER_RESOURCE"
    echo ""
    echo "  To find your REAPER resource path:"
    echo "    REAPER > Options > Show REAPER resource path"
    echo ""
    read -r -p "  Enter path: " REAPER_RESOURCE < /dev/tty

    if [ ! -f "$REAPER_RESOURCE/reaper.ini" ]; then
        echo ""
        echo "  ERROR: reaper.ini not found in: $REAPER_RESOURCE"
        echo "  Make sure REAPER is installed and has been run at least once."
        exit 1
    fi
fi

REAPER_SCRIPTS="$REAPER_RESOURCE/Scripts"

# Step 1: Install uv if needed
echo ""
echo "  [1/4] Checking uv..."
if command -v uv &>/dev/null; then
    echo "         uv found: $(which uv)"
else
    echo "         Installing uv (Python package manager)..."
    curl -LsSf https://astral.sh/uv/install.sh | sh
    export PATH="$HOME/.local/bin:$PATH"
    if ! command -v uv &>/dev/null; then
        echo "  ERROR: uv installation failed. Install manually:"
        echo "         https://docs.astral.sh/uv/"
        exit 1
    fi
    echo "         uv installed: $(which uv)"
fi

# Step 2: Clone or update repo
echo ""
echo "  [2/4] Getting ReaBeat..."
if command -v git &>/dev/null && [ -d "$INSTALL_DIR/.git" ]; then
    # Existing git repo — pull updates
    echo "         Updating existing installation..."
    cd "$INSTALL_DIR"
    git pull --ff-only 2>/dev/null || echo "         (could not auto-update, using existing)"
elif command -v git &>/dev/null && [ ! -d "$INSTALL_DIR" ]; then
    # Fresh install with git
    echo "         Downloading to $INSTALL_DIR..."
    git clone "$REPO_URL" "$INSTALL_DIR"
    cd "$INSTALL_DIR"
else
    # No git — download ZIP archive instead
    echo "         git not found — downloading ZIP..."
    ZIP_URL="https://github.com/b451c/ReaBeat/archive/refs/heads/main.zip"
    ZIP_FILE="/tmp/ReaBeat-main.zip"
    EXTRACT_DIR="/tmp/ReaBeat-extract"
    curl -sSL "$ZIP_URL" -o "$ZIP_FILE" || { echo "  ERROR: Download failed"; exit 1; }
    rm -rf "$EXTRACT_DIR"
    unzip -q "$ZIP_FILE" -d "$EXTRACT_DIR" || { echo "  ERROR: Extraction failed (install unzip: sudo apt install unzip)"; exit 1; }
    if [ -d "$INSTALL_DIR" ]; then
        # Update: overwrite files but preserve .venv (800MB Python deps)
        cp -a "$EXTRACT_DIR/ReaBeat-main/." "$INSTALL_DIR/"
        rm -rf "$EXTRACT_DIR/ReaBeat-main"
    else
        mv "$EXTRACT_DIR/ReaBeat-main" "$INSTALL_DIR"
    fi
    rm -f "$ZIP_FILE"
    rm -rf "$EXTRACT_DIR"
    cd "$INSTALL_DIR"
    echo "         Downloaded to $INSTALL_DIR"
fi

# Step 3: Install Python dependencies
echo ""
echo "  [3/4] Installing Python dependencies (torch + beat-this)..."
echo "         This may take a few minutes on first install (~800MB)."
echo ""
uv sync

# Install CUDA PyTorch if NVIDIA GPU detected and not already installed
if command -v nvidia-smi &>/dev/null; then
    CUDA_READY=$(uv run python -c "import torch; print(torch.cuda.is_available())" 2>/dev/null)
    if [ "$CUDA_READY" != "True" ]; then
        echo ""
        echo "         NVIDIA GPU detected — installing CUDA acceleration (~2.5GB)..."
        echo "         This enables 10-50x faster beat detection."
        echo ""
        uv pip install torch torchaudio --index-url https://download.pytorch.org/whl/cu124 --reinstall-package torch --reinstall-package torchaudio || \
            echo "         CUDA install failed — continuing with CPU (still works, just slower)."
    else
        echo "         NVIDIA GPU + CUDA PyTorch already installed."
    fi
fi

echo ""
echo "         Verifying backend..."
uv run python -m reabeat check
echo ""

# Step 4: Link Lua scripts to REAPER
echo ""
echo "  [4/4] Installing REAPER scripts..."
mkdir -p "$REAPER_SCRIPTS/ReaBeat"

# Copy (not symlink — more reliable across volumes)
cp "$INSTALL_DIR/scripts/reaper/"*.lua "$REAPER_SCRIPTS/ReaBeat/"
echo "         Copied to: $REAPER_SCRIPTS/ReaBeat/"

echo ""
echo "  ╔══════════════════════════════════════════════════════╗"
echo "  ║  Installation complete!                              ║"
echo "  ║                                                      ║"
echo "  ║  Next steps in REAPER:                               ║"
echo "  ║  1. Install ReaImGui & mavriq-lua-sockets via ReaPack║"
echo "  ║  2. Actions > New action > Load ReaScript            ║"
echo "  ║     Select: $REAPER_SCRIPTS/ReaBeat/reabeat.lua"
echo "  ║  3. Select an audio item and run ReaBeat             ║"
echo "  ║                                                      ║"
echo "  ║  ReaPack repo for sockets:                           ║"
echo "  ║  https://github.com/mavriq-dev/public-reascripts/    ║"
echo "  ║         raw/master/index.xml                         ║"
echo "  ╚══════════════════════════════════════════════════════╝"
echo ""
