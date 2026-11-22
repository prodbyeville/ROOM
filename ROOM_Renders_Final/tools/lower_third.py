#!/usr/bin/env python3
"""ROOM product lower third: the 3D room box pops up, drifts, and the
"room" wordmark + PRODBYEVILLE.COM wipe in beside it. Grainy, cinematic finish.

Usage:
  python3 lower_third.py [--out DIR] [--grain 1.0] [--preview]

Writes, in --out (default ROOM_Renders_Final/06_Lower_Third):
  room_lower_third_black.mp4   1920x1080 24 fps, on black with full-frame grain.
                               Drop it on top of footage with blend mode Screen/Add.
  room_lower_third_alpha.mov   ProRes 4444 with alpha, grain only on the graphic.
  room_lower_third_still.png   a hold frame for review.

Everything is drawn from geometry (no AI): the box is real 3D, projected each frame.
"""
import argparse, math, os, shutil, subprocess, sys, tempfile
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
FONT_MED = os.path.join(REPO, "plugin/Resources/Ui/Satoshi-Medium.otf")
WORDMARK = os.path.join(REPO, "plugin/Design/brand/wordmark/room-wordmark-offwhite-2048.png")

W, H, FPS, DUR = 1920, 1080, 24, 6.0
SS = 2                                   # supersampling for line work
OFFWHITE = (245, 243, 239)
RED = (226, 74, 74)                      # thin-on-dark red from the brand sheet
DEEP_RED = (186, 39, 39)

# ---------- easing ----------
def clamp(x, a=0.0, b=1.0): return max(a, min(b, x))
def seg(t, a, b): return clamp((t - a) / (b - a))
def ease_out_cubic(x): return 1 - (1 - x) ** 3
def ease_in_out(x): return x * x * (3 - 2 * x)
def ease_in_cubic(x): return x ** 3
def spring(x, zeta=0.42, freq=2.1):
    """0 -> 1 with a damped overshoot, x in [0,1]."""
    if x <= 0: return 0.0
    if x >= 1: return 1.0
    w = freq * 2 * math.pi
    return 1 - math.exp(-zeta * w * x) * math.cos(w * math.sqrt(1 - zeta ** 2) * x) * (1 - x ** 6) - 0 * x

# ---------- 3D ----------
BOX_W, BOX_D, BOX_H = 1.45, 1.0, 0.88
CENTER = (258, 872)                      # where the box sits on screen (1x px)
SCALE = 112.0                            # px per world unit at the box
CAM_D, PITCH = 6.0, math.radians(22)

def project(pts, yaw, lift):
    """pts: (N,3) world -> (N,2) screen px at SS scale."""
    p = np.asarray(pts, float).copy()
    p[:, 1] -= BOX_H * 0.42              # pivot near the box's middle
    c, s = math.cos(yaw), math.sin(yaw)
    x = p[:, 0] * c + p[:, 2] * s
    z = -p[:, 0] * s + p[:, 2] * c
    y = p[:, 1]
    cp, sp = math.cos(PITCH), math.sin(PITCH)
    y2 = y * cp + z * sp
    z2 = -y * sp + z * cp
    f = CAM_D / (CAM_D + z2)
    sx = CENTER[0] + x * f * SCALE
    sy = CENTER[1] - lift - y2 * f * SCALE
    return np.stack([sx, sy], 1) * SS

def draw_poly_line(d, pts, color, width):
    pts = [tuple(map(float, q)) for q in pts]
    d.line(pts, fill=color, width=width, joint="curve")

def trace(points, frac):
    """Return the leading `frac` of a polyline (for draw-on)."""
    if frac >= 1: return points
    L = [np.linalg.norm(np.subtract(points[i + 1], points[i])) for i in range(len(points) - 1)]
    tot, out, acc = sum(L), [points[0]], 0.0
    target = tot * frac
    for i, l in enumerate(L):
        if acc + l >= target:
            k = (target - acc) / l if l else 0
            out.append(tuple(np.add(points[i], np.multiply(np.subtract(points[i + 1], points[i]), k))))
            return out
        out.append(points[i + 1]); acc += l
    return out

# ---------- text layers (built once) ----------
def build_wordmark(height):
    wm = Image.open(WORDMARK).convert("RGBA")
    r = height / wm.height
    return wm.resize((round(wm.width * r), height), Image.LANCZOS)

