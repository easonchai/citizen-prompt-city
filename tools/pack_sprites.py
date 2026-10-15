#!/usr/bin/env python3
"""Pack 24 sprite PNGs into core/sprites.c. Run from repo root. Needs ImageMagick `magick`."""
import subprocess, sys
import numpy as np

SRC = "/Users/easonchai/Documents/projects/citizen-badge/sprites/"
MAGICK = "/opt/homebrew/bin/magick"
CREATURES = ["tapir", "hornbill", "tiger"]
POSES = ["IDLE", "BLINK", "EAT", "PLAY", "SLEEP", "SULK", "TALK", "CHEER"]
PAL = ["141c3a", "1e2a58", "0a0e1f", "f6f1e4", "f0b840", "f06a5a", "2bb8a8", "2a2a2e", "f4a3b5", "ffe38a"]  # indices 1..10
PAL = np.array([[int(h[i:i+2], 16) for i in (0, 2, 4)] for h in PAL], dtype=np.int32)

def load(path):
    raw = subprocess.run([MAGICK, path, "-depth", "8", "rgba:-"], capture_output=True, check=True).stdout
    if len(raw) != 64 * 64 * 4:
        sys.exit(f"{path}: magick gave {len(raw)} bytes, expected {64*64*4}")
    px = np.frombuffer(raw, dtype=np.uint8).reshape(4096, 4).astype(np.int32)
    d = ((px[:, None, :3] - PAL[None]) ** 2).sum(2)
    idx = d.argmin(1) + 1
    idx[px[:, 3] < 128] = 0
    return idx, d.min(1), px

out = ['#include "citizen.h"', "", "static const uint8_t SPR[CZ_CREATURES][CZ_POSES][CZ_SPRITE*CZ_SPRITE] = {"]
for c in CREATURES:
    out.append("{")
    for p in POSES:
        name = f"{c}-{p}.png"
        idx, dist, px = load(SRC + name)
        bad = [(i, dist[i]) for i in range(4096) if idx[i] and dist[i] > 0]
        if bad:
            print(f"{name}: {len(bad)} non-exact px, worst dist2={max(d for _, d in bad)}", file=sys.stderr)
        out.append("{")
        for i in range(0, 4096, 32):
            out.append(",".join(f"0x{v:02x}" for v in idx[i:i+32]) + ",")
        out.append("},")
    out.append("},")
out.append("};")
out.append("")
out.append("const uint8_t *cz_sprite(int creature, int pose){ if(creature<0||creature>=CZ_CREATURES||pose<0||pose>=CZ_POSES) return SPR[0][0]; return SPR[creature][pose]; }")
open("core/sprites.c", "w").write("\n".join(out) + "\n")
