#!/usr/bin/env python3
"""Build a Kling end frame that simulates a real camera move from a single still.

Perspective-warps the image so the near edge (bottom) shifts more than the far edge (top)
-> parallax / arc, with an optional push (zoom) about a centre point and a small roll.

Usage: python3 make_endframe.py SRC OUT --zoom 1.06 --cx 0.5 --cy 0.54 --dxf -30 --dxn -110 [--dyl 0 --dyr 0]
  dxf/dxn: horizontal shift in source px of the sampling window at the top / bottom edge.
           Negative = camera moves left (content slides right). Near (dxn) should exceed far (dxf).
  dyl/dyr: vertical shift of the left / right edges (roll for an arc).
  Rise: use --dyf / --dyn (vertical shift at top / bottom edges).
"""
import argparse

import numpy as np
from PIL import Image

p = argparse.ArgumentParser()
p.add_argument("src"); p.add_argument("out")
for k, v in dict(zoom=1.06, cx=0.5, cy=0.5, dxf=0.0, dxn=0.0, dyl=0.0, dyr=0.0, dyf=0.0, dyn=0.0).items():
    p.add_argument("--" + k, type=float, default=v)
a = p.parse_args()
im = Image.open(a.src).convert("RGB"); W, H = im.size
w, h = W / a.zoom, H / a.zoom
cx, cy = a.cx * W, a.cy * H
src = [(cx - w / 2 + a.dxf, cy - h / 2 + a.dyl + a.dyf), (cx + w / 2 + a.dxf, cy - h / 2 + a.dyr + a.dyf),
       (cx + w / 2 + a.dxn, cy + h / 2 + a.dyr + a.dyn), (cx - w / 2 + a.dxn, cy + h / 2 + a.dyl + a.dyn)]
for x, y in src:
    if not (-1 <= x <= W + 1 and -1 <= y <= H + 1):
        raise SystemExit(f"window outside image at ({x:.0f},{y:.0f}); raise --zoom or move --cx/--cy")
dst = [(0, 0), (W, 0), (W, H), (0, H)]
A, B = [], []
for (x, y), (u, v) in zip(dst, src):
    A += [[x, y, 1, 0, 0, 0, -u * x, -u * y], [0, 0, 0, x, y, 1, -v * x, -v * y]]; B += [u, v]
co = np.linalg.solve(np.array(A, float), np.array(B, float)).tolist()
im.transform((W, H), Image.PERSPECTIVE, co, Image.BICUBIC).save(a.out)
print("wrote", a.out, [tuple(round(c) for c in q) for q in src])
