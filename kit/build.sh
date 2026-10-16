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
cp "$ART"/screens/*-4x.png "$KIT/img/screens/"
cp "$ART"/sprites/*-IDLE.png "$KIT/img/sprites/"
cp "$ART/ref/diymore-case-front-back.png" "$KIT/img/board.png"
magick "$ART/assets/v2/3-hero.png" -resize 1600x -quality 85 "$KIT/img/hero.jpg"

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
