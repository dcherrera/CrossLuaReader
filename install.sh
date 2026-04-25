#!/bin/bash
#
# CrossLua Reader — Install Script
#
# This script:
# 1. Checks and installs required dependencies
# 2. Converts font files to .cfont format
# 3. Builds the firmware
# 4. Prepares the SD card contents
# 5. Optionally flashes the firmware to the device
#
# Usage:
#   ./install.sh              # Full install (build + prepare SD)
#   ./install.sh --flash      # Full install + flash firmware to device
#   ./install.sh --sd-only    # Only prepare SD card contents (no build)
#   ./install.sh --build-only # Only build firmware (no SD prep)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SD_OUTPUT="$SCRIPT_DIR/build/sdcard"
FONT_SRC="$SCRIPT_DIR/../crosspoint-reader/lib/EpdFont/builtinFonts/source"
FONT_OUT="$SD_OUTPUT/fonts"
CONVERTER="$SCRIPT_DIR/tools/cfont-convert/cfont_convert.py"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC} $1"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $1"; }
error() { echo -e "${RED}[ERROR]${NC} $1"; exit 1; }

# ── Step 1: Check dependencies ─────────────────────────────────────

check_deps() {
    info "Checking dependencies..."

    # Python 3
    if ! command -v python3 &> /dev/null; then
        error "Python 3 is required. Install it from https://python.org"
    fi
    info "  Python 3: $(python3 --version)"

    # PlatformIO
    if command -v pio &> /dev/null; then
        info "  PlatformIO: $(pio --version)"
    elif python3 -m platformio --version &> /dev/null; then
        info "  PlatformIO (via python3 -m): $(python3 -m platformio --version)"
    else
        warn "  PlatformIO not found. Installing..."
        pip3 install platformio
        info "  PlatformIO installed"
    fi

    # Python font dependencies
    python3 -c "import freetype" 2>/dev/null || {
        warn "  freetype-py not found. Installing..."
        pip3 install freetype-py
    }
    python3 -c "import fontTools" 2>/dev/null || {
        warn "  fonttools not found. Installing..."
        pip3 install fonttools
    }

    # PlatformIO ESP32 dependencies
    python3 -c "import littlefs" 2>/dev/null || {
        warn "  littlefs-python not found. Installing..."
        pip3 install littlefs-python
    }
    python3 -c "import fatfs" 2>/dev/null || true  # May not be needed

    info "Dependencies OK"
}

# ── Step 2: Convert fonts ──────────────────────────────────────────

convert_fonts() {
    info "Converting fonts to .cfont format..."
    mkdir -p "$FONT_OUT"

    if [ ! -d "$FONT_SRC" ]; then
        warn "Font source not found at $FONT_SRC"
        warn "Skipping font conversion — you'll need to provide .cfont files manually"
        return
    fi

    if [ -f "$SCRIPT_DIR/tools/cfont-convert/convert_all.sh" ]; then
        cd "$SCRIPT_DIR/tools/cfont-convert"
        bash convert_all.sh
        cd "$SCRIPT_DIR"
        info "Fonts converted to $FONT_OUT"
    else
        warn "Font converter script not found — skipping"
    fi
}

# ── Step 3: Build firmware ─────────────────────────────────────────

build_firmware() {
    info "Building firmware..."
    cd "$SCRIPT_DIR"

    if command -v pio &> /dev/null; then
        pio run
    else
        python3 -m platformio run
    fi

    info "Firmware built successfully"
}

# ── Step 4: Prepare SD card contents ───────────────────────────────

prepare_sd() {
    info "Preparing SD card contents..."
    mkdir -p "$SD_OUTPUT/plugins/lib"
    mkdir -p "$SD_OUTPUT/fonts"
    mkdir -p "$SD_OUTPUT/templates"
    mkdir -p "$SD_OUTPUT/books"

    # Copy plugins
    cp -r "$SCRIPT_DIR/sdcard/plugins/"* "$SD_OUTPUT/plugins/" 2>/dev/null || true

    # Copy templates
    cp -r "$SCRIPT_DIR/sdcard/templates/"* "$SD_OUTPUT/templates/" 2>/dev/null || true

    info "SD card contents prepared at: $SD_OUTPUT"
    echo ""
    info "═══════════════════════════════════════════════════════"
    info "  SD Card Setup Instructions:"
    info "═══════════════════════════════════════════════════════"
    echo ""
    info "  1. Remove the SD card from your Xteink X4"
    info "  2. Insert it into your computer"
    info "  3. Copy the contents of $SD_OUTPUT/ to the SD card root:"
    echo ""
    echo "     cp -r $SD_OUTPUT/* /path/to/sdcard/"
    echo ""
    info "  Your SD card should look like:"
    echo "     /plugins/home.lua"
    echo "     /plugins/file_browser.lua"
    echo "     /plugins/settings.lua"
    echo "     /plugins/lib/theme.lua"
    echo "     /plugins/lib/ui.lua"
    echo "     /plugins/lib/status_bar.lua"
    echo "     /fonts/*.cfont"
    echo "     /templates/*.lua"
    echo "     /books/  (your epub, txt, md files)"
    echo ""
    info "  4. Eject SD card and re-insert into the Xteink X4"
    info "═══════════════════════════════════════════════════════"
}

# ── Step 5: Flash firmware ─────────────────────────────────────────

flash_firmware() {
    info "Flashing firmware to device..."

    # Detect serial port
    local port=""
    if [ "$(uname)" = "Darwin" ]; then
        port=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)
    else
        port=$(ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | head -1)
    fi

    if [ -z "$port" ]; then
        error "No USB device found. Make sure your Xteink X4 is connected via USB and awake."
    fi

    info "Found device at: $port"

    if command -v pio &> /dev/null; then
        pio run -t upload --upload-port "$port"
    else
        python3 -m platformio run -t upload --upload-port "$port"
    fi

    info "Firmware flashed successfully!"
    echo ""
    info "Make sure the SD card has plugins and fonts before booting."
}

# ── Main ───────────────────────────────────────────────────────────

echo ""
echo "╔═══════════════════════════════════════════╗"
echo "║       CrossLua Reader — Installer         ║"
echo "╚═══════════════════════════════════════════╝"
echo ""

case "${1:-}" in
    --flash)
        check_deps
        convert_fonts
        build_firmware
        prepare_sd
        flash_firmware
        ;;
    --sd-only)
        check_deps
        convert_fonts
        prepare_sd
        ;;
    --build-only)
        check_deps
        build_firmware
        ;;
    *)
        check_deps
        convert_fonts
        build_firmware
        prepare_sd
        echo ""
        info "To flash the firmware, run: ./install.sh --flash"
        ;;
esac

echo ""
info "Done!"
