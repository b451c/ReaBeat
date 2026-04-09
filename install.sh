#!/bin/bash
# REABeat Installer for macOS / Linux
# One command: curl -sSL https://raw.githubusercontent.com/USER/REABeat/main/install.sh | bash

set -e

REPO_URL="https://github.com/b451c/REABeat.git"
INSTALL_DIR="$HOME/REABeat"
REAPER_SCRIPTS=""

echo ""
echo "  ╔══════════════════════════════════════╗"
echo "  ║     REABeat Installer                ║"
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
    REAPER_SCRIPTS="$HOME/Library/Application Support/REAPER/Scripts"
else
    REAPER_SCRIPTS="$HOME/.config/REAPER/Scripts"
fi

if [ ! -d "$(dirname "$REAPER_SCRIPTS")" ]; then
    echo ""
    echo "  WARNING: REAPER config directory not found."
    echo "  Expected: $(dirname "$REAPER_SCRIPTS")"
    echo "  Make sure REAPER is installed and has been run at least once."
    echo ""
fi

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
echo "  [2/4] Getting REABeat..."
if [ -d "$INSTALL_DIR" ]; then
    echo "         Updating existing installation..."
    cd "$INSTALL_DIR"
    git pull --ff-only 2>/dev/null || echo "         (could not auto-update, using existing)"
else
    echo "         Downloading to $INSTALL_DIR..."
    git clone "$REPO_URL" "$INSTALL_DIR"
    cd "$INSTALL_DIR"
fi

# Step 3: Install Python dependencies
echo ""
echo "  [3/4] Installing Python dependencies (torch + beat-this)..."
echo "         This may take a few minutes on first install (~800MB)."
echo ""
uv sync

echo ""
echo "         Verifying backend..."
uv run python -m reabeat check
echo ""

# Step 4: Link Lua scripts to REAPER
echo ""
echo "  [4/4] Installing REAPER scripts..."
mkdir -p "$REAPER_SCRIPTS/REABeat"

# Copy (not symlink — more reliable across volumes)
cp "$INSTALL_DIR/scripts/reaper/"*.lua "$REAPER_SCRIPTS/REABeat/"
echo "         Copied to: $REAPER_SCRIPTS/REABeat/"

echo ""
echo "  ╔══════════════════════════════════════════════════════╗"
echo "  ║  Installation complete!                              ║"
echo "  ║                                                      ║"
echo "  ║  Next steps in REAPER:                               ║"
echo "  ║  1. Install ReaImGui & mavriq-lua-sockets via ReaPack║"
echo "  ║  2. Actions > New action > Load ReaScript            ║"
echo "  ║     Select: $REAPER_SCRIPTS/REABeat/reabeat.lua"
echo "  ║  3. Select an audio item and run REABeat             ║"
echo "  ║                                                      ║"
echo "  ║  ReaPack repo for sockets:                           ║"
echo "  ║  https://github.com/mavriq-dev/public-reascripts/    ║"
echo "  ║         raw/master/index.xml                         ║"
echo "  ╚══════════════════════════════════════════════════════╝"
echo ""
