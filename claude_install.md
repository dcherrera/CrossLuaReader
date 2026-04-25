# CrossLua Reader — Installation Guide for Claude

This file contains everything an AI assistant needs to install CrossLua Reader on a user's Xteink X4 e-reader. Follow each section in order.

## Prerequisites

Before starting, confirm the user has:
- **Xteink X4** e-reader (ESP32-C3 based)
- **USB-C cable** (data cable, not charge-only)
- **Computer** with macOS, Linux, or Windows
- **SD card** (the one from the Xteink — must be removed for file transfer)
- **Python 3.8+** installed

## Step 1: Check and Install Dependencies

Run these checks. Install anything missing.

```bash
# Python 3
python3 --version

# PlatformIO (the build tool)
pip3 install platformio

# Font conversion dependencies
pip3 install freetype-py fonttools

# ESP32 build dependencies (may be needed)
pip3 install littlefs-python
```

**Platform-specific PlatformIO note:**
- On some systems, PlatformIO installs to its own venv at `~/.platformio/penv/`
- If `pio` command not found, try: `~/.platformio/penv/bin/pio` or `python3 -m platformio`

## Step 2: Clone the Repository

```bash
git clone https://github.com/dcherrera/CrossLuaReader.git
cd CrossLuaReader
```

## Step 3: Get Font Source Files

CrossLua Reader needs TTF font source files to generate .cfont files. These come from the CrossPoint Reader project:

```bash
# Clone CrossPoint (for font sources only)
git clone --recursive https://github.com/crosspoint-reader/crosspoint-reader.git ../crosspoint-reader
```

The font sources are at `../crosspoint-reader/lib/EpdFont/builtinFonts/source/`. The converter script expects them at this relative path.

If the user already has CrossPoint cloned, just verify the path.

## Step 4: Convert Fonts to .cfont Format

```bash
cd tools/cfont-convert
bash convert_all.sh
cd ../..
```

This generates `.cfont` files in `sdcard/fonts/`. Expected output: ~50 font files (Bookerly, NotoSans, NotoSansHebrew, OpenDyslexic, Ubuntu in multiple sizes/styles).

**If font conversion fails:**
- Check that freetype-py is installed: `python3 -c "import freetype"`
- Check that font source path exists: `ls ../crosspoint-reader/lib/EpdFont/builtinFonts/source/`
- The converter may print "python: command not found" — edit `convert_all.sh` to use `python3` instead of `python`

## Step 5: Build the Firmware

```bash
pio run
```

**Expected output:** Build succeeds, firmware at `.pio/build/default/firmware.bin`
**Expected size:** Flash ~555KB (8.5%), RAM ~75KB (22.9%)

**First build will be slow** (~5-10 minutes) as PlatformIO downloads the ESP32-C3 toolchain. Subsequent builds are fast (~10 seconds).

**Common build issues:**
- `ModuleNotFoundError: No module named 'littlefs'` → `pip3 install littlefs-python`
- `ModuleNotFoundError: No module named 'fatfs'` → Usually resolved by using PlatformIO's own venv: `~/.platformio/penv/bin/pio run`
- Symlink errors → The SDK is copied into `lib/open-x4-sdk/`, no symlinks needed

## Step 6: Prepare the SD Card

**IMPORTANT:** The Xteink X4 does NOT mount as USB mass storage. The SD card must be physically removed from the device, inserted into the computer, and files copied manually.

### SD card layout required:

```
SD Card Root/
├── plugins/
│   ├── home.lua
│   ├── file_browser.lua
│   ├── settings.lua
│   └── lib/
│       ├── theme.lua
│       ├── ui.lua
│       └── status_bar.lua
├── fonts/
│   ├── NotoSans-14-Regular.cfont
│   ├── NotoSans-14-Bold.cfont
│   └── ... (all generated .cfont files)
├── templates/
│   ├── home_lyra.lua
│   └── home_classic.lua
└── books/
    └── (user's epub, txt, md files)
```

### Copy commands:

```bash
# Set SD_PATH to the mount point of the SD card
SD_PATH="/Volumes/SD_CARD"  # macOS example
# SD_PATH="/media/user/SD_CARD"  # Linux example

# Copy plugins
cp -r sdcard/plugins "$SD_PATH/"

# Copy fonts (generated in Step 4)
cp -r sdcard/fonts "$SD_PATH/"

# Copy templates
cp -r sdcard/templates "$SD_PATH/"

# Create books directory
mkdir -p "$SD_PATH/books"
```

**Ask the user** where their SD card is mounted. On macOS it's usually `/Volumes/<name>`. On Linux it's usually `/media/<user>/<name>` or `/mnt/<name>`.

### After copying:
1. Safely eject the SD card from the computer
2. Re-insert it into the Xteink X4

## Step 7: Flash the Firmware

Connect the Xteink X4 via USB-C. The device must be **awake** (not sleeping).

### Detect the serial port:

**macOS:**
```bash
ls /dev/cu.usbmodem*
```

**Linux:**
```bash
ls /dev/ttyACM* /dev/ttyUSB*
```

**Windows (Git Bash):**
```bash
# Check Device Manager, or:
mode
```

### Flash:

```bash
pio run -t upload --upload-port /dev/cu.usbmodem101
```

Replace `/dev/cu.usbmodem101` with the actual port detected above.

**If the port is not found:**
- Is the USB cable a data cable? Charge-only cables won't work.
- Is the device awake? Press the power button.
- Try unplugging and replugging.
- On macOS, the ESP32-C3 shows as "USB JTAG/serial debug unit" in System Information.

**If PlatformIO's pio can't find the port:**
Use the full path to PlatformIO's venv:
```bash
~/.platformio/penv/bin/pio run -t upload --upload-port /dev/cu.usbmodem101
```

### After flashing:
The device will reset automatically. If the SD card has plugins and fonts, the home screen should appear.

## Step 8: Verify

After the device boots:
1. **Home screen** should show with "Continue Reading", "Browse Files", "Settings"
2. Navigate with **Up/Down** side buttons
3. Press **Confirm** (front button) to select
4. "Browse Files" should show SD card contents
5. "Settings" should show configuration options

**If the screen is blank or shows garbage:**
- Check that the SD card has `/plugins/home.lua` and `/fonts/NotoSans-14-Regular.cfont`
- Connect USB and check serial output: `pio device monitor` (look for error logs)

**If fonts don't render (replacement characters):**
- Check that `.cfont` files were generated correctly in Step 4
- The font path in home.lua expects `/fonts/NotoSans-14-Regular.cfont`

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| Blank screen | No plugins on SD | Copy sdcard/ contents to SD card |
| "?" characters | No font files | Run font conversion (Step 4), copy to SD |
| Build fails | Missing deps | Check Step 1 dependencies |
| Can't flash | Wrong port / device asleep | Wake device, check port, try different cable |
| Plugin crash | Lua error | Check serial log: `pio device monitor` |

## Quick Reference

```bash
# One-shot install (build + prepare SD contents):
./install.sh

# Build + flash to device:
./install.sh --flash

# Just prepare SD contents (no build):
./install.sh --sd-only

# Monitor serial output for debugging:
pio device monitor
```

## Hardware Notes

- **MCU:** ESP32-C3 (single-core RISC-V @ 160MHz)
- **RAM:** ~380KB (no PSRAM)
- **Flash:** 16MB
- **Display:** 800x480 e-ink (monochrome)
- **Storage:** SD card (FAT32)
- **USB:** Firmware flashing + serial debug only (NOT mass storage)
- **Buttons:** 4 front + 2 side + power
