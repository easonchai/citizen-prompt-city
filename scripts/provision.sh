#!/bin/zsh
# Flash a badge over USB and link it to a citizen: scripts/provision.sh <slug>
# The device key is on /admin/badges/<slug>; pass it in the environment so it never lands in shell history or logs:
#   DEVICE_KEY=... scripts/provision.sh tenuk-a1f3
# Backs up the factory flash first (once per board, by MAC) to $BACKUP (default ../factory-backup), erases, flashes
# the current firmware build, then sends url/slug/token over serial. Needs a USB-A to C data cable; one badge at a time.
set -e
SLUG=${1:?usage: DEVICE_KEY=... scripts/provision.sh <slug>}
: ${DEVICE_KEY:?set DEVICE_KEY (shown on /admin/badges/$SLUG)}
ROOT=${0:A:h:h}; BACKUP=${BACKUP:-$ROOT/../factory-backup}
PY=~/.platformio/penv/bin/python; E=~/.platformio/packages/tool-esptoolpy/esptool.py
port() { ls /dev/cu.usbmodem* 2>/dev/null | head -1; }
PORT=$(port); [ -n "$PORT" ] || { echo "no badge on USB (charge-only cable?)"; exit 1; }
MAC=$($PY $E --port $PORT read_mac 2>&1 | grep -m1 MAC | awk '{print $2}' | tr -d :)
mkdir -p $BACKUP; B=$BACKUP/factory-$MAC.bin
[ -f $B ] || $PY $E --port $PORT --baud 921600 read_flash 0 0x400000 $B >/dev/null
echo "backup $B"
$PY $E --port $PORT erase_flash >/dev/null && echo erased
(cd $ROOT/firmware && pio run -e citizen -t upload --upload-port $PORT | grep -c "Hash of data verified" | xargs echo "parts verified:")
sleep 4; PORT=$(port)
PORT=$PORT SLUG=$SLUG $PY - <<'PY'
import os, serial, time
s = serial.Serial(os.environ["PORT"], 115200, timeout=0.3); time.sleep(0.8); s.read(65536)
for c in ["url https://connect.prmpt.city", "slug " + os.environ["SLUG"], "token " + os.environ["DEVICE_KEY"]]:
    s.write((c + "\n").encode()); time.sleep(0.8)
s.write(b"version\n"); time.sleep(0.8)
print("version", s.read(65536).decode(errors="replace").strip().splitlines()[-1])
PY
echo "linked $SLUG (mac $MAC); add it to $BACKUP/devices.csv"
