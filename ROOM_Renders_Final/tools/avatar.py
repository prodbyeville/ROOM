#!/usr/bin/env python3
"""Instagram avatar: a logo standing inside the 3D room box, in the lower third's
glowing, grainy look (lighter grain so it reads at small sizes).

Usage:
  python3 avatar.py LOGO.png [--out DIR] [--grain 0.6] [--size 1080]

LOGO.png: a white logo on a transparent background.
Writes avatar_<size>.png and a circle-crop check (avatar_check.png) in --out.
"""
import argparse, math, os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
OFFWHITE = np.array((245, 243, 239), np.float32) / 255
RED = np.array((226, 74, 74), np.float32) / 255
DEEP = np.array((186, 39, 39), np.float32) / 255

N = 1080          # working size
SS = 2
FRAME = 820          # front frame size in px at 1080 (keeps the corners inside the circle crop)
LINE = 3.0           # line weight in px
LOGO_H = 0.47        # logo height as a fraction of the front frame
LOGO_BASE = 340      # where the logo stands on the floor, in mark units (back wall bottom is 308)
BW, BD, BH = 1.30, 1.00, 1.00
YAW, PITCH, CAM_D = math.radians(-20), math.radians(17), 6.0
SCALE = 455.0
CENTER = (N / 2 + 6, N / 2 - 4)

def project(pts):
    p = np.asarray(pts, float).copy()
    p[:, 1] -= BH * 0.5
    c, s = math.cos(YAW), math.sin(YAW)
    x = p[:, 0] * c + p[:, 2] * s
    z = -p[:, 0] * s + p[:, 2] * c
    y = p[:, 1]
    cp, sp = math.cos(PITCH), math.sin(PITCH)
    y2 = y * cp + z * sp
    z2 = -y * sp + z * cp
    f = CAM_D / (CAM_D + z2)
    return np.stack([CENTER[0] + x * f * SCALE, CENTER[1] - y2 * f * SCALE], 1), z2

def blur(a, r):
    im = Image.fromarray((np.clip(a, 0, 1) * 255).astype(np.uint8))
    return np.asarray(im.filter(ImageFilter.GaussianBlur(r)), np.float32) / 255

def find_coeffs(dst, src):
    """PIL perspective coeffs mapping output (dst) quad to input (src) quad."""
    A, b = [], []
    for (x, y), (u, v) in zip(dst, src):
        A += [[x, y, 1, 0, 0, 0, -u * x, -u * y], [0, 0, 0, x, y, 1, -v * x, -v * y]]
        b += [u, v]
    return np.linalg.solve(np.array(A, float), np.array(b, float)).tolist()

def warp_logo(logo, quad, size):
    """Place logo (L mask) onto the quad (TL, TR, BR, BL) in an image of `size`."""
    w, h = logo.size
    co = find_coeffs(quad, [(0, 0), (w, 0), (w, h), (0, h)])
    return logo.transform(size, Image.PERSPECTIVE, co, Image.BICUBIC)

