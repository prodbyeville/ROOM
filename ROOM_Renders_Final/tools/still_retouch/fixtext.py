import sys
from PIL import Image, ImageDraw, ImageFont
import numpy as np

src, dst = sys.argv[1], sys.argv[2]
FONT = '/home/user/ROOM/plugin/Resources/Ui/Satoshi-Medium.otf'
img = Image.open(src).convert('RGBA')
a = np.array(img).astype(float)

def erase(x0, y0, x1, y1):
    # per-row linear interpolation between pixels just outside the box
    # vertical interpolation between clean rows above and below the box
    T = a[y0 - 2, x0:x1 + 1, :3].copy(); B = a[y1 + 2, x0:x1 + 1, :3].copy()
    n = y1 - y0 + 1
    for i in range(n):
        t = (i + 1) / (n + 1)
        a[y0 + i, x0:x1 + 1, :3] = T * (1 - t) + B * t

def glyph(ch, bx0, by0, bx1, by1, color, ss=8):
    w, h = bx1 - bx0 + 1, by1 - by0 + 1
    f = ImageFont.truetype(FONT, 400)
    l, t, r, b = f.getbbox(ch)
    m = Image.new('L', (r - l + 40, b - t + 40), 0)
    ImageDraw.Draw(m).text((20 - l, 20 - t), ch, font=f, fill=255)
    m = m.crop(m.getbbox())
    gw = max(1, round(m.width * h / m.height))
    m = m.resize((gw * ss, h * ss), Image.LANCZOS).resize((gw, h), Image.LANCZOS)
    alpha = np.array(m).astype(float) / 255.0
    ox = bx0 + (w - gw) // 2
    for yy in range(h):
        for xx in range(gw):
            k = alpha[yy, xx]
            if k > 0:
                p = a[by0 + yy, ox + xx, :3]
                a[by0 + yy, ox + xx, :3] = p * (1 - k) + np.array(color) * k

# TAIL: "2.5.5" -> "2.5 S"  (remove second dot, swap last 5 for S)
tail_ink = a[835:851, 1731:1742, :3].reshape(-1, 3)
tail_col = tail_ink[tail_ink.sum(1).argmin()]
erase(1743, 831, 1764, 854)
glyph('S', 1752, 835, 1761, 850, tail_col)

# GLITCH SYNC: "SYVC" -> "SYNC"
sync_ink = a[1078:1088, 1797:1803, :3].reshape(-1, 3)
sync_col = sync_ink[sync_ink.sum(1).argmax()]
erase(1815, 1075, 1826, 1090)
glyph('N', 1817, 1078, 1824, 1087, sync_col)

Image.fromarray(a.clip(0, 255).astype('uint8'), 'RGBA').save(dst)
