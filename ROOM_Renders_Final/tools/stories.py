#!/usr/bin/env python3
"""Black Friday Instagram Stories (1080x1920) in the ROOM grainy look.

Facts come from the store (Shopify discounts + Omnisend BF emails, 11/27/2026):
  - BF40: 40% off every kit and bundle, ends Cyber Monday 11/30
  - Refund Drop: 3 random orders placed 12pm ET 11/28 to 12pm ET 11/29 refunded in full,
    no purchase necessary, rules at prodbyeville.com/pages/refund-drop
  - Build a Bundle: on top of BF40, 2 kits +10%, 3 kits +20%, 4+ kits +25%, automatic at checkout

Usage: python3 stories.py [--out DIR] [--grain 0.9]
Key content stays inside Instagram's safe zone (clear of the top 250 px and bottom 340 px).
"""
import argparse, os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
FD = os.path.join(REPO, "plugin/Resources/Ui")
W, H = 1080, 1920
OFF = (245, 243, 239)
RED = (226, 74, 74)
DEEP = (186, 39, 39)
GREY = (150, 145, 138)

def F(weight, size): return ImageFont.truetype(os.path.join(FD, f"Satoshi-{weight}.otf"), size)

def text(d, x, y, s, font, fill, track=0, anchor="l"):
    """Draw tracked text; anchor l/c/r on x, y is the top of the cap height."""
    ws = [font.getlength(ch) for ch in s]
    total = sum(ws) + track * (len(s) - 1)
    x0 = x if anchor == "l" else x - total / 2 if anchor == "c" else x - total
    top = font.getbbox("H")[1]
    for ch, w in zip(s, ws):
        d.text((x0, y - top), ch, font=font, fill=fill); x0 += w + track
    return total

def blur(im, r): return np.asarray(im.filter(ImageFilter.GaussianBlur(r)), np.float32) / 255

