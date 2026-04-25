#!/bin/bash
# Convert all CrossPoint fonts to .cfont binary format for CrossLua Reader.
# Source TTFs expected in ../../crosspoint-reader/lib/EpdFont/builtinFonts/source/

set -e
cd "$(dirname "$0")"

FONT_SRC="../../../../crosspoint-reader/lib/EpdFont/builtinFonts/source"
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

style_num() {
  case "$1" in
    Regular) echo 0 ;;
    Bold) echo 1 ;;
    Italic) echo 2 ;;
    BoldItalic) echo 3 ;;
    *) echo 0 ;;
  esac
}

echo "Converting Noto Sans..."
for size in ${NOTOSANS_SIZES[@]}; do
  for style in ${READER_STYLES[@]}; do
    name="NotoSans-${size}-${style}"
    hebrew_path="$FONT_SRC/NotoSansHebrew/NotoSansHebrew-$(hebrew_weight $style).ttf"
    python3 cfont_convert.py "notosans_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')" $size \
      "$FONT_SRC/NotoSans/NotoSans-${style}.ttf" "$hebrew_path" \
      --2bit --compress \
      --additional-intervals 0x0590,0x05FF \
      --cfont "$OUT_DIR/${name}.cfont"
  done
done

# Small Noto Sans
python3 cfont_convert.py notosans_8_regular 8 \
  "$FONT_SRC/NotoSans/NotoSans-Regular.ttf" \
  "$FONT_SRC/NotoSansHebrew/NotoSansHebrew-Regular.ttf" \
  --additional-intervals 0x0590,0x05FF \
  --cfont "$OUT_DIR/NotoSans-8-Regular.cfont"

echo "Converting Bookerly..."
for size in ${BOOKERLY_SIZES[@]}; do
  for style in ${READER_STYLES[@]}; do
    name="Bookerly-${size}-${style}"
    python3 cfont_convert.py "bookerly_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')" $size \
      "$FONT_SRC/Bookerly/Bookerly-${style}.ttf" \
      --2bit --compress \
      --cfont "$OUT_DIR/${name}.cfont"
  done
done

echo "Converting OpenDyslexic..."
for size in ${OPENDYSLEXIC_SIZES[@]}; do
  for style in ${READER_STYLES[@]}; do
    name="OpenDyslexic-${size}-${style}"
    python3 cfont_convert.py "opendyslexic_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')" $size \
      "$FONT_SRC/OpenDyslexic/OpenDyslexic-${style}.otf" \
      --2bit --compress \
      --cfont "$OUT_DIR/${name}.cfont"
  done
done

echo "Converting Ubuntu UI..."
for size in ${UI_SIZES[@]}; do
  for style in ${UI_STYLES[@]}; do
    name="Ubuntu-${size}-${style}"
    hebrew_path="$FONT_SRC/NotoSansHebrew/NotoSansHebrew-$(hebrew_weight $style).ttf"
    python3 cfont_convert.py "ubuntu_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')" $size \
      "$FONT_SRC/Ubuntu/Ubuntu-${style}.ttf" "$hebrew_path" \
      --additional-intervals 0x0590,0x05FF \
      --cfont "$OUT_DIR/${name}.cfont"
  done
done

echo ""
echo "Done. Fonts written to $OUT_DIR/"
ls -la "$OUT_DIR/"*.cfont | wc -l
echo "font files generated."
