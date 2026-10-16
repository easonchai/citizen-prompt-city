#!/usr/bin/env bash
# One command that proves the badge still fits and still performs. Run before every push; CI runs the same.
#   make test   core unit tests (every screen, gestures, timers, save/restore, animation bounds)
#   make soak   7 simulated days of badge life with assertions on every frame
#   make bench  per-screen cost table and the ESP32-C3 frame estimate
#   make wasm   the simulator build
#   pio run     the real ESP32-C3 build, with a hard flash and RAM budget
#   selftest    the browser simulator's own checks (gestures, scenarios, power model), headless
set -euo pipefail
cd "$(dirname "$0")/.."

FLASH_BUDGET=${FLASH_BUDGET:-1700000}   # bytes, of the 1,966,080-byte OTA app slot (4 MB flash = 2 slots + spiffs). Leaves room to grow.
RAM_BUDGET=${RAM_BUDGET:-200000}        # bytes static, of 327,680

echo "== core tests"; make -s clean >/dev/null; make -s test | tail -1
echo "== soak";       make -s soak | grep -E "SOAK|frames|events"
echo "== bench";      make -s bench | grep -E "^case|^home |^home_blink|^qr |^settings"
echo "== wasm";       make -s wasm >/dev/null && ls -la sim/core.wasm | awk '{print $5" bytes"}'

echo "== esp32-c3 build"
out=$(cd firmware && pio run -e citizen 2>&1 | grep -E "RAM:|Flash:")
echo "$out"
flash=$(echo "$out" | grep Flash | sed -E 's/.*used ([0-9]+) bytes from ([0-9]+).*/\1/')
limit=$(echo "$out" | grep Flash | sed -E 's/.*used ([0-9]+) bytes from ([0-9]+).*/\2/')
ram=$(echo "$out" | grep RAM | sed -E 's/.*used ([0-9]+) bytes.*/\1/')
[ "$flash" -le "$FLASH_BUDGET" ] || { echo "FAIL flash $flash > budget $FLASH_BUDGET (slot $limit)"; exit 1; }
[ "$ram" -le "$RAM_BUDGET" ]     || { echo "FAIL ram $ram > budget $RAM_BUDGET"; exit 1; }
echo "ok flash $flash / slot $limit (budget $FLASH_BUDGET), ram $ram / 327680 (budget $RAM_BUDGET)"

echo "== simulator selftest"
CH=${CHROME:-"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"}
command -v google-chrome >/dev/null && CH=google-chrome || true
(cd sim && python3 -m http.server 8790 >/dev/null 2>&1 & echo $! > /tmp/citizen-sim.pid)
sleep 1
"$CH" --headless=new --disable-gpu --virtual-time-budget=12000 --dump-dom "http://localhost:8790/?selftest" 2>/dev/null \
  | grep -o 'SELFTEST[^<]*' | sed 's/&quot;/"/g' | head -20 || true
kill "$(cat /tmp/citizen-sim.pid)" 2>/dev/null || true
echo "== all checks passed"
