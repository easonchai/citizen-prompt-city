#!/usr/bin/env python3
"""Read core/sprites.c back and write /tmp/sprites-preview.png (8x3 grid, 2x). Run from repo root."""
import re, subprocess
import numpy as np

hexes = ["141c3a", "141c3a", "1e2a58", "0a0e1f", "f6f1e4", "f0b840", "f06a5a", "2bb8a8", "2a2a2e", "f4a3b5", "ffe38a"]  # 0 -> navy
pal = np.array([[int(h[i:i+2], 16) for i in (0, 2, 4)] for h in hexes], dtype=np.uint8)
src = open("core/sprites.c").read()
vals = [int(v, 16) for v in re.findall(r"0x([0-9a-f]{2})", src)]
assert len(vals) == 24 * 4096, len(vals)
a = np.array(vals, dtype=np.uint8).reshape(3, 8, 64, 64)
grid = a.transpose(0, 2, 1, 3).reshape(3 * 64, 8 * 64)
rgb = pal[grid].repeat(2, 0).repeat(2, 1)
subprocess.run(["/opt/homebrew/bin/magick", "-size", f"{rgb.shape[1]}x{rgb.shape[0]}", "-depth", "8", "rgb:-", "/tmp/sprites-preview.png"], input=rgb.tobytes(), check=True)
