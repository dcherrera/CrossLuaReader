#!/bin/bash
# Convert all CrossPoint fonts to .cfont binary format for CrossLua Reader.
# Source TTFs from CrossPoint's font source directory.

set -e
cd "$(dirname "$0")"

# Find font sources — check common locations or use FONT_SRC env var
if [ -n "${FONT_SRC:-}" ] && [ -d "$FONT_SRC" ]; then
    echo "Using FONT_SRC=$FONT_SRC"
elif [ -d "../../lib/EpdFont/builtinFonts/source" ]; then
    FONT_SRC="../../lib/EpdFont/builtinFonts/source"
elif [ -d "../../../lib/EpdFont/builtinFonts/source" ]; then
    FONT_SRC="../../../lib/EpdFont/builtinFonts/source"
else
    # Search for it
    FOUND=$(find /Users -path "*/crosspoint-reader/lib/EpdFont/builtinFonts/source" -maxdepth 8 2>/dev/null | head -1)
    if [ -n "$FOUND" ]; then
        FONT_SRC="$FOUND"
        echo "Auto-detected font sources at: $FONT_SRC"
    else
        echo "ERROR: Cannot find font source directory."
        echo "Set FONT_SRC environment variable to the path containing Bookerly/, NotoSans/, etc."
        echo "Example: FONT_SRC=/path/to/crosspoint-reader/lib/EpdFont/builtinFonts/source bash convert_all.sh"
        exit 1
    fi
fi

OUT_DIR="../../sdcard/fonts"
mkdir -p "$OUT_DIR"

READER_STYLES=("Regular" "Italic" "Bold" "BoldItalic")
NOTOSANS_SIZES=(12 14 16 18)
BOOKERLY_SIZES=(12 14 16 18)
OPENDYSLEXIC_SIZES=(8 10 12 14)
UI_STYLES=("Regular" "Bold")
UI_SIZES=(10 12)

hebrew_weight() {
  case "$1" in
    Bold|BoldItalic) echo "Bold" ;;
    *) echo "Regular" ;;
  esac
}

# Check if Hebrew font sources exist
HAS_HEBREW=false
if [ -f "$FONT_SRC/NotoSansHebrew/NotoSansHebrew-Regular.ttf" ]; then
    HAS_HEBREW=true
    echo "Hebrew font sources found — including Hebrew glyphs"
else
    echo "No Hebrew font sources — converting without Hebrew"
fi

echo "Converting Noto Sans..."
for size in ${NOTOSANS_SIZES[@]}; do
  for style in ${READER_STYLES[@]}; do
    name="NotoSans-${size}-${style}"
    args="notosans_${size}_$(echo $style | tr '[:upper:]' '[:lower:]') $size"
    args="$args $FONT_SRC/NotoSans/NotoSans-${style}.ttf"

    if [ "$HAS_HEBREW" = true ]; then
        args="$args $FONT_SRC/NotoSansHebrew/NotoSansHebrew-$(hebrew_weight $style).ttf"
        args="$args --additional-intervals 0x0590,0x05FF"
    fi

    python3 cfont_convert.py $args --2bit --compress --cfont "$OUT_DIR/${name}.cfont"
    echo "  Generated ${name}.cfont"
  done
done

# Small Noto Sans
args="notosans_8_regular 8 $FONT_SRC/NotoSans/NotoSans-Regular.ttf"
if [ "$HAS_HEBREW" = true ]; then
    args="$args $FONT_SRC/NotoSansHebrew/NotoSansHebrew-Regular.ttf"
    args="$args --additional-intervals 0x0590,0x05FF"
fi
python3 cfont_convert.py $args --cfont "$OUT_DIR/NotoSans-8-Regular.cfont"
echo "  Generated NotoSans-8-Regular.cfont"

echo ""
echo "Converting Bookerly..."
for size in ${BOOKERLY_SIZES[@]}; do
  for style in ${READER_STYLES[@]}; do
    name="Bookerly-${size}-${style}"
    python3 cfont_convert.py "bookerly_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')" $size \
      "$FONT_SRC/Bookerly/Bookerly-${style}.ttf" \
      --2bit --compress \
      --cfont "$OUT_DIR/${name}.cfont"
    echo "  Generated ${name}.cfont"
  done
done

echo ""
echo "Converting OpenDyslexic..."
for size in ${OPENDYSLEXIC_SIZES[@]}; do
  for style in ${READER_STYLES[@]}; do
    name="OpenDyslexic-${size}-${style}"
    python3 cfont_convert.py "opendyslexic_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')" $size \
      "$FONT_SRC/OpenDyslexic/OpenDyslexic-${style}.otf" \
      --2bit --compress \
      --cfont "$OUT_DIR/${name}.cfont"
    echo "  Generated ${name}.cfont"
  done
done

echo ""
echo "Converting Ubuntu UI..."
for size in ${UI_SIZES[@]}; do
  for style in ${UI_STYLES[@]}; do
    name="Ubuntu-${size}-${style}"
    args="ubuntu_${size}_$(echo $style | tr '[:upper:]' '[:lower:]') $size"
    args="$args $FONT_SRC/Ubuntu/Ubuntu-${style}.ttf"

    if [ "$HAS_HEBREW" = true ]; then
        args="$args $FONT_SRC/NotoSansHebrew/NotoSansHebrew-$(hebrew_weight $style).ttf"
        args="$args --additional-intervals 0x0590,0x05FF"
    fi

    python3 cfont_convert.py $args --cfont "$OUT_DIR/${name}.cfont"
    echo "  Generated ${name}.cfont"
  done
done

echo ""
echo "Done. Fonts in $OUT_DIR/:"
ls "$OUT_DIR/"*.cfont 2>/dev/null | wc -l | tr -d ' '
echo " font files generated."
