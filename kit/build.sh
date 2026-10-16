#!/usr/bin/env bash
# Builds the speaker kit: booklet.pdf, one thank-you card PDF per badge, and copies of the renders and videos.
set -euo pipefail
KIT="$(cd "$(dirname "$0")" && pwd)"
ART="$(cd "$KIT/../.." && pwd)"   # citizen-badge/: screens, sprites, assets, ref
CHROME="${CHROME:-/Applications/Google Chrome.app/Contents/MacOS/Google Chrome}"

pdf() { # html -> pdf; virtual time lets Google Fonts finish loading before print
  "$CHROME" --headless=new --disable-gpu --no-pdf-header-footer --run-all-compositor-stages-before-draw \
    --virtual-time-budget=8000 --print-to-pdf="$2" "file://$1" 2>/dev/null
  [ -s "$2" ] || { echo "failed: $2" >&2; exit 1; }
}

# images the HTML pages reference (kept inside kit/ so the folder stands alone)
mkdir -p "$KIT/img/screens" "$KIT/img/sprites" "$KIT/renders" "$KIT/videos"
# 300 dpi print assets: integer nearest-neighbour upscales of the 1x pixel art, full-res sources for photos
for f in "$ART"/sprites/*-IDLE.png; do
  magick "$f" -filter Point -resize 800% "$KIT/img/sprites/$(basename "$f")"          # 64 -> 512 px, alpha kept
done
for f in "$ART"/screens/*.png; do
  case "$f" in *-4x.png) continue;; esac
  magick "$f" -filter Point -resize 400% "$KIT/img/screens/$(basename "$f" .png)-4x.png"   # 240 -> 960 px
done
for t in 0700 1200 1830 0300; do
  magick "$ART/citizen/tests/out/home_$t.ppm" -filter Point -resize 400% "$KIT/img/screens/day-$t-4x.png"
done
magick "$ART/assets/v2/2-flatlay.png" -resize 2000x -quality 90 "$KIT/img/board.jpg"   # 2K render of the real case, front and back
magick "$ART/assets/v2/3-hero.png" -quality 90 "$KIT/img/hero.jpg"   # full 2688 px

# renders and videos to share (real copies, not links)
for f in 0-badge-shirt 1-back-pin 2-flatlay 3-hero 4-scan 5-handover 9-world 10-cities; do
  cp "$ART/assets/v2/$f.png" "$KIT/renders/"
done
cp "$ART/assets/6-teaser.mp4" "$ART/assets/7-launch-video.mp4" "$KIT/videos/"

pdf "$KIT/booklet/index.html" "$KIT/booklet.pdf"
echo "booklet.pdf"

node "$KIT/thank-you/make.mjs"
for h in "$KIT"/thank-you/build/*.html; do
  slug="$(basename "$h" .html)"
  pdf "$h" "$KIT/thank-you/$slug.pdf"
  echo "thank-you/$slug.pdf"
done