def finish(base, glow, grain, seed, leak=None):
    """base/glow: RGB images. Adds bloom, optional red light leak, halation, grain, vignette."""
    img = np.asarray(base, np.float32) / 255
    img += blur(glow, 12) * 0.55 + blur(glow, 40) * 0.45
    if leak is not None:
        cx, cy, rad, amt = leak
        yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
        r = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2) / rad
        img += (np.exp(-r ** 2) * amt)[..., None] * np.array(DEEP, np.float32) / 255
    lum = img.mean(2)
    hal = Image.fromarray((np.clip(lum - 0.55, 0, 1) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(18))
    img += (np.asarray(hal, np.float32) / 255)[..., None] * np.array([0.55, 0.12, 0.08]) * 0.6
    img[..., 0] = np.roll(img[..., 0], 1, 1); img[..., 2] = np.roll(img[..., 2], -1, 1)
    img = img / (1 + img * 0.35) * 1.35
    img = img * 0.97 + np.array([0.016, 0.012, 0.012])
    rng = np.random.default_rng(seed)
    co = rng.standard_normal((H // 2, W // 2)).astype(np.float32)
    co = np.asarray(Image.fromarray(((co * 40 + 128).clip(0, 255)).astype(np.uint8)).resize((W, H), Image.BICUBIC), np.float32)
    n = ((co - 128) / 40 * 0.7 + rng.standard_normal((H, W)).astype(np.float32) * 0.45)[..., None]
    l = np.clip(img.mean(2, keepdims=True), 0, 1)
    img += n * (0.055 + 0.2 * l * (1 - l) * 2.2) * grain
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    r = np.sqrt(((xx - W / 2) / (W / 2)) ** 2 + ((yy - H / 2) / (H / 2)) ** 2)
    img *= (1 - 0.42 * np.clip(r - 0.55, 0, None) ** 1.5)[..., None]
    return Image.fromarray((np.clip(img, 0, 1) * 255 + 0.5).astype(np.uint8))

def canvas():
    b = Image.new("RGB", (W, H), (0, 0, 0)); g = Image.new("RGB", (W, H), (0, 0, 0))
    return b, g, ImageDraw.Draw(b), ImageDraw.Draw(g)

def footer(d, y=1555):
    text(d, W / 2, y, "PRODBYEVILLE.COM", F("Medium", 30), OFF, track=10, anchor="c")

# ---------- 1. Black Friday: big type poster ----------
def story_bf():
    b, g, d, dg = canvas()
    text(d, W / 2, 300, "BLACK FRIDAY", F("Bold", 40), RED, track=16, anchor="c")
    text(dg, W / 2, 300, "BLACK FRIDAY", F("Bold", 40), RED, track=16, anchor="c")
    big = F("Black", 380)
    for layer, col in ((d, OFF), (dg, (200, 70, 60))):
        text(layer, W / 2 + 8, 430, "40%", big, col, track=-12, anchor="c")
        text(layer, W / 2, 750, "OFF", F("Black", 250), col, track=6, anchor="c")
    text(d, W / 2, 1080, "EVERY KIT. EVERY BUNDLE.", F("Medium", 38), OFF, track=8, anchor="c")
    # code ticket
    x0, y0, x1, y1 = 250, 1170, 830, 1380
    for layer, col in ((d, OFF), (dg, RED)):
        layer.rounded_rectangle([x0, y0, x1, y1], radius=26, outline=col, width=3)
    text(d, W / 2, 1205, "USE CODE", F("Medium", 28), GREY, track=12, anchor="c")
    for layer, col in ((d, OFF), (dg, (210, 80, 70))):
        text(layer, W / 2, 1258, "BF40", F("Black", 96), col, track=18, anchor="c")
    text(d, W / 2, 1440, "ENDS CYBER MONDAY 11/30", F("Bold", 32), RED, track=10, anchor="c")
    footer(d)
    return b, g, (1000, 380, 650, 0.38)

# ---------- 2. Refund Drop: three $0 receipts ----------
def story_refund():
    b, g, d, dg = canvas()
    text(d, 90, 290, "THE REFUND DROP", F("Bold", 36), RED, track=14)
    text(dg, 90, 290, "THE REFUND DROP", F("Bold", 36), RED, track=14)
    hf = F("Black", 132)
    for i, line in enumerate(["YOUR ORDER", "MIGHT BE", "FREE."]):
        col = RED if line == "FREE." else OFF
        text(d, 84, 380 + i * 142, line, hf, col, track=-2)
        if line == "FREE.": text(dg, 84, 380 + i * 142, line, hf, (230, 80, 70), track=-2)
    # three receipts
    cw, ch, gap, y0 = 268, 300, 38, 850
    xs = [W / 2 - cw * 1.5 - gap + i * (cw + gap) for i in range(3)]
    for i, x in enumerate(xs):
        for layer, col in ((d, OFF), (dg, (150, 50, 45))):
            layer.rounded_rectangle([x, y0, x + cw, y0 + ch], radius=22, outline=col, width=3)
        text(d, x + cw / 2, y0 + 36, f"ORDER 0{i + 1}", F("Medium", 26), GREY, track=8, anchor="c")
        d.line([x + 30, y0 + 86, x + cw - 30, y0 + 86], fill=(90, 86, 82), width=2)
        for layer, col in ((d, OFF), (dg, (230, 80, 70))):
            text(layer, x + cw / 2, y0 + 128, "$0", F("Black", 104), col, anchor="c")
        text(d, x + cw / 2, y0 + 254, "REFUNDED", F("Bold", 24), RED, track=8, anchor="c")
    # steps
    sf, nf = F("Medium", 34), F("Black", 34)
    steps = ["Order between 12pm ET Sat 11/28", "and 12pm ET Sun 11/29.", "Sunday, I pick 3 orders at random.", "Picked? You get every dollar back."]
    y = 1235
    for n, (a, rows) in enumerate([(1, steps[0:2]), (2, steps[2:3]), (3, steps[3:4])]):
        text(d, 110, y, f"{n + 1}", nf, RED)
        for r in rows:
            text(d, 160, y, r, sf, OFF); y += 48
        y += 18
    text(d, W / 2, 1490, "No purchase necessary. Free entry + rules:", F("Regular", 24), GREY, anchor="c")
    text(d, W / 2, 1524, "prodbyeville.com/pages/refund-drop", F("Regular", 24), GREY, anchor="c")
    return b, g, (60, 1000, 560, 0.28)

# ---------- 3. Build a Bundle: rising stacks ----------
def story_bundle():
    b, g, d, dg = canvas()
    text(d, W / 2, 290, "BUILD A BUNDLE", F("Bold", 38), RED, track=16, anchor="c")
    text(dg, W / 2, 290, "BUILD A BUNDLE", F("Bold", 38), RED, track=16, anchor="c")
    hf = F("Black", 124)
    text(d, W / 2, 380, "MORE KITS.", hf, OFF, track=-2, anchor="c")
    text(d, W / 2, 520, "MORE OFF.", hf, RED, track=-2, anchor="c")
    text(dg, W / 2, 520, "MORE OFF.", hf, (230, 80, 70), track=-2, anchor="c")
    text(d, W / 2, 690, "Extra savings on top of BF40.", F("Medium", 36), OFF, anchor="c")
    tiers = [(2, "+10%", "2 KITS"), (3, "+20%", "3 KITS"), (4, "+25%", "4+ KITS")]
    bw, bh, bg_, base = 220, 92, 14, 1340
    for i, (n, pct, lab) in enumerate(tiers):
        cx = W / 2 + (i - 1) * 300
        for k in range(n):
            y1 = base - k * (bh + bg_); y0 = y1 - bh
            shade = 0.25 + 0.75 * (i / 2)
            fill = tuple(int(c * 0.10 * (0.6 + shade)) for c in RED)
            d.rounded_rectangle([cx - bw / 2, y0, cx + bw / 2, y1], radius=14, fill=fill, outline=OFF, width=3)
            dg.rounded_rectangle([cx - bw / 2, y0, cx + bw / 2, y1], radius=14, outline=(int(120 + 60 * i), 45, 40), width=4)
            text(d, cx, (y0 + y1) / 2 - 11, "KIT", F("Bold", 24), (205, 200, 192), track=10, anchor="c")
        top = base - n * (bh + bg_) - 96
        for layer, col in ((d, OFF if i < 2 else RED), (dg, (220, 80, 70))):
            text(layer, cx, top, pct, F("Black", 78), col, anchor="c")
        text(d, cx, base + 34, lab, F("Bold", 30), OFF, track=8, anchor="c")
    text(d, W / 2, 1455, "Applies automatically at checkout.", F("Medium", 32), GREY, anchor="c")
    footer(d, 1540)
    return b, g, (540, 1300, 520, 0.26)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(REPO, "ROOM_Renders_Final/09_BF_Stories"))
    ap.add_argument("--grain", type=float, default=0.9)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    for i, (name, fn) in enumerate([("01_black_friday_bf40", story_bf), ("02_refund_drop", story_refund), ("03_build_a_bundle", story_bundle)]):
        b, g, leak = fn()
        p = os.path.join(a.out, f"{name}.png"); finish(b, g, a.grain, 20 + i, leak).save(p); print(p)

if __name__ == "__main__":
    main()
