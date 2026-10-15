#!/usr/bin/env python3
"""Turn the Higgsfield part photos into plugin UI assets.

Each part was photographed top-down on the surface it sits on (dust-white
steel or black powder coat). The photo is re-lit so its surface matches our
panel texture exactly, then cut out with a soft round (or rounded-square)
mask: the part keeps its real contact shadow and blends into the panel.
The results are small PNGs in plugin/Resources/Ui.

    python3 plugin/Tools/process_ui_assets.py SOURCE_DIR
"""
import sys
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

OUT = Path(__file__).resolve().parents[1] / "Resources" / "Ui"

# source, output, surface the part sits on, output size,
# crop centre (fractions of the photo), crop half-size (fraction of width), mask shape
PARTS = [
    ("knob_paper.png",  "knob_paper.png",  "paper", 320, (0.50, 0.50), 0.27, "circle"),
    ("key_paper.png",   "key_paper.png",   "paper", 200, (0.50, 0.48), 0.30, "round"),
    ("screw_paper.png", "screw_paper.png", "paper",  72, (0.50, 0.48), 0.24, "circle"),
    ("toggle.png",      "toggle.png",      "paper", 200, (0.50, 0.43), 0.21, "circle"),
    ("led_on.png",      "led_on.png",      "paper",  96, (0.50, 0.50), 0.27, "circle"),
    ("led_off.png",     "led_off.png",     "paper",  96, (0.50, 0.50), 0.25, "circle"),
    ("knob_black.png",  "knob_black.png",  "black", 320, (0.50, 0.50), 0.33, "circle"),
    ("key_black.png",   "key_black.png",   "black", 200, (0.50, 0.47), 0.29, "round"),
    ("screw_black.png", "screw_black.png", "black",  72, (0.50, 0.47), 0.22, "circle"),
    ("jack.png",        "jack.png",        "black", 160, (0.50, 0.50), 0.26, "circle"),
    # keys with a light in them: the glow pair is cropped so on and off are the same size;
    # the mini key (sync / retrig) is lit in code over the unlit photo
    ("key_glow_on.png",  "key_glow_on.png",  "paper", 200, (0.497, 0.494), 0.255, "round"),
    ("key_glow_off.png", "key_glow_off.png", "paper", 200, (0.499, 0.493), 0.222, "round"),
    ("minikey_off.png",  "minikey_off.png",  "black", 120, (0.50, 0.50), 0.22, "round"),
]
TEXTURES = [
    ("desk.png", "tex_desk.jpg", 1024),
    ("paper.png", "tex_paper.jpg", 1024),
    ("black.png", "tex_black.jpg", 1024),
]


def border_colour(a):
    border = np.concatenate([a[:16].reshape(-1, 3), a[-16:].reshape(-1, 3), a[:, :16].reshape(-1, 3), a[:, -16:].reshape(-1, 3)])
    return np.median(border, axis=0).astype(np.float32)


def process(src, panel_colour, size, centre, half_frac, shape):
    a = np.asarray(Image.open(src).convert("RGB")).astype(np.float32)
    h, w = a.shape[:2]
    # Re-light the photo so its surface matches our panel exactly: then the soft
    # edge of the mask blends into the panel without a halo.
    a = np.clip(a * (panel_colour / border_colour(a)), 0, 255)
    cx, cy, half = centre[0] * w, centre[1] * h, half_frac * w
    crop = Image.fromarray(a.astype(np.uint8)).crop((int(cx - half), int(cy - half), int(cx + half), int(cy + half))).convert("RGBA")
    s = crop.size[0]
    mask = Image.new("L", crop.size, 0)
    d = ImageDraw.Draw(mask)
    if shape == "circle":
        d.ellipse((s * 0.06, s * 0.06, s * 0.94, s * 0.94), fill=255)
    else:
        d.rounded_rectangle((s * 0.06, s * 0.06, s * 0.94, s * 0.94), radius=s * 0.2, fill=255)
    crop.putalpha(mask.filter(ImageFilter.GaussianBlur(s * 0.035)))
    return crop.resize((size, size), Image.LANCZOS)


def main(src_dir):
    src = Path(src_dir)
    OUT.mkdir(parents=True, exist_ok=True)
    for name, out, size in TEXTURES:
        img = Image.open(src / name).convert("RGB").resize((size, size), Image.LANCZOS)
        # Keep only the fine grain: take out the photo's uneven lighting (we light the panels ourselves).
        px = np.asarray(img).astype(np.float32)
        low = np.asarray(img.filter(ImageFilter.GaussianBlur(48))).astype(np.float32)
        img = Image.fromarray(np.clip(px - low + px.reshape(-1, 3).mean(axis=0), 0, 255).astype(np.uint8))
        if out == "tex_paper.jpg":   # warm the steel to Eville dust white (#ECE8DF)
            px = np.asarray(img).astype(np.float32)
            px *= np.array([236.0, 232.0, 223.0]) / px.reshape(-1, 3).mean(axis=0)
            img = Image.fromarray(np.clip(px, 0, 255).astype(np.uint8))
        img.save(OUT / out, quality=90)
        print("  ", out)
    panel = {
        "paper": np.asarray(Image.open(OUT / "tex_paper.jpg").convert("RGB")).reshape(-1, 3).mean(axis=0),
        "black": np.asarray(Image.open(OUT / "tex_black.jpg").convert("RGB")).reshape(-1, 3).mean(axis=0),
    }
    for name, out, surface, size, centre, half, shape in PARTS:
        process(src / name, panel[surface], size, centre, half, shape).save(OUT / out)
        print("  ", out)


if __name__ == "__main__":
    main(sys.argv[1])