def build_url(size, tracking):
    font = ImageFont.truetype(FONT_MED, size)
    text = "PRODBYEVILLE.COM"
    widths = [font.getlength(ch) for ch in text]
    total = sum(widths) + tracking * (len(text) - 1)
    asc, desc = font.getmetrics()
    img = Image.new("RGBA", (math.ceil(total) + 8, asc + desc + 8), (0, 0, 0, 0))
    d = ImageDraw.Draw(img); x = 4
    for ch, w in zip(text, widths):
        d.text((x, 4), ch, font=font, fill=OFFWHITE + (255,)); x += w + tracking
    return img

def wipe_mask(size, prog, soft=0.18):
    """Left-to-right soft wipe; prog 0..1."""
    w, h = size
    xs = np.linspace(0, 1, w)
    edge = prog * (1 + soft)
    m = np.clip((edge - xs) / soft, 0, 1)
    return np.tile(m, (h, 1))

# ---------- frame ----------
def render_graphic(t, wm, url):
    """Return a float32 RGB graphic layer (H,W,3) in 0..1 on black, pre-grain."""
    # timings (s)
    floor_draw = ease_out_cubic(seg(t, 0.05, 0.45))
    rise = spring(seg(t, 0.30, 1.35))
    glow_in = ease_out_cubic(seg(t, 0.15, 0.9))
    out = ease_in_cubic(seg(t, 5.05, 5.6))
    collapse = 1 - ease_in_out(seg(t, 5.15, 5.75))
    fade = 1 - ease_in_out(seg(t, 5.45, 5.95))

    yaw = math.radians(-24 - 10 * ease_in_out(seg(t, 0.0, 6.0)) + 4 * (1 - ease_out_cubic(seg(t, 0.2, 1.4))))
    lift = 3.0 * math.sin(t * 1.3) * seg(t, 0.8, 1.6) + 16 * (1 - ease_out_cubic(seg(t, 0.05, 0.9)))
    h = BOX_H * max(0.0, rise) * collapse
    pop = 0.82 + 0.18 * ease_out_cubic(seg(t, 0.05, 0.55))    # whole box scales up a touch

    hw, hd = BOX_W / 2 * pop, BOX_D / 2 * pop
    hb = h * pop
    base = [(-hw, 0, -hd), (hw, 0, -hd), (hw, 0, hd), (-hw, 0, hd)]
    top = [(x, hb, z) for x, _, z in base]
    P = project(base + top, yaw, lift)
    B, T = [tuple(P[i]) for i in range(4)], [tuple(P[i]) for i in range(4, 8)]

    big = (W * SS, H * SS)
    lines = Image.new("L", big, 0); dl = ImageDraw.Draw(lines)
    floor = Image.new("L", big, 0); df = ImageDraw.Draw(floor)
    glowfill = Image.new("L", big, 0); dg = ImageDraw.Draw(glowfill)

    # floor fill (for the red under-glow) and hatch
    if glow_in > 0:
        dg.polygon(B, fill=int(255 * glow_in))
    n_hatch = 12
    for i in range(1, n_hatch):
        k = i / n_hatch
        if k > floor_draw * 1.05: break
        a = project([(-hw, 0, -hd + 2 * hd * k)], yaw, lift)[0]
        b = project([(hw, 0, -hd + 2 * hd * k)], yaw, lift)[0]
        flick = 0.75 + 0.25 * math.sin(t * 9 + i * 1.7) * math.sin(t * 3.1 + i)
        df.line([tuple(a), tuple(b)], fill=int(200 * flick * glow_in), width=SS)

    lw = 2 * SS
    # floor outline draws on as one loop
    draw_poly_line(dl, trace(B + [B[0]], floor_draw), 255, lw)
    if h > 0.004:
        for i in range(4):
            dl.line([B[i], T[i]], fill=255, width=lw)
        draw_poly_line(dl, T + [T[0]], 255, lw)

    lines = lines.resize((W, H), Image.LANCZOS)
    floor = floor.resize((W, H), Image.LANCZOS)
    glowfill = glowfill.resize((W, H), Image.LANCZOS)

    L = np.asarray(lines, np.float32) / 255
    F = np.asarray(floor, np.float32) / 255
    G = np.asarray(glowfill, np.float32) / 255
    Lg = (np.asarray(lines.filter(ImageFilter.GaussianBlur(5)), np.float32) / 255 * 0.9 +
          np.asarray(lines.filter(ImageFilter.GaussianBlur(16)), np.float32) / 255 * 0.55)
    Gg = (np.asarray(glowfill.filter(ImageFilter.GaussianBlur(22)), np.float32) / 255 * 0.40 +
          np.asarray(glowfill.filter(ImageFilter.GaussianBlur(60)), np.float32) / 255 * 0.75)
    Fg = np.asarray(floor.filter(ImageFilter.GaussianBlur(3)), np.float32) / 255

    off = np.array(OFFWHITE, np.float32) / 255
    red = np.array(RED, np.float32) / 255
    deep = np.array(DEEP_RED, np.float32) / 255
    breathe = 0.9 + 0.1 * math.sin(t * 2.2)

    img = np.zeros((H, W, 3), np.float32)
    img += Gg[..., None] * deep * 1.1 * breathe
    img += G[..., None] * deep * 0.16
    img += (F * 1.0 + Fg * 0.35)[..., None] * red
    img += Lg[..., None] * (off * 0.55 + red * 0.25)          # warm-tinted line glow
    img += L[..., None] * off
    img *= fade

    # ----- text -----
    txt = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    wm_x, wm_y = 404, 812
    p_wm = ease_out_cubic(seg(t, 0.70, 1.45)) * (1 - out)
    if p_wm > 0:
        m = wipe_mask(wm.size, p_wm if out == 0 else 1 - out)
        a = np.asarray(wm, np.float32).copy(); a[..., 3] *= m
        layer = Image.fromarray(a.astype(np.uint8))
        dx = round(-14 * (1 - ease_out_cubic(seg(t, 0.70, 1.45))))
        blur = 3.0 * (1 - ease_out_cubic(seg(t, 0.7, 1.25)))
        if blur > 0.2: layer = layer.filter(ImageFilter.GaussianBlur(blur))
        txt.alpha_composite(layer, (wm_x + dx, wm_y))
    # red rule
    rule_p = ease_out_cubic(seg(t, 1.0, 1.6)) * (1 - ease_in_cubic(seg(t, 5.0, 5.4)))
    dr = ImageDraw.Draw(txt)
    if rule_p > 0:
        dr.rectangle([wm_x + 4, 903, wm_x + 4 + round(34 * rule_p), 905], fill=RED + (255,))
    # url with tracking settle
    p_url = seg(t, 1.05, 1.95)
    if p_url > 0 and out < 1:
        trk = 9.5 + 9 * (1 - ease_out_cubic(p_url))
        u = build_url(27, trk)
        m = wipe_mask(u.size, ease_out_cubic(p_url) if out == 0 else 1 - out, soft=0.25)
        a = np.asarray(u, np.float32).copy(); a[..., 3] *= m * 0.86
        txt.alpha_composite(Image.fromarray(a.astype(np.uint8)), (wm_x + 52, 884))

    ta = np.asarray(txt, np.float32) / 255
    alpha = ta[..., 3:4]
    tglow = np.asarray(txt.getchannel("A").filter(ImageFilter.GaussianBlur(10)), np.float32)[..., None] / 255
    img = img * (1 - alpha) + ta[..., :3] * alpha + tglow * (off * 0.10 + red * 0.05)
    return img

