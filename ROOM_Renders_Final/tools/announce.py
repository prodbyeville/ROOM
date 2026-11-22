#!/usr/bin/env python3
"""Instagram announcement background: the empty straight-on room (ROOM mark
proportions) with the avatar's glow and grain, and open space above and below
for text. No text or logo is drawn.

Usage:
  python3 announce.py [--out DIR] [--grain 0.8]

Writes announce_4x5.png (1080x1350, feed post) and announce_3x4.png
(1080x1440, profile-grid shape) in --out.
"""
import argparse, os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
OFFWHITE = np.array((245, 243, 239), np.float32) / 255
RED = np.array((226, 74, 74), np.float32) / 255
DEEP = np.array((186, 39, 39), np.float32) / 255
SS = 2
LINE = 3.0
FRAME_FRAC = 0.70     # front frame width as a fraction of the canvas width
CENTER_Y = 0.48       # frame centre as a fraction of the canvas height

def blur(a, r):
    im = Image.fromarray((np.clip(a, 0, 1) * 255).astype(np.uint8))
    return np.asarray(im.filter(ImageFilter.GaussianBlur(r)), np.float32) / 255

def render(W, H, grain, seed=11):
    frame = W * FRAME_FRAC
    k = frame / 384.0 * SS
    ox = (W * SS - 384 * k) / 2 - 64 * k
    oy = H * SS * CENTER_Y - 256 * k
    def M(x, y): return (ox + x * k, oy + y * k)
    big = (W * SS, H * SS)
    lw = round(LINE * SS)

    back = Image.new("L", big, 0); db = ImageDraw.Draw(back)
    front = Image.new("L", big, 0); dfr = ImageDraw.Draw(front)
    dfr.rounded_rectangle([M(64, 64), M(448, 448)], radius=50 * k, outline=255, width=lw)
    db.rectangle([M(174, 164), M(338, 308)], outline=255, width=lw)
    for a, b in [((78.6, 78.6), (174, 164)), ((433.4, 78.6), (338, 164)),
                 ((78.6, 433.4), (174, 308)), ((433.4, 433.4), (338, 308))]:
        db.line([M(*a), M(*b)], fill=255, width=lw)

    floor_pts = [M(174, 308), M(338, 308), M(433.4, 433.4), M(400, 448), M(112, 448), M(78.6, 433.4)]
    floorfill = Image.new("L", big, 0); ImageDraw.Draw(floorfill).polygon(floor_pts, fill=255)
    wall = Image.new("L", big, 0); ImageDraw.Draw(wall).rectangle([M(174, 164), M(338, 308)], fill=255)
    hatch = Image.new("L", big, 0); dh = ImageDraw.Draw(hatch)
    zb, zf = 1.0, 0.43
    for i in range(1, 14):
        z = 1 / (1 / zb + (1 / zf - 1 / zb) * i / 14)
        tt = (1 / z - 1 / zb) / (1 / zf - 1 / zb)
        y = 308 + (440 - 308) * tt
        xl = 174 + (72 - 174) * tt; xr = 338 + (440 - 338) * tt
        dh.line([M(xl, y), M(xr, y)], fill=210, width=SS)
    hatch = Image.fromarray(np.minimum(np.asarray(hatch), np.asarray(floorfill)))

    def down(im): return np.asarray(im.resize((W, H), Image.LANCZOS), np.float32) / 255
    Lb, Lf, Hh, Ff, Wl = map(down, (back, front, hatch, floorfill, wall))

    # back wall: crimson-to-black gradient, like the plugin's room screen
    yy = np.arange(H, dtype=np.float32)[:, None]
    wt, wb = M(0, 164)[1] / SS, M(0, 308)[1] / SS
    grad = np.clip((yy - wt) / (wb - wt), 0, 1) ** 1.6
    Wg = Wl * grad

    img = np.zeros((H, W, 3), np.float32)
    img += (blur(Ff, 26) * 0.45 + blur(Ff, 90) * 0.8)[..., None] * DEEP
    img += Ff[..., None] * DEEP * 0.14
    img += (Wg * 0.55)[..., None] * DEEP + blur(Wg, 30)[..., None] * DEEP * 0.4
    img += (Hh * 0.9 + blur(Hh, 2) * 0.3)[..., None] * RED
    img += (blur(Lb, 6) * 0.6 + blur(Lb, 18) * 0.35)[..., None] * (OFFWHITE * 0.5 + RED * 0.3) * 0.75
    img += Lb[..., None] * OFFWHITE * 0.75
    img += (blur(Lf, 6) * 0.7 + blur(Lf, 18) * 0.4)[..., None] * (OFFWHITE * 0.5 + RED * 0.3)
    img = np.maximum(img, Lf[..., None] * OFFWHITE)

    # film finish
    lum = img.mean(2)
    img += blur(np.clip(lum - 0.55, 0, 1), 20)[..., None] * np.array([0.55, 0.12, 0.08]) * 0.6
    img[..., 0] = np.roll(img[..., 0], 1, 1); img[..., 2] = np.roll(img[..., 2], -1, 1)
    img = img / (1 + img * 0.35) * 1.35
    img = img * 0.96 + np.array([0.022, 0.016, 0.016])
    rng = np.random.default_rng(seed)
    coarse = rng.standard_normal((H // 2, W // 2)).astype(np.float32)
    coarse = np.asarray(Image.fromarray(((coarse * 40 + 128).clip(0, 255)).astype(np.uint8)).resize((W, H), Image.BICUBIC), np.float32)
    n = ((coarse - 128) / 40 * 0.7 + rng.standard_normal((H, W)).astype(np.float32) * 0.45)[..., None]
    l = np.clip(img.mean(2, keepdims=True), 0, 1)
    img += n * (0.05 + 0.2 * l * (1 - l) * 2.2) * grain
    gy, gx = np.mgrid[0:H, 0:W].astype(np.float32)
    r = np.sqrt(((gx - W / 2) / (W / 2)) ** 2 + ((gy - H / 2) / (H / 2)) ** 2)
    img *= (1 - 0.40 * np.clip(r - 0.5, 0, None) ** 1.5)[..., None]
    return np.clip(img, 0, 1)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(REPO, "ROOM_Renders_Final/08_Announcement"))
    ap.add_argument("--grain", type=float, default=0.8)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    for name, (w, h) in {"4x5": (1080, 1350), "3x4": (1080, 1440)}.items():
        img = Image.fromarray((render(w, h, a.grain) * 255 + 0.5).astype(np.uint8))
        p = os.path.join(a.out, f"announce_{name}.png"); img.save(p); print(p)

if __name__ == "__main__":
    main()
