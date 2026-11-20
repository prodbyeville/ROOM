#!/usr/bin/env python3
"""Add a time-varying depth-of-field focus pull to an existing clip (no AI, no audio).

The depth plane is approximated as depth = kx*x + ky*(1-y) in normalised frame coords
(near = bottom-left, far = top-right). The focus point travels from `focus_from` to
`focus_to` (normalised frame coords) between t0 and t1 with an eased curve. The extra
blur is scaled by an envelope so frames that already carry the model's own blur
(start / end) are not doubled.

Usage: python3 dof_pass.py <config.json>
"""
import json
import subprocess
import sys

import numpy as np
from PIL import Image, ImageFilter


def ease(x):
    x = min(max(x, 0.0), 1.0)
    return x * x * x * (x * (6 * x - 15) + 10)


def envelope(t, env):
    a, b, c, d = env  # ramp up a->b, hold, ramp down c->d
    if t <= a or t >= d:
        return 0.0
    if t < b:
        return ease((t - a) / (b - a))
    if t <= c:
        return 1.0
    return 1.0 - ease((t - c) / (d - c))


def main():
    cfg = json.load(open(sys.argv[1]))
    W, H, fps = cfg.get("width", 1920), cfg.get("height", 1080), cfg.get("fps", 24)
    kx, ky = cfg["depth"]["kx"], cfg["depth"]["ky"]
    yy, xx = np.mgrid[0:H, 0:W].astype(float)
    depth = kx * xx / W + ky * (1 - yy / H)
    fd0 = kx * cfg["focus_from"][0] + ky * (1 - cfg["focus_from"][1])
    fd1 = kx * cfg["focus_to"][0] + ky * (1 - cfg["focus_to"][1])
    levels = [0, 1.5, 3, 5, 8, 12, 17]
    dec = subprocess.Popen(["ffmpeg", "-v", "error", "-i", cfg["in"], "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
                           stdout=subprocess.PIPE)
    enc = subprocess.Popen(["ffmpeg", "-v", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}",
                            "-r", str(fps), "-i", "-", "-an", "-c:v", "libx264", "-preset", "slow", "-crf", "14",
                            "-pix_fmt", "yuv420p", "-movflags", "+faststart", cfg["out"]], stdin=subprocess.PIPE)
    i = 0
    while True:
        buf = dec.stdout.read(W * H * 3)
        if len(buf) < W * H * 3:
            break
        t = i / fps
        frame = np.frombuffer(buf, np.uint8).reshape(H, W, 3).astype(float)
        e = envelope(t, cfg["envelope"])
        if e > 0.001:
            fs = ease((t - cfg["t0"]) / (cfg["t1"] - cfg["t0"]))
            fd = fd0 + (fd1 - fd0) * fs
            r = np.clip(np.abs(depth - fd) * cfg["strength"], 0, cfg["max_blur"]) * e
            im = Image.fromarray(frame.astype(np.uint8))
            stack = [frame] + [np.asarray(im.filter(ImageFilter.GaussianBlur(l)), float) for l in levels[1:]]
            out = frame.copy()
            for k in range(len(levels) - 1):
                lo, hi = levels[k], levels[k + 1]
                m = (r >= lo) & (r < hi)
                if m.any():
                    w = ((r - lo) / (hi - lo))[..., None]
                    out[m] = (stack[k] * (1 - w) + stack[k + 1] * w)[m]
            m = r >= levels[-1]
            out[m] = stack[-1][m]
            frame = out
        enc.stdin.write(np.clip(frame, 0, 255).astype(np.uint8).tobytes())
        i += 1
    enc.stdin.close()
    enc.wait()
    dec.wait()
    print("done", cfg["out"], i, "frames")


if __name__ == "__main__":
    main()