def render(logo_path, grain, seed=3):
    """Straight-on view into the room, using the ROOM mark's proportions
    (plugin/Design/brand/mark/room-mark-red.svg, 512 viewBox)."""
    k = FRAME / 384.0 * SS                                    # mark units -> big px
    ox = oy = (N * SS - 384 * k) / 2 - 64 * k                 # centre the 64..448 frame
    def M(x, y): return (ox + x * k, oy + y * k)
    big = (N * SS, N * SS)
    lw = round(LINE * SS)

    back = Image.new("L", big, 0); db = ImageDraw.Draw(back)      # back wall + corner lines
    front = Image.new("L", big, 0); dfr = ImageDraw.Draw(front)   # rounded front frame
    dfr.rounded_rectangle([M(64, 64), M(448, 448)], radius=50 * k, outline=255, width=lw)
    db.rectangle([M(174, 164), M(338, 308)], outline=255, width=lw)
    for a, b in [((78.6, 78.6), (174, 164)), ((433.4, 78.6), (338, 164)),
                 ((78.6, 433.4), (174, 308)), ((433.4, 433.4), (338, 308))]:
        db.line([M(*a), M(*b)], fill=255, width=lw)

    # floor: trapezoid from the back wall's bottom edge to the front frame's bottom
    floor_pts = [M(174, 308), M(338, 308), M(433.4, 433.4), M(400, 448), M(112, 448), M(78.6, 433.4)]
    floorfill = Image.new("L", big, 0); ImageDraw.Draw(floorfill).polygon(floor_pts, fill=255)
    hatch = Image.new("L", big, 0); dh = ImageDraw.Draw(hatch)
    # perspective-spaced lines (even steps in depth -> 1/z on screen)
    zb, zf = 1.0, 0.43                                        # back / front depth (front is 164/384 of back)
    for i in range(1, 12):
        z = 1 / (1 / zb + (1 / zf - 1 / zb) * i / 12)
        tt = (1 / z - 1 / zb) / (1 / zf - 1 / zb)
        y = 308 + (440 - 308) * tt
        xl = 174 + (72 - 174) * tt; xr = 338 + (440 - 338) * tt
        dh.line([M(xl, y), M(xr, y)], fill=210, width=SS)
    hatch = Image.fromarray(np.minimum(np.asarray(hatch), np.asarray(floorfill)))

    # logo standing mid-room, straight on
    src = Image.open(logo_path).convert("RGBA")
    m = src.getchannel("A"); m = m.crop(m.getbbox())
    lh = LOGO_H * 384 * k; lw_ = lh * m.width / m.height
    base_y = M(0, LOGO_BASE)[1]; cx = M(256, 0)[0]
    lm = m.resize((round(lw_), round(lh)), Image.LANCZOS)
    logo = Image.new("L", big, 0); logo.paste(lm, (round(cx - lw_ / 2), round(base_y - lh)))
    # faint reflection on the floor, squashed by the floor's perspective
    rm = lm.transpose(Image.FLIP_TOP_BOTTOM).resize((round(lw_), round(lh * 0.22)), Image.LANCZOS)
    refl = Image.new("L", big, 0); refl.paste(rm, (round(cx - lw_ / 2), round(base_y + 2 * k)))

    def down(im): return np.asarray(im.resize((N, N), Image.LANCZOS), np.float32) / 255
    Lb, Lf, Hh, Ff, Lo, Rf = map(down, (back, front, hatch, floorfill, logo, refl))
    Rf = Rf * Ff

    img = np.zeros((N, N, 3), np.float32)
    img += (blur(Ff, 24) * 0.45 + blur(Ff, 80) * 0.75)[..., None] * DEEP
    img += Ff[..., None] * DEEP * 0.14
    img += (Hh * 0.9 + blur(Hh, 2) * 0.3)[..., None] * RED
    img += (blur(Rf, 5) * 0.14)[..., None] * (RED * 0.6 + OFFWHITE * 0.25)
    img += (blur(Lb, 6) * 0.6 + blur(Lb, 18) * 0.35)[..., None] * (OFFWHITE * 0.5 + RED * 0.3) * 0.75
    img += Lb[..., None] * OFFWHITE * 0.75
    lg = blur(Lo, 10) * 0.55 + blur(Lo, 34) * 0.4
    img += lg[..., None] * (OFFWHITE * 0.45 + RED * 0.35)
    img = img * (1 - Lo[..., None]) + Lo[..., None] * OFFWHITE
    img += (blur(Lf, 6) * 0.7 + blur(Lf, 18) * 0.4)[..., None] * (OFFWHITE * 0.5 + RED * 0.3)
    img = np.maximum(img, Lf[..., None] * OFFWHITE)

    # ----- film finish (lighter than the lower third) -----
    lum = img.mean(2)
    img += blur(np.clip(lum - 0.55, 0, 1), 20)[..., None] * np.array([0.55, 0.12, 0.08]) * 0.6
    img[..., 0] = np.roll(img[..., 0], 1, 1); img[..., 2] = np.roll(img[..., 2], -1, 1)
    img = img / (1 + img * 0.35) * 1.35
    img = img * 0.96 + np.array([0.022, 0.016, 0.016])
    rng = np.random.default_rng(seed)
    coarse = rng.standard_normal((N // 2, N // 2)).astype(np.float32)
    coarse = np.asarray(Image.fromarray(((coarse * 40 + 128).clip(0, 255)).astype(np.uint8)).resize((N, N), Image.BICUBIC), np.float32)
    n = ((coarse - 128) / 40 * 0.7 + rng.standard_normal((N, N)).astype(np.float32) * 0.45)[..., None]
    l = np.clip(img.mean(2, keepdims=True), 0, 1)
    img += n * (0.05 + 0.2 * l * (1 - l) * 2.2) * grain
    yy, xx = np.mgrid[0:N, 0:N].astype(np.float32)
    r = np.sqrt(((xx - N / 2) / (N / 2)) ** 2 + ((yy - N / 2) / (N / 2)) ** 2)
    img *= (1 - 0.45 * np.clip(r - 0.45, 0, None) ** 1.5)[..., None]
    return np.clip(img, 0, 1)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("logo")
    ap.add_argument("--out", default=os.path.join(REPO, "ROOM_Renders_Final/07_Avatar"))
    ap.add_argument("--grain", type=float, default=0.6)
    ap.add_argument("--size", type=int, default=1080)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    img = Image.fromarray((render(a.logo, a.grain) * 255 + 0.5).astype(np.uint8))
    if a.size != N: img = img.resize((a.size, a.size), Image.LANCZOS)
    out = os.path.join(a.out, f"avatar_{a.size}.png"); img.save(out)
    # check sheet: full, plus circle crops at Instagram sizes (profile 150 px, feed 32 px)
    sheet = Image.new("RGB", (1080 + 40 + 330, 1080), (250, 250, 250))
    sheet.paste(img.resize((1080, 1080)), (0, 0))
    y = 40
    for s in (300, 150, 56, 32):
        c = img.resize((s, s), Image.LANCZOS)
        mask = Image.new("L", (s * 4, s * 4), 0); ImageDraw.Draw(mask).ellipse([0, 0, s * 4 - 1, s * 4 - 1], fill=255)
        sheet.paste(c, (1120, y), mask.resize((s, s), Image.LANCZOS)); y += s + 30
    sheet.save(os.path.join(a.out, "avatar_check.png"))
    print(out)

if __name__ == "__main__":
    main()