# ---------- film finish ----------
class Film:
    def __init__(self, grain=1.0, seed=7):
        self.g = grain; self.rng = np.random.default_rng(seed)
        yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
        r = np.sqrt(((xx - W / 2) / (W / 2)) ** 2 + ((yy - H / 2) / (H / 2)) ** 2)
        self.vig = (1 - 0.38 * np.clip(r - 0.35, 0, None) ** 1.6)[..., None]

    def noise(self):
        n = self.rng.standard_normal((H // 2, W // 2, 1)).astype(np.float32)
        n = np.asarray(Image.fromarray(((n[..., 0] * 40 + 128).clip(0, 255)).astype(np.uint8))
                       .resize((W, H), Image.BICUBIC), np.float32)[..., None]
        fine = self.rng.standard_normal((H, W, 1)).astype(np.float32)
        return ((n - 128) / 40) * 0.7 + fine * 0.45      # clumpy + fine

    def apply(self, img, t, full_frame=True):
        # halation: bright parts bleed warm red
        lum = img.mean(2)
        hi = Image.fromarray((np.clip(lum - 0.55, 0, 1) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(22))
        img = img + (np.asarray(hi, np.float32) / 255)[..., None] * np.array([0.55, 0.12, 0.08]) * 0.7
        # chromatic fringe (lateral, tiny)
        img[..., 0] = np.roll(img[..., 0], 1, 1); img[..., 2] = np.roll(img[..., 2], -1, 1)
        # gate weave (sub-pixel) + exposure flicker
        wx = 0.6 * math.sin(t * 7.3) + 0.4 * math.sin(t * 17.1)
        wy = 0.5 * math.sin(t * 5.9 + 1)
        im8 = Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8))
        im8 = im8.transform((W, H), Image.AFFINE, (1, 0, wx, 0, 1, wy), resample=Image.BILINEAR)
        img = np.asarray(im8, np.float32) / 255
        img *= 1 + 0.025 * math.sin(t * 23.0) * math.sin(t * 3.7)
        # tone: lifted, slightly warm blacks; soft roll-off
        img = img / (1 + img * 0.35) * 1.35
        n = self.noise() * self.g
        if full_frame:
            img = img * 0.96 + np.array([0.022, 0.016, 0.016])
            l = np.clip(img.mean(2, keepdims=True), 0, 1)
            mid = 0.05 + 0.20 * l * (1 - l) * 2.2
            img = img + n * mid
            img = img * self.vig
        else:
            l = np.clip(img.mean(2, keepdims=True), 0, 1)
            mid = 0.03 + 0.20 * l * (1 - l) * 2.2
            img = img + n * mid * (img.max(2, keepdims=True) > 0.01)
        return np.clip(img, 0, 1)

# ---------- main ----------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(REPO, "ROOM_Renders_Final/06_Lower_Third"))
    ap.add_argument("--grain", type=float, default=1.0)
    ap.add_argument("--preview", action="store_true", help="render 5 check frames only")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    wm, url = build_wordmark(64), None
    film = Film(a.grain)
    n = int(DUR * FPS)
    frames = [0, 12, 24, 40, 96, 130] if a.preview else range(n)
    tmp = tempfile.mkdtemp()
    for i in frames:
        t = i / FPS
        g = render_graphic(t, wm, url)
        black = film.apply(g.copy(), t, full_frame=True)
        Image.fromarray((black * 255 + 0.5).astype(np.uint8)).save(f"{tmp}/b_{i:04d}.png")
        if a.preview:
            shutil.copy(f"{tmp}/b_{i:04d}.png", os.path.join(a.out, f"preview_{i:04d}.png")); continue
        # alpha version: luma-keyed graphic, grain on the graphic only
        fg = film.apply(g.copy(), t, full_frame=False)
        al = np.clip(g.max(2) * 1.6, 0, 1)
        rgb = np.where(al[..., None] > 1e-3, fg / np.maximum(al[..., None], 1e-3), 0)
        rgba = np.dstack([np.clip(rgb, 0, 1), al])
        Image.fromarray((rgba * 255 + 0.5).astype(np.uint8), "RGBA").save(f"{tmp}/a_{i:04d}.png")
        if i % 24 == 0: print(f"frame {i}/{n}", flush=True)
    if a.preview: return
    shutil.copy(f"{tmp}/b_{96:04d}.png", os.path.join(a.out, "room_lower_third_still.png"))
    ff = ["ffmpeg", "-v", "error", "-y", "-framerate", str(FPS)]
    subprocess.run(ff + ["-i", f"{tmp}/b_%04d.png", "-c:v", "libx264", "-preset", "slow", "-crf", "19",
                         "-tune", "grain", "-pix_fmt", "yuv420p", "-movflags", "+faststart",
                         os.path.join(a.out, "room_lower_third_black.mp4")], check=True)
    subprocess.run(ff + ["-i", f"{tmp}/a_%04d.png", "-c:v", "prores_ks", "-profile:v", "4444",
                         "-pix_fmt", "yuva444p10le", "-vendor", "apl0",
                         os.path.join(a.out, "room_lower_third_alpha.mov")], check=True)
    shutil.rmtree(tmp)
    print("done:", a.out)

if __name__ == "__main__":
    main()
