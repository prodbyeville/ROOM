#!/usr/bin/env python3
"""Local motion-control animator for the ROOM renders.

Takes an all-in-focus plate and renders a 5 s, 24 fps, 1920x1080 clip with:
  * a smooth eased camera move (zoom / pan / slight parallax skew) - no AI, no shake
  * a real depth-of-field focus pull driven by an approximate depth plane
  * optional knob top-face rotations (ellipse-space rotation, printed parts stay put)
  * a soft specular highlight sliding across the knob tops
Nothing else in the image is redrawn, so screens, cards and text cannot morph.

Usage: python3 animate_plate.py <config.json>
"""
import json
import subprocess
import sys

import numpy as np
from PIL import Image, ImageFilter

OUT_W, OUT_H, FPS = 1920, 1080, 24


def ease(x):
    x = min(max(x, 0.0), 1.0)
    return x * x * x * (x * (6 * x - 15) + 10)  # smootherstep


def window(t, cfg, W, H):
    """Source-space corners (tl, tr, br, bl) of the camera window at time t."""
    s = ease(t / cfg["duration"])
    cam = cfg["camera"]
    zoom = cam["zoom_start"] + (cam["zoom_end"] - cam["zoom_start"]) * s
    cx = (cam["center_start"][0] + (cam["center_end"][0] - cam["center_start"][0]) * s) * W
    cy = (cam["center_start"][1] + (cam["center_end"][1] - cam["center_start"][1]) * s) * H
    h = H / zoom
    w = h * OUT_W / OUT_H
    # parallax skew: near (bottom) content shifts more than far (top) when the camera slides
    par = cam.get("parallax_px", 0.0) * s
    rise = cam.get("rise_px", 0.0) * s
    tl = (cx - w / 2 + par * 0.0, cy - h / 2 - rise * 0.0)
    tr = (cx + w / 2 + par * 0.0, cy - h / 2 - rise * 0.0)
    br = (cx + w / 2 + par, cy + h / 2 + rise)
    bl = (cx - w / 2 + par, cy + h / 2 + rise)
    return [tl, tr, br, bl]


def perspective_coeffs(dst_pts, src_pts):
    """Coefficients for PIL's PERSPECTIVE transform mapping output->source."""
    A, B = [], []
    for (x, y), (u, v) in zip(dst_pts, src_pts):
        A.append([x, y, 1, 0, 0, 0, -u * x, -u * y]); B.append(u)
        A.append([0, 0, 0, x, y, 1, -v * x, -v * y]); B.append(v)
    return np.linalg.solve(np.array(A, float), np.array(B, float)).tolist()


def rotate_knob(img, knob, angle_deg):
    """Rotate pixels inside the knob's elliptical top face by angle_deg (ellipse space)."""
    if abs(angle_deg) < 1e-3:
        return img
    cx, cy, a, b = knob["cx"], knob["cy"], knob["a"], knob["b"]
    tilt = np.radians(knob.get("tilt", 0.0))
    pad = int(a + 8)
    x0, y0 = int(cx - pad), int(cy - pad)
    patch = img[y0:y0 + 2 * pad, x0:x0 + 2 * pad].copy()
    ys, xs = np.mgrid[0:patch.shape[0], 0:patch.shape[1]].astype(float)
    dx, dy = xs + x0 - cx, ys + y0 - cy
    # into ellipse-aligned frame
    ux = dx * np.cos(tilt) + dy * np.sin(tilt)
    uy = -dx * np.sin(tilt) + dy * np.cos(tilt)
    r = np.sqrt((ux / a) ** 2 + (uy / b) ** 2)
    # inverse-rotate in circle space to find the source pixel
    th = np.radians(-angle_deg)
    cxn, cyn = ux / a, uy / b
    sx = (cxn * np.cos(th) - cyn * np.sin(th)) * a
    sy = (cxn * np.sin(th) + cyn * np.cos(th)) * b
    srcx = sx * np.cos(tilt) - sy * np.sin(tilt) + cx - x0
    srcy = sx * np.sin(tilt) + sy * np.cos(tilt) + cy - y0
    # bilinear sample
    sx0 = np.clip(np.floor(srcx).astype(int), 0, patch.shape[1] - 2)
    sy0 = np.clip(np.floor(srcy).astype(int), 0, patch.shape[0] - 2)
    fx, fy = (srcx - sx0)[..., None], (srcy - sy0)[..., None]
    p = patch
    samp = (p[sy0, sx0] * (1 - fx) * (1 - fy) + p[sy0, sx0 + 1] * fx * (1 - fy)
            + p[sy0 + 1, sx0] * (1 - fx) * fy + p[sy0 + 1, sx0 + 1] * fx * fy)
    m = np.clip((knob.get("inner", 0.92) - r) / 0.06 + 1, 0, 1)[..., None]  # feathered edge
    out = img.copy()
    out[y0:y0 + 2 * pad, x0:x0 + 2 * pad] = patch * (1 - m) + samp * m
    return out


def knob_highlight(shape, knobs, t, cfg):
    """Additive soft specular band sliding across each knob top."""
    hl = cfg.get("highlight")
    if not hl:
        return None
    H, W = shape[:2]
    add = np.zeros((H, W), float)
    s = ease(t / cfg["duration"])
    for k in knobs:
        cx, cy, a, b = k["cx"], k["cy"], k["a"], k["b"]
        pad = int(a + 4)
        x0, y0 = int(cx - pad), int(cy - pad)
        ys, xs = np.mgrid[y0:y0 + 2 * pad, x0:x0 + 2 * pad].astype(float)
        r = np.sqrt(((xs - cx) / a) ** 2 + ((ys - cy) / b) ** 2)
        inside = np.clip((0.95 - r) / 0.08, 0, 1)
        band_x = cx + a * (hl["from"] + (hl["to"] - hl["from"]) * s)
        band = np.exp(-((xs - band_x) / (a * hl["width"])) ** 2)
        add[y0:y0 + 2 * pad, x0:x0 + 2 * pad] += band * inside * hl["strength"]
    return add


def depth_map(W, H, cfg):
    d = cfg["depth"]
    yy, xx = np.mgrid[0:H, 0:W].astype(float)
    return d["kx"] * xx / W + d["ky"] * (1 - yy / H)


def dof_blur(frame, depth, focus_d, cfg):
    f = cfg["focus"]
    r = np.clip(np.abs(depth - focus_d) * f["strength"], 0, f["max_blur"])
    levels = [0, 1.5, 3, 5, 8, 12, 17, 23]
    levels = [l for l in levels if l <= f["max_blur"] + 6]
    im = Image.fromarray(frame.astype(np.uint8))
    stack = [frame.astype(float)] + [np.asarray(im.filter(ImageFilter.GaussianBlur(l)), float) for l in levels[1:]]
    out = stack[-1].copy()
    for i in range(len(levels) - 1):
        lo, hi = levels[i], levels[i + 1]
        m = (r >= lo) & (r < hi)
        if m.any():
            t = ((r - lo) / (hi - lo))[..., None]
            blend = stack[i] * (1 - t) + stack[i + 1] * t
            out[m] = blend[m]
    return out


def main():
    cfg = json.load(open(sys.argv[1]))
    plate = np.asarray(Image.open(cfg["plate"]).convert("RGB"), float)
    H, W = plate.shape[:2]
    depth_src = depth_map(W, H, cfg)
    n = int(round(cfg["duration"] * FPS))
    out_pts = [(0, 0), (OUT_W, 0), (OUT_W, OUT_H), (0, OUT_H)]
    ff = subprocess.Popen(
        ["ffmpeg", "-v", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{OUT_W}x{OUT_H}",
         "-r", str(FPS), "-i", "-", "-an", "-c:v", "libx264", "-preset", "slow", "-crf", "14",
         "-pix_fmt", "yuv420p", "-movflags", "+faststart", cfg["out"]],
        stdin=subprocess.PIPE)
    focus_pts = cfg["focus"]["points"]  # [[x,y], ...] normalised plate coords
    dcfg = cfg["depth"]
    focus_ds = [dcfg["kx"] * x + dcfg["ky"] * (1 - y) for x, y in focus_pts]
    for i in range(n):
        t = i / FPS
        img = plate
        for k in cfg.get("knobs", []):
            ka = k["angle"] * ease((t - k["t0"]) / (k["t1"] - k["t0"]))
            img = rotate_knob(img, k, ka)
        hl = knob_highlight(img.shape, cfg.get("knobs", []), t, cfg)
        if hl is not None:
            img = np.clip(img + hl[..., None] * 255, 0, 255)
        coeffs = perspective_coeffs(out_pts, window(t, cfg, W, H))
        frame = np.asarray(Image.fromarray(img.astype(np.uint8)).transform(
            (OUT_W, OUT_H), Image.PERSPECTIVE, coeffs, Image.BICUBIC), float)
        dep = np.asarray(Image.fromarray(depth_src.astype(np.float32), mode="F").transform(
            (OUT_W, OUT_H), Image.PERSPECTIVE, coeffs, Image.BILINEAR), float)
        fr = cfg["focus"]
        fs = ease((t - fr["t0"]) / (fr["t1"] - fr["t0"]))
        focus_d = focus_ds[0] + (focus_ds[-1] - focus_ds[0]) * fs
        frame = dof_blur(frame, dep, focus_d, cfg)
        ff.stdin.write(np.clip(frame, 0, 255).astype(np.uint8).tobytes())
        if i % 24 == 0:
            print(f"frame {i}/{n}", flush=True)
    ff.stdin.close()
    ff.wait()
    print("done", cfg["out"])


if __name__ == "__main__":
    main()
